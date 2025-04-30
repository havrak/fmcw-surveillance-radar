classdef radarDataCube < handle
	properties(Access=public)
		yawBinMin=0;
		yawBinMax=359;
		yawBins;   % 1° resolution
		pitchBinMin=-20;
		pitchBinMax=60;     % keeping dimension divisible by 8 to use AVX2
		pitchBins;          % 1° resolution

		rawCube;          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		rawCubeMap;
		rawCubeSize;

		cfarCube;          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		cfarCubeMap;
		cfarCubeSize;

		spreadPattern               % Weighting matrix [Yaw x Pitch]
		bufferA = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []); % Active buffer
		bufferB = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []); % Processing buffer
		bufferActive;
		bufferActiveWriteIdx = 1;
		batchSize = 6;

		decay = true;
		isProcessing = false;
		overflow = false;
		keepRaw;
		keepCFAR;
	end

	events
		updateFinished
	end

	methods(Static)

		function mask = createSectorMask(diffYaw, diffPitch, patternSize, speed)
			if speed == 0
				mask = zeros(patternSize);
				return;
			end
			epsilon = 1e-6;
			effectiveSpeed = max(speed, epsilon);

			[yawGrid, pitchGrid] = meshgrid(...
				linspace(-1, 1, patternSize(1)), ...
				linspace(-1, 1, patternSize(2)) ...
				);

			moveAngle = atan2d(diffPitch, -diffYaw);
			sectorWidth = 30 + 150 * exp(-speed/5); % upon stationary we should get 360 -> null whole area

			gridAngles = atan2d(pitchGrid, yawGrid);
			angleDiff = abs(mod(gridAngles - moveAngle + 180, 360) - 180);

			lineVector = [cosd(moveAngle), sind(moveAngle)];
			perpendicularDistances = abs(pitchGrid * lineVector(1) - yawGrid * lineVector(2));

			mask = zeros(patternSize);
			sectorMask = angleDiff <= sectorWidth;
			dropOffFactor = 1 / (effectiveSpeed^2);

			mask(sectorMask) = exp(- (perpendicularDistances(sectorMask).^2) * dropOffFactor);

			maxVal = max(mask(:));
			if maxVal > 0
				mask = mask ./ maxVal;
			end
		end

		function testFunction()
			disp("Test");
		end

		function processBatch(buffer, spreadPattern, rawCubeSize, yawBins, pitchBins, processRaw, processCFAR, decay)
			%% Updating cube for raw data
			if(processRaw && isempty(spreadPattern))
				m = memmapfile('rawCube.dat', ...
					'Format', {'single', rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				if(decay)
					batchDecay = single(prod([buffer.decay]));
					scripts.decayCube_avx2(m.Data.rawCube, batchDecay);
				end

				for i = 1:numel(buffer.yawIdx)
					yaw = buffer.yawIdx(i);
					pitch = buffer.pitchIdx(i);
					contribution = buffer.rangeDoppler(:, :, i);

					if(decay)
						batchDecay = single(prod(buffer.decay(i:end)));
						scripts.decayCube_avx2(contribution, batchDecay);
					end
					m.Data.rawCube(:, :, yaw, pitch) = m.Data.rawCube(:, :, yaw, pitch) + contribution;
				end
			elseif(processRaw)
				m = memmapfile('rawCube.dat', ...
					'Format', {'single', rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				% --- 1. Calculate composite region ---
				halfYaw = floor(size(spreadPattern, 1)/2);
				halfPitch = floor(size(spreadPattern, 2)/2);
				numYawBins = length(yawBins);

				% Get raw min/max indices
				% --- 1.1 Expand Yar range for each update ---
				%  we need to find most distant elements -> let's label them min and
				%  max (don't correspond to actuall minimums)
				%  max + halfYaw and min - halfYaw are ok -> great
				%  max + halfYaw results in overflow, min-halfYaw doesn't -> create array max-halfYaw->end and 1-> min+halfYaw
				%  ...
				%  -> calculate all intervals we want to fill in
				%  -> concart into larger array and remove duplicat elements, mode
				%  that array
				%  find breaks in this array (wrap fro 360 to 1) -> fill in elements
				expandedYaws = arrayfun(@(y) [(y-halfYaw) : (y+halfYaw)], [buffer.yawIdx], 'UniformOutput', false);
				allYaws = unique(mod(cat(2, expandedYaws{:}) - 1, numYawBins) + 1);
				% Split into contiguous segments if wrap-around exists
				wrapIdx = find(diff(allYaws) < 0);
				if ~isempty(wrapIdx)
					yawIndices = [allYaws(1:wrapIdx), allYaws(wrapIdx+1:end)];
				else
					yawIndices = allYaws;
				end

				% Clamp pitch indices
				minPitch = max(1, min([buffer.pitchIdx]) - halfPitch);
				maxPitch = min(length(pitchBins), max([buffer.pitchIdx]) + halfPitch);
				pitchIndices = minPitch:maxPitch;


				% --- 2. Initialize subrawCube for new contributions ---
				subCube = zeros(...
					rawCubeSize(1), ...
					rawCubeSize(2), ...
					length(yawIndices), ...
					length(pitchIndices), ...
					'single' ...
					);

				% --- 3. Apply updates into subrawCube ---

				yawMap = containers.Map('KeyType', 'double', 'ValueType', 'double');
				for i = 1:length(yawIndices)
					yawMap(yawIndices(i)) = i; % Maps original yaw index to subrawCube position
				end

				for i = 1:numel(buffer.yawIdx)
					% time2 = tic;
					yaw = buffer.yawIdx(i);
					pitch = buffer.pitchIdx(i);

					% --- 3.1 Reindex from rawCube into subrawCube ---
					validYaw = mod((yaw - halfYaw : yaw + halfYaw) - 1, length(yawBins)) + 1;
					localYaw = arrayfun(@(x) yawMap(x), validYaw);

					validPitch = max(1, pitch - halfPitch):min(length(pitchBins), pitch + halfPitch);
					localPitchOffset = validPitch(1) - minPitch + 1;
					localPitch = localPitchOffset : (localPitchOffset + length(validPitch) - 1);

					% --- 3.2 Adjust pattern ---
					startPitchPat = max(1,(halfPitch+1)-(pitch-validPitch(1)));
					endPitchPat = min(size(spreadPattern, 2), startPitchPat + length(validPitch) - 1);
					adjPattern = spreadPattern(:, startPitchPat:endPitchPat)*prod(buffer.decay(i:end));

					% --- 3.3 Spread contribution into 4D with pattern ---
					rangerDoppler = buffer.rangeDoppler(:, :, i);
					if(decay)
						batchDecay = single(prod([buffer.decay(i:end)]));
						scripts.decayCube_avx2(rangerDoppler, batchDecay);
					end
					contribution = scripts.applyPattern(adjPattern, rangerDoppler);
				


					% --- 3.4 Update subrawCube with contribution ---
					%%subCube(:, :, localYaw, localPitch) = ...
					%	subCube( :, :, localYaw, localPitch) + contribution;
					scripts.updateCube(subCube, contribution, localYaw, localPitch);
				end


				% --- 4. Decay rawCube ---
				if(decay)
					batchDecay = single(prod([buffer.decay]));
					scripts.decayCube_avx2(m.Data.rawCube, batchDecay); % speed varies widely, probalby due to non consistent memory managment
				end

				% --- 5. Merge data ---
				scripts.updateCube(m.Data.rawCube, subCube, yawIndices, pitchIndices);
				% m.Data.rawCube(:, :,yawIndices, pitchIndices) = m.Data.rawCube( :, :,yawIndices, pitchIndices) + subCube;
			end

			%% Updating cube for CFAR data
			if(processCFAR)
				cfarCubeSize=rawCubeSize([1 3 4]);
				m = memmapfile('cfarCube.dat', ...
					'Format', {'single', cfarCubeSize, 'cfarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				for i = 1:numel(buffer.yawIdx)
					fprintf("CFAR | idx=%f, sum=%f", buffer.pitchIdx(i), sum(buffer.cfar(:, i)));
					m.Data.cfarCube(:, buffer.yawIdx(i), buffer.pitchIdx(i)) = buffer.cfar(:, i);
				end
			end
		end
	end

	methods(Access=private)
		function afterBatchProcessing(obj)
			obj.isProcessing = false;
			notify(obj, 'updateFinished');
			fprintf("radarDataCube | updateFinished\n");
		end

		function generateSpreadPattern(obj, spreadPatternYaw, spreadPatternPitch)
			dimensionsYaw = -spreadPatternYaw:spreadPatternYaw;
			dimensionsPitch = -spreadPatternPitch:spreadPatternPitch;
			yawSigma = 3*spreadPatternYaw / (sqrt(8*log(2)));
			pitchSigma = 1.5*spreadPatternPitch / (sqrt(8*log(2)));
			[yawMash, pitchMesh] = meshgrid(dimensionsPitch,dimensionsYaw);
			obj.spreadPattern = single(exp(-0.5*( (yawMash/yawSigma).^2 + (pitchMesh/pitchSigma).^2 )));
			%imagesc(dimensionsYaw, dimensionsPitch, pattern);
		end

	end

	methods(Access=public)


		function obj = radarDataCube(numRangeBins, numDopplerBins, spreadPatternYaw, spreadPatternPitch, keepRaw, keepCFAR, decay)
			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution

			obj.bufferActive = obj.bufferA;
			if(spreadPatternYaw == 0 || spreadPatternPitch == 0)
				obj.spreadPattern = [];
			else
				obj.generateSpreadPattern(spreadPatternYaw, spreadPatternPitch);
			end
			obj.keepRaw = keepRaw;
			obj.keepCFAR = keepCFAR;
			obj.decay = decay;
			obj.rawCubeSize = [ ...
				numRangeBins, ...
				numDopplerBins ...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				];
			%% Initialize radar cube for raw data

			if obj.keepRaw



				if ~exist('rawCube.dat', 'file')
					% TODO: this is linux only, would be nice to fix
					system('fallocate -l 256M rawCube.dat');
				end

				fprintf("radarDataCube | radarDataCube | Initializing rawCube with yaw %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)

				obj.bufferA.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');
				obj.bufferB.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');


				% Create memory map
				obj.rawCubeMap = memmapfile('rawCube.dat', ...
					'Format', {'single', obj.rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);


				scripts.zeroCube(obj.rawCubeMap.Data.rawCube);
				obj.rawCube = obj.rawCubeMap.Data.rawCube;
			end

			%% Initialize radar cube for cfar

			if obj.keepCFAR
				obj.bufferA.cfar = zeros([numRangeBins, obj.batchSize], 'single');
				obj.bufferB.cfar = zeros([numRangeBins, obj.batchSize], 'single');

				if ~exist('cfarCube.dat', 'file')
					system('fallocate -l 64M cfarCube.dat');
				end

				% Create memory map
				obj.cfarCubeMap = memmapfile('cfarCube.dat', ...
					'Format', {'single', obj.rawCubeSize([1 3 4]), 'cfarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				scripts.zeroCube(obj.cfarCubeMap.Data.cfarCube);
				obj.cfarCube = obj.cfarCubeMap.Data.cfarCube;
			end
		end

		function addData(obj, yaw, pitch, cfar, rangeDoppler, speed)
			if nargin < 6
				speed = 0.01;
			end
			% disp(sum(cfar))
			[~, yawIdx] = min(abs(obj.yawBins - yaw));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));
			decay = exp(-speed/100);

			fprintf("radarDataCube | addData | adding to rawCube %d: yaw %f, pitch %f, decay %f\n", obj.bufferActiveWriteIdx, yaw, pitch, decay);

			obj.bufferA.yawIdx(obj.bufferActiveWriteIdx) = yawIdx;
			obj.bufferA.pitchIdx(obj.bufferActiveWriteIdx) = pitchIdx;
			if(obj.keepRaw)
				obj.bufferA.rangeDoppler(:, :, obj.bufferActiveWriteIdx) = single(rangeDoppler);
			end

			if(obj.keepCFAR)
				obj.bufferA.cfar(:, obj.bufferActiveWriteIdx) = single(cfar);
				%	fprintf("AFTER SINGLE\n");
				%disp(sum(obj.bufferA.cfar(:, obj.bufferActiveWriteIdx)));
			end
			obj.bufferA.decay(obj.bufferActiveWriteIdx) = decay;

			if obj.bufferActiveWriteIdx > obj.batchSize
				obj.overflow = true;
				obj.bufferActiveWriteIdx = 1;
			else
				obj.bufferActiveWriteIdx = obj.bufferActiveWriteIdx + 1;
			end


		end

		function status = isBatchFull(obj)
			status = (obj.bufferActiveWriteIdx > obj.batchSize) || obj.overflow;
		end

		function future = startBatchProcessing(obj)
			if obj.isProcessing
				fprintf("radarDataCube | startBatchProcessing | already processing\n");
				return;
			end


			fprintf("radarDataCube | startBatchProcessing | starting processing\n");

			obj.isProcessing = true;
			obj.overflow = false;

			processingBuffer = obj.bufferA;
			obj.bufferA = obj.bufferB; % Reset active buffer
			obj.bufferB = processingBuffer; % Assign to processing buffer
			obj.bufferActiveWriteIdx = 1;

			% future = parfeval(gcp, @radarDataCube.processBatch, 0, processingBuffer, obj.spreadPattern, obj.rawCubeSize, obj.yawBins, obj.pitchBins, obj.keepRaw, obj.keepCFAR);


			% NOTE: main thread execution for debug
			radarDataCube.processBatch(processingBuffer, ...
				obj.spreadPattern, ...
				obj.rawCubeSize, ...
				obj.yawBins, ...
				obj.pitchBins, ...
				obj.keepRaw, ...
				obj.keepCFAR, ...
				obj.decay);
			future = parfeval(gcp, @radarDataCube.testFunction, 0);

			afterAll(future, @(varargin) obj.afterBatchProcessing(varargin{:}), 0);

		end

		function zeroCubes(obj)
			if obj.keepCFAR
				scripts.zeroCube(obj.cfarCube)
			end
			if obj.keepRaw
				scripts.zeroCube(obj.rawCube)
			end
		end

	end
end

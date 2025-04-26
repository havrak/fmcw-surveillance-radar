classdef radarDataCube < handle
	properties(Access=public)
		yawBinMin=0;
		yawBinMax=359;
		yawBins;   % 1° resolution
		pitchBinMin=-20;
		pitchBinMax=60;     % keeping dimension divisible by 8 to use AVX2
		pitchBins;          % 1° resolution

		radarCube;          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		radarCubeMap;
		radarCubeSize;

		cfarCube;          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		cfarCubeMap;
		cfarCubeSize;

		antennaPattern               % Weighting matrix [Yaw x Pitch]
		bufferA = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []); % Active buffer
		bufferB = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []); % Processing buffer
		bufferActive;
		bufferActiveWriteIdx = 1;
		batchSize = 6;

		isProcessing = false;
		overflow = false;
		keepRaw = true;
		keepCFAR = true;
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

		function processBatch(buffer, antennaPattern, radarCubeSize, yawBins, pitchBins, processRaw, processCFAR)

			%% Updating cube for raw data
			if(processRaw)
				m = memmapfile('radarCube.dat', ...
					'Format', {'single', radarCubeSize, 'radarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				% --- 1. Calculate composite region ---
				halfYaw = floor(size(antennaPattern, 1)/2);
				halfPitch = floor(size(antennaPattern, 2)/2);
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


				% --- 2. Initialize subradarCube for new contributions ---
				subCube = zeros(...
					radarCubeSize(1), ...
					radarCubeSize(2), ...
					length(yawIndices), ...
					length(pitchIndices), ...
					'single' ...
					);

				% fprintf("SubradarCube size:");
				% disp(size(subCube));

				% time = toc(time);
				% fprintf(fid,...
				% 	'[BATCH] Initialization done (%f ms), Composite Region: Yaw=%d:%d (%d bins), Pitch=%d:%d (%d bins)\n',...
				% 	time*1000, min(yawIndices), max(yawIndices), length(yawIndices), minPitch, maxPitch, length(pitchIndices)...
				% 	);
				% time = tic;


				% buffer.decay(buffer.decay == 0) = 1;

				% --- 3. Apply updates into subradarCube ---
				% fprintf(fid, "[BATCH] starting subradarCube processing\n");

				yawMap = containers.Map('KeyType', 'double', 'ValueType', 'double');
				for i = 1:length(yawIndices)
					yawMap(yawIndices(i)) = i; % Maps original yaw index to subradarCube position
				end

				for i = 1:numel(buffer.yawIdx)
					% time2 = tic;
					yaw = buffer.yawIdx(i);
					pitch = buffer.pitchIdx(i);

					% --- 3.1 Reindex from radarCube into subradarCube ---
					validYaw = mod((yaw - halfYaw : yaw + halfYaw) - 1, length(yawBins)) + 1;
					localYaw = arrayfun(@(x) yawMap(x), validYaw);

					validPitch = max(1, pitch - halfPitch):min(length(pitchBins), pitch + halfPitch);
					localPitchOffset = validPitch(1) - minPitch + 1;
					localPitch = localPitchOffset : (localPitchOffset + length(validPitch) - 1);

					% --- 3.2 Adjust pattern ---
					startPitchPat = max(1,(halfPitch+1)-(pitch-validPitch(1)));
					endPitchPat = min(size(antennaPattern, 2), startPitchPat + length(validPitch) - 1);
					adjPattern = antennaPattern(:, startPitchPat:endPitchPat)*prod(buffer.decay(i:end));

					% --- 3.3 Spread contribution into 4D with pattern ---
					rangerDoppler = buffer.rangeDoppler(:, :, i);
					contribution = scripts.applyPattern(adjPattern, rangerDoppler);
					%
					% time2 = toc(time2);
					% fprintf(fid, '[UPDATE %2d] (%f ms) Initialization done\n', i, time2*1000);
					% time2 = tic;


					% --- 3.4 Update subradarCube with contribution ---
					%%subCube(:, :, localYaw, localPitch) = ...
					%	subCube( :, :, localYaw, localPitch) + contribution;
					scripts.updateCube(subCube, contribution, localYaw, localPitch);

					% time2 = toc(time2);
					% fprintf(fid,...
					% 	'[UPDATE %2d] (%f ms) AZ=%3d, PITCH=%3d | PatternRows=%3d:%3d | LocalYaw=%3d:%3d, LocalPitch=%3d:%3d | Decay=%d\n',...
					% 	i, time2*1000, yaw, pitch, startPitchPat, endPitchPat,...
					% 	localYaw(1), localYaw(end), localPitch(1), localPitch(end),...
					% 	prod(buffer.decay(i:end)) ...
					% 	);
				end
				% time = toc(time);
				% fprintf(fid,"[BATCH] Updates processed (%f ms)\n", time*1000);
				% time = tic;


				% --- 4. Decay radarCube ---
				batchDecay = single(prod([buffer.decay]));
				scripts.decayCube_avx2(m.Data.radarCube, batchDecay); % speed varies widely, probalby due to non consistent memory managment

				% time = toc(time);
				% fprintf(fid,"[BATCH] Decaying radarCube (%f ms), decaying with %f\n", time*1000, batchDecay);
				% time = tic;

				% --- 5. Merge data ---
				scripts.updateCube(m.Data.radarCube, subCube, yawIndices, pitchIndices);
				% m.Data.radarCube(:, :,yawIndices, pitchIndices) = m.Data.radarCube( :, :,yawIndices, pitchIndices) + subCube;
				% time = toc(time);
				% fprintf(fid,"[BATCH] Updating radarCube (%f ms)\n", time*1000);
			end

			%% Updating cube for CFAR data
			if(processCFAR)
				cfarCubeSize=radarCubeSize([1 3 4]);
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

		function generateAntennaPattern(obj, radPatternYaw, radPatternPitch)
			dimensionsYaw = -radPatternYaw:radPatternYaw;
			dimensionsPitch = -radPatternPitch:radPatternPitch;
			yawSigma = 3*radPatternYaw / (sqrt(8*log(2)));
			pitchSigma = 1.5*radPatternPitch / (sqrt(8*log(2)));
			[yawMash, pitchMesh] = meshgrid(dimensionsPitch,dimensionsYaw);
			obj.antennaPattern = single(exp(-0.5*( (yawMash/yawSigma).^2 + (pitchMesh/pitchSigma).^2 )));
			%imagesc(dimensionsYaw, dimensionsPitch, pattern);
		end

	end

	methods(Access=public)


		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternYaw, radPatternPitch, keepRaw, keepCFAR)
			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution

			obj.bufferActive = obj.bufferA;
			obj.generateAntennaPattern(radPatternYaw, radPatternPitch);
			obj.keepRaw = keepRaw;
			obj.keepCFAR = keepCFAR;
			obj.radarCubeSize = [ ...
				numRangeBins, ...
				numDopplerBins ...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				];
			%% Initialize radar cube for raw data

			if obj.keepRaw



				if ~exist('radarCube.dat', 'file')
					% TODO: this is linux only, would be nice to fix
					system('fallocate -l 256M radarCube.dat');
				end

				fprintf("radarDataCube | radarDataCube | Initializing radarCube with yaw %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)

				obj.bufferA.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');
				obj.bufferB.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');


				% Create memory map
				obj.radarCubeMap = memmapfile('radarCube.dat', ...
					'Format', {'single', obj.radarCubeSize, 'radarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);


				scripts.zeroCube(obj.radarCubeMap.Data.radarCube);
				obj.radarCube = obj.radarCubeMap.Data.radarCube;
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
					'Format', {'single', obj.radarCubeSize([1 3 4]), 'cfarCube'}, ...
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

			fprintf("radarDataCube | addData | adding to radarCube %d: yaw %f, pitch %f, decay %f\n", obj.bufferActiveWriteIdx, yaw, pitch, decay);

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

			% future = parfeval(gcp, @radarDataCube.processBatch, 0, processingBuffer, obj.antennaPattern, obj.radarCubeSize, obj.yawBins, obj.pitchBins, obj.keepRaw, obj.keepCFAR);
			

			% NOTE: main thread execution for debug
			radarDataCube.processBatch(processingBuffer, obj.antennaPattern, obj.radarCubeSize, obj.yawBins, obj.pitchBins, obj.keepRaw, obj.keepCFAR);
			future = parfeval(gcp, @radarDataCube.testFunction, 0);

			afterAll(future, @(varargin) obj.afterBatchProcessing(varargin{:}), 0);

		end

	end
end

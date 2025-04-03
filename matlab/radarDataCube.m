classdef radarDataCube < handle
	properties(Access=public)
		yawBinMin=0;
		yawBinMax=359;
		yawBins;   % 1° resolution
		pitchBinMin=-20;
		pitchBinMax=60;     % keeping dimension divisible by 8 to use AVX2
		pitchBins;          % 1° resolution
		cube;          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		cubeMap;
		cubeSize;

		antennaPattern               % Weighting matrix [Yaw x Pitch]
		bufferA = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'decay', []); % Active buffer
		bufferB = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'decay', []); % Processing buffer
		bufferActive;
		bufferActiveWriteIdx = 1;
		batchSize = 16;
		isProcessing = false;
		overflow = false;
	end

	events
		updateFinished
	end

	methods(Static)

		% TODO: consider removal
		function mask = createSectorMask(diffYaw, diffPitch, patternSize, speed)

			%mask=ones(patternSize);
			%return;

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

			% For actual speed = 0, return zero array
			if speed == 0
				mask = zeros(patternSize);
			end


			%filename = sprintf('mask_speed%g_diffYaw%g_diffPitch%g.png', speed, diffYaw, diffPitch);

			% Save the mask as a grayscale image (scaled 0 to 1)
			%cmap = parula(256);
			%rgbImage = ind2rgb(im2uint8(mat2gray(mask)), cmap);
			%imwrite(rgbImage, filename);
		end

		function testFunction()
			disp("Test");
		end

		function processBatch(buffer, antennaPattern, cubeSize, yawBins, pitchBins)
			time = tic;
			fid = fopen("out.txt", "a+");

			m = memmapfile('cube.dat', ...
				'Format', {'single', cubeSize, 'cube'}, ...
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


			% --- 2. Initialize subcube for new contributions ---
			subCube = zeros(...
				cubeSize(1), ...
				cubeSize(2), ...
				length(yawIndices), ...
				length(pitchIndices), ...
				'single' ...
				);

			% fprintf("Subcube size:");
			% disp(size(subCube));

			time = toc(time);
			fprintf(fid,...
				'[BATCH] Initialization done (%f ms), Composite Region: Yaw=%d:%d (%d bins), Pitch=%d:%d (%d bins)\n',...
				time*1000, min(yawIndices), max(yawIndices), length(yawIndices), minPitch, maxPitch, length(pitchIndices)...
				);
			time = tic;


			% --- 3. Apply updates into subcube ---
			fprintf(fid, "[BATCH] starting subcube processing\n");

			yawMap = containers.Map('KeyType', 'double', 'ValueType', 'double');
			for i = 1:length(yawIndices)
				yawMap(yawIndices(i)) = i; % Maps original yaw index to subcube position
			end

			for i = 1:numel(buffer.yawIdx)
				time2 = tic;
				yaw = buffer.yawIdx(i);
				pitch = buffer.pitchIdx(i);

				% --- 3.1 Reindex from cube into subcube ---
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
				time2 = toc(time2);
				fprintf(fid, '[UPDATE %2d] (%f ms) Initialization done\n', i, time2*1000);
				time2 = tic;


				% --- 3.4 Update subcube with contribution ---
				% Stupid fucking column major order fucks up this to be efficient with AVX2 (Inpossible to crete efficient 256 bit fields to run addition on)
				% TOOD: consider TBB, or just OpenMP
				%subCube(:, :, localYaw, localPitch) = ...
				%	subCube( :, :, localYaw, localPitch) + contribution;
				scripts.updateCube(subCube, contribution, localYaw, localPitch);

				time2 = toc(time2);
				fprintf(fid,...
					'[UPDATE %2d] (%f ms) AZ=%3d, PITCH=%3d | PatternRows=%3d:%3d | LocalYaw=%3d:%3d, LocalPitch=%3d:%3d\n',...
					i, time2*1000, yaw, pitch, startPitchPat, endPitchPat,...
					localYaw(1), localYaw(end), localPitch(1), localPitch(end)...
					);
			end
			time = toc(time);
			fprintf(fid,"[BATCH] Updates processed (%f ms)\n", time*1000);
			time = tic;


			% --- 4. Decay cube ---
			batchDecay = single(prod([buffer.decay]));
			scripts.decayCube_avx2(m.Data.cube, batchDecay); % speed varies widely, probalby due to non consistent memory managment

			time = toc(time);
			fprintf(fid,"[BATCH] Decaying cube (%f ms), decaying with %f\n", time*1000, batchDecay);
			time = tic;

			% --- 5. Merge data ---

			scripts.updateCube(m.Data.cube, subCube, yawIndices, pitchIndices);
			% m.Data.cube(:, :,yawIndices, pitchIndices) = m.Data.cube( :, :,yawIndices, pitchIndices) + subCube;
			time = toc(time);


			fprintf(fid,"[BATCH] Updating cube (%f ms)\n", time*1000);
		end
	end

	methods(Access=private)
		function afterBatchProcessing(obj)
			obj.isProcessing = false;
			notify(obj, 'updateFinished');

			fprintf("radarDataCube | updateFinished\n");
		end

		function pattern = generateAntennaPattern(obj, radPatternYaw, radPatternPitch)
			dimensionsYaw = -radPatternYaw:radPatternYaw;
			dimensionsPitch = -radPatternPitch:radPatternPitch;
			yawSigma = 3*radPatternYaw / (sqrt(8*log(2)));
			pitchSigma = 1.5*radPatternPitch / (sqrt(8*log(2)));
			[yawMash, pitchMesh] = meshgrid(dimensionsPitch,dimensionsYaw);
			pattern = single(exp(-0.5*( (yawMash/yawSigma).^2 + (pitchMesh/pitchSigma).^2 )));
			size(pattern);
			%imagesc(dimensionsYaw, dimensionsPitch, pattern);
		end

	end

	methods(Access=public)


		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternYaw, radPatternPitch)
			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution

			fprintf("radarDataCube | radarDataCube | Initializing cube with yaw %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)
			obj.antennaPattern = obj.generateAntennaPattern(radPatternYaw, radPatternPitch);
			obj.bufferActive = obj.bufferA;


			% NOTE: I know this sucks in terms of memory layout, but I realized
			% too late and have no energy to fix it now.
			% it MATLAB's fault that their memory storage approach is non
			% conventional to most programmers
			obj.cubeSize = [ ...
				numRangeBins, ...
				numDopplerBins ...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				];

			if ~exist('cube.dat', 'file')
				% TODO: this is linux only, would be nice to fix
				system('fallocate -l 250M cube.dat'); % 360x81x128x8 single
			end

			obj.bufferA.rangeDoppler = zeros([128, 8, obj.batchSize], 'single');

			obj.bufferB.rangeDoppler = zeros([128, 8, obj.batchSize], 'single');
			% Create memory map
			obj.cubeMap = memmapfile('cube.dat', ...
				'Format', {'single', obj.cubeSize, 'cube'}, ...
				'Writable', true, ...
				'Repeat', 1);


			scripts.zeroCube(obj.cubeMap.Data.cube);
			% Reshape to 4D array
			obj.cube = obj.cubeMap.Data.cube;

			fid = fopen("out.txt", "a+");
			fprintf(fid, "\n\n--------------------------------------------\n---                 START                ---\n--------------------------------------------\n");
			fclose(fid);
		end

		function addData(obj, yaw, pitch, ~, rangeDoppler, speed, ~)
			if nargin < 6
				speed = 0.01;
			end

			[~, yawIdx] = min(abs(obj.yawBins - yaw));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));
			decay = exp(-speed/1000); % XXX random consntat for debug

			fprintf("radarDataCube | addData | adding to cube %d: yaw %f, pitch %f, decay %f\n", obj.bufferActiveWriteIdx, yaw, pitch, decay);
			fprintf("radarDataCube | addData | processing %d\n", obj.isProcessing);
			obj.bufferA.yawIdx(obj.bufferActiveWriteIdx) = yawIdx;
			obj.bufferA.pitchIdx(obj.bufferActiveWriteIdx) = pitchIdx;
			obj.bufferA.rangeDoppler(:, :, obj.bufferActiveWriteIdx) = single(rangeDoppler);
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

			future = parfeval(gcp, @radarDataCube.processBatch, 0, processingBuffer, obj.antennaPattern, obj.cubeSize, obj.yawBins, obj.pitchBins);

			% radarDataCube.processBatch(processingBuffer, obj.antennaPattern, obj.cubeSize, obj.yawBins, obj.pitchBins);
			% future = parfeval(gcp, @radarDataCube.testFunction, 0);
			afterAll(future, @(varargin) obj.afterBatchProcessing(varargin{:}), 0);

		end

	end
end

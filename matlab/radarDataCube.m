classdef radarDataCube < handle
	properties(Access=public)
		yawBinMin=-179;
		yawBinMax=180;
		yawBins;   % 1° resolution
		pitchBinMin=-20;
		pitchBinMax=60;
		pitchBins;          % 1° resolution
		cube;          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		cubeMap;
		cubeSize;

		antennaPattern               % Weighting matrix [Yaw x Pitch]
		bufferA = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'decay', []); % Active buffer
		bufferB = struct('yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'decay', []); % Processing buffer
		bufferActive;
		bufferActiveWriteIdx = 1;
		batchSize = 8;
		isProcessing = false;
	end

	methods(Static)
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
			fid = fopen("out.txt", "a+");

			disp(cubeSize);
			m = memmapfile('cube.dat', ...
               'Format', {'single', cubeSize, 'cube'}, ...
               'Writable', true, ...
							 'Repeat', 1);

			% --- 1. Calculate composite region ---
			halfYaw = floor(size(antennaPattern, 1)/2);
			halfPitch = floor(size(antennaPattern, 2)/2);

			minYaw = min([buffer.yawIdx]) - halfYaw;
			maxYaw = max([buffer.yawIdx]) + halfYaw;
			minPitch = max(1, min([buffer.pitchIdx]) - halfPitch);
			maxPitch = min(length(pitchBins), max([buffer.pitchIdx]) + halfPitch);

			% Wrap yaw indices and clamp pitch
			yawIndices = mod((minYaw:maxYaw) - 1, length(yawBins)) + 1;
			pitchIndices = minPitch:maxPitch;

			fprintf(fid,...
				'[BATCH] Composite Region: Yaw=%d:%d (%d bins), Pitch=%d:%d (%d bins)\n',...
				minYaw, maxYaw, length(yawIndices), minPitch, maxPitch, length(pitchIndices)...
				);


			% --- 3. Initialize subcube for new contributions ---
			subCube = zeros(...
				length(yawIndices), ...
				length(pitchIndices), ...
				cubeSize(3), ...
				cubeSize(4), ...
				'single' ...
				);

			%printf("[BATCH] starting subcube processing\n");
			% Process each update
			for i = 1:numel(buffer.yawIdx)
				yaw = buffer.yawIdx(i);
				pitch = buffer.pitchIdx(i);
				fprintf(fid,"[UPDATE %d] Updating yaw=%f, pitch=%f\n", i, yaw, pitch);

				% Valid indices for this update
				validYaw = mod((yaw - halfYaw : yaw + halfYaw) - 1, length(yawBins)) + 1; % LGTM
				validPitch = max(1, pitch - halfPitch):min(length(pitchBins), pitch + halfPitch); % LGTM

				% Crop antenna pattern
				startPitchPat = max(1,(halfPitch+1)-(pitch-validPitch(1)));
				% disp(startPitchPat);
				endPitchPat = min(size(antennaPattern, 2), startPitchPat + length(validPitch) - 1);
				% disp(endPitchPat);
				adjPattern = antennaPattern(:, startPitchPat:endPitchPat);


				% Map to composite subcube
				[~, localYaw] = ismember(validYaw, yawIndices);
				[~, localPitch] = ismember(validPitch, pitchIndices);

				% Apply contribution
				contribution = buffer.rangeDoppler(:, :, i);
				contribution4D = reshape(contribution, [1, 1, size(contribution)]);

				weightedContribution = (adjPattern*prod(buffer.decay(i:end))) .* contribution4D;
				% Log update details
				fprintf(fid,...
					'[UPDATE %d] AZ=%d, PITCH=%d | PatternRows=%d:%d | LocalYaw=%d:%d, LocalPitch=%d:%d\n',...
					i, yaw, pitch, startPitchPat, endPitchPat,...
					localYaw(1), localYaw(end), localPitch(1), localPitch(end)...
					);

				% Add to subcube
				subCube(localYaw, localPitch, :, :) = ...
					subCube(localYaw, localPitch, :, :) + weightedContribution;
			end

			batchDecay = prod([buffer.decay]);
			fprintf(fid,"[BATCH] Accumulated decay: %d\n", batchDecay);


			% --- 5. Merge data ---

			m.Data.cube = m.Data.cube*batchDecay;
			m.Data.cube(yawIndices, pitchIndices, :, :) = m.Data.cube(yawIndices, pitchIndices, :, :) + subCube;
			fprintf(fid,"[BATCH] Updating radar cube, decaying with %d\n", batchDecay);
		end


	end

	methods(Access=public)


		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternYaw, radPatternPitch)
			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution

			fprintf("radarDataCube | radarDataCube | Initializing cube with yawimuth %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)
			obj.antennaPattern = obj.generateAntennaPattern(radPatternYaw, radPatternPitch);
			obj.bufferActive = obj.bufferA;

			obj.cubeSize = [ ...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				numRangeBins, ...
				numDopplerBins ...
				];
			
			if ~exist('cube.dat', 'file')
				system('fallocate -l 250M cube.dat'); % 360x81x128x8 single
			end

			% Create memory map
			obj.cubeMap = memmapfile('cube.dat', ...
               'Format', {'single', obj.cubeSize, 'cube'}, ...
               'Writable', true, ...
							 'Repeat', 1);

			% Reshape to 4D array
			obj.cube = obj.cubeMap.Data.cube;
		end

		function addData(obj, yaw, pitch, ~, rangeDoppler, speed, ~)
			if nargin < 6
				speed = 0.01;
			end

			[~, yawIdx] = min(abs(obj.yawBins - yaw));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));
			decay = exp(-speed/100); % XXX random consntat for debug
			obj.bufferA.yawIdx(obj.bufferActiveWriteIdx) = yawIdx;
			obj.bufferA.pitchIdx(obj.bufferActiveWriteIdx) = pitchIdx;
			obj.bufferA.rangeDoppler(:, :, obj.bufferActiveWriteIdx) = single(rangeDoppler);
			obj.bufferA.decay(obj.bufferActiveWriteIdx) = decay;
			obj.bufferActiveWriteIdx=obj.bufferActiveWriteIdx+1;

		end

		function status = isBatchFull(obj)
			status = (obj.bufferActiveWriteIdx+1 == obj.batchSize);
		end

		function future = processBatchAsync(obj)
			% Swap buffers and reset flags
			% TODO: verify if MATLAB isn't doing stupid copying data into new memory space
			
			% obj.isProcessing = true;

			processingBuffer = obj.bufferA;
			obj.bufferA = obj.bufferB; % Reset active buffer
			obj.bufferB = processingBuffer; % Assign to processing buffer
			obj.bufferActiveWriteIdx = 1;

			future = parfeval(gcp, @radarDataCube.processBatch, 0, processingBuffer, obj.antennaPattern, obj.cubeSize, obj.yawBins, obj.pitchBins);

			% radarDataCube.processBatch(processingBuffer, obj.antennaPattern, obj.cubeSize, obj.yawBins, obj.pitchBins);
			% future = parfeval(gcp, @radarDataCube.testFunction, 0);

		end





		function pattern = generateAntennaPattern(obj, radPatternYaw, radPatternPitch)
			dimensionsYaw = -radPatternYaw:radPatternYaw;
			dimensionsPitch = -radPatternPitch:radPatternPitch;
			yawSigma = 3*radPatternYaw / (sqrt(8*log(2)));
			pitchSigma = 1.5*radPatternPitch / (sqrt(8*log(2)));
			[yawMash, pitchMesh] = meshgrid(dimensionsPitch,dimensionsYaw);
			pattern = exp(-0.5*( (yawMash/yawSigma).^2 + (pitchMesh/pitchSigma).^2 ));
			% imagesc(dimensionsYaw, dimensionsPitch, pattern);
		end
	end
end

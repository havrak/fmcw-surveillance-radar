classdef radarDataCube < handle
	properties(Access=public)
		yawBinMin=-179;
		yawBinMax=180;
		yawBins;   % 1° resolution
		pitchBinMin=-20;
		pitchBinMax=60;
		pitchBins;          % 1° resolution
		cube          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		antennaPattern               % Weighting matrix [Yaw x Pitch]

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
	end



	methods(Access=public)


		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternYaw, radPatternPitch)
			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution

			fprintf("Initializing cube with azimuth %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)
			obj.antennaPattern = obj.generateAntennaPattern(radPatternYaw, radPatternPitch);

			obj.cube = zeros(...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				numRangeBins, ...
				numDopplerBins ...
				);

		end

		function addData(obj, azimuth, pitch, rangeProfile, rangeDoppler, speed, movementMask)
			tic;
			if nargin < 6
				speed = 0.01;
			end
			if nargin < 7
				movementMask = ones(size(obj.antennaPattern));
			end

			% decay = exp(-speed);
			% obj.cube = obj.cube * decay;
			fprintf("CHECKPOINT 1\n");
			[~, azIdx] = min(abs(obj.yawBins - azimuth));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));

			fprintf("CHECKPOINT 2\n");
			halfYawPat = floor(size(obj.antennaPattern, 1)/2);
			yawIndices = azIdx + (-halfYawPat:halfYawPat);
			yawIndices = mod(yawIndices - 1, length(obj.yawBins)) + 1;


			fprintf("CHECKPOINT 3\n");
			halfPitchPat = floor(size(obj.antennaPattern, 2)/2);
			pitchStart = max(1, pitchIdx - halfPitchPat); % we might need to do a crop
			pitchEnd = min(length(obj.pitchBins), pitchIdx + halfPitchPat);
			pitchIndices = pitchStart:pitchEnd;


			startPatternRow = max(1, (halfPitchPat + 1) - (pitchIdx - pitchStart));
			endPatternRow = min(size(obj.antennaPattern, 2), startPatternRow + length(pitchIndices) - 1);
			patternIdx = startPatternRow:endPatternRow;
			adjAntennaPattern = obj.antennaPattern(patternIdx, :);
			adjMovementMask = movementMask(patternIdx, :);

			fprintf("CHECKPOINT 6\n");
			% Reshape movementMask and antennaPattern to [Yaw x Pitch x 1 x 1]
			movementMask_4D = reshape(adjMovementMask, [size(adjMovementMask), 1, 1]); % [Yaw x Pitch x 1 x 1]
			antennaPattern_4D = reshape(adjAntennaPattern, [size(adjAntennaPattern), 1, 1]); % [Yaw x Pitch x 1 x 1]

			% Reshape rangeDoppler to [1 x 1 x FastTime x SlowTime]
			rangeDoppler_4D = reshape(rangeDoppler, [1, 1, size(rangeDoppler)]); % [1 x 1 x F x S]

			% Scale new data by accumulated decay
			% scaledRangeDoppler = rangeDoppler_4D * obj.accumulatedDecay;

			% Update subcube: Apply movementMask to old data, antennaPattern to new data
			subCube = obj.cube(yawIndices, pitchIndices, :, :);
			subCube = subCube .* movementMask_4D + antennaPattern_4D .* rangeDoppler_4D;
			obj.cube(yawIndices, pitchIndices, :, :) = subCube;
			toc;
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

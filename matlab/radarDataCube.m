classdef radarDataCube < handle
	properties(Access=public)
		yawBinMin=-179;
		yawBinMax=180;
		yawBins;   % 1° resolution
		pitchBinMin=-20;
		pitchBinMax=60;
		pitchBins;          % 1° resolution
		rangeAzimuthDoppler          % 4D matrix [Yaw x Pitch x (Fast time x Slow Time)]
		antennaPattern               % Weighting matrix [Yaw x Pitch]


	end

	methods(Static)
		function mask = createSectorMask(diffYaw, diffPitch, patternSize, speed)
			[yawGrid, pitchGrid] = meshgrid(...
				linspace(-1, 1, patternSize(2)), ...
				linspace(-1, 1, patternSize(1)) ...
				);

			moveAngle = atan2d(diffPitch, -diffYaw);
			sectorWidth = 30 + 150 * exp(-speed/5); % upon staationary we should get 360 -> null whole area

			gridAngles = atan2d(pitchGrid, yawGrid);
			angleDiff = abs(mod(gridAngles - moveAngle + 180, 360) - 180);

			distances = sqrt(yawGrid.^2 + pitchGrid.^2);

			mask = zeros(patternSize);
			sector = angleDiff <= sectorWidth;
			mask(sector) = exp(-distances(sector).^2 / 0.2); % Gaussian falloff

			mask = mask ./ max(mask(:));

			% imagesc(mask);
			% axis equal tight; colorbar;
			% drawnow;
		end
	end



	methods


		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternYaw, radPatternPitch)
			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution

			obj.rangeAzimuthDoppler = zeros(...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				numRangeBins, ...
				numDopplerBins ...
				);
			fprintf("Initializing cube with azimuth %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)
			obj.antennaPattern = obj.generateAntennaPattern(radPatternYaw, radPatternPitch);


			%   diffYaw = 30-40;
			%   diffPitch = 40-0;
			% speed = 30;
			%     movement_mask = radarDataCube.createSectorMask(diffYaw, diffPitch, ...
			%                                   size(obj.antennaPattern), ...
			%                                   speed);

		end

		function addData(obj, azimuth, pitch, rangeProfile, rangeDoppler)
			[~, azIdx] = min(abs(obj.yawBins - azimuth));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));

			limitYawMin = azIdx-floor(size(obj.antennaPattern, 2)/2); % this needs to figure out overflow
			limitYawMax = azIdx+floor(size(obj.antennaPattern, 2)/2);
			limitPitchMin = max(1, pitchIdx-floor(size(obj.antennaPattern, 1)/2));
			limitPitchMax = min(length(obj.pitchBins), pitchIdx+floor(size(obj.antennaPattern, 1)/2));

			fprintf("Yaw: [%f  %f] Pitch [%f %f]", limitYawMin, limitYawMax, limitPitchMin, limitPitchMax);
			azPatternIndex = 1;
			pitchPatterIndex=max(1,1-(pitchIdx-floor(size(obj.antennaPattern, 1)/2)));


			for az = limitYawMin:limitYawMax
				for tl = limitPitchMin:limitPitchMax
					% now find correct index in pattern map

					% fprintf("Starting applying pattern from: yaw: %f pitch: %f, applying to yaw: %f pitch %f\n", azPatternIndex, pitchPatterIndex, az, tl);

					weight = obj.antennaPattern(pitchPatterIndex,azPatternIndex);
					oldVal(:,:) = obj.rangeAzimuthDoppler(az, tl, :, :);
					obj.rangeAzimuthDoppler(az, tl, :, :) = ...
						weight * rangeDoppler; % Weight range profile, don't weight doppler profile
					% oldVal + ...
					pitchPatterIndex= pitchPatterIndex+1;
				end
				pitchPatterIndex=1;
				azPatternIndex= azPatternIndex+1;
			end

		end

		function pattern = generateAntennaPattern(obj, radPatternYaw, radPatternPitch)
			dimensionsH = -2*radPatternYaw:2*radPatternYaw;
			dimensionsT = -radPatternPitch:radPatternPitch;
			azSigma = radPatternYaw / (sqrt(8*log(2)));
			pitchSigma = radPatternPitch / (sqrt(8*log(2)));
			[azMesh, pitchMesh] = meshgrid(dimensionsH, dimensionsT);
			pattern = exp(-0.5*( (azMesh/azSigma).^2 + (pitchMesh/pitchSigma).^2 ));
			imagesc(dimensionsH, dimensionsT, pattern);
		end
	end
end

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
				linspace(-1, 1, patternSize(2)), ...
				linspace(-1, 1, patternSize(1)) ...
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
			if nargin < 6
				speed = 0.01;
			end

			if nargin < 7
				movementMask = ones(size(obj.antennaPattern));
			end

			% NOTE: if stationary there is no decay (only on current position we
			% aren't using old data);
			decay=exp(-speed); % the faster we are moving the faster we should forget
			% obj.rangeAzimuthDoppler = obj.rangeAzimuthDoppler*decay;
			
			[~, azIdx] = min(abs(obj.yawBins - azimuth));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));
		
			limitYawMin = azIdx-floor(size(obj.antennaPattern, 2)/2); % this needs to figure out overflow
			limitYawMax = azIdx+floor(size(obj.antennaPattern, 2)/2);
		
			yawIndRange = limitYawMin:limitYawMax;
			yawIndRangeWrapped = mod(yawIndRange - 1, 360) + 1;
			
			limitPitchMin = max(1, pitchIdx-floor(size(obj.antennaPattern, 1)/2));
			limitPitchMax = min(length(obj.pitchBins), pitchIdx+floor(size(obj.antennaPattern, 1)/2));
			
			yawPatternIndex = 1;
			pitchPatternIndex=max(1,1-(pitchIdx-floor(size(obj.antennaPattern, 1)/2)));
			
			fprintf("Writing to: Yaw: [%f  %f] Pitch [%f %f] Speed %f, Decay %f\n", limitYawMin, limitYawMax, limitPitchMin, limitPitchMax, speed, decay);
			
			
			for i = 1:length(yawIndRangeWrapped)
				yawIdx = yawIndRangeWrapped(i);
				for pitchIdx = limitPitchMin:limitPitchMax

					weightNew = obj.antennaPattern(pitchPatternIndex,yawPatternIndex);
					weightOld = movementMask(pitchPatternIndex, yawPatternIndex);

					oldVal(:,:) = obj.cube(yawIdx, pitchIdx, :, :)*weightOld;

					obj.cube(yawIdx, pitchIdx, :, :) = ...
						oldVal + ...
						weightNew * rangeDoppler; % Weight range profile, don't weight doppler profile
					pitchPatternIndex= pitchPatternIndex+1;
				end
				pitchPatternIndex=1;
				yawPatternIndex = yawPatternIndex+1;
			end
		end


		function pattern = generateAntennaPattern(obj, radPatternYaw, radPatternPitch)
			dimensionsH = -2*radPatternYaw:2*radPatternYaw;
			dimensionsT = -radPatternPitch:radPatternPitch;
			azSigma = radPatternYaw / (sqrt(8*log(2)));
			pitchSigma = radPatternPitch / (sqrt(8*log(2)));
			[azMesh, pitchMesh] = meshgrid(dimensionsH, dimensionsT);
			pattern = exp(-0.5*( (azMesh/azSigma).^2 + (pitchMesh/pitchSigma).^2 ));
			% imagesc(dimensionsH, dimensionsT, pattern);
		end
	end
end

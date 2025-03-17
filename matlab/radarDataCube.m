classdef radarDataCube < handle
	properties(Access=public)
		AzimuthBins = -179:1:180     % 1° resolution
		TiltBins = -10:1:30       % 1° resolution
		RangeAzimuthDoppler       % 4D matrix [Azimuth x Tilt x (Fast time x Slow Time)]
		RangeAzimuth;             % 3D matrix [Azimuth x Tilt x Fast time]
		AntennaPattern            % Weighting matrix [Azimuth x Tilt]
		radPatterH
		radPatterT
	end

	methods
		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternH, radPatternT)
			obj.RangeAzimuthDoppler = zeros(...
				length(obj.AzimuthBins), ...
				length(obj.TiltBins), ...
				numRangeBins, ...
				numDopplerBins ...
				);

			obj.AntennaPattern = obj.generateAntennaPattern(radPatternH, radPatternT);
		end

		function addData(obj, azimuth, tilt, rangeProfile, dopplerProfile)
			[~, azIdx] = min(abs(obj.AzimuthBins - azimuth));
			[~, tiltIdx] = min(abs(obj.TiltBins - tilt));

			azWeights = obj.AntennaPattern(azIdx, :);
			tiltWeights = obj.AntennaPattern(:, tiltIdx);

			for az = 1:length(obj.AzimuthBins)
				for tl = 1:length(obj.TiltBins)
					weight = azWeights(az) * tiltWeights(tl);
					obj.RangeAzimuthDoppler(az, tl, :, :) = ...
						obj.RangeAzimuthDoppler(az, tl, :, :) + ...
						(weight * rangeProfile') * dopplerProfile; % Weight range profile, don't weight doppler profile
					obj.RangeAzimuth(az, tl, :, :) = ...
						obj.RangeAzimuth(az, tl, :, :) + ...
						(weight * rangeProfile);

				end
			end

			% TODO normalize energy (current approach leads to saturation)
		end

		function pattern = generateAntennaPattern(obj, radPatternH, radPatternT)
			% For now we model radiation pattern as 2D gaussian function
			azSigma = radPatternH / (sqrt(8*log(2)));
			tiltSigma = radPatternT / (sqrt(8*log(2)));
			[azMesh, tiltMesh] = meshgrid(obj.AzimuthBins, obj.TiltBins);
			pattern = exp(-0.5*( (azMesh/azSigma).^2 + (tiltMesh/tiltSigma).^2 ));
		end
	end
end
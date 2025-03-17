classdef radarDataCube < handle
    properties
        AzimuthBins = 0:1:360     % 1° resolution 
        TiltBins = -10:1:30       % 1° resolution 
        RangeAzimuthDoppler       % 4D matrix [Azimuth x Tilt x (Fast time x Slow Time)]
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
            % Find nearest azimuth/tilt bins and apply antenna weighting
            [~, azIdx] = min(abs(obj.AzimuthBins - azimuth));
            [~, tiltIdx] = min(abs(obj.TiltBins - tilt));
            
            % Apply antenna pattern weights to neighboring bins
            azWeights = obj.AntennaPattern(azIdx, :);
            tiltWeights = obj.AntennaPattern(:, tiltIdx);
            
            % Update 4D cube (simplified example)
            for az = 1:length(obj.AzimuthBins)
                for tl = 1:length(obj.TiltBins)
                    weight = azWeights(az) * tiltWeights(tl);
                    obj.RangeAzimuthDoppler(az, tl, :, :) = ...
                        obj.RangeAzimuthDoppler(az, tl, :, :) + ...
                        (weight * rangeProfile') * dopplerProfile; % Weight range profile, don't weight doppler profile
                end
            end
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
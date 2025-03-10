classdef radarDataCube < handle
    properties
        AzimuthBins = 0:1:360    % 1° resolution (customize)
        TiltBins = -10:1:30       % 1° resolution (customize)
        RangeBins                 % Derived from FFT
        DopplerBins               % Derived from Doppler FFT
        RangeAzimuthDoppler       % 4D matrix [Azimuth x Tilt x Range x Doppler]
        AntennaPattern            % Weighting matrix [Azimuth x Tilt]
    end

    methods
        function obj = radarDataCube(numRangeBins, numDopplerBins)
            obj.RangeBins = 1:numRangeBins;
            obj.DopplerBins = 1:numDopplerBins;
            obj.RangeAzimuthDoppler = zeros(...
                length(obj.AzimuthBins), ...
                length(obj.TiltBins), ...
                numRangeBins, ...
                numDopplerBins ...
            );
            obj.AntennaPattern = obj.generateAntennaPattern();
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
                        weight * (rangeProfile' * dopplerProfile);
                end
            end
        end

        function pattern = generateAntennaPattern(obj)
            % Example: Gaussian beam pattern (3° beamwidth)
            azSigma = 3 / (2*sqrt(2*log(2))); % Convert to std
            tiltSigma = 3 / (2*sqrt(2*log(2)));
            [azMesh, tiltMesh] = meshgrid(obj.AzimuthBins, obj.TiltBins);
            pattern = exp(-0.5*( (azMesh/azSigma).^2 + (tiltMesh/tiltSigma).^2 ));
        end
    end
end
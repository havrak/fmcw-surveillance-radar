classdef radarDataCube < handle
	properties(Access=public)
		azimuthBins = -179:1:180     % 1° resolution
		tiltBins = -90:1:90       % 1° resolution
		rangeAzimuthDoppler       % 4D matrix [Azimuth x Tilt x (Fast time x Slow Time)]
		antennaPattern            % Weighting matrix [Azimuth x Tilt]
		
	end

	methods
		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternH, radPatternT)
			obj.rangeAzimuthDoppler = zeros(...
				length(obj.azimuthBins), ...
				length(obj.tiltBins), ...
				numRangeBins, ...
				numDopplerBins ...
				);

			obj.antennaPattern = obj.generateAntennaPattern(radPatternH, radPatternT);
			size(obj.antennaPattern)
		end

		function addData(obj, azimuth, tilt, rangeProfile, dopplerProfile)
			[~, azIdx] = min(abs(obj.azimuthBins - azimuth));
			[~, tiltIdx] = min(abs(obj.tiltBins - tilt));

			%azWeights = obj.antennaPattern(azIdx, :);
			%dim(azWeights)
			%tiltWeights = obj.antennaPattern(:, tiltIdx);
			%dim(tiltWeights)
			limitAzMin = azIdx-floor(size(obj.antennaPattern, 2)/2);
			limitAzMax = azIdx+floor(size(obj.antennaPattern, 2)/2);
			limitTlMin = max(-90, tiltIdx-floor(size(obj.antennaPattern, 1)/2));
			limitTlMax = min(90, tiltIdx-floor(size(obj.antennaPattern, 1)/2));
			azPatternIndex = 1;
			tiltPatterIndex=tiltIdx-floor(size(obj.antennaPattern, 1)/2)-limitTlMin+1;
			disp(tiltPatterIndex);
			
			size(obj.rangeAzimuthDoppler)
			size(obj.antennaPattern)
			for az = limitAzMin:limitAzMax
				for tl = limitTlMin:limitTlMax
					% now find correct index in pattern map
					
					fprintf("Starting applying pattern from: horz: %f tilt: %f", azPatternIndex, tiltPatterIndex);
					weight = obj.antennaPattern(tiltPatterIndex,azPatternIndex);
					obj.rangeAzimuthDoppler(az, tl, :, :) = ...
						obj.rangeAzimuthDoppler(az, tl, :, :) + ...
					weight * (rangeProfile' * dopplerProfile); % Weight range profile, don't weight doppler profile
					tiltPatterIndex= tiltPatterIndex+1;
				end
				azPatternIndex= azPatternIndex+1;
			end

			% TODO normalize energy (current approach leads to saturation)
		end

		function pattern = generateAntennaPattern(obj, radPatternH, radPatternT)
			% For now we model radiation pattern as 2D gaussian function
			dimensionsH = -radPatternH:radPatternH;
			dimensionsT = -radPatternT:radPatternT;
			azSigma = radPatternH / (sqrt(8*log(2)));
			tiltSigma = radPatternT / (sqrt(8*log(2)));
			[azMesh, tiltMesh] = meshgrid(dimensionsH, dimensionsT);
			pattern = exp(-0.5*( (azMesh/azSigma).^2 + (tiltMesh/tiltSigma).^2 ));
			% imagesc(dimensionsH, dimensionsT, pattern);
			
		end
	end
end
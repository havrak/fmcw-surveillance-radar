classdef radarDataCube < handle
	properties(Access=public)
		azimuthBinMin=-179;
		azimuthBinMax=180;
		azimuthBins;   % 1° resolution
		tiltBinMin=-20;
		tiltBinMax=60;
		tiltBins;          % 1° resolution
		rangeAzimuthDoppler          % 4D matrix [Azimuth x Tilt x (Fast time x Slow Time)]
		antennaPattern               % Weighting matrix [Azimuth x Tilt]

		
	end

	methods
		function obj = radarDataCube(numRangeBins, numDopplerBins, radPatternH, radPatternT)
			obj.azimuthBins = obj.azimuthBinMin:obj.azimuthBinMax;     % 1° resolution
			obj.tiltBins = obj.tiltBinMin:obj.tiltBinMax;          % 1° resolution

			obj.rangeAzimuthDoppler = zeros(...
				length(obj.azimuthBins), ...
				length(obj.tiltBins), ...
				numRangeBins, ...
				numDopplerBins ...
				);
			fprintf("Initializing cube with azimuth %f, tilt %f, range %d, doppler %f\n", length(obj.azimuthBins), length(obj.tiltBins), numRangeBins, numDopplerBins)
			obj.antennaPattern = obj.generateAntennaPattern(radPatternH, radPatternT);

		end

		function addData(obj, azimuth, tilt, rangeProfile, rangeDoppler)
			[~, azIdx] = min(abs(obj.azimuthBins - azimuth));
			[~, tiltIdx] = min(abs(obj.tiltBins - tilt));

			limitAzMin = azIdx-floor(size(obj.antennaPattern, 2)/2); % this needs to figure out overflow 
			limitAzMax = azIdx+floor(size(obj.antennaPattern, 2)/2);
			limitTlMin = max(1, tiltIdx-floor(size(obj.antennaPattern, 1)/2));
			limitTlMax = min(length(obj.tiltBins), tiltIdx+floor(size(obj.antennaPattern, 1)/2));
			fprintf("Az: [%f  %f] Tl [%f %f]", limitAzMin, limitAzMax, limitTlMin, limitTlMax);
			azPatternIndex = 1;
			tiltPatterIndex=max(1,1-(tiltIdx-floor(size(obj.antennaPattern, 1)/2)));
			

			for az = limitAzMin:limitAzMax
				for tl = limitTlMin:limitTlMax
					% now find correct index in pattern map
					
					% fprintf("Starting applying pattern from: horz: %f tilt: %f, applying to horz: %f tilt %f\n", azPatternIndex, tiltPatterIndex, az, tl);
					
					weight = obj.antennaPattern(tiltPatterIndex,azPatternIndex);
					oldVal(:,:) = obj.rangeAzimuthDoppler(az, tl, :, :);
					obj.rangeAzimuthDoppler(az, tl, :, :) = ...
						weight * rangeDoppler; % Weight range profile, don't weight doppler profile
						% oldVal + ...
					tiltPatterIndex= tiltPatterIndex+1;
				end
				tiltPatterIndex=1;
				azPatternIndex= azPatternIndex+1;
			end

			% TODO normalize energy (current approach leads to saturation)
		end

		function pattern = generateAntennaPattern(obj, radPatternH, radPatternT)
			% For now we model radiation pattern as 2D gaussian function
			dimensionsH = -2*radPatternH:2*radPatternH;
			dimensionsT = -radPatternT:radPatternT;
			azSigma = radPatternH / (sqrt(8*log(2)));
			tiltSigma = radPatternT / (sqrt(8*log(2)));
			[azMesh, tiltMesh] = meshgrid(dimensionsH, dimensionsT);
			pattern = exp(-0.5*( (azMesh/azSigma).^2 + (tiltMesh/tiltSigma).^2 ));
			% imagesc(dimensionsH, dimensionsT, pattern);
			
		end
	end
end
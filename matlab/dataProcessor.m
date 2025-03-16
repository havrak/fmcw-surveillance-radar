classdef dataProcessor < handle
	properties
		hRadar radar              % Radar object
		hPlatform platform        % Platform object
		hPreferences preferences  % Preferences object
		parallelPool              % Parallel pool handle
		isProcessing = false      % Flag to prevent overlap
		hRadarBuffer radarBuffer; %
		hDataCube radarDataCube;  % RadarDataCube instance

		readIdx = 0;
	end

	methods
		function obj = dataProcessor(radarObj, platformObj, preferencesObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			obj.parallelPool = gcp('nocreate'); % Start parallel pool
			if isempty(obj.parallelPool)
				obj.parallelPool = parpool(1); % Start pool with 1 worker
			end
			obj.hRadarCube = radarDataCube();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
		end

		function onNewDataAvailable(obj)
			% Called automatically when radar data arrives
			% TODO: add chirp to buffer -> run processing on buffer
			% on addition to chirps processing will also get data about positions

			%
			% if ~obj.isProcessing
			%	obj.isProcessing = true; % Lock processing

			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(obj.readIdx), ...
				obj.hRadar.bufferQ(obj.readIdx), ...
				obj.hRadar.bufferTime(obj.readIdx));

			[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
			[posTimes, horz, tilt] = obj.hPlatform.getPositionsInInterval(min(batchTimes), max(batchTimes));


			future = parfeval(obj.parallelPool, ...
				@DataProcessor.processBatch, 5, ...
				batchRangeFFTs, batchTimes, posTimes, horz, tilt);

			afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
		end


		function [horz, tilt, rangeProfile, dopplerProfile] = ...
			processBatch(batchRangeFFTs, batchTimes, posTimes, posHorz, posTilt)
			% Check platform dynamics
			
			dt = diff(posTimes);
			dHorz = diff(posHorz)./dt;
			dTilt = diff(posTilt)./dt;
			
			if any(abs(dHorz) > 3) || any(abs(dTilt) > 1) % Thresholds (deg/s)
				error('Platform movement too fast');
			end

			% Interpolate angles to chirp timestamps
			% interp1(posTimes, posHorz, batchTimes, 'linear', 'extrap');
			% interp1(posTimes, posTilt, batchTimes, 'linear', 'extrap');

			% Compute Doppler FFT (using cached Range FFTs)
			dopplerProfile = fftshift(fft(batchRangeFFTs, [], 1), 2);

			% Use latest sample's angles for output
			rangeProfile = batchRangeFFTs(end, :);
			horz = posHorz(end);
			tilt = posTilt(end);
		end

		function mergeResults(obj, azimuth, tilt, rangeProfile, dopplerProfile)
			obj.hDataCube.addData(azimuth, tilt, rangeProfile, dopplerProfile);

			end
	end
end
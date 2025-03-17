classdef dataProcessor < handle
	properties
		hRadar radar              % Radar object
		hPlatform platformControl % Platform object
		hPreferences preferences  % Preferences object
		parallelPool              % Parallel pool handle
		isProcessing = false      % Flag to prevent overlap
		hRadarBuffer radarBuffer; %
		hDataCube radarDataCube;  % RadarDataCube instance

		readIdx = 0;
	end

	methods(Access=private)
		function [horz, tilt, rangeProfile, dopplerProfile] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posHorz, posTilt)

			% Allow only small movement changes (deg/s)
			dt = diff(posTimes);
			dHorz = gradient(posHorz, dt);
			dTilt = gradient(posTilt, dt);
			lowBoundIndex = 1;

			for i=length(dHorz):-1:1
				if any(abs(dHorz) > 3) || any(abs(dTilt) > 1)
					break;
				end
				lowBoundIndex=i;
			end

			batchRangeFFTs(1:lowBoundIndex) = [];
			batchTimes(1:lowBoundIndex) = [];


			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			maxDeviation = max(abs(timeDeltas - meanInterval)) / meanInterval;
			useNUFFT = maxDeviation > 0.2;

			if useNUFFT
				% USE FFT
			else
				% Regular FFT for uniform sampling
				dopplerProfile = fftshift(fft(batchRangeFFTs, [], 1), 1);
			end


			% Decimate/Interpolate spectrum to fit 16 slots

			% Compute Doppler FFT (using cached Range FFTs)
			% Use latest sample's angles for output
			rangeProfile = batchRangeFFTs(end, :);
			horz = posHorz(end);
			tilt = posTilt(end);
		end

		function mergeResults(obj, azimuth, tilt, rangeProfile, dopplerProfile)
			obj.hDataCube.addData(azimuth, tilt, rangeProfile, dopplerProfile);

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
	end

	methods(Access=public)
		function obj = dataProcessor(radarObj, platformObj, preferencesObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			obj.parallelPool = gcp('nocreate'); % Start parallel pool
			if isempty(obj.parallelPool)
				obj.parallelPool = parpool(1); % Start pool with 1 worker
			end
			[radPatterH, radPatterT] = preferencesObj.getRadarRadiationParamters();
			[samples, ~, ~ ] = preferencesObj.getRadarBasebandParameters();
			obj.hDataCube = radarDataCube(samples, 32, radPatterH, radPatterT);

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
		end



		function endProcesses(obj)
			% endProcesses: safely stops all class processes
			delete(obj.parallelPool);
		end



	end
end
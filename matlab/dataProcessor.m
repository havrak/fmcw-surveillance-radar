classdef dataProcessor < handle
	properties
		hRadar radar              % Radar object
		hPlatform platformControl % Platform object
		hPreferences preferences  % Preferences object
		parallelPool              % Parallel pool handle
		isProcessing = false      % Flag to prevent overlap
		hRadarBuffer radarBuffer; %
		hDataCube radarDataCube;  % RadarDataCube instance
		hPanel;

		hImage;
		hAxes;

		readIdx = 1;
		radarBufferSize = 100;
	end

	methods(Static, Access=private)

	
		function [horz, tilt, rangeProfile, dopplerProfile] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posHorz, posTilt)

			if(isempty(posTimes)) % NOTE just for testing
				posHorz = [0];
				posTilt = [0];
				posTimes = [0];
			
			end
			fid = fopen('processing.txt', 'a+');
			fprintf(fid, "New batch\n");
			

			% Allow only small movement changes (deg/s)
			dt = diff(posTimes);
			dHorz = gradient(posHorz, dt);
			dTilt = gradient(posTilt, dt);
			lowBoundIndex = length(dHorz);

			for i=length(dHorz):-1:1
				% stop on fast change
				if any(abs(dHorz) > 5) || any(abs(dTilt) > 10) 
					break;
				end
				% TODO stop on accumulated distance (if radar moved too much we
				% cannot use these spectrum also
				lowBoundIndex=i;
			end

			if lowBoundIndex ~= 1
				% we need to find closes timestamp in batchTimes 
				[~, idxMin] = min(abs(batchTimes - posTimes(lowBoundIndex)));
				batchRangeFFTs(1:idxMin) = [];
				batchTimes(1:idxMin) = [];
				fprintf(fid, "Restricting spectrum count due to too fast movement")
			end



			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			maxDeviation = max(abs(timeDeltas - meanInterval)) / meanInterval;
			useNUFFT = maxDeviation > 0.2;

			if useNUFFT
				% USE FFT
				fprintf(fid, "ugly timing detected\n");
			end
			%else
				% Regular FFT for uniform sampling
			dopplerProfile = fftshift(fft(batchRangeFFTs, [], 1), 1);
			%end


			% TODO Decimate/Interpolate spectrum to fit 16 slots

			% Compute Doppler FFT (using cached Range FFTs)
			% Use latest sample's angles for output
			rangeProfile = batchRangeFFTs(end, :);
			horz = posHorz(end);
			tilt = posTilt(end);
			fclose(fid);

		end
	end

	methods(Access=private)
		function mergeResults(obj, azimuth, tilt, rangeProfile, dopplerProfile)
			fprintf("Processing ended\n");
			disp(azimuth)
			obj.hDataCube.addData(azimuth, tilt, rangeProfile, dopplerProfile);
			obj.isProcessing = false;

			% Draw range azimuth
			data = obj.hDataCube.RangeAzimuth(:, 10, :);
			obj.hImage.CData = data;
			drawnow limitrate;

		end

		function onNewDataAvailable(obj)
			% Called automatically when radar data arrives
			% TODO: add chirp to buffer -> run processing on buffer
			% on addition to chirps processing will also get data about positions



			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(obj.readIdx), ...
				obj.hRadar.bufferQ(obj.readIdx), ...
				obj.hRadar.bufferTime(obj.readIdx));
			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;

			% if ~obj.isProcessing
			%	obj.isProcessing = true; % Lock processing
			[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
			[posTimes, horz, tilt] = obj.hPlatform.getPositionsInInterval(min(batchTimes), max(batchTimes));
		
			disp("Launching parfeval");
			future = parfeval(obj.parallelPool, ...
				@dataProcessor.processBatch, 4, ...
				batchRangeFFTs, batchTimes, posTimes, horz, tilt);
			
			
			afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			%end
		end

		   function initializeARDisplay(obj)
            obj.hAxes = axes('Parent', obj.hPanel, ...
                          'Units', 'normalized', ...
                          'Position', [0 0 1 1]);
            
						[samples, ~, ~ ] = obj.hPreferences.getRadarBasebandParameters();
            initialData = zeros(360, samples);
            shiftedData = fftshift(initialData, 2);
            
            obj.hImage = imagesc(obj.hAxes, ...
                obj.hDataCube.AzimuthBins, ...
                1:samples, shiftedData);
            
            axis(obj.hAxes, 'xy');
            xlabel(obj.hAxes, 'Azimuth (degrees)');
            ylabel(obj.hAxes, 'Range (bin)');
            title(obj.hAxes, 'Azimuth-Range Map');
            colormap(obj.hAxes, 'jet');
            colorbar(obj.hAxes);
			 end

	end

	methods(Access=public)
		function obj = dataProcessor(radarObj, platformObj, preferencesObj, panelObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			obj.parallelPool = gcp('nocreate'); % Start parallel pool
			if isempty(obj.parallelPool)
				obj.parallelPool = parpool(4); % Start pool with 1 worker
			end

			% TODO -> move some of these paramters to be updateable on the fly
			[radPatterH, radPatterT] = preferencesObj.getRadarRadiationParamters();
			[samples, ~, ~ ] = preferencesObj.getRadarBasebandParameters();
			obj.hDataCube = radarDataCube(samples, 32, radPatterH, radPatterT);
			obj.hRadarBuffer = radarBuffer(32, samples);
			obj.hPanel = panelObj;

			obj.initializeARDisplay();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
		end




		function endProcesses(obj)
			% endProcesses: safely stops all class processes
			delete(obj.parallelPool);
		end



	end
end


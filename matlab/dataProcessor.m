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
		currentDisplayMethod;

		readIdx = 1;
		radarBufferSize = 100;
		speedBins = 16;
		currentVisualizationStyle = '';
	end

	methods(Static, Access=private)


		function [horz, tilt, rangeProfile, rangeDoppler] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posHorz, posTilt, speedBins)
	
			%fid = fopen('processing.txt', 'a+');
			fprintf("-------------------\nNew batch\n");


			if(isempty(posTimes)) % NOTE just for testing
				posHorz = 0;
				posTilt = 0;
				posTimes = [0 0];

			end
		
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
				fprintf("Restricting spectrum count due to too fast movement");
			end



			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			maxDeviation = max(abs(timeDeltas - meanInterval)) / meanInterval;
			useNUFFT = maxDeviation > 0.2;

			if useNUFFT
				% USE FFT
				fprintf("ugly timing detected\n");
			end
			% else
			% Regular FFT for uniform sampling
			%rangeDoppler = abs(fftshift(fft(batchRangeFFTs, 16, 1), 1)');

			rangeDoppler = abs(fft(batchRangeFFTs, 16, 1))';
			%end


			% TODO Decimate/Interpolate spectrum to fit 16 slots

			% Compute Doppler FFT (using cached Range FFTs)
			% Use latest sample's angles for output
			rangeProfile = batchRangeFFTs(end, :);
			if posHorz(end) > 180
				horz = posHorz(end)-360;
			else
				horz = posHorz(end);
			end
			tilt = posTilt(end);

			fprintf("Dimensions Fast time: %f, Slow time %f\n", length(rangeProfile), length(rangeDoppler))
			% fclose(fid);

		end
	end

	methods(Access=private)
		function mergeResults(obj, azimuth, tilt, rangeProfile, rangeDoppler)
			fprintf("Processing ended\n");
			disp(azimuth)
			obj.hDataCube.addData(azimuth, tilt, rangeProfile, rangeDoppler);
			obj.isProcessing = false;

			% Draw range azimuth
			if strcmp(obj.currentVisualizationStyle,'Range-Azimuth')
				data = sum(obj.hDataCube.rangeAzimuthDoppler,4);
				toDraw(:,:) = data(:,20, :);
				obj.hImage.CData = toDraw';
				drawnow limitrate;
			end

		end

		function onNewConfigAvailable(obj)
			% TODO Deinitilize variables

			[radPatterH, radPatterT] = obj.hPreferences.getRadarRadiationParamters();
			[samples, ~, ~ ] = obj.hPreferences.getRadarBasebandParameters();

			obj.speedBins = obj.hPreferences.getProcessingSpeedBins();
			visual=obj.hPreferences.getProcessingVisualization();
			
			obj.hDataCube = radarDataCube(samples, obj.speedBins, radPatterH, radPatterT);
			obj.hRadarBuffer = radarBuffer(32, samples);
			
			
			if strcmp(visual, 'Range-Azimuth') && ~strcmp(obj.currentVisualizationStyle, 'Range-Azimuth')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing as azimuth-range map.\n")
				% this will requite deinitialization of previous visualization
				obj.currentVisualizationStyle = 'Range-Azimuth';
				% obj.deinitializeDisplay();
				obj.initializeARDisplay();
			end
			java.lang.System.gc();
		end

		function onNewDataAvailable(obj) 
			% Called automatically when radar data arrives
			% TODO: add chirp to buffer -> run processing on buffer
			% on addition to chirps processing will also get data about positions



			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(obj.readIdx, :), ...
				obj.hRadar.bufferQ(obj.readIdx, :), ...
				obj.hRadar.bufferTime(obj.readIdx));
			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;

			% if ~obj.isProcessing
			%	obj.isProcessing = true; % Lock processing
			[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
			[posTimes, horz, tilt] = obj.hPlatform.getPositionsInInterval(min(batchTimes), max(batchTimes));

			%future = parfeval(obj.parallelPool, ...
			%	@dataProcessor.processBatch, 4, ...
			%	batchRangeFFTs, batchTimes, posTimes, horz, tilt, obj.speedBins);

			[azimuth, tilt, rangeProfile, dopplerProfile] = dataProcessor.processBatch(batchRangeFFTs, batchTimes, posTimes, horz, tilt, obj.speedBins);
			obj.mergeResults(azimuth, tilt, rangeProfile, dopplerProfile);

			%afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			%end
		end
		
		function deinitializeDisplay(obj)
			if ~isempty(obj.hAxes)
				delete(obj.hAxes);
				delete(obj.hImage);
			end
		end
		function initializeARDisplay(obj)
			panelPos = get(obj.hPanel, 'Position');
			width = panelPos(3);
			height = panelPos(4);

			obj.hAxes = axes('Parent', obj.hPanel, ...
				'Units', 'normalized', ...
				'Position', [0.1 0.1 0.8 0.8]);

			[samples, ~, ~ ] = obj.hPreferences.getRadarBasebandParameters();
			initialData = zeros(360, samples);
			%initialData = [zeros(120, samples) ones(120, samples) zeros(120, samples)];
		
			obj.hImage = imagesc(obj.hAxes, ...
				obj.hDataCube.azimuthBins, ...
				1:samples, initialData);
			%axis(obj.hAxes, 'xy');

			xlabel(obj.hAxes, 'Azimuth (degrees)');
			ylabel(obj.hAxes, 'Range (bin)');
			title(obj.hAxes, 'Azimuth-Range Map');
			colormap(obj.hAxes, 'jet');
			%colorbar(obj.hAxes);
		end

	end

	methods(Access=public)
		function obj = dataProcessor(radarObj, platformObj, preferencesObj, panelObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			fprintf("dataProcessor | dataProcessor | starting paraller pool\n");
			
			% XXX
			%obj.parallelPool = gcp('nocreate'); % Start parallel pool
			%if isempty(obj.parallelPool)
			%	obj.parallelPool = parpool(4); % Start pool with 1 worker
			%end

			% TODO -> move some of these paramters to be updateable on the fly
			fprintf("dataProcessor | dataProcessor |  starting gui\n");
			obj.hPanel = panelObj;

			obj.onNewConfigAvailable();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
			addlistener(preferencesObj, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());
		end


		function endProcesses(obj)
			% endProcesses: safely stops all class processes
			delete(obj.parallelPool);
		end



	end
end


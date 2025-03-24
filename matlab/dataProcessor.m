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
		calcSpeed = 0;
		currentVisualizationStyle = '';


	end

	methods(Static, Access=public)

		function [yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posYaw, posPitch, speedBins, calcSpeed, maskSize)

			rangeProfile = abs(batchRangeFFTs(end, :));
			yaw = abs(mod(posYaw(end)+180, 360))-180;
			pitch = posPitch(end);

			% fprintf("-------------------\nNew batch\n-------------------\n");


			% Verify how much has the platform moved
			% we will cut number of samples used, the number of FFT points stays
			% the same regardless
			%   * if we straied too far from main lobe we cannot use these samples
			%   * if the platform was moving too fast/there were too many changed
			%     we need to crop
			%
			% we need a segmetn where accumulated change is smaller than main
			% lobe size, within this segment maximal change should be limited


			dt = diff(posTimes);
			rawDiffYaw = diff(posYaw);
			dYaw = (mod(rawDiffYaw + 180, 360) - 180)./dt;
			rawDiffPitch = diff(posPitch);
			dPitch = (mod(rawDiffPitch + 180, 360) - 180)./dt;

			%lowIndex = length(dYaw);
			lowIndex = 1;
			for i=length(dYaw):-1:1
				if any(abs(dYaw) > 5) || any(abs(dPitch) > 10)
					break;
				end
				lowIndex=i;
			end

			% if lowIndex ~= 1
			% we need to find closes timestamp in batchTimes
			%	[~, idxMin] = min(abs(batchTimes - posTimes(lowIndex)));
			%	batchRangeFFTs(1:idxMin) = [];
			%	batchTimes(1:idxMin) = [];
			%	fprintf("Restricting spectrum count due to too fast movement");
			% end

			% Let's say here we have cropped the data, based on movement
			% we can calculate movement mask that will be used when running
			% update


			totalDiffYaw = mod(posYaw(end) - posYaw(lowIndex) + 180, 360) - 180;
			totalDiffPitch = posPitch(end) - posPitch(lowIndex);
			timeElapsed = posTimes(end) - posTimes(lowIndex);

			% angular speed
			speed = sqrt((totalDiffYaw^2 + totalDiffPitch^2)) / (timeElapsed + 1e-6);

			% we don't take into account data from area we are moving into, only
			% are we are moving from
			movementMask = radarDataCube.createSectorMask(totalDiffYaw, totalDiffPitch, maskSize, speed);

			% in case we aren't calculating speed we can end here
			%if ~calcSpeed
			%	fprintf("No speed calculations, exitting\n");
			%	rangeDoppler = [zeros(128, speedBins-1), rangeProfile'];
			%	yaw = abs(mod(posYaw(end)+180, 360))-180;
			%	pitch = posPitch(end);
			%	return;
			%end


			% Timing based analysis
			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			[~, idx] = max(abs(timeDeltas - meanInterval));
			maxDeviation = timeDeltas(idx) / meanInterval;
			useNUFFT = maxDeviation > 0.2;

			% Run FFT
			if useNUFFT
				% fprintf("ugly timing detected\n");
				rangeDoppler = abs(nufft(batchRangeFFTs, batchTimes, speedBins, 1))';
			else
				rangeDoppler = abs(fft(batchRangeFFTs, speedBins, 1))';
			end

			% fprintf("Dimensions Fast time: %f, Slow time %f\n", length(rangeProfile), length(rangeDoppler))
		end
	end

	methods(Access=private)
		function mergeResults(obj, yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask)
			% fprintf("mergeResults | adding to cube: yaw %f, pitch %f\n", yaw, pitch);
			obj.hDataCube.addData(yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask);
			fprintf("mergeResults | Data added\n");
			% Draw range yaw
			%if strcmp(obj.currentVisualizationStyle,'Range-Azimuth')
			%	data = sum(obj.hDataCube.cube,4);
			%	toDraw(:,:) = data(:,20, :);
			%	obj.hImage.CData = toDraw';
			%	drawnow limitrate;
			%end

		end

		function onNewConfigAvailable(obj)
			% TODO Deinitilize variables

			[radPatterH, radPatterT] = obj.hPreferences.getRadarRadiationParamters();
			[samples, ~, ~ ] = obj.hPreferences.getRadarBasebandParameters();

			[obj.calcSpeed, obj.speedBins]= obj.hPreferences.getProcessingSpeedParamters();
			visual=obj.hPreferences.getProcessingVisualization();

			obj.hDataCube = radarDataCube(samples, obj.speedBins, radPatterH, radPatterT);
			obj.hRadarBuffer = radarBuffer(32, samples);


			if strcmp(visual, 'Range-Azimuth') && ~strcmp(obj.currentVisualizationStyle, 'Range-Azimuth')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing as yaw-range map.\n")
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


			fprintf("onNewDataAvailable | busy workers %f\n", obj.parallelPool.Busy);
			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(obj.readIdx, :), ...
				obj.hRadar.bufferQ(obj.readIdx, :), ...
				obj.hRadar.bufferTime(obj.readIdx));
			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;

			if obj.parallelPool.NumWorkers > obj.parallelPool.Busy
				[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
				[posTimes, yaw, pitch] = obj.hPlatform.getPositionsInInterval(min(batchTimes), max(batchTimes));
				fprintf("onNewDataAvailable | yaw: %f, pitch %f\n", yaw(end), pitch(end));
				maskSize = size(obj.hDataCube.antennaPattern);

				future = parfeval(obj.parallelPool, ...
					@dataProcessor.processBatch, 6, ...
					batchRangeFFTs, batchTimes, posTimes, yaw, pitch, obj.speedBins, obj.calcSpeed, maskSize);


				afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			else
				fprintf("onNewDataAvailable | processing overloaded\n");
			end
			% [yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask] = dataProcessor.processBatch( ...
			%	batchRangeFFTs,  ...
			%	batchTimes, ...
			%	posTimes, ...
			%	yaw, ...
			%	pitch, ...
			%	obj.speedBins, ...
			%	obj.calcSpeed, ...
			%	maskSize);

			%obj.mergeResults(yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask);

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

			obj.hImage = imagesc(obj.hAxes, ...
				obj.hDataCube.yawBins, ...
				1:samples, initialData);

			axis(obj.hAxes, 'xy');

			xlabel(obj.hAxes, 'Azimuth (degrees)');
			ylabel(obj.hAxes, 'Range (bin)');
			title(obj.hAxes, 'Azimuth-Range Map');
			colormap(obj.hAxes, 'jet');
		end

	end

	methods(Access=public)
		function obj = dataProcessor(radarObj, platformObj, preferencesObj, panelObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;

			% XXX
			obj.parallelPool = gcp('nocreate'); % Start parallel pool
			if isempty(obj.parallelPool)
				fprintf("dataProcessor | dataProcessor | starting paraller pool\n");
				obj.parallelPool = parpool(6);
			end

			% TODO -> move some of these paramters to be updateable on the fly
			fprintf("dataProcessor | dataProcessor |  starting gui\n");
			obj.hPanel = panelObj;

			obj.onNewConfigAvailable();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
			addlistener(preferencesObj, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());
		end


		function endProcesses(obj)
			% endProcesses: safely stops all class processes
			% delete(obj.parallelPool);
		end



	end
end


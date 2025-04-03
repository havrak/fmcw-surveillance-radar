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
			%yaw = abs(mod(posYaw(end)+180, 360))-180;
			yaw = posYaw(end);
			pitch = posPitch(end);



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

			tmp = diff(posYaw);
			rawDiffYaw = abs((mod(tmp + 180, 360) - 180));
			% dYaw = rawDiffYaw./dt;

			rawDiffPitch = abs(diff(posPitch));
			% dPitch = rawDiffPitch./dt;

			%lowIndex = length(dYaw);
			lowIndex = 1;
			%for i=length(dYaw):-1:1
			%	if any(abs(dYaw) > 5) || any(abs(dPitch) > 10)
			%		break;
			%	end
			%	lowIndex=i;
			%end

			% if lowIndex ~= 1
			% we need to find closes timestamp in batchTimes
			%	[~, idxMin] = min(abs(batchTimes - posTimes(lowIndex)));
			%	batchRangeFFTs(1:idxMin) = [];
			%	batchTimes(1:idxMin) = [];
			%	fprintf("Restricting spectrum count due to too fast movement");
			% end

			% Let's say here we have cropped the data, based on movement
			% we can calculate movement mask that will be used when running

			% angular speed
			%sum(rawDiffYaw)^2
			timeElapsed = posTimes(end) - posTimes(lowIndex);
			speed = sqrt((sum(rawDiffYaw(lowIndex:end))^2 + sum(rawDiffPitch(lowIndex:end))^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason

			% we don't take into account data from area we are moving into, only
			% are we are moving from
			movementMask = ones(maskSize);
			% movementMask = radarDataCube.createSectorMask(totalDiffYaw, totalDiffPitch, maskSize, speed);

			% in case we aren't calculating speed we can end here
			if ~calcSpeed
				% fprintf("No speed calculations, exitting\n");
				rangeDoppler = [zeros(128, speedBins-1), rangeProfile'];
				yaw = abs(mod(posYaw(end)+180, 360))-180;
				pitch = posPitch(end);
				return;
			end


			% Timing based analysis
			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			[~, idx] = max(abs(timeDeltas - meanInterval));
			maxDeviation = timeDeltas(idx) / meanInterval;
			useNUFFT = maxDeviation > 0.2;

			% Run FFT
			%if useNUFFT
			%	% fprintf("ugly timing detected\n");
			%	rangeDoppler = abs(nufft(batchRangeFFTs, batchTimes, speedBins, 1))';
			%else
			tmp = abs(fft(batchRangeFFTs, speedBins, 1))';
			rangeDoppler = single(tmp);
			%end

			% fprintf("Dimensions Fast time: %f, Slow time %f\n", length(rangeProfile), length(rangeDoppler))
		end
	end

	methods(Access=private)
		function mergeResults(obj, yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask)

			
			obj.hDataCube.addData(yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask);

			if obj.hDataCube.isBatchFull()
				fprintf("dataProcessor | mergeResults | starting batch processing\n");
				future = obj.hDataCube.processBatchAsync();
				afterAll(future, @(varargin) obj.updateFinished(varargin{:}), 0);
			end
		end

		function updateFinished(obj)
			fprintf("dataProcessor | updateFinished\n");

			if strcmp(obj.currentVisualizationStyle,'Range-Azimuth')

				fprintf("dataProcessor | updateFinished | drawing r-a map\n");

				 data = sum(obj.hDataCube.cube, 2);
				 disp(size(data));
        % Extract the slice for pitch 20 (now dimension 4)
        % Note: we need to take the 3D slice and then squeeze to 2D
				
        toDraw = squeeze(data(:, 1, :, 20));
				disp(size(toDraw));
				obj.hImage.CData = 10*log10(toDraw);
				drawnow limitrate;
			end
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


			% fprintf("onNewDataAvailable | busy workers %f\n", obj.parallelPool.Busy);
			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(obj.readIdx, :), ...
				obj.hRadar.bufferQ(obj.readIdx, :), ...
				obj.hRadar.bufferTime(obj.readIdx));
			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;

			if obj.parallelPool.NumWorkers > obj.parallelPool.Busy
				[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
				[posTimes, yaw, pitch] = obj.hPlatform.getPositionsInInterval(min(batchTimes), max(batchTimes));
				maskSize = size(obj.hDataCube.antennaPattern);


				[yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask] = dataProcessor.processBatch( ...
				batchRangeFFTs,  ...
				batchTimes, ...
				posTimes, ...
				yaw, ...
				pitch, ...
				obj.speedBins, ...
				obj.calcSpeed, ...
				maskSize);

				obj.mergeResults(yaw, pitch, rangeProfile, rangeDoppler, speed, movementMask);

				%fprintf("dataProcessor | onNewDataAvailable | yaw: %f, pitch %f\n", yaw(end), pitch(end));

				%future = parfeval(obj.parallelPool, ...
				%	@dataProcessor.processBatch, 6, ...
				%	batchRangeFFTs, batchTimes, posTimes, yaw, pitch, obj.speedBins, obj.calcSpeed, maskSize);
				%afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			else
				fprintf("dataProcessor | onNewDataAvailable | processing overloaded\n");
			end



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


classdef dataProcessor < handle
	properties
		hRadar radar              % Radar object
		hPlatform platformControl % Platform object
		hPreferences preferences  % Preferences object
		parallelPool              % Parallel pool handle
		isProcessing = false      % Flag to prevent overlap
		hRadarBuffer radarBuffer; %
		hDataCube radarDataCube;  % RadarDataCube instance
		hPanel = [];

		hImage;
		hSurf;
		hAxes;

		hPitchSlider; % pitch slider for RA map
		pitchIndex = 21; 

		currentDisplayMethod;

		readIdx = 1;

		radarBufferSize = 100;
		processingParamters;
		calcSpeed = 0;
		currentVisualizationStyle = '';


	end

	methods(Static, Access=public)

		function [yaw, pitch, cfar, rangeDoppler, speed] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posYaw, posPitch, processingParamters)

			yaw = posYaw(end);
			pitch = posPitch(end);
			distance = (0:(processingParamters.rangeNFFT/2-1))*processingParamters.rangeBinWidth;

			lastFFT = abs(batchRangeFFTs(:,end))';
			rangeProfile = lastFFT(1:processingParamters.rangeNFFT/2);
			rangeProfile = ((rangeProfile.^2).*distance.^4)';
			
			cfarDetector = phased.CFARDetector('NumTrainingCells',processingParamters.cfarTraining, ...
				'NumGuardCells',processingParamters.cfarGuard);
			cfarDetector.ThresholdFactor = 'Auto';
			cfarDetector.ProbabilityFalseAlarm = 1e-3;
			
			cfar = cfarDetector(rangeProfile, 1:processingParamters.rangeNFFT/2);
			
			tmp = diff(posYaw);
			rawDiffYaw = abs((mod(tmp + 180, 360) - 180));
			rawDiffPitch = abs(diff(posPitch));


			if ~processingParamters.calcSpeed


				timeElapsed = posTimes(end) - posTimes(1);
				speed = sqrt((sum(rawDiffYaw)^2 + sum(rawDiffPitch)^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason

				% fprintf("No speed calculations, exitting\n");
				rangeDoppler = [zeros(processingParamters.rangeNFFT/2, (processingParamters.speedNFFT/2)-1), rangeProfile];
				yaw = posYaw(end);
				pitch = posPitch(end);
				return;
			end

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

			dYaw = rawDiffYaw./dt;
			dPitch = rawDiffPitch./dt;


			idxPosition = length(dYaw);
			for i=length(dYaw):-1:1
				if any(abs(dYaw) > 5) || any(abs(dPitch) > 10)
					break;
				end
				idxPosition=i;
			end


			if idxPosition ~= 1
				[~, idxBatch] = min(abs(batchTimes - posTimes(idxPosition)));
			else
				idxBatch = 1;
			end

			timeElapsed = posTimes(end) - posTimes(idxPosition);
			speed = sqrt((sum(rawDiffYaw(idxPosition:end))^2 + sum(rawDiffPitch(idxPosition:end))^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason


			distanceMap = repmat(distance, [1, processingParamters.speedNFFT/2]);

			% Timing based analysis
			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			[~, idx] = max(abs(timeDeltas - meanInterval));
			maxDeviation = timeDeltas(idx) / meanInterval;
			useNUFFT = maxDeviation > 0.2;

			% Run FFT
			if useNUFFT
				tmp = abs(nufft(batchRangeFFTs, batchTimes, speedSamples, 1));
			else
				tmp = abs(fft(batchRangeFFTs(1:idxBatch), processingParamters.speedNFFT, 1));
			end

			tmp = (tmp.^2).*distanceMap;
			rangeDoppler = single(tmp(1:processingParamters.rangeNFFT/2, 1:processingParamters.speedNFFT/2));

		end
	end

	methods(Access=private)
		function mergeResults(obj, yaw, pitch, cfar, rangeDoppler, speed)


			obj.hDataCube.addData(yaw, pitch, cfar, rangeDoppler, speed);

			if obj.hDataCube.isBatchFull()
				fprintf("dataProcessor | mergeResults | starting batch processing\n");
				obj.hDataCube.startBatchProcessing();
			end
		end

		function onCubeUpdateFinished(obj)
			fprintf("dataProcessor | updateFinished\n");

			if strcmp(obj.currentVisualizationStyle,'Range-Azimuth')

				fprintf("dataProcessor | updateFinished | drawing r-a map\n");

				% data = sum(obj.hDataCube.cube, 2);
				% % Extract the slice for pitch 20 (now dimension 4)
				% % Note: we need to take the 3D slice and then squeeze to 2D
				%
				%
				% toDraw = squeeze(data(:, 1, :, 20));
				% toDraw(1) = 40;
				% % toDraw(toDraw < -80) = -Inf;
				%
				% fprintf("onCubeUpdateFinished | Min value %f\n", min(toDraw(:)));
				% obj.hImage.CData = toDraw;
				% drawnow limitrate;

				% data = sum(obj.hDataCube.radarCube, 2);
				% toDraw = squeeze(data(:, 1, :, obj.pitchIndex)); % Transpose for dimensions [range, azimuth]
				% toDraw(1) = 50; % NOTE: just to fix color scale
				% disp(sub)
				disp(sum(obj.hDataCube.cfarCube(:)));
				disp(obj.pitchIndex);
				toDraw = squeeze(obj.hDataCube.cfarCube(:, :, obj.pitchIndex));
				
				obj.hSurf.CData = toDraw;
				drawnow limitrate;
			end
		end

		function onNewConfigAvailable(obj)

			java.lang.System.gc();
			% TODO Deinitilize variables

			[radPatterH, radPatterT] = obj.hPreferences.getRadarRadiationParamters();

			obj.processingParamters = obj.hPreferences.getProcessingParamters();
			visual=obj.hPreferences.getProcessingVisualization();
			fprintf("dataProcessor | onNewConfigAvailable | Distance bin size: %f mm\n" , obj.processingParamters.rangeBinWidth*1000);
			obj.hDataCube = radarDataCube(obj.processingParamters.rangeNFFT/2, obj.processingParamters.speedNFFT/2, radPatterH, radPatterT, false, true);
			obj.hRadarBuffer = radarBuffer(floor(obj.processingParamters.speedNFFT*1.5), obj.processingParamters.rangeNFFT);


			if strcmp(visual, 'Range-Azimuth') && ~strcmp(obj.currentVisualizationStyle, 'Range-Azimuth')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing as yaw-range map.\n")

				obj.currentVisualizationStyle = 'Range-Azimuth';
				obj.deinitializeDisplay();
				obj.initializeARDisplay();
			end


			addlistener(obj.hDataCube, 'updateFinished', @(~,~) obj.onCubeUpdateFinished());
		end

		function onNewDataAvailable(obj)
			% Called automatically when radar data arrives
			% TODO: add chirp to buffer -> run processing on buffer
			% on addition to chirps processing will also get data about positions


			% fprintf("onNewDataAvailable | busy workers %f\n", obj.parallelPool.Busy);
			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(:, obj.readIdx), ...
				obj.hRadar.bufferQ(:, obj.readIdx), ...
				obj.hRadar.bufferTime(obj.readIdx));


			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;


			[minTime, maxTime] = obj.hRadarBuffer.getTimeInterval();
			[posTimes, yaw, pitch] = obj.hPlatform.getPositionsInInterval(minTime, maxTime);
			fprintf("dataProcessor | onNewDataAvailable | yaw: %f, pitch %f\n", yaw(end), pitch(end));


			diffYaw = abs((mod(yaw(end)-obj.hRadarBuffer.lastProcesingYaw + 180, 360) - 180));
			distance = sqrt(diffYaw^2 + (pitch(end)-obj.hRadarBuffer.lastProcesingPitch)^2);
			if distance < 2
				% I can process every single frame without issue but since I have
				% trouble with getting the visualizations to be pretty I'm skipping
				% those frames where positions didn't chaged much
				return;
			end

			if obj.parallelPool.NumWorkers > obj.parallelPool.Busy
				[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
				obj.hRadarBuffer.lastProcesingYaw = yaw(end);
				obj.hRadarBuffer.lastProcesingPitch = pitch(end);


				% [yaw, pitch, cfar, rangeDoppler, speed] = dataProcessor.processBatch( ...
				% 	batchRangeFFTs,  ...
				% 	batchTimes, ...
				% 	posTimes, ...
				% 	yaw, ...
				% 	pitch, ...
				% 	obj.processingParamters);
				% 
				% obj.mergeResults(yaw, pitch, cfar, rangeDoppler, speed);


					future = parfeval(obj.parallelPool, ...
						@dataProcessor.processBatch, 5, ...
						batchRangeFFTs, batchTimes, posTimes, yaw, pitch, obj.processingParamters);
					afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			else
				fprintf("dataProcessor | onNewDataAvailable | processing overloaded\n");
			end



		end

		function deinitializeDisplay(obj)
			if ~isempty(obj.hAxes) && isvalid(obj.hAxes)
				delete(obj.hAxes);
			end
			if ~isempty(obj.hImage) && isvalid(obj.hImage)
				delete(obj.hImage);
			end
			if ~isempty(obj.hSurf) && isvalid(obj.hSurf)
				delete(obj.hSurf);
			end
			if ~isempty(obj.hPitchSlider) && isvalid(obj.hPitchSlider)
				delete(obj.hPitchSlider);
			end
			obj.hAxes = [];
			obj.hImage = [];
			obj.hSurf = [];
			obj.hPitchSlider = [];
		end

		function initializeARDisplay(obj)

			obj.hAxes = axes('Parent', obj.hPanel, ...
				'Units', 'pixels');

			[samples, ~, ~ ] = obj.hPreferences.getRadarBasebandParameters();
			rangeBins = samples/2;
			yawBins = 360;

			theta = deg2rad(obj.hDataCube.yawBins);
			r = 1:rangeBins;
			[THETA, R] = meshgrid(theta, r);
			[X, Y] = pol2cart(THETA, R);

			initialData = zeros(rangeBins, yawBins);

			X = [X, X(:,1)]; % Add 360° = 0°
			Y = [Y, Y(:,1)];
			initialData(:, end) = initialData(:, 1);

			obj.hSurf = surf(obj.hAxes, X, Y, zeros(size(X)), initialData,...
				'EdgeColor', 'none',...
				'FaceColor', 'flat');



			view(obj.hAxes, 2);
			axis(obj.hAxes, 'equal', 'off');
			colormap(obj.hAxes, 'jet');
			title(obj.hAxes, 'Azimuth-Range Polar Map');

			obj.hPitchSlider = uislider(obj.hPanel,...
				'Limits', [obj.hDataCube.pitchBinMin obj.hDataCube.pitchBinMax],...
				'Value', obj.pitchIndex-21,...
				'ValueChangedFcn', @(src,event) obj.sliderCallback(src),...
				'Tooltip', 'Select pitch index');

		
			obj.resizeUI();
		end

		function sliderCallback(obj, src)
			obj.pitchIndex=floor(src.Value)-obj.hDataCube.pitchBinMin+1;
		end
	end

	methods(Access=public)
		function obj = dataProcessor(radarObj, platformObj, preferencesObj, panelObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			obj.hPanel = panelObj;

			% obj.parallelPool = gcp('nocreate'); % Start parallel pool
			%
			% if isempty(obj.parallelPool)
			% 	 fprintf("dataProcessor | dataProcessor | starting paraller pool\n");
			% 	 obj.parallelPool = parpool(6);
			% end

			% TODO -> move some of these paramters to be updateable on the fly
			fprintf("dataProcessor | dataProcessor |  starting gui\n");

			obj.onNewConfigAvailable();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
			addlistener(preferencesObj, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());
		end

		function resizeUI(obj)
			if isempty(obj.hPanel) || ~isvalid(obj.hPanel)
				return;
			end
			panelPos = get(obj.hPanel, 'Position');
			panelWidth = panelPos(3);
			panelHeight = panelPos(4);

			axesMarginX = 60;
			axesMarginY = 60;
			
			sliderMarginX = 30;
			sliderMarginY = 40;

			if isvalid(obj.hAxes)
				set(obj.hAxes, 'Position', [axesMarginX, axesMarginY, panelWidth-2*axesMarginX, panelHeight-2*axesMarginY]);
				axis(obj.hAxes, 'equal'); 
			end

			if isvalid(obj.hPitchSlider)
					pos = get(obj.hPitchSlider,'Position');
					pos(1:3) = [sliderMarginX sliderMarginY 200];
					set(obj.hPitchSlider,'Position',pos);
			end

			% if isvalid(obj.hSurf)
			%caxis(obj.hAxes, 'auto');
			 %end
		end

		function endProcesses(obj)
			% endProcesses: safely stops all class processes
			% delete(obj.parallelPool);
		end



	end
end


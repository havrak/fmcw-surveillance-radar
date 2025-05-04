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
		hSurf = [];
		hImage = [];
		hAxes = [];
		hScatter3D = [];

		yawIndex = 1;        % Default yaw index for RD map
		pitchIndex = 21;     % Default pitch index for RD map
		hEditYaw;            % Yaw input textbox
		hEditPitch;          % Pitch input textbox
		hLabelYaw;
		hLabelPitch;

		currentDisplayMethod;

		readIdx = 1;

		cfarDrawThreshold = 0.2;

		radarBufferSize = 100;
		decayType = 1;
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

			if ~processingParamters.calcRaw
				timeElapsed = posTimes(end) - posTimes(1);
				rangeDoppler = [];
				speed = sqrt((sum(rawDiffYaw)^2 + sum(rawDiffPitch)^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason
				return;
			end

			if ~processingParamters.calcSpeed
				timeElapsed = posTimes(end) - posTimes(1);
				speed = sqrt((sum(rawDiffYaw)^2 + sum(rawDiffPitch)^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason
				% fprintf("No speed calculations, exitting\n");
				rangeDoppler = [zeros(processingParamters.rangeNFFT/2, (processingParamters.speedNFFT)-1), rangeProfile];
				yaw = posYaw(end);
				pitch = posPitch(end);
				return;
			end


			% only allow samples whose position was within 4 degrees of our
			% current one
			% four is totaly arbitrary number, given radars capabilities speed
			% processing is more of an demonstration of processing than anything
			% practical

			tolerace = 4;
			idxPosition = length(posYaw);
			for i=length(dYaw):-1:1
				if sqrt((posYaw(i)- posYaw(end))^2 + (posPitch(i)- posPitch(end))^2) > tolerace
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
				tmp = abs(fftshift(nufft(batchRangeFFTs, batchTimes, speedSamples, 1)),1);
			else
				tmp = abs(fftshift(fft(batchRangeFFTs(1:idxBatch), processingParamters.speedNFFT, 1)),1 );
			end

			tmp = (tmp.^2).*distanceMap;
			rangeDoppler = single(tmp(1:processingParamters.rangeNFFT/2, 1:processingParamters.speedNFFT));

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

			if strcmp(obj.currentVisualizationStyle,'Range-Azimuth')
				fprintf("dataProcessor | updateFinished | drawing r-a map\n");

				if obj.processingParamters.calcRaw && obj.processingParamters.calcCFAR
					data = sum(obj.hDataCube.rawCube, 2);
					toDraw = squeeze(data(:, 1, :, obj.pitchIndex));
					cfarData = squeeze(obj.hDataCube.cfarCube(:, :, obj.pitchIndex));
					toDraw = toDraw+cfarData.*max(toDraw);
				elseif obj.processingParamters.calcRaw
					data = sum(obj.hDataCube.rawCube, 2);
					toDraw = squeeze(data(:, 1, :, obj.pitchIndex));
				elseif obj.processingParamters.calcCFAR
					toDraw = squeeze(obj.hDataCube.cfarCube(:, :, obj.pitchIndex));
				else
					toDraw = zeros(obj.hDataCube.rawCubeSize([1 3]));
				end

				obj.hSurf.CData = toDraw;
			elseif strcmp(obj.currentVisualizationStyle, 'Target-3D')
				fprintf("dataProcessor | updateFinished | updating 3D plot\n");
				idx = find(obj.hDataCube.cfarCube >= obj.cfarDrawThreshold);
				[rangeBin, yawBin, pitchBin] = ind2sub(size(obj.hDataCube.cfarCube), idx);

				if ~isempty(rangeBin)
					range = (rangeBin - 1) * obj.processingParamters.rangeBinWidth;
					yaw = obj.hDataCube.yawBins(yawBin);
					pitch = obj.hDataCube.pitchBins(pitchBin);
					X = range' .* cosd(pitch) .* cosd(-yaw+90);
					Y = range' .* cosd(pitch) .* sind(-yaw+90);
					Z = range' .* sind(pitch);
					% fprintf("dataProcessor | updateFinished | X:%d, Y:%d, Z%d. limX = [%d, %d], limY = [%d, %d], limZ = [%d, %d]\n", ...
					% 	length(X), length(Y), length(Z), ...
					% 	min(X), max(X), ...
					% 	min(Y), max(Y), ...
					% 	min(Z), max(Z));
					set(obj.hScatter3D, 'XData', X, 'YData', Y, 'ZData', Z, 'CData', obj.hDataCube.cfarCube(idx));
					
					else
						set(obj.hScatter3D, 'XData', [], 'YData', [], 'ZData', []);
				end
			elseif strcmp(obj.currentVisualizationStyle, 'Range-Doppler')
				if obj.processingParamters.calcSpeed == 1
					fprintf("Updating Range-Doppler Map\n");

					data = squeeze(obj.hDataCube.rangeDopplerCube(:, :, obj.yawIndex, obj.yawIndex));

					if isempty(data)
						data = zeros(obj.processingParamters.rangeNFFT/2, ...
							obj.processingParamters.speedNFFT/2);
					end
					set(obj.hImage, 'CData', data);
				else
				end
			end

			drawnow limitrate;
		end

		function onPlatformTriggerYawHit(obj)
			if obj.decayType == 0
				obj.hDataCube.zeroCubes();
			end
		end

		function onNewConfigAvailable(obj)

			java.lang.System.gc();
			% TODO Deinitilize variables

			[spreadPatternEnabled, spreadPatternYaw, spreadPatternPitch] = obj.hPreferences.getProcessingSpreadPatternParamters();

			if spreadPatternEnabled == 0
				spreadPatternYaw = 0;
				spreadPatternPitch = 0;
			end

			obj.processingParamters = obj.hPreferences.getProcessingParamters();
			visual=obj.hPreferences.getProcessingVisualization();
			obj.decayType = obj.hPreferences.getDecayType();
			obj.hDataCube = radarDataCube( ...
				obj.processingParamters.rangeNFFT/2, ...
				obj.processingParamters.speedNFFT, ...
				obj.hPreferences.getProcessingBatchSize(), ...
				spreadPatternYaw, ...
				spreadPatternPitch, ...
				obj.processingParamters.calcRaw , ...
				obj.processingParamters.calcCFAR, ...
				obj.decayType ...
				);
			obj.hRadarBuffer = radarBuffer(floor(obj.processingParamters.speedNFFT*1.5), obj.processingParamters.rangeNFFT);


			if strcmp(visual, 'Range-Azimuth') && ~strcmp(obj.currentVisualizationStyle, 'Range-Azimuth')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing as yaw-range map.\n")
				obj.currentVisualizationStyle = 'Range-Azimuth';
				obj.deinitializeDisplay();
				obj.initializeARDisplay();
			end

			if strcmp(visual, 'Target-3D') && ~strcmp(obj.currentVisualizationStyle, 'Target-3D')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing CFAR in 3D.\n")
				obj.currentVisualizationStyle = 'Target-3D';
				obj.deinitializeDisplay();
				obj.initialize3DDisplay();
			end

			if strcmp(visual, 'Range-Doppler') && ~strcmp(obj.currentVisualizationStyle, 'Range-Doppler')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing Range-Doppler.\n")
				obj.currentVisualizationStyle = 'Range-Doppler';
				obj.deinitializeDisplay();
				obj.initializeRDDisplay();
			end


			addlistener(obj.hDataCube, 'updateFinished', @(~,~) obj.onCubeUpdateFinished());
		end

		function onNewDataAvailable(obj)
			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(:, obj.readIdx), ...
				obj.hRadar.bufferQ(:, obj.readIdx), ...
				obj.hRadar.bufferTime(obj.readIdx));


			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;




			if obj.parallelPool.NumWorkers > obj.parallelPool.Busy


				[minTime, maxTime] = obj.hRadarBuffer.getTimeInterval();
				[posTimes, yaw, pitch] = obj.hPlatform.getPositionsInInterval(minTime, maxTime);


				diffYaw = abs((mod(yaw(end)-obj.hRadarBuffer.lastProcesingYaw + 180, 360) - 180));
				distance = sqrt(diffYaw^2 + (pitch(end)-obj.hRadarBuffer.lastProcesingPitch)^2);
				if obj.processingParamters.requirePosChange == 1 && distance < 1
					% I can process every single frame without issue but there no needz
					% to process frames where position has not changed, this data is
					% only usefull for speed calculation
					return;
				end

				[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
				obj.hRadarBuffer.lastProcesingYaw = yaw(end);
				obj.hRadarBuffer.lastProcesingPitch = pitch(end);


				% [ yaw, pitch, cfar, rangeDoppler, speed] = dataProcessor.processBatch( ...
				% 	batchRangeFFTs,  ...
				% 	batchTimes, ...
				% 	posTimes, ...
				% 	yaw, ...
				% 	pitch, ...
				% 	obj.processingParamters);
				%
				% obj.mergeResults(yaw, pitch, cfar, rangeDoppler, speed);

				fprintf("dataProcessor | onNewDataAvailable | processing: yaw: %f, pitch %f\n", yaw(end), pitch(end));
				future = parfeval(obj.parallelPool, ...
					@dataProcessor.processBatch, 5, ...
					batchRangeFFTs, ...
					batchTimes, ...
					posTimes, ...
					yaw, ...
					pitch, ...
					obj.processingParamters);
				afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			else
				fprintf("dataProcessor | onNewDataAvailable | processing overloaded\n");
			end



		end

		function deinitializeDisplay(obj)
			if ~isempty(obj.hAxes) && isvalid(obj.hAxes)
				delete(obj.hAxes);
			end

			if ~isempty(obj.hSurf) && isvalid(obj.hSurf)
				delete(obj.hSurf);
			end

			if ~isempty(obj.hEditPitch) && isvalid(obj.hEditPitch)
				delete(obj.hEditPitch);
			end

			if ~isempty(obj.hEditYaw) && isvalid(obj.hEditYaw)
				delete(obj.hEditYaw);
			end

			if ~isempty(obj.hImage) && isvalid(obj.hImage)
				delete(obj.hImage)
			end

			if ~isempty(obj.hLabelYaw) && isvalid(obj.hLabelYaw)
				delete(obj.hLabelYaw)
			end

			if ~isempty(obj.hLabelPitch) && isvalid(obj.hLabelPitch)
				delete(obj.hLabelPitch)
			end
			obj.hAxes = [];
			obj.hSurf = [];
			obj.hEditPitch = [];
			obj.hEditYaw = [];
		end

		function initializeARDisplay(obj)

			obj.hAxes = axes('Parent', obj.hPanel, ...
				'Units', 'pixels');

			[samples, ~, ~ ] = obj.hPreferences.getRadarBasebandParameters();
			rangeBins = samples/2;
			yawBins = 360;

			theta = -deg2rad(obj.hDataCube.yawBins)+pi/2; % minus to rotate counter clock wise, +pi/2 to center 0 deg 
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

			obj.hEditPitch = uicontrol(obj.hPanel, ...
				'Style', 'edit', ...
				'String', num2str(obj.hDataCube.pitchBins(obj.pitchIndex)), ...
				'Max',1, ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,~) obj.updatePitchIndex(src));

			obj.hLabelPitch = uilabel(obj.hPanel, ...
				'Text', 'Pitch [deg]:');

			axis(obj.hAxes, 'equal'); % otherwise circle wont be much of an circle

			obj.resizeUI();
		end

		function initialize3DDisplay(obj)

			maxRange = obj.processingParamters.rangeNFFT/2* obj.processingParamters.rangeBinWidth;

			xlimits = [-maxRange, maxRange];
			ylimits = [-maxRange, maxRange];
			zlimits = [maxRange* sind(obj.hDataCube.pitchBinMin), maxRange* sind(obj.hDataCube.pitchBinMax)];

			obj.hAxes = axes('Parent', obj.hPanel, ...
				'Units', 'pixels', ...
				'XLim',   xlimits, ...
				'YLim',   ylimits, ...
				'ZLim',   zlimits, ...
				'XLimMode','manual', ...
				'YLimMode','manual', ...
				'ZLimMode','manual', ...
				'NextPlot','add');
			
			view(obj.hAxes, 3);
			grid(obj.hAxes, 'on');
			axis(obj.hAxes, 'manual');
	
			obj.hScatter3D = scatter3(obj.hAxes, [], [], [], 'filled');
			colormap(obj.hAxes, 'jet');
			xlabel(obj.hAxes, 'X (m)');
			ylabel(obj.hAxes, 'Y (m)');
			zlabel(obj.hAxes, 'Z (m)');
			title(obj.hAxes, '3D Target Visualization');

			set(obj.hScatter3D, 'XData', [2,1,0], 'YData', [2,1,0], 'ZData', [1,0,0]);

			obj.resizeUI();
		end

		function initializeRDDisplay(obj)

			obj.hAxes = axes('Parent', obj.hPanel, 'Units', 'pixels');
			obj.hEditYaw = uicontrol(obj.hPanel, 'Style', 'edit', ...
				'String', num2str(obj.hDataCube.yawBins(obj.yawIndex)), ...
				'Max',1, ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,~) obj.updateYawIndex(src));

			obj.hEditPitch = uicontrol(obj.hPanel, 'Style', 'edit', ...
				'String', num2str(obj.hDataCube.pitchBins(obj.pitchIndex)), ...
				'Max',1, ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,~) obj.updatePitchIndex(src));
			obj.hLabelYaw = uilabel(obj.hPanel, ...
				'Text', 'Yaw [deg]:');

			obj.hLabelPitch = uilabel(obj.hPanel, ...
				'Text', 'Pitch [deg]:');
			% initilaize axes, two text boxes, one for yaw, second for pitch
			if(obj.processingParamters.calcSpeed)

				maxRange = obj.processingParamters.rangeNFFT/2* obj.processingParamters.rangeBinWidth;
				maxSpeed = obj.processingParamters.speedNFFT*obj.hPreferences.getSpeedBinWidth();
				initialData = zeros(obj.processingParamters.speedNFFT, ...
					obj.processingParamters.rangeNFFT/2);
				speedBins = linspace(-maxSpeed, maxSpeed-obj.hPreferences.getSpeedBinWidth(), obj.processingParamters.speedNFFT)*1000;
				rangeBins = linspace(0, maxRange, obj.processingParamters.rangeNFFT/2);

				obj.hImage = imagesc(obj.hAxes, ...
					rangeBins, ...
					speedBins, ...
					initialData);

				%obj.hAxes.YAxis.Exponent = 3;
				title(obj.hAxes, 'Range-Doppler Map');
				xlabel(obj.hAxes, 'Range [m]');
				ylabel(obj.hAxes, 'Speed [mm/s]');
				colormap(obj.hAxes, 'jet');
			else

			end
			obj.resizeUI();

		end


		function updatePitchIndex(obj, src)
			inputPitch = str2double(get(src, 'String'));
			if isnan(inputPitch)
				warndlg('Pitch must be a number');
				return;
			end
			[~, obj.pitchIndex] = min(abs(obj.hDataCube.pitchBins - inputPitch));
		end

		function updateYawIndex(obj, src)
			inputYaw = str2double(get(src, 'String'));
			if isnan(inputYaw)
				warndlg('Yaw must be a number');
				return;
			end
			[~, obj.yawIndex] = min(abs(obj.hDataCube.pitchBins - inputYaw));
		end
	end

	methods(Access=public)
		function obj = dataProcessor(radarObj, platformObj, preferencesObj, panelObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			obj.hPanel = panelObj;

			obj.parallelPool = gcp('nocreate'); % Start parallel pool

			if isempty(obj.parallelPool)
				fprintf("dataProcessor | dataProcessor | starting paraller pool\n");
				obj.parallelPool = parpool(6);
			end

			% TODO -> move some of these paramters to be updateable on the fly
			fprintf("dataProcessor | dataProcessor |  starting gui\n");

			obj.onNewConfigAvailable();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
			addlistener(preferencesObj, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());
			addlistener(platformObj, 'positionTriggerHit', @(~,~) obj.onPlatformTriggerYawHit());
		end

		function resizeUI(obj)

			if isempty(obj.hPanel) || ~isvalid(obj.hPanel)
				return;
			end
			panelPos = get(obj.hPanel, 'Position');
			panelWidth = panelPos(3);
			panelHeight = panelPos(4);

			axesMarginX = 80;
			axesMarginY = 70;

			if ~isempty(obj.hAxes) && isvalid(obj.hAxes)
				set(obj.hAxes, 'Position', [axesMarginX, axesMarginY+10, panelWidth-2*axesMarginX, panelHeight-2*axesMarginY-10]);
			end

			if ~isempty(obj.hEditPitch) && isvalid(obj.hEditPitch)
				set(obj.hLabelPitch, 'Position', [30, 20, 70, 25]);
				set(obj.hEditPitch,'Position',[100, 20, 100, 25]);
			end

			if ~isempty(obj.hEditYaw) && isvalid(obj.hEditYaw)
				set(obj.hEditYaw, 'Position', [278, 20, 100, 25]);
				set(obj.hLabelYaw,'Position',[210, 20, 70, 25]);
			end

		end

		function endProcesses(obj)
			% endProcesses: safely stops all class processes
			% delete(obj.parallelPool);
		end



	end
end


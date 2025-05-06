classdef dataProcessor < handle
	% dataProcessor: Coordinates radar data processing, visualization, and batch management
	%
	% Integrates radar hardware, platform control, and preferences to process FFT batches,
	% manage 3D/2D visualizations, and handle asynchronous updates via parallel processing.

	properties
		hRadar radar;              % Radar hardware interface object
		hPlatform platformControl; % Platform position/control interface
		hPreferences preferences;  % User configuration/preferences object
		parallelPool;              % Parallel pool for asynchronous processing
		isProcessing = false;      % Flag to prevent overlapping batch jobs
		hRadarBuffer radarBuffer;  % Circular buffer for raw radar FFT data
		hDataCube radarDataCube;   % 4D radar data cube manager

		% Visualization components
		hPanel = [];               % UI panel for displays
		hSurf = [];                % Surface plot handle (Range-Azimuth)
		hImage = [];               % Image plot handle (Range-Doppler)
		hPlot = [];                % Basic plot used to display (Range-"RCS")
		hAxes = [];                % Axes handle for current visualization
		hScatter3D = [];           % 3D scatter plot handle (Target-3D)

		% Display configuration
		yawIndex = 1;              % Selected yaw index for Range-Doppler view
		pitchIndex = 21;           % Selected pitch index (default: 21 -> 0°)
		hEditYaw;                  % textbox for yaw input
		hEditPitch;                % textbox for pitch input
		hLabelYaw;                 % label for yaw input
		hLabelPitch;               % label for pitch input
		processingActive = true;   % Global processing enable/disable flag
		lastProcesingYaw;          % Last processed yaw angle (degrees)
		lastProcesingPitch;        % Last processed pitch angle (degrees)

		currentDisplayMethod;      % Active visualization mode
		readIdx = 1;               % Radar buffer read index
		cfarDrawThreshold = 0.2;   % CFAR detection threshold for 3D visualization
		radarBufferSize = 100;     % Size of radar buffer (chirps)
		decayType = 1;             % Data decay mode (0=off, 1=exponential)
		processingParamters;       % Processing configuration parameters
		calcSpeed = 0;             % Speed calculation flag (legacy)
		currentVisualizationStyle; % Current display style identifier


	end

	methods(Static, Access=public)

		function [yaw, pitch, cfar, rangeDoppler, speed] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posYaw, posPitch, processingParamters)
			% processBatch: Processes FFT batch into CFAR detections and Range-Doppler maps
			%
			% Inputs:
			%   batchRangeFFTs ... Batch of range FFTs [rangeBins x chirps]
			%   batchTimes ... Timestamps for each chirp [1 x chirps]
			%   posTimes ... Platform timestamps [1 x N]
			%   posYaw ... Platform yaw angles [1 x N] (degrees)
			%   posPitch ... Platform pitch angles [1 x N] (degrees)
			%   processingParamters . Configuration struct (CFAR/FFT params)
			%
			% Outputs:
			%   yaw ... Final yaw angle (degrees)
			%   pitch ... Final pitch angle (degrees)
			%   cfar ... CFAR detection vector [rangeBins x 1]
			%   rangeDoppler ... Range-Doppler matrix [rangeBins x dopplerBins]
			%   speed ... Platform motion speed (m/s)


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
			% four is totally arbitrary number, given radars capabilities speed
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
				tmp = abs(fftshift(nufft(batchRangeFFTs(1:idxBatch), batchTimes, processingParamters.speedNFFT, 1)),1);
			else
				tmp = abs(fftshift(fft(batchRangeFFTs(1:idxBatch), processingParamters.speedNFFT, 1)),1 );
			end

			tmp = (tmp.^2).*distanceMap;
			rangeDoppler = single(tmp(1:processingParamters.rangeNFFT/2, 1:processingParamters.speedNFFT));

		end
	end

	methods(Access=private)
		function mergeResults(obj, yaw, pitch, cfar, rangeDoppler, speed)
			% mergeResults: Adds processed data to radarDataCube and triggers batch processing
			%
			% by default output from processBatch is only buffered in radarDataCube, if
			% the cube is full new parallel process will be started that will process
			% the buffer
			%
			% Inputs:
			%   yaw ... Yaw angle (degrees)
			%   pitch ... Pitch angle (degrees)
			%   cfar ... CFAR detection vector
			%   rangeDoppler ... Range-Doppler matrix
			%   speed ... Platform speed (m/s)


			obj.hDataCube.addData(yaw, pitch, cfar, rangeDoppler, speed);

			if obj.hDataCube.isBatchFull()
				fprintf("dataProcessor | mergeResults | starting batch processing\n");
				obj.hDataCube.startBatchProcessing();
			end
		end

		function onCubeUpdateFinished(obj)
			% onCubeUpdateFinished: Updates visualizations after data cube refresh
			%
			% Range-Azimuth -
			%
			% Function is called by radarDataCube's updateFinished event

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
					data = squeeze(sum(obj.hDataCube.rangeDopplerCube(:, :, obj.yawIndex, obj.pitchIndex),2));
					set(obj.hPlot, 'YData', data);
				end
			end

			drawnow limitrate;
		end

		function onPlatformTriggerYawHit(obj)
			% onPlatformTriggerYawHit: Resets data cubes on platform position trigger
			%
			% Function is called by platformControl's positionTriggerHit event
			if obj.decayType == 0
				obj.hDataCube.zeroCubes();
			end
		end

		function onNewConfigAvailable(obj)
			% onNewConfigAvailable: Reinitializes system with new preferences
			%
			% Function is called by preference's newConfigEvent event
			% Updates processing parameters, visualization mode, and data cubes

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
			% onNewDataAvailable: Setups processing of new radar data
			%
			% Function is called by radars's newDataAvailable event
			% Depending on current configuration if processing is active and platform
			% position has changed processing will be launched in parallel process
			obj.hRadarBuffer.addChirp(obj.hRadar.bufferI(:, obj.readIdx), ...
				obj.hRadar.bufferQ(:, obj.readIdx), ...
				obj.hRadar.bufferTime(obj.readIdx));


			obj.readIdx =  mod(obj.readIdx, obj.radarBufferSize) + 1;


			if ~obj.processingActive
				return
			end

			if obj.parallelPool.NumWorkers > obj.parallelPool.Busy


				[minTime, maxTime] = obj.hRadarBuffer.getTimeInterval();
				[posTimes, yaw, pitch] = obj.hPlatform.getPositionsInInterval(minTime, maxTime);

				diffYaw = abs((mod(yaw(end)-obj.lastProcesingYaw + 180, 360) - 180));
				distance = sqrt(diffYaw^2 + (pitch(end)-obj.lastProcesingPitch)^2);
				if obj.processingParamters.requirePosChange == 1 && distance < 1
					% I can process every single frame without issue but there no needz
					% to process frames where position has not changed, this data is
					% only usefull for speed calculation
					return;
				end

				[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();
				obj.lastProcesingYaw = yaw(end);
				obj.lastProcesingPitch = pitch(end);


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
			% deinitializeDisplay: Clears current visualization components

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
			% initializeARDisplay: Creates Range-Azimuth polar surface plot

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
			% initialize3DDisplay: Creates 3D scatter plot for target visualization

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
			% initializeRDDisplay: Creates Range-Doppler heatmap visualization
			%
			% In case speed processing is off only range-RCS plot is displayed

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
				maxRange = obj.processingParamters.rangeNFFT/2* obj.processingParamters.rangeBinWidth;
				rangeBins = linspace(0, maxRange, obj.processingParamters.rangeNFFT/2);
				initialData = zeros(length(rangeBins));  % Row vector for initial YData

				obj.hPlot = plot(obj.hAxes, rangeBins, initialData);
				title(obj.hAxes, 'Range-RCS Map');
				xlabel(obj.hAxes, 'Range [m]');
				ylabel(obj.hAxes, '~RCS');
				obj.hAxes.YDir = 'normal';  % Ensure y-axis is not reversed
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
			% dataProcessor: Initializes radar data processor
			%
			% Parallel pooled is also launcehd from here
			%
			% Inputs:
			%   radarObj ........... Initialized radar hardware object
			%   platformObj ........ Platform control interface
			%   preferencesObj ..... User preferences/configurations
			%   panelObj ........... UI panel for visualizations


			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.hPreferences = preferencesObj;
			obj.hPanel = panelObj;

			% obj.parallelPool = gcp('nocreate'); % Start parallel pool
			%
			% if isempty(obj.parallelPool)
			% 	fprintf("dataProcessor | dataProcessor | starting paraller pool\n");
			% 	obj.parallelPool = parpool(6);
			% end

			% TODO -> move some of these paramters to be updateable on the fly
			fprintf("dataProcessor | dataProcessor |  starting gui\n");

			obj.onNewConfigAvailable();

			addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
			addlistener(preferencesObj, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());
			addlistener(platformObj, 'positionTriggerHit', @(~,~) obj.onPlatformTriggerYawHit());
		end

		function saveScene(obj)
			% saveScene: saves current scene to file
			%
			% File is created in project directory, filename contains a timestamp
			if strcmp(obj.currentVisualizationStyle,'Range-Azimuth')
				type = "RA";
			elseif strcmp(obj.currentVisualizationStyle, 'Target-3D')
				type = "3D";
			elseif strcmp(obj.currentVisualizationStyle, 'Range-Doppler')
				if obj.processingParamters.calcSpeed == 1
					type = "RD";
				else
					type = "RR";
				end
			end
			exportgraphics(obj.hAxes, fullfile("images", type+string(datetime('now','Format','d_M_HH:mm:ss'))+".jpg"));
		end

		function resizeUI(obj)
			% resizeUI: Adjusts UI component positions on panel resize
			%
			% called from app.m which manages uifigure all dataProcessor's elements are  in
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


		function status = toggleProcessing(obj)
			% toggleProcessing: Enables/disables data processing
			%
			% Output:
			%   status ... Current processing state (true=active)
			obj.processingActive = ~obj.processingActive;
			status = obj.processingActive;
		end


	end
end


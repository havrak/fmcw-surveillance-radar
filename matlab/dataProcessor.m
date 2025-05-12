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
		hLine = [];

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
		processingParameters;       % Processing configuration parameters
		calcSpeed = 0;             % Speed calculation flag (legacy)
		currentVisualizationStyle; % Current display style identifier


	end

	methods(Static, Access=public)

		function D = polarEuclidDistance(X, Y)
			% polarEuclidDistance: calculate euclidean distance of two points specified
			% by their polar coordinates
			%
			% Inputs:
			%   X ... point X position [range, yaw, pitch]
			%   Y ... point Y position [range, yaw, pitch]
			% Outputs:
			%   D ... distance
			Xrange = X(:,1);
			Xyaw = X(:,2);
			Xpitch = X(:,3);

			Yrange = Y(:,1);
			Yyaw = Y(:,2);
			Ypitch = Y(:,3);

			Xx = Xrange .* cosd(Xpitch) .* cosd(Xyaw); % I don't need to do the 90-Xyaw here
			Xy = Xrange .* cosd(Xpitch) .* sind(Xyaw);
			Xz = Xrange .* sind(Xpitch);

			Yx = Yrange .* cosd(Ypitch) .* cosd(Yyaw);
			Yy = Yrange .* cosd(Ypitch) .* sind(Yyaw);
			Yz = Yrange .* sind(Ypitch);

			D = sqrt(...
				(Xx - Yx.').^2 + ...
				(Xy - Yy.').^2 + ...
				(Xz - Yz.').^2 ...
				)';
			D=D./((Xrange+Yrange)/10); % scale linearly with distance
		end
		function [yaw, pitch, cfar, rangeDoppler, speed] = ...
				processBatch(batchRangeFFTs, batchTimes, posTimes, posYaw, posPitch, processingParameters)
			% processBatch: Processes FFT batch into CFAR detections and Range-Doppler maps
			%
			% Inputs:
			%   batchRangeFFTs ... Batch of range FFTs [rangeBins x chirps]
			%   batchTimes ... Timestamps for each chirp [1 x chirps]
			%   posTimes ... Platform timestamps [1 x N]
			%   posYaw ... Platform yaw angles [1 x N] (degrees)
			%   posPitch ... Platform pitch angles [1 x N] (degrees)
			%   processingParameters . Configuration struct (CFAR/FFT params)
			%
			% Outputs:
			%   yaw ... Final yaw angle (degrees)
			%   pitch ... Final pitch angle (degrees)
			%   cfar ... CFAR detection vector [rangeBins x 1]
			%   rangeDoppler ... Range-Doppler matrix [rangeBins x dopplerBins]
			%   speed ... Platform motion speed (m/s)


			yaw = posYaw(end);
			pitch = posPitch(end);
			distance = (0:(processingParameters.rangeNFFT/2-1))*processingParameters.rangeBinWidth;

			lastFFT = abs(batchRangeFFTs(:,end))';

			rangeProfile = lastFFT(1:processingParameters.rangeNFFT/2);
			rangeProfile = ((rangeProfile.^2).*distance.^4)';

			if processingParameters.calcCFAR == 1
				cfarDetector = phased.CFARDetector('NumTrainingCells',processingParameters.cfarTraining, ...
					'NumGuardCells',processingParameters.cfarGuard);
				cfarDetector.ThresholdFactor = 'Auto';
				cfarDetector.ProbabilityFalseAlarm = 1e-3;

				cfar = cfarDetector(rangeProfile, 1:processingParameters.rangeNFFT/2);
				delete(cfarDetector);
			else
				cfar = [];
			end

			tmp = diff(posYaw);
			rawDiffYaw = abs((mod(tmp + 180, 360) - 180));
			rawDiffPitch = abs(diff(posPitch));

			if ~processingParameters.calcRaw
				timeElapsed = posTimes(end) - posTimes(1);
				rangeDoppler = [];
				speed = sqrt((sum(rawDiffYaw)^2 + sum(rawDiffPitch)^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason
				return;
			end

			if ~processingParameters.calcSpeed
				timeElapsed = posTimes(end) - posTimes(1);
				speed = sqrt((sum(rawDiffYaw)^2 + sum(rawDiffPitch)^2)) / (timeElapsed + 1e-6); % speed falls to zero for some reason
				rangeDoppler = rangeProfile;
				yaw = posYaw(end);
				pitch = posPitch(end);
				return;
			end


			% only allow samples whose position was within 4 degrees of our
			% current one
			% four is totally arbitrary number, given radars capabilities speed
			% processing is more of an demonstration of processing than anything
			% practical
			tolerace = 6;
			idxPosition = length(posYaw);
			for i=length(posYaw):-1:1
				distanceYaw = abs((mod((posYaw(i)-posYaw(end)) + 180, 360) - 180));
				distancePitch = posPitch(i)- posPitch(end);
				if sqrt(distanceYaw^2 + distancePitch^2) > tolerace
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

			distanceMap = repmat(distance', [1,processingParameters.speedNFFT]);


			% Timing based analysis
			timeDeltas = diff(batchTimes);
			meanInterval = mean(timeDeltas);

			% Check uniformity of sampling (20% threshold)
			[~, idx] = max(abs(timeDeltas - meanInterval));
			maxDeviation = timeDeltas(idx) / meanInterval;
			useNUFFT = maxDeviation > 0.2;


			% Run FFT
			spectrumns = batchRangeFFTs(:, idxBatch:end);

			if useNUFFT
				batchTimes = batchTimes(idxBatch:end);
				t0 = batchTimes(1);
				timeRelative = batchTimes - t0;
				timeTotal = max(timeRelative(end) - timeRelative(1), 0.001);
				freqDoppler = (-processingParameters.speedNFFT/2 : processingParameters.speedNFFT/2-1) * (1 / timeTotal);
				% Vectorized NUFFT along slow-time (dim=1), hopefully this is correct
				tmp = nufft(spectrumns', timeRelative, freqDoppler,1 );
				tmp = abs(tmp)';
				%tmp = abs(fftshift(tmp,1))'; % NUFFT spectrum doesn't need shift to be
				%correct
			else
				tmp = abs(fftshift(fft(spectrumns, processingParameters.speedNFFT, 2),2));
			end

			tmp = tmp(1:processingParameters.rangeNFFT/2, 1:processingParameters.speedNFFT);
			rangeDoppler = single((tmp.^2).*distanceMap);
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

				if obj.processingParameters.calcRaw && obj.processingParameters.calcCFAR
					fprintf("dataProcessor | updateFinished | Range-Azimuth | RAW + CFAR\n");
					data = sum(obj.hDataCube.rawCube, 2);
					toDraw = squeeze(data(:, 1, :, obj.pitchIndex));
					cfarData = squeeze(obj.hDataCube.cfarCube(:, :, obj.pitchIndex));
					cfarData(cfarData > obj.cfarDrawThreshold) = max(toDraw); % give cfar data distinct value
					toDraw = toDraw+cfarData;

				elseif obj.processingParameters.calcRaw
					fprintf("dataProcessor | updateFinished | Range-Azimuth | RAW\n");
					data = sum(obj.hDataCube.rawCube, 2);
					toDraw = squeeze(data(:, 1, :, obj.pitchIndex));
				elseif obj.processingParameters.calcCFAR
					fprintf("dataProcessor | updateFinished | Range-Azimuth | CFAR\n");
					toDraw = squeeze(obj.hDataCube.cfarCube(:, :, obj.pitchIndex));
					toDraw(toDraw < obj.cfarDrawThreshold) = 0;
				else
					fprintf("dataProcessor | updateFinished | Range-Azimuth | NO DATA\n");
					toDraw = zeros(obj.hDataCube.rawCubeSize([1 3]));
				end

				obj.hSurf.CData = toDraw;
			elseif strcmp(obj.currentVisualizationStyle, 'Target-3D')
				% Update platform direction line
				[yaw_platform, pitch_platform] = obj.hPlatform.getLastPosition();
				maxRange = (obj.processingParameters.rangeNFFT/2) * obj.processingParameters.rangeBinWidth;

				Xline = maxRange * cosd(pitch_platform) * cosd(-yaw_platform + 90);
				Yline = maxRange * cosd(pitch_platform) * sind(-yaw_platform + 90);
				Zline = maxRange * sind(pitch_platform);

				set(obj.hLine, ...
					'XData', [0, Xline], ...
					'YData', [0, Yline], ...
					'ZData', [0, Zline]);

				% Update data itself
				idx = find(obj.hDataCube.cfarCube >= obj.cfarDrawThreshold);
				[rangeBin, yawBin, pitchBin] = ind2sub(size(obj.hDataCube.cfarCube), idx);
				range = (rangeBin - 1) * obj.processingParameters.rangeBinWidth;
				yaw = obj.hDataCube.yawBins(yawBin);
				pitch = obj.hDataCube.pitchBins(pitchBin);

				if isempty(rangeBin)
					fprintf("dataProcessor | updateFinished | updating 3D plot | no data\n");
					set(obj.hScatter3D, 'XData', [], 'YData', [], 'ZData', []);
				elseif obj.processingParameters.dbscanEnable == 1

					fprintf("dataProcessor | updateFinished | updating 3D plot | DBSCAN\n");
					normalizedRange = range / obj.processingParameters.dbscanRangeT;
					normalizedYaw = yaw / obj.processingParameters.dbscanAngleT;
					normalizedPitch = pitch / obj.processingParameters.dbscanAngleT;

					epsilon = 1.0;   % Points must be within 1 normalized unit in ALL dimensions
					points = [normalizedRange, normalizedYaw', normalizedPitch'];
					numel(normalizedRange)
					labels = dbscan(points, epsilon, obj.processingParameters.dbscanMinDetections, 'Distance', @(X,Y) dataProcessor.polarEuclidDistance(X, Y));

					validLabels = labels(labels ~= -1); % remove garbage
					validPoints = points(labels ~= -1, :);
					numel(validLabels)
					clusteredRange = validPoints(:, 1) * obj.processingParameters.dbscanRangeT;
					clusteredYaw = validPoints(:, 2) * obj.processingParameters.dbscanAngleT;
					clusteredPitch = validPoints(:, 3) * obj.processingParameters.dbscanAngleT;

					X = clusteredRange .* cosd(clusteredPitch) .* cosd(-clusteredYaw + 90);
					Y = clusteredRange .* cosd(clusteredPitch) .* sind(-clusteredYaw + 90);
					Z = clusteredRange .* sind(clusteredPitch);

					set(obj.hScatter3D, 'XData', X, 'YData', Y, 'ZData', Z, 'CData', validLabels);
				else
					fprintf("dataProcessor | updateFinished | updating 3D plot | CFAR\n");
					X = range' .* cosd(pitch) .* cosd(-yaw+90);
					Y = range' .* cosd(pitch) .* sind(-yaw+90);
					Z = range' .* sind(pitch);
					% fprintf("dataProcessor | updateFinished | X:%d, Y:%d, Z%d. limX = [%d, %d], limY = [%d, %d], limZ = [%d, %d]\n", ...
					%		length(X), length(Y), length(Z), ...
					%		min(X), max(X), ...
					%		min(Y), max(Y), ...
					%		min(Z), max(Z));
					%set(obj.hScatter3D, 'XData', [0, 1], 'YData', [0, 1], 'ZData', [0,1], 'CData', [0,-Inf]);
					set(obj.hScatter3D, 'XData', X, 'YData', Y, 'ZData', Z, 'CData', obj.hDataCube.cfarCube(idx));
				end
			elseif strcmp(obj.currentVisualizationStyle, 'Range-Doppler')
				if obj.processingParameters.calcSpeed == 1
					fprintf(dataProcessor | updateFinished | "Updating Range-Doppler Map\n");
					data = squeeze(obj.hDataCube.rawCube(:, :, obj.yawIndex, obj.pitchIndex));

					set(obj.hImage, 'CData', data');
				else
					data = squeeze(sum(obj.hDataCube.rawCube(:, :, obj.yawIndex, obj.pitchIndex),2));
					ylim(obj.hAxes, [0, max(data)]);
					set(obj.hPlot, 'YData', data);
				end
			end




			drawnow limitrate;

			java.lang.System.gc();
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

			% TODO Deinitilize variables

			[spreadPatternEnabled, spreadPatternYaw, spreadPatternPitch] = obj.hPreferences.getProcessingSpreadPatternParamters();

			if spreadPatternEnabled == 0
				spreadPatternYaw = 0;
				spreadPatternPitch = 0;
			end

			obj.processingParameters = obj.hPreferences.getProcessingParamters();
			visual=obj.hPreferences.getProcessingVisualization();
			obj.decayType = obj.hPreferences.getDecayType();
			obj.hRadarBuffer = radarBuffer(floor(obj.processingParameters.speedNFFT*1.5), obj.processingParameters.rangeNFFT); % we need

			if obj.processingParameters.calcSpeed == 0
				obj.processingParameters.speedNFFT = 1;
			end

			obj.hDataCube = radarDataCube( ...
				obj.processingParameters.rangeNFFT/2, ...
				obj.processingParameters.speedNFFT, ...
				obj.hPreferences.getProcessingBatchSize(), ...
				spreadPatternYaw, ...
				spreadPatternPitch, ...
				obj.processingParameters.calcRaw , ...
				obj.processingParameters.calcCFAR, ...
				obj.decayType ...
				);



			if strcmp(visual, 'Range-Azimuth')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing as yaw-range map.\n")
				obj.currentVisualizationStyle = 'Range-Azimuth';
				obj.deinitializeDisplay();
				obj.initializeARDisplay();
			end

			if strcmp(visual, 'Target-3D')
				fprintf("dataProcessor | onNewConfigAvailable | visualizing CFAR in 3D.\n")
				obj.currentVisualizationStyle = 'Target-3D';
				obj.deinitializeDisplay();
				obj.initialize3DDisplay();
			end

			if strcmp(visual, 'Range-Doppler')
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
				if obj.processingParameters.requirePosChange == 1


					diffYaw = abs((mod(yaw(end)-obj.lastProcesingYaw + 180, 360) - 180));
					distance = sqrt(diffYaw^2 + (pitch(end)-obj.lastProcesingPitch)^2);
					if distance < 1
						% I can process every single frame without issue but there no need
						% to process frames where position has not changed, this data is
						% only useful for speed calculation
						return;
					else
						% we exclude last frame as that one will have different position that the
						% previous one
						[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatchOld();
						[posTimes, yaw, pitch] = obj.hPlatform.getPositionsInInterval(min(batchTimes), max(batchTimes));
					end
				else
					[batchRangeFFTs, batchTimes] = obj.hRadarBuffer.getSlidingBatch();

				end

				% this needs to stay this way regardless if we take last frame or not;
				obj.lastProcesingYaw = yaw(end);
				obj.lastProcesingPitch = pitch(end);



				% [ yaw, pitch, cfar, rangeDoppler, speed] = dataProcessor.processBatch( ...
				% 		batchRangeFFTs,  ...
				% 		batchTimes, ...
				% 		posTimes, ...
				% 		yaw, ...
				% 		pitch, ...
				% 		obj.processingParameters);
				%
				% obj.mergeResults(yaw, pitch, cfar, rangeDoppler, speed);

				%fprintf("dataProcessor | onNewDataAvailable | processing: yaw: %f, pitch %f\n", yaw(end), pitch(end));
				future = parfeval(obj.parallelPool, ...
					@dataProcessor.processBatch, 5, ...
					batchRangeFFTs, ...
					batchTimes, ...
					posTimes, ...
					yaw, ...
					pitch, ...
					obj.processingParameters);
				afterAll(future, @(varargin) obj.mergeResults(varargin{:}), 0);
			else
				fprintf("dataProcessor | onNewDataAvailable | pool empty\n");
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

			if ~isempty(obj.hLine) && isvalid(obj.hLine)
				delete(obj.hLine)
			end
			if ~isempty(obj.hPlot) && isvalid(obj.hPlot)
				delete(obj.hPlot)
			end
			obj.hAxes = [];
			obj.hSurf = [];
			obj.hEditPitch = [];
			obj.hEditYaw = [];
			obj.hImage = [];
			obj.hPlot = [];
			obj.hLine = [];
			obj.hLabelPitch = [];
			obj.hLabelYaw = [];
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
			colormap(obj.hAxes, 'abyss');
			if obj.processingParameters.calcRaw && obj.processingParameters.calcCFAR
				title(obj.hAxes, 'Azimuth-Range Polar Map (RAW+CFAR)');
			elseif obj.processingParameters.calcRaw
				title(obj.hAxes, 'Azimuth-Range Polar Map (RAW)');
			elseif obj.processingParameters.calcCFAR
				title(obj.hAxes, 'Azimuth-Range Polar Map (CFAR)');
			else
				title(obj.hAxes, 'Azimuth-Range Polar Map (NO DATA)');
			end

			obj.hEditPitch = uicontrol(obj.hPanel, ...
				'Style', 'edit', ...
				'String', num2str(obj.hDataCube.pitchBins(obj.pitchIndex)), ...
				'Max',1, ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,~) obj.updatePitchIndex(src));

			obj.hLabelPitch = uilabel(obj.hPanel, ...
				'Text', 'Pitch [deg]:');

			axis(obj.hAxes, 'equal'); % otherwise circle wont be much of an circle
			hold(obj.hAxes, 'on');


			circleRadii = linspace(0, obj.processingParameters.rangeNFFT/2, 6);

			markerColor = [0.9 0.9 0.9];
			for r = circleRadii(2:end)
				theta = linspace(0, 2*pi, 100);
				[x, y] = pol2cart(theta, r);
				plot(obj.hAxes, x, y, ':', 'Color', markerColor, 'LineWidth', 1);
				text(obj.hAxes, 6, r, sprintf('%3.3f m', r* obj.processingParameters.rangeBinWidth), 'VerticalAlignment', 'top', ...
					'HorizontalAlignment', 'center', 'Color', markerColor, 'FontSize', 10);
			end

			%Angle markers at 0°, 90°, 180°, 270°
			angles = [0, 45, 90, 135, 180, 225, 270, 315];
			for angle = angles
				th = -deg2rad(angle) + pi/2;  % Match coordinate system rotation
				[x, y] = pol2cart(th, obj.processingParameters.rangeNFFT/2);
				plot(obj.hAxes, [0, x], [0, y], '--', 'Color', markerColor, 'LineWidth', 1/ (mod(angle,2)+1));
			end


			hold(obj.hAxes, 'off');

			obj.resizeUI();
		end

		function initialize3DDisplay(obj)
			% initialize3DDisplay: Creates 3D scatter plot for target visualization

			maxRange = obj.processingParameters.rangeNFFT/2* obj.processingParameters.rangeBinWidth;

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

			hold(obj.hAxes, 'on'); % Ensure radar marker stays on top of other plots
			scatter3(obj.hAxes, 0, 0, 0, ...
				'Marker', 'pentagram', ...   % Star-shaped marker
				'SizeData', 100, ...         % Larger size
				'MarkerFaceColor', [1 0 0], ... % Red color
				'MarkerEdgeColor', [0 0 0], ...  % Black border
				'DisplayName', 'Radar' ...
				);
			obj.hLine = plot3(obj.hAxes, [0, 0], [0, 0], [0, 0], ...
				'Color', 'r', 'LineWidth', 2, 'Visible', 'on');

			hold(obj.hAxes, 'off');

			if obj.processingParameters.dbscanEnable == 1
				title(obj.hAxes, '3D Target Visualization (DBSCAN)');
			else
				title(obj.hAxes, '3D Target Visualization');
			end





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
			if(obj.processingParameters.calcSpeed)

				maxRange = obj.processingParameters.rangeNFFT/2* obj.processingParameters.rangeBinWidth;
				maxSpeed = obj.processingParameters.speedNFFT*obj.hPreferences.getSpeedBinWidth();
				initialData = zeros(obj.processingParameters.speedNFFT, ...
					obj.processingParameters.rangeNFFT/2);
				speedBins = linspace(-maxSpeed, maxSpeed-obj.hPreferences.getSpeedBinWidth(), obj.processingParameters.speedNFFT)*1000;
				rangeBins = linspace(0, maxRange, obj.processingParameters.rangeNFFT/2);

				obj.hImage = imagesc(obj.hAxes, ...
					rangeBins, ...
					speedBins, ...
					initialData);

				%obj.hAxes.YAxis.Exponent = 3;
				if obj.processingParameters.calcRaw
					title(obj.hAxes, 'Range-Doppler Map');
				else
					title(obj.hAxes, 'Range-Doppler Map (NO DATA)');
				end
				xlabel(obj.hAxes, 'Range [m]');
				ylabel(obj.hAxes, 'Speed [mm/s]');
				colormap(obj.hAxes, 'jet');
			else
				maxRange = obj.processingParameters.rangeNFFT/2* obj.processingParameters.rangeBinWidth;
				rangeBins = linspace(0, maxRange, obj.processingParameters.rangeNFFT/2);
				initialData = zeros(length(rangeBins));  % Row vector for initial YData

				obj.hPlot = plot(obj.hAxes, rangeBins, initialData);

				if obj.processingParameters.calcRaw
					title(obj.hAxes, 'Range-RCS Map');
				else
					title(obj.hAxes, 'Range-Doppler Map (NO DATA)');
				end
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
			% Parallel pooled is also launched from here
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

			obj.parallelPool = gcp('nocreate'); % Start parallel pool

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
				if obj.processingParameters.calcSpeed == 1
					type = "RD";
				else
					type = "RR";
				end
			end
			pathDir = fullfile(pwd,+'images');
			if ~exist(pathDir, 'file')
				mkdir(pathDir);
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


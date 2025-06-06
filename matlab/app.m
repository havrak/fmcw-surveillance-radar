classdef app < handle
	% APP Main application class for surveillance radar

	properties (Access = private)
		% GUI %
		hPanelBtn              % uipanel - panel to group buttons
		hBtnConnectRadar;      % uicontrol/pushBtn - new program
		hBtnConnectPlatform;   % uicontrol/pushBtn - delete a program
		hBtnToggleProcessing;
		hBtnStopPlatform;
		hBtnSaveScene;
		hPanelView;

		% OTHER VARS%
		hPreferences preferences;
		hPlatformControl platformControl;
		hDataProcessor dataProcessor;
		hRadar;
		hTask;

		hFig;
		hToolbar;
	end


	methods(Access=private)
		function obj = app()
			% APP constructor for the app class

			fprintf('App | launching application\n');
			addpath("scripts");
			obj.constructGUI();

			parallelPool = gcp('nocreate'); % Start parallel pool

			if isempty(parallelPool)
				filesToAttach = {
					'app.m', 'cfarCube.dat', 'dataProcessor.m', 'main.m', ...
					'platformControl.m', 'preferences.m', 'radar.m', ...
					'radarBuffer.m', 'radarDataCube.m', 'rawCube.dat', ...
					'scripts/', ... % Attach entire scripts directory
					'matlab demo/'  % Attach demo folder if needed
					};
				fprintf("App | starting paraller pool\n");

				% If MATLAB's parallelization toolkit wasn't broken mess this could have
				% been declared as Threads instead of processes. But it is and memmafile's
				% don't work in threads memory sharing doesn't work with threads and
				% calling external scripts even if they are in path doesn't work with
				% threads. So I need to wait ages and
				parpool('Processes', 3, 'AttachedFiles', filesToAttach);
			end

			time = tic; % establish common time base, call to get unix timestamp si rather lengthy
			obj.hPreferences = preferences();
			obj.hPlatformControl = platformControl(obj.hPreferences, time);
			obj.hRadar = radar(obj.hPreferences, time);
			obj.hDataProcessor = dataProcessor(obj.hRadar, obj.hPlatformControl, obj.hPreferences, obj.hPanelView);

		end


		function shutdown(obj)
			% SHUTDOWN safely stops all app processes

			fprintf("App | shutdown\n")
			obj.hRadar.endProcesses();
			obj.hPlatformControl.endProcesses();
		end

		function constructGUI(obj)
			% CONSTRUCTGUI initializes all GUI elements

			figSize = [1200, 800];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = uifigure('Name', 'FMCW', ...
				'Position', [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar', 'none', ...
				'NumberTitle', 'off', ...
				'Resize', 'on', ...
				'Visible', 'on', ...
				'AutoResizeChildren', 'off', ...
				'DeleteFcn',@(~,~) obj.shutdown());


			obj.hPanelView = uipanel(obj.hFig, 'Title','Radar view');
			obj.hToolbar = uitoolbar(obj.hFig);

			[img,map] = imread(fullfile(matlabroot,...
				'toolbox','matlab','icons','profiler.gif'));
			ptImage = ind2rgb(img,map);

			uipushtool(obj.hToolbar, ...
				'TooltipString', 'Preferences', ...
				'ClickedCallback', @(~,~) obj.hPreferences.showGUI(), ...
				'CData', ptImage); % Example icon

			[img,map] = imread(fullfile(matlabroot,...
				'toolbox','matlab','icons','tool_rotate_3d.gif'));
			ptImage = ind2rgb(img,map);

			uipushtool(obj.hToolbar, ...
				'TooltipString', 'Platform', ...
				'ClickedCallback', @(~,~) obj.hPlatformControl.showGUI(), ...
				'CData', ptImage); % Example icon


			[img,map] = imread(fullfile(matlabroot,...
				'toolbox','matlab','icons','helpicon.gif'));
			ptImage = ind2rgb(img,map);

			uipushtool(obj.hToolbar, ...
				'TooltipString', 'Help', ...
				'CData', ptImage);

			obj.hPanelBtn = uipanel('Parent', obj.hFig, ...
				'Title', 'Actions', ...
				'Tag', 'ButtonPanel', ...
				'Units', 'pixels');

			obj.hBtnConnectPlatform = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Platform', ...
				'Callback', @(src, event) obj.setupPlatformSerial(), ...
				'BackgroundColor', '#E57373');


			obj.hBtnConnectRadar = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Radar', ...
				'Callback', @(src, event) obj.setupRadarSerial(), ...
				'BackgroundColor', '#E57373');

			obj.hBtnToggleProcessing = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Processing', ...
				'Callback', @(src, event) obj.toggleProcessing(), ...
				'BackgroundColor', '#66BB6A');

			obj.hBtnSaveScene = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Save scene', ...
				'Callback', @(src, event) obj.hDataProcessor.saveScene());

			obj.hBtnStopPlatform = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'STOP PLATFORM', ...
				'Callback', @(src, event) obj.hPlatformControl.stopPlatform());

			set(obj.hFig, 'SizeChangedFcn', @(src, event) obj.resizeUI());

			obj.resizeUI();

		end

		function setupPlatformSerial(obj)
			% SETUPPLATFORMSERIAL attempts to connec to the platform
			%
			% if successfull it will change bg color of connect button

			if obj.hPlatformControl.setupSerial()
				set(obj.hBtnConnectPlatform, 'BackgroundColor', '#66BB6A');
			else
				set(obj.hBtnConnectPlatform, 'BackgroundColor', '#E57373');
			end
		end


		function toggleProcessing(obj)
			% TOGGLEPROCESSING toggles the processing of the radar data

			if obj.hDataProcessor.toggleProcessing()
				set(obj.hBtnToggleProcessing, 'BackgroundColor', '#66BB6A');
			else
				set(obj.hBtnToggleProcessing, 'BackgroundColor', '#E57373');
			end
		end

		function setupRadarSerial(obj)
			% SETUPRADARSERIAL attempts to connec to the radar
			%
			% if successfull it will change bg color of connect button

			if obj.hRadar.setupSerial()
				set(obj.hBtnConnectRadar, 'BackgroundColor', '#66BB6A');
			else
				set(obj.hBtnConnectRadar, 'BackgroundColor', '#E57373');
			end

		end

		function resizeUI(obj)
			% RESIZEUI resizes GUI to fit current window size
			%
			% called on change of size of the main figure

			figPos = get(obj.hFig, 'Position');
			width = figPos(3);
			height = figPos(4);

			displayWidth = width - 180;

			buttonPanelWidth = 180;
			obj.hPanelBtn.Position = [displayWidth, 20, buttonPanelWidth-10, height - 30];
			buttonHeight = 40;
			spacing = 10;
			obj.hBtnConnectPlatform.Position = [10, height - 50 - buttonHeight - spacing, buttonPanelWidth - 30, buttonHeight];
			obj.hBtnConnectRadar.Position = [10, height - 50 - 2 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];

			obj.hBtnToggleProcessing.Position = [10, height - 50 - 3 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hBtnSaveScene.Position = [10, height - 50 - 4 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];

			obj.hBtnStopPlatform.Position = [10, 20, buttonPanelWidth - 30, buttonHeight];
			obj.hPanelView.Position = [10, 20, width-200, height-30];

			% resize callback gets called right after creating gui so before hDataProcessor is even initialized
			if ~isempty(obj.hDataProcessor) && isvalid(obj.hDataProcessor)
				obj.hDataProcessor.resizeUI();
			end
		end

	end


	methods(Static=true)

		function handle=getInstance()
			% GETINSTANCE access method to app's singleton instance

			persistent instanceStatic;
			if isempty(instanceStatic) || (~isempty(instanceStatic.hToolbar) && ~isvalid(instanceStatic.hToolbar))

				instanceStatic = app();
			end
			handle=instanceStatic;
		end
	end
end

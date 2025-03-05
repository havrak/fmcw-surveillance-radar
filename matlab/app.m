classdef app < handle
	properties (Access = public)
		% GUI %
		hPanelBtn              % uipanel - panel to group buttons
		hBtnConnectRadar;      % uicontrol/pushBtn - new program
		hBtnConnectPlatform;   % uicontrol/pushBtn - delete a program
		hBtnConnectPlatformS;
		hTextTelemetry;        % uicontrol/text - basic telemetry


		% OTHER VARS%
		hPreferences preferences
		hPlatformControl platformControl
		hRadar;
		hTask;

		hFig;
    hToolbar;
	end

	methods(Access=private)
		function obj = app()
			obj.constructGUI();
            time = tic; % establish common time base, call to get unix timestamp si rather lengthy
			obj.hPreferences = preferences();
			obj.hPlatformControl = platformControl(obj.hPreferences, time); 
			obj.hRadar = radar(obj.hPreferences, time);
		end


		function shutdown(obj)
			fprintf("App | shutdown\n")
			obj.hRadar.endProcesses();
			obj.hPlatformControl.endProcesses();
		end

		function constructGUI(obj)
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

			obj.hTextTelemetry =  uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'ProgramDisplay', ...
				'Max', 100, ...
				'HorizontalAlignment', 'left', ...
				'String', 'LOG');

			obj.hBtnConnectPlatform = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Platform Connect', ...
				'Callback', @(src, event) obj.setupPlatform(), ...
				'BackgroundColor', '#E57373');


			obj.hBtnConnectRadar = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Radar Connect', ...
				'Callback', @(src, event) obj.setupRadarSerial(), ...
				'BackgroundColor', '#E57373');

			set(obj.hFig, 'SizeChangedFcn', @(src, event) obj.resizeUI());

		end

        function setupPlatform(obj)
			if obj.hPlatformControl.setupSerial()
				set(obj.hBtnConnectPlatform, 'BackgroundColor', '#66BB6A');
			else 
				set(obj.hBtnConnectPlatform, 'BackgroundColor', '#E57373');
			end

    	end

		function setupRadarSerial(obj)
			if obj.hRadar.setupSerial()
				set(obj.hBtnConnectRadar, 'BackgroundColor', '#66BB6A');
			else 
				set(obj.hBtnConnectRadar, 'BackgroundColor', '#E57373');
			end

		end

		function resizeUI(obj)
			figPos = get(obj.hFig, 'Position');
			width = figPos(3);
			height = figPos(4);

			displayWidth = width - 180;

			obj.hTextTelemetry.Position = [10, 20, displayWidth-20, 200];

			buttonPanelWidth = 180;
			obj.hPanelBtn.Position = [displayWidth, 20, buttonPanelWidth-10, height - 30];
			buttonHeight = 40;
			spacing = 10;
			obj.hBtnConnectPlatform.Position = [10, height - 50 - buttonHeight - spacing, buttonPanelWidth - 30, buttonHeight];
			obj.hBtnConnectRadar.Position = [10, height - 50 - 2 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];

		end

	end


	methods(Static=true)

		function handle=getInstance()
			persistent instanceStatic;
			if isempty(instanceStatic) || (~isempty(instanceStatic.hToolbar) && ~isvalid(instanceStatic.hToolbar))
				fprintf('App | getInstance | Creating new instance\n');
				instanceStatic = app();
			end
			handle=instanceStatic;
		end
	end
end

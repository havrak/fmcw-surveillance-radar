classdef platformControl < handle
	% platformControl:  Manages rotary platform communication, data acquisition, and configuration
	%
	% Handles serial communication with a rotary platform,  processes incoming
	% data streams, and dynamically updates configurations via a preferences
	% object. Supports real-time data buffering and event-driven processing.

	properties (Access = private)

		% GUI %
		hFig;                  % Main GUI figure window
		hListboxSidebar;       % Listbox for program selection
		hEditCommand;          % Quick command input field - gets sent to platform immediately
		hEditProgramHeader;    % Editor window for program header section
		hEditProgramMain;      % Editor window for program main section

		hPanelBtn              % Panel to group buttons
		hBtnNew;               % Create a new program
		hBtnDelete;            % Delete a program
		hBtnStore;             % Store programs in permanent config
		hBtnSave;              % Save program form form to struct
		hBtnUpload;            % Upload current program to the platform
		hBtnStart;             % Send start command with correct program name to the platform
		hBtnClose;             % Close figure button
		hLabelCommand;         % Label for quick command text box
		hTextOut;              % Output display for data coming from the platform


		% OTHER VARS%
		hPreferences preferences;     % Handle to preferences object
		hSerial = [];                 % Serial port object for platform communication
		programs;                     % Struct storing all platform programs
		currentProgramName;           % Name of currently selected program

		bufferSize double = 500;      % Size of circular position buffers (default: 500)
		positionTimes;                % Array of timestamps (seconds since start)
		positionYaw;                  % Array of yaw positions
		positionPitch;                % Array of pitch positions
		currentIdx double = 1;        % Circular buffer write index

		angleOffsetYaw = 0;           % Yaw calibration offset (degrees)
		angleOffsetPitch = 0;         % Pitch calibration offset (degrees)
		stepCountYaw = 200;           % Stepper motor steps per full yaw rotation
		stepCountPitch = 200;         % Stepper motor steps per full pitch rotation
		angleTriggerYaw =10000;       % Target yaw angle for event triggering (-1 = disabled)
		angleTriggerYawTorelance = 2; % How far can we be for trigger to trigger
		angleTriggerYawTimestamp = 0; % Just for debounce yaw trigger

		mockDataTimer;
		mockDataYaw=0;
		mockDataPeriod=0.02;

		log cell = {};      % Console output history cell array
		startTime uint64;   % Base timestamp (uint64) for relative timing

	end

	events
		positionTriggerHit; % notify on position change
	end

	methods(Access=private)

		function mockData(obj)
			% mockData: simulates rotation in yaw axes
			%
			% used solely for debug

			RMP = 1;
			addition = RMP*6*obj.mockDataPeriod; % deg/s* mockDataPeriod
			obj.mockDataYaw = mod(obj.mockDataYaw+addition, 360);
			obj.positionTimes(obj.currentIdx) = toc(obj.startTime);
			obj.positionYaw(obj.currentIdx) = mod(obj.mockDataYaw-obj.angleOffsetYaw,360);
			obj.positionPitch(obj.currentIdx) = 0;

			if obj.angleTriggerYaw ~= -1 && ...
					(mod(obj.positionYaw(obj.currentIdx) - obj.angleTriggerYaw, 360) <= 2*obj.angleTriggerYawTorelance) && ...
					toc(obj.angleTriggerYawTimestamp) > 1
				obj.angleTriggerYawTimestamp = tic;
				notify(obj, 'positionTriggerHit');
			end

			obj.currentIdx = mod(obj.currentIdx, obj.bufferSize) + 1;

		end
		function constructGUI(obj)
			% constructGUI: initializes all GUI elements

			figSize = [800, 600];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = uifigure('Name', 'Platform Control', ...
				'Position',  [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar', 'none', ...
				'NumberTitle', 'off', ...
				'Resize', 'on', ...
				'Visible', 'off', ...
				'AutoResizeChildren', 'off');

			obj.hListboxSidebar = uicontrol('Style', 'listbox', ...
				'Parent', obj.hFig, ...
				'Tag', 'Sidebar', ...
				'Value', 1, ...
				'String', fieldnames(obj.programs), ...
				'Callback', @(src, event) obj.loadProgram());

			obj.hLabelCommand = uilabel(obj.hFig, ...
				'Text', 'QUICK COMMAND:', ...
				'FontWeight', 'bold', ...
				'FontSize', 16);

			obj.hEditCommand = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'CommandField', ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,data) obj.callbackQuickCommand());


			obj.hEditProgramHeader = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'ProgramDisplay', ...
				'Max', 2, ...
				'HorizontalAlignment', 'left', ...
				'String', '');

			obj.hEditProgramMain = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'ProgramDisplay', ...
				'Max', 2, ...
				'HorizontalAlignment', 'left', ...
				'String', '');

			obj.hTextOut =  uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'ProgramDisplay', ...
				'Min', 0, ...
				'Max', 200, ...
				'HorizontalAlignment', 'left', ...
				'String', 'Platform serial output');

			obj.hPanelBtn = uipanel('Parent', obj.hFig, ...
				'Title', 'Actions', ...
				'Tag', 'ButtonPanel', ...
				'Units', 'pixels');

			obj.hBtnNew = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'New Program', ...
				'Callback', @(src, event) obj.newProgram());

			obj.hBtnDelete = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Delete Program', ...
				'Callback', @(src, event) obj.deleteProgram());

			obj.hBtnStore = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Store Programs', ...
				'Callback', @(src, event) obj.storePrograms());

			obj.hBtnSave = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Save Program', ...
				'Callback', @(src, event) obj.saveProgram());

			obj.hBtnUpload = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Upload Program', ...
				'Callback', @(src, event) obj.uploadProgram());

			obj.hBtnStart = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Start Program', ...
				'Callback', @(src, event) obj.startProgram());

			obj.hBtnClose = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hPanelBtn, ...
				'String', 'Close', ...
				'Callback', @(src, event) set(obj.hFig, 'Visible', 'off'));
			obj.resizeUI();

			fprintf('PlatformControl | constructGUI | GUI constructed\n');

			set(obj.hFig,	'SizeChangedFcn', @(src, event) obj.resizeUI());

			loadProgram(obj);
		end

		function resizeUI(obj)
			% resizeUI: Dynamically adjusts GUI layout on window resize
			%
			% called by callback from the figure

			figPos = get(obj.hFig, 'Position');
			width = figPos(3);
			height = figPos(4);

			sidebarWidth = 150;
			obj.hListboxSidebar.Position = [10, 20, sidebarWidth, height - 70];
			obj.hLabelCommand.Position = [10, height-40, sidebarWidth, 30];
			obj.hEditCommand.Position = [sidebarWidth + 20, height - 40, width - sidebarWidth - 30, 30];
			displayWidth = width - sidebarWidth - 210;

			obj.hEditProgramHeader.Position = [sidebarWidth + 20, height-200, displayWidth, 150];
			obj.hEditProgramMain.Position = [sidebarWidth + 20, 230, displayWidth, height-440];

			obj.hTextOut.Position = [sidebarWidth + 20, 20, displayWidth, 200];

			buttonPanelWidth = 180;
			obj.hPanelBtn.Position = [sidebarWidth + 30 + displayWidth, 20, buttonPanelWidth-10, height - 70];
			buttonHeight = 40;
			spacing = 10;
			obj.hBtnNew.Position = [10, height - 90 - buttonHeight - spacing, buttonPanelWidth - 30, buttonHeight];
			obj.hBtnSave.Position = [10, height - 90 - 2 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hBtnDelete.Position = [10, height - 90 - 3 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hBtnUpload.Position = [10, height - 90 - 4 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hBtnStart.Position = [10, height - 90 - 5 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];

			obj.hBtnStore.Position = [10, 60, buttonPanelWidth - 30, buttonHeight]; % Fixed at the bottom of the panel

			obj.hBtnClose.Position = [10, 10, buttonPanelWidth - 30, buttonHeight]; % Fixed at the bottom of the panel

		end

		function loadProgram(obj)
			% loadProgram: Loads selected program into editor windows

			fprintf('PlatformControl | loadProgram\n');
			selected = obj.hListboxSidebar.Value; % Get selected index
			fields = fieldnames(obj.programs); % Get fieldnames
			if selected > 0 && selected <= numel(fields)
				obj.currentProgramName = fields{selected};
				text = split(obj.programs.(obj.currentProgramName), 'P91\n');
				disp(obj.currentProgramName);
				tmp=numel(text);

				if(tmp > 2)
					fprintf("platformControl | loadProgram | too many fields");
				elseif tmp == 1
					obj.hEditProgramHeader.String = sprintf(text{1}); % sprintf will execute line breaks
					obj.hEditProgramMain.String="";
				else
					obj.hEditProgramHeader.String = sprintf(text{1}); % sprintf will execute line breaks
					obj.hEditProgramMain.String = sprintf(text{2}); % sprintf will execute line breaks

				end
			end
		end

		function callbackQuickCommand(obj)
			% callbackQuickCommand: sends command from text field to the platform

			value = append(get(obj.hEditCommand, 'String'));
			set(obj.hEditCommand, 'String', '');
			flush(obj.hSerial);
			writeline(obj.hSerial, value);
		end


		function processIncommingData(obj, src)
			% processIncommingData: reads next line on serial and parses the data
			%
			% stores current position to buffer, log is displayed in text window

			line = strtrim(readline(src));
			if isempty(line)
				return;
			end

			if length(obj.log) > 200
				obj.log(1) = [];
			end

			if strncmp(line,'!P',2)
				tmp = char(line);
				vals = strtrim(split(tmp(3:end), ','));

				obj.positionTimes(obj.currentIdx) = toc(obj.startTime);
				obj.positionYaw(obj.currentIdx) = mod(str2double(vals{2})-obj.angleOffsetYaw,360);
				obj.positionPitch(obj.currentIdx) = str2double(vals{3})-obj.angleOffsetPitch;

				if obj.angleTriggerYaw ~= -1 && ...
						(mod(obj.positionYaw(obj.currentIdx) - obj.angleTriggerYaw, 360) <= 2*obj.angleTriggerYawTorelance) && ...
						toc(obj.angleTriggerYawTimestamp) > 1
					obj.angleTriggerYawTimestamp = tic;
					notify(obj, 'positionTriggerHit');
				end

				obj.currentIdx = mod(obj.currentIdx, obj.bufferSize) + 1;
				return;
			elseif strncmp(line,'!R',2)
				obj.log{end+1} = line;
				set(obj.hTextOut, 'String', obj.log);
				return;
			elseif obj.hPreferences.getPlatformDebug()
				if startsWith(line, ["I", "W", "E"])
					obj.log{end+1} = line;
					set(obj.hTextOut, 'String', obj.log);
				end
				if contains(line, 'boot: ESP-IDF')
					obj.log = {};
				end
			end
		end

		function newProgram(obj)
			% newProgram: creates new program
			%
			% after calling text box window is generated to entry program name

			userInput = inputdlg('Enter new program name', 'Create new Program', [1 50], "");
			name = userInput{1,1};
			disp(~isempty(name))
			disp(isstrprop(name, 'alphanum'))
			if all([~isempty(name) isstrprop(name, 'alphanum')])
				obj.programs.(name) = "";
				set(obj.hListboxSidebar, 'String', fieldnames(obj.programs));
			end
		end

		function deleteProgram(obj)
			% deleteProgram: deletes currently picked program

			obj.programs = rmfield(obj.programs, obj.currentProgramName);
			progs = fieldnames(obj.programs);
			set(obj.hListboxSidebar, 'String', progs);

			if(isempty(progs))
				obj.hEditProgramHeader.String = "";
			else
				set(obj.hListboxSidebar, 'Value', 1);
				obj.loadProgram();
			end
			
		end

		function storePrograms(obj)
			% storePrograms: store program in permanent configuration file

			obj.hPreferences.setPrograms(obj.programs);
			obj.hPreferences.storeConfig();
		end

		function saveProgram(obj)
			% saveProgram: saves content of editor window to internal structure

			valueHeader = get(obj.hEditProgramHeader, 'String');
			trimmedHeader = (strtrim(string(valueHeader)));
			tosaveHeader = strjoin(trimmedHeader, "\n");

			valueMain = get(obj.hEditProgramMain, 'String');
			trimmedMain = (strtrim(string(valueMain)));
			tosaveMain = strjoin(trimmedMain, "\n");
			tosave =append(tosaveHeader, "P91\n",tosaveMain);

			disp (tosave);
			obj.programs.(obj.currentProgramName) = tosave;
		end

		function startProgram(obj)
			% startProgram: starts picked program on the platform

			flush(obj.hSerial);
			writeline(obj.hSerial, "M82"); % stop current move
			writeline(obj.hSerial, "P1 "+obj.currentProgramName);
			flush(obj.hSerial);
		end

		function uploadProgram(obj)
			% uploadProgram: upload program to the platform
			%
			% P90 and P92 commands are automatically added, user is not expected
			% to enter them in program declarations

			flush(obj.hSerial);
			writeline(obj.hSerial, "P90 "+obj.currentProgramName);
			valueHeader = get(obj.hEditProgramHeader, 'String');
			trimmedHeader = (strtrim(string(valueHeader)));
			for i=1:(numel(trimmedHeader)-1)
				writeline(obj.hSerial, trimmedHeader(i));
				pause(0.02); % incoming buffer on ESP32 is not infinite so we introduce a small delay
			end
			writeline(obj.hSerial, "P91");
			valueMain = get(obj.hEditProgramMain, 'String');
			trimmedMain = (strtrim(string(valueMain)));
			for i=1:(numel(trimmedMain)-1)
				writeline(obj.hSerial, trimmedMain(i));
				pause(0.02); % incoming buffer on ESP32 is not infinite so we introduce a small delay
			end

			writeline(obj.hSerial, "P92");
			flush(obj.hSerial);
		end

		function onNewConfigAvailable(obj)
			% onNewConfigAvailable: Updates configuration from preferences
			%
			% Function is called by preference's newConfigEvent event
			% Yaw trigger is configured, step count is sent to the platform
			[obj.angleOffsetYaw, obj.angleOffsetPitch, localStepCountYaw, localStepCountPitch] = obj.hPreferences.getPlatformParamters();
			if obj.hPreferences.getDecayType() == 0
				obj.angleTriggerYaw = mod(obj.hPreferences.getTriggerYaw()-obj.angleTriggerYawTorelance, 360);
			else
				obj.angleTriggerYaw = -1;
			end

			if ~isempty(obj.hSerial) && ((localStepCountYaw ~= obj.stepCountYaw) || (localStepCountPitch ~= obj.stepCountPitch))
				obj.stepCountPitch = localStepCountPitch;
				obj.stepCountYaw = localStepCountYaw;
				command = "M92 Y"+obj.stepCountYaw + " P"+obk.stepCountPitch;
				flush(obj.hSerial);
				writeline(obj.hSerial, command);
			end
		end

	end

	methods(Access=public)

		function obj = platformControl(hPreferences, startTime)
			% platformControl: Initializes platform control with preferences and timing reference
			%
			% Inputs:
			%   hPreferences ... Handle to preferences object
			%   startTime    ... Base timestamp (output of `tic`)
			%
			% Output:
			%   obj ... Initialized platformControl instance
			obj.hPreferences = hPreferences;
			obj.startTime = startTime;
			obj.programs = obj.hPreferences.getPrograms();
			obj.positionTimes = zeros(obj.bufferSize, 1);
			obj.positionYaw = zeros(obj.bufferSize, 1);
			obj.positionPitch = zeros(obj.bufferSize, 1);
			obj.angleTriggerYawTimestamp = tic;

			obj.onNewConfigAvailable();
			addlistener(hPreferences, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());

		end

		function endProcesses(obj)
			% endProcesses: Safely stops serial communication and releases resources
			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
			end
			if ~isempty(obj.mockDataTimer)
				stop(obj.mockDataTimer);
				delete(obj.mockDataTimer);
			end
	
		end

		function status = setupSerial(obj)
			% setupSerial: Establishes serial connection to the platform
			%
			% Output:
			%   status ... true if connection succeeded



			% DEBUG
			% obj.mockDataTimer = timer;
			% obj.mockDataTimer.StartDelay = 2;
			% obj.mockDataTimer.Period = obj.mockDataPeriod;
			% obj.mockDataTimer.ExecutionMode = 'fixedSpacing';
			% obj.mockDataTimer.UserData = 0;
			% obj.mockDataTimer.TimerFcn = @(~,~) obj.mockData();
			% start(obj.mockDataTimer);
			% 
			% status = true;
			% return;
			
			% NORMAL
			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial);
				status = false;
				return;
			end
			[port, baudrate] = obj.hPreferences.getConnectionPlatform();
			try
				fprintf("platFormControl | setupSerial | port: %s, baud: %f\n", port, baudrate)
				obj.hSerial = serialport(port, baudrate, "Timeout", 5);
				configureTerminator(obj.hSerial,"CR");

				command = "M92 Y"+obj.stepCountYaw + " P"+obj.stepCountPitch; % send step count
				flush(obj.hSerial);
				writeline(obj.hSerial, command);

				fprintf("platFormControl | setupSerial | starting thread\n");
				configureCallback(obj.hSerial, "terminator", @(src, ~) obj.processIncommingData(src))
				status = true;
			catch ME
				fprintf("platFormControl | setupSerial | Failed to setup serial")
				obj.hSerial = [];
				status = false;
			end
		end


		function stopPlatform(obj)
			% stopPlatform: Emergency stop command for platform
			command = "M82"; % clears queues, issues stop requests, shuts down power
			writeline(obj.hSerial, command);
		end

		function [timestamps, yaw, pitch] = getPositionsInInterval(obj, timeMin, timeMax)
			% getPositionsInInterval: Returns position data within specified time range
			%
			% Inputs:
			%   timeMin ... Start timestamp
			%   timeMax ... End timestamp
			%
			% Outputs:
			%   timestamps ... Vector of timestamps
			%   yaw        ... Corresponding yaw angles
			%   pitch      ... Corresponding pitch angles
			[~, idxMin] = min(abs(obj.positionTimes - timeMin));
			[~, idxMax] = min(abs(obj.positionTimes - timeMax));

			if idxMin <= idxMax
				% No wrapping needed: extract the contiguous block from idxMin to idxMax
				timestamps = obj.positionTimes(idxMin:idxMax);
				yaw = obj.positionYaw(idxMin:idxMax, :);
				pitch = obj.positionPitch(idxMin:idxMax);
			else
				% Wrapping around the buffer: concatenate the end and start parts
				timestamps = [obj.positionTimes(idxMin:end); obj.positionTimes(1:idxMax)];
				yaw = [obj.positionYaw(idxMin:end, :); obj.positionYaw(1:idxMax, :)];
				pitch = [obj.positionPitch(idxMin:end); obj.positionPitch(1:idxMax)];
			end
		end

		function [yaw, pitch] = getPositionAtTime(obj, time)
			% getPositionAtTime: Returns platform position closest to specified timestamp
			%
			% Input:
			%   time ... Target timestamp
			%
			% Outputs:
			%   yaw  ... Yaw angle (0-360°)
			%   pitch ... Pitch angle (-90°-90°)
			if isempty(obj.positionTimes)
				error('No position data available.');
			end
			[~, idx] = min(abs(obj.positionTimes - time));
			yaw = obj.positionYaw(idx);
			pitch = obj.positionPitch(idx);
		end

		function showGUI(obj)
			% showGUI: displays generated GUI that is hidden
			if isempty(obj.hFig) | ~isvalid(obj.hFig)
				constructGUI(obj);
			end
			set(obj.hFig, 'Visible', 'on');
		end
	end

end

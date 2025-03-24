classdef platformControl < handle
	%properties (SetAccess = private, GetAccess=public)
	properties (Access = private)

		% GUI %
		hFig;                  % uifigure - main figure
		hListboxSidebar;       % listbox - sidebar with different programs
		hEditCommand;          % uicontrol/edit - text field for commands
		hEditProgramHeader;          % uicontrol/edit - large text field for program declaration
		hEditProgramMain;          % uicontrol/edit - large text field for program declaration

		hPanelBtn              % uipanel - panel to group buttons
		hBtnNew;               % uicontrol/pushBtn - new program
		hBtnDelete;            % uicontrol/pushBtn - delete a program
		hBtnStore;             % uicontrol/pushBtn - store programs to file
		hBtnSave;              % uicontrol/pushBtn - save program form form to struct
		hBtnUpload;            % uicontrol/pushBtn - upload program to device
		hBtnStart;             % uicontrol/pushBtn - upload program to device
		hBtnClose;             % uicontrol/pushBtn - close figure
		hLabelCommand;         % uilabel - quck commnad lable
		hTextOut;              % uicontrol/text - output from platfrom


		% OTHER VARS%
		hPreferences preferences;
		hSerial;
		programs;
		currentProgramName;

		bufferSize double = 500;
		positionTimes;      % Array of timestamps (seconds since start)
		positionYaw;        % Array of yaw positions
		positionPitch;      % Array of pitch positions
		currentIdx double = 1;

		log cell = {};
		startTime uint64;

	end

	events
		platformPositionChanged; % notify on position change
	end
	methods(Access=private)

		function constructGUI(obj)
			% constructGUI: initializes all GUI elements

			figSize = [800, 600];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = figure('Name', 'Platform Control', ...
				'Position',  [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar', 'none', ...
				'NumberTitle', 'off', ...
				'Resize', 'on', ...
				'Visible', 'off');

			obj.hListboxSidebar = uicontrol('Style', 'listbox', ...
				'Parent', obj.hFig, ...
				'Tag', 'Sidebar', ...
				'Value', 1, ...
				'String', fieldnames(obj.programs), ...
				'Callback', @(src, event) obj.loadProgram());

			obj.hLabelCommand = uilabel('Parent', obj.hFig, ...
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

			set(obj.hFig,		'SizeChangedFcn', @(src, event) obj.resizeUI());

			loadProgram(obj);
		end

		function resizeUI(obj)
			% resizeUI: resizes GUI to fit current window size
			% called on change of size of the main figure

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
			% loadProgram: loads transcript of program picked in sidebar to the
			% main editor window

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
			% callbackQuickCommand: sends value of quick command text fiedl to
			% the platform over serial

			value = append(get(obj.hEditCommand, 'String'));
			set(obj.hEditCommand, 'String', '');
			flush(obj.hSerial);
			writeline(obj.hSerial, value);
		end


		function processIncommingData(obj, src)
			% processIncommingData: reads next line on serial and parses the data

			line = strtrim(readline(src));
			if isempty(line)
				return;
			end

			if length(obj.log) > 200
				obj.log(1) = [];
			end


			if strncmp(line,'!P',2)
				% [angleOffsetH, angleOffsetT, ~] = obj.hPreferences.getPlatformParamters();
				tmp = char(line);
				vals = strtrim(split(tmp(3:end), ','));

				obj.positionTimes(obj.currentIdx) = toc(obj.startTime);
				% obj.positionYaw(obj.currentIdx) = str2double(vals{2})-angleOffsetH;
				% obj.positionPitch(obj.currentIdx) = str2double(vals{3})-angleOffsetT;



				obj.positionYaw(obj.currentIdx) = str2double(vals{2});
				obj.positionPitch(obj.currentIdx) = str2double(vals{3});
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
			% newProgram: opens dialog for user to enter name of new program and
			% than opens it in editor window

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
			% with aid of preferences class

			obj.hPreferences.setPrograms(obj.programs);
			obj.hPreferences.storeConfig();
		end

		function saveProgram(obj)
			% saveProgram: saves content of editor window to internal structure
			% if not executed program description will not be the one stored

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
			% before starting platform is stopped, homed

			flush(obj.hSerial);
			writeline(obj.hSerial, "M82"); % stop current move
			writeline(obj.hSerial, "M81"); % enable stepper drivers
			writeline(obj.hSerial, "G28"); % home steppers
			writeline(obj.hSerial, "G92"); % set home to current location
			writeline(obj.hSerial, "P1 "+obj.currentProgramName);
			flush(obj.hSerial);
		end

		function uploadProgram(obj)
			% uploadProgram: upload program to the platform
			% P90 and P92 commands are automatically added

			% Empty callback for Upload Program button

			% -> call to send
			flush(obj.hSerial);
			writeline(obj.hSerial, "P90 "+obj.currentProgramName);
			valueHeader = get(obj.hEditProgramHeader, 'String');
			trimmedHeader = (strtrim(string(valueHeader)));
			for i=1:numel(trimmedHeader)
				disp(trimmedHeader(i));
				writeline(obj.hSerial, trimmedHeader(i));
				pause(0.02); % incomming buffer on esp32 is not infinite so we introduce a small delay
			end
			writeline(obj.hSerial, "P91");
			valueMain = get(obj.hEditProgramMain, 'String');
			trimmedMain = (strtrim(string(valueMain)));
			for i=1:numel(trimmedMain)
				disp(trimmedMain(i));
				writeline(obj.hSerial, trimmedMain(i));
				pause(0.02); % incomming buffer on esp32 is not infinite so we introduce a small delay
			end

			writeline(obj.hSerial, "P92");
			flush(obj.hSerial);
		end

	end

	methods(Access=public)

		function obj = platformControl(hPreferences, startTime)
			% platformControl: constructor for the platformControl class
			%
			% INPUT:
			% hPreferences ... handle to preferences object
			% startTime ... output of tic command, timestamp to which all others
			% are calculated from

			obj.hPreferences = hPreferences;
			obj.startTime = startTime;
			obj.programs = obj.hPreferences.getPrograms();
			obj.positionTimes = zeros(obj.bufferSize, 1);
			obj.positionYaw = zeros(obj.bufferSize, 1);
			obj.positionPitch = zeros(obj.bufferSize, 1);
		end

		function endProcesses(obj)
			% endProcesses: safely stops all class processes

			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
			end
		end

		function status = setupSerial(obj)
			% setupSerial: establish serial connection to the platform
			%
			% OUTPUT:
			% status ... true if connection was established

			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
			end
			[port, baudrate] = obj.hPreferences.getConnectionPlatform();
			try
				fprintf("platFormControl | setupSerial | port: %s, baud: %f\n", port, baudrate)
				obj.hSerial = serialport(port, baudrate, "Timeout", 5);
				configureTerminator(obj.hSerial,"CR");
				flush(obj.hSerial);
				fprintf("platFormControl | setupSerial | starting thread\n");
				configureCallback(obj.hSerial, "terminator", @(src, ~) obj.processIncommingData(src))
				status = true;
			catch ME
				fprintf("platFormControl | setupSerial | Failed to setup serial")
				status = false;
			end
		end

		function [vec] = getSpeedVector(obj, timeMin, timeMax)
			[timestamps, yaw, pitch] = obj.getPositionsInInterval(timeMin, timeMax);
			[~,~, r] = obj.hPreferences.getPlatformParamters();

			[x,y,z] = sph2cart(yaw/180*pi, pitch/180*pi-pi, r);

		end

		function [timestamps, yaw, pitch] = getPositionsInInterval(obj, timeMin, timeMax)
			% getPositionsInInterval: returns lits of position logs that fall
			% within a given time interval in both cases timestamps closest is
			% chosen
			%
			% INPUTS:
			% timeMin ... lower bound of the interval
			% timeMax ... upper bound of the interval
			%
			% OUTPUT:
			% timestamps ... vector of timestamps for positions
			% yaw ... vector yaw angles
			% pitch ... vector of pitch angles

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
			% getPositionAtTime: retrieve the closest platform position for a given timestamp
			%
			% INPUT:
			% time ... wanted timestmap of data
			%
			% OUTPUT:
			% yaw ... yaw angle [0, 360]
			% pitch .. pitch angle [-90, 90]

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

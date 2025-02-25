classdef platformControl < handle
	%properties (SetAccess = private, GetAccess=public)
	properties (Access = public)

		% GUI %
		hFig;                  % uifigure - main figure
		hListboxSidebar;       % listbox - sidebar with different programs
		hEditCommand;          % uicontrol/edit - text field for commands
		hEditProgram;          % uicontrol/edit - large text field for program declaration
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
		timestamp double = 0;
		currPosHorz double = 0;
		currPosTilt double = 0;
        log cell = {};
		
	end
	methods(Access=private)
		function loadSavedPrograms(obj)
			obj.programs = obj.hPreferences.getPrograms(); % might need to replace \n with ;
			fprintf('PlatformControl | loadSavedPrograms | loaded programs:\n');
			disp (obj.programs)
		end

		function constructGUI(obj)
			fprintf('PlatformControl | constructGUI | starting GUI constructions\n');
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

			obj.hLabelCommand = uilabel('Parent', obj.hFig, 'Text', 'QUICK COMMAND:', 'FontWeight', 'bold', 'FontSize', 16);

			obj.hEditCommand = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'CommandField', ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,event) obj.callbackQuickCommand());


			obj.hEditProgram = uicontrol('Style', 'edit', ...
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

			set(obj.hFig, 	'SizeChangedFcn', @(src, event) obj.resizeUI());

			loadProgram(obj);
		end

		function resizeUI(obj)
			figPos = get(obj.hFig, 'Position');
			width = figPos(3);
			height = figPos(4);

			sidebarWidth = 150;
			obj.hListboxSidebar.Position = [10, 20, sidebarWidth, height - 70];
			obj.hLabelCommand.Position = [10, height-40, sidebarWidth, 30];
			obj.hEditCommand.Position = [sidebarWidth + 20, height - 40, width - sidebarWidth - 30, 30];
			displayWidth = width - sidebarWidth - 210;
			
			obj.hEditProgram.Position = [sidebarWidth + 20, 130, displayWidth, height-180];
			obj.hTextOut.Position = [sidebarWidth + 20, 20, displayWidth, 100];
			
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
			% Callback for loading a program when selected from the sidebar
			selected = obj.hListboxSidebar.Value; % Get selected index
			fields = fieldnames(obj.programs); % Get fieldnames
			if selected > 0 && selected <= numel(fields)
				obj.currentProgramName = fields{selected};
				obj.hEditProgram.String = sprintf(obj.programs.(obj.currentProgramName)); % sprintf will execute line breaks
			end
		end
		
		function callbackQuickCommand(obj)
			value = get(obj.hEditProgram, 'String');
			flush(obj.hSerial); 
			writeline(obj.hSerial, value);
		end


		function processIncommingData(obj, src)
		    line = readline(src);
            if length(obj.log) > 200
                obj.log(1) = [];
            end

			if strncmp(line,'!P',2)
				vals = split(line(2:end), ',');
				obj.timestamp = str2double(vals{1});
				obj.currPosHorz = str2double(vals{1});
				obj.currPosTilt = str2double(vals{1});
                return;
			elseif strncmp(line,'!R',2)
                obj.log{end+1} = line;
                 set(obj.hTextOut, 'String', obj.log);
                return;
            elseif obj.hPreferences.getPlatformDebug()
                if(extract(line,1) == "I" || extract(line,1) == "W" || extract(line,1) == "E")
				   obj.log(end+1) = line;
                   set(obj.hTextOut, 'String', obj.log);
                end
            end
		end

		function newProgram(obj)
			% Empty callback for Delete Program button

			fprintf('PlatformControl | newProgram\n');
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
			fprintf('PlatformControl | deleteProgram\n');
			obj.programs = rmfield(obj.programs, obj.currentProgramName);
			set(obj.hListboxSidebar, 'String', fieldnames(obj.programs));
		end

		function storePrograms(obj)
			fprintf('PlatformControl | storePrograms\n');
			obj.hPreferences.setPrograms(obj.programs);
			obj.hPreferences.storeConfig();
		end

		function saveProgram(obj)
			fprintf('PlatformControl | saveProgram\n');
			value = get(obj.hEditProgram, 'String');
			trimmed = (strtrim(string(value)));
			tosave = strjoin(trimmed, "\n");
			disp (tosave);
			obj.programs.(obj.currentProgramName) = tosave;
		end

		function startProgram(obj)
			fprintf('PlatformControl | startProgram\n');
			flush(obj.hSerial); 
			writeline(obj.hSerial, "M82"); % stop current move
			writeline(obj.hSerial, "G28"); % home steppers
			writeline(obj.hSerial, "G92"); % set home to current location
			writeline(obj.hSerial, "P1 "+obj.currentProgramName);
			flush(obj.hSerial);
		end

		function uploadProgram(obj)
			% Empty callback for Upload Program button
			value = get(obj.hEditProgram, 'String');
			trimmed = (strtrim(string(value)));
			% -> call to send 
			flush(obj.hSerial); 
			writeline(obj.hSerial, "P90 "+obj.currentProgramName);
			for i=1:numel(trimmed)
				writeline(obj.hSerial, trimmed(i));
				pause(0.002); % incomming buffer on esp32 is not infinit so we introduce a small delay
			end

			writeline(obj.hSerial, "P99");
			flush(obj.hSerial); 


		end

	end

	methods(Access=public)

		function obj = platformControl(hPreferences)
			fprintf('PlatformControl | platformControl | constructing object\n');
			obj.hPreferences = hPreferences;
			loadSavedPrograms(obj);
           
		end
		
		function endProcesses(obj)
			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
			end
		end

		function status = setupSerial(obj)
		if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
			end
			[port, baudrate] = obj.hPreferences.getConnectionPlatform();
			try 
				fprintf("platFormControl | setupSerial | port: %s, baud: %f\n", port, baudrate)
				obj.hSerial = serialport(port, baudrate, "Timeout", 5);
				configureTerminator(obj.hSerial,"LF");
				flush(obj.hSerial);
				fprintf("platFormControl | setupSerial | starting thread\n");			
				configureCallback(obj.hSerial, "terminator", @(src, ~) obj.processIncommingData(src))
				status = true;
			catch ME
				fprintf("platFormControl | setupSerial | Failed to setup serial")
				status = false;
			end

		end

		function showGUI(obj)
			if isempty(obj.hFig) % closing should be also overwritten to hide
				constructGUI(obj);
			end
			set(obj.hFig, 'Visible', 'on');
		end
	end

end

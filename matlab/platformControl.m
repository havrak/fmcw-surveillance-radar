classdef platformControl < handle
	%properties (SetAccess = private, GetAccess=public)
	properties (Access = public)
		hFig;
		hSidebar;               % Sidebar for program fieldnames
		hQuickCommand;          % Single-line text field for commands
		hProgrammingField;      % Large text field for program declaration
		hButtonPanel            % Button panel
		hButtonNew;
		hButtonDelete;          % Button to delete a program
		hButtonStore;           % Button to save store programs
		hButtonSave;            % Button to save program
		hButtonUpload;          % Button to upload programs
		hButtonClose;
		hPreferences;           
		programs;
		currentProgramName;
	end
	methods(Access=private)
		function loadSavedPrograms(obj)
			obj.programs = obj.hPreferences.getPrograms(); % might need to replace \n with ;
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

			obj.hSidebar = uicontrol('Style', 'listbox', ...
				'Parent', obj.hFig, ...
				'Tag', 'Sidebar', ...
				'Value', 1, ...
				'String', fieldnames(obj.programs), ...
				'Callback', @(src, event) obj.loadProgram());

			obj.hQuickCommand = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'CommandField', ...
				'HorizontalAlignment', 'left', ...
				'Callback', @(src,event) obj.callbackQuickCommand());


			obj.hProgrammingField = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'ProgramDisplay', ...
				'Max', 2, ...
				'HorizontalAlignment', 'left', ...
				'String', '');

			obj.hButtonPanel = uipanel('Parent', obj.hFig, ...
				'Title', 'Actions', ...
				'Tag', 'ButtonPanel', ...
				'Units', 'pixels');
			
			obj.hButtonNew = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'New Program', ...
				'Callback', @(src, event) obj.newProgram());

			obj.hButtonDelete = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'Delete Program', ...
				'Callback', @(src, event) obj.deleteProgram());

			obj.hButtonStore = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'Store Programs', ...
				'Callback', @(src, event) obj.storePrograms());

			obj.hButtonSave = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'save Programs', ...
				'Callback', @(src, event) obj.saveProgram());

			obj.hButtonUpload = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'Upload Program', ...
				'Callback', @(src, event) obj.uploadProgram());

			obj.hButtonClose = uicontrol('Style', 'pushbutton', ...
                'Parent', obj.hButtonPanel, ...
                'String', 'Close', ...
                'Callback', @(src, event) set(obj.hFig, 'Visible', 'off'));
			obj.resizeUI();

			fprintf('PlatformControl | constructGUI | GUI constructed\n');


			set(obj.hFig, 	'SizeChangedFcn', @(src, event) obj.resizeUI());
		end

		function resizeUI(obj)
			figPos = get(obj.hFig, 'Position');
			width = figPos(3);
			height = figPos(4);

			sidebarWidth = 150;
			obj.hSidebar.Position = [10, 50, sidebarWidth, height - 110];
			obj.hQuickCommand.Position = [sidebarWidth + 20, height - 40, width - sidebarWidth - 30, 30];
			displayWidth = width - sidebarWidth - 210;
			obj.hProgrammingField.Position = [sidebarWidth + 20, 50, displayWidth, height - 110];
			buttonPanelWidth = 180;
			obj.hButtonPanel.Position = [sidebarWidth + 30 + displayWidth, 50, buttonPanelWidth-10, height - 110];
			buttonHeight = 40;
			spacing = 10;
			obj.hButtonNew.Position = [10, height - 130 - buttonHeight - spacing, buttonPanelWidth - 30, buttonHeight];
			obj.hButtonStore.Position = [10, height - 130 - 2 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hButtonDelete.Position = [10, height - 130 - 3 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hButtonUpload.Position = [10, height - 130 - 4 * (buttonHeight + spacing), buttonPanelWidth - 30, buttonHeight];
			obj.hButtonClose.Position = [10, 10, buttonPanelWidth - 30, buttonHeight]; % Fixed at the bottom of the panel
      
			loadProgram(obj);

		end

		function loadProgram(obj)
			% Callback for loading a program when selected from the sidebar
			selected = obj.hSidebar.Value; % Get selected index
			fields = fieldnames(obj.programs); % Get fieldnames
			if selected > 0 && selected <= numel(fields)
				obj.currentProgramName = fields{selected};
				obj.hProgrammingField.String = sprintf(obj.programs.(obj.currentProgramName)); % sprintf will execute line breaks
			end
		end
		
		function  callbackQuickCommand(obj)
			% push commadn to the front of upload queue
		end
		function newProgram(obj)
			% Empty callback for Delete Program button

			fprintf('PlatformControl | newProgram\n');
			userInput = inputdlg('Enter new program name', 'Create new Program', [1 50], "");

			if ~isempty(userInput) && isstrprop(userInput, 'alphanum')
				obj.programs.(userInput) = "";
				set(obj.hSidebar, 'String', fieldnames(obj.programs));
			end
		end

		function deleteProgram(obj)
			fprintf('PlatformControl | deleteProgram\n');
			obj.programs = rmfield(obj.programs, obj.currentProgramName);

		end

		function storePrograms(obj)
			fprintf('PlatformControl | storePrograms\n');
			obj.hPreferences.storeProgramss(obj.programs);
		end

			function saveProgram(obj)
			fprintf('PlatformControl | saveProgram\n');
			value = get(obj.hProgrammingField, 'String');
			trimmed = (strtrim(string(value)));
			tosave = strjoin(trimmed, "\n");
			disp (tosave);
			obj.programs(obj.currentProgramName) = tosave;
		end

		function uploadProgram(obj)
			% Empty callback for Upload Program button
			disp (string(get(obj.hProgrammingField, 'String')));
			% -> call to send 
			
		end


	end

	methods(Access=public)

		function obj = platformControl(hPreferences)
			fprintf('PlatformControl | platformControl | constructing object\n');
			obj.hPreferences = hPreferences;
			loadSavedPrograms(obj);
		end

		function showGUI(obj)
			if isempty(obj.hFig) % closing should be also overwritten to hide
				constructGUI(obj);
			end
			set(obj.hFig, 'Visible', 'on');
		end
	end

end

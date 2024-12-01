classdef platformControl < handle
	%properties (SetAccess = private, GetAccess=public)
	properties (Access = public)
		hFig;
		hSidebar;        % Sidebar for program fieldnames
		hEditCommandField;      % Single-line text field for commands
		hEditProgramDisplay;    % Large text field for program declaration
		hButtonPanel            % Button panel
		hButtonNew;
		hButtonDelete;          % Button to delete a program
		hButtonSave;            % Button to save programs
		hButtonUpload;          % Button to upload programs
		hButtonClose;
		programs;
		currentProgramName;
	end
	methods(Access=private)
		function loadSavedPrograms(obj)
			obj.programs = preferences.getInstance().getPrograms(); % might need to replace \n with ;
			disp (obj.programs)
		end



		function obj = platformControl()

			% obj.programs.test1="Banana";
			obj.programs.test2="Mango";

			loadSavedPrograms(obj);
		end

		function constructGUI(obj)
			fprintf("PlatformControl | constructGUI | starting GUI constructions\n");
			figSize = [800, 600];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = figure('Name', 'Platform Control', ...
				'Position',  [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar', 'none', ...
				'NumberTitle', 'off', ...
				'Resize', 'on', ...
				'Visible', 'off');

			% 'Visible', 'off', ...
			obj.hSidebar = uicontrol('Style', 'listbox', ...
				'Parent', obj.hFig, ...
				'Tag', 'Sidebar', ...
				'String', fieldnames(obj.programs), ...
				'Callback', @(src, event) obj.loadProgram());

			obj.hEditCommandField = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'CommandField', ...
				'HorizontalAlignment', 'left');

			obj.hEditProgramDisplay = uicontrol('Style', 'edit', ...
				'Parent', obj.hFig, ...
				'Tag', 'ProgramDisplay', ...
				'Max', 2, ...
				'HorizontalAlignment', 'left', ...
				'Enable', 'inactive', ...
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

			obj.hButtonSave = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'Save Programs', ...
				'Callback', @(src, event) obj.saveProgram());

			obj.hButtonUpload = uicontrol('Style', 'pushbutton', ...
				'Parent', obj.hButtonPanel, ...
				'String', 'Upload Program', ...
				'Callback', @(src, event) obj.uploadProgram());
			obj.hButtonClose = uicontrol('Style', 'pushbutton', ...
                'Parent', obj.hButtonPanel, ...
                'String', 'Close', ...
                'Callback', @(src, event) set(obj.hFig, "Visible", "off"));
			obj.resizeUI();

			fprintf("PlatformControl | constructGUI | GUI constructed\n");


			set(obj.hFig, 	'SizeChangedFcn', @(src, event) obj.resizeUI());
		end

		function resizeUI(obj)
			figPos = get(obj.hFig, 'Position');
			width = figPos(3);
			height = figPos(4);

			sidebarWidth = 150;
			obj.hSidebar.Position = [10, 50, sidebarWidth, height - 110];
			obj.hEditCommandField.Position = [sidebarWidth + 20, height - 40, width - sidebarWidth - 30, 30];
			displayWidth = width - sidebarWidth - 210;
			obj.hEditProgramDisplay.Position = [sidebarWidth + 20, 50, displayWidth, height - 110];
			buttonPanelWidth = 180;
			obj.hButtonPanel.Position = [sidebarWidth + 30 + displayWidth, 50, buttonPanelWidth, height - 110];
			buttonHeight = 40;
			spacing = 10;
			obj.hButtonNew.Position = [10, height - 130 - buttonHeight - spacing, buttonPanelWidth - 20, buttonHeight];
			obj.hButtonSave.Position = [10, height - 130 - 2 * (buttonHeight + spacing), buttonPanelWidth - 20, buttonHeight];
			obj.hButtonDelete.Position = [10, height - 130 - 3 * (buttonHeight + spacing), buttonPanelWidth - 20, buttonHeight];
			obj.hButtonUpload.Position = [10, height - 130 - 4 * (buttonHeight + spacing), buttonPanelWidth - 20, buttonHeight];
			obj.hButtonClose.Position = [10, 10, buttonPanelWidth - 20, buttonHeight]; % Fixed at the bottom of the panel
      

		end

		function loadProgram(obj)
			% Callback for loading a program when selected from the sidebar
			selected = obj.hSidebar.Value; % Get selected index
			fields = fieldnames(obj.programs); % Get fieldnames
			if selected > 0 && selected <= numel(fields)
				obj.currentProgramName = fields{selected};
				obj.hEditProgramDisplay.String = obj.programs.(obj.currentProgramName);
			end
		end
		
		function newProgram(obj)
			% Empty callback for Delete Program button

			fprintf("PlatformControl | newProgram\n");
			userInput = inputdlg("Enter new program name", "Create new Program", [1 50], "");

			if ~isempty(userInput) && isstrprop(userInput, "alphanum")
				obj.programs.(userInput) = "";
				set(obj.hSidebar, 'String', fieldnames(obj.programs));
			end
		end

		function deleteProgram(obj)
			% Empty callback for Delete Program button
			fprintf("PlatformControl | deleteProgram\n");
			obj.programs = rmfield(obj.programs, obj.currentProgramName);

		end

		function saveProgram(obj)
			% Empty callback for Save Program button
			fprintf("PlatformControl | saveProgram\n");
			prefernces.getInstance().savePrograms(obj.programs);
		end

		function uploadProgram(obj)
			% Empty callback for Upload Program button
			fprintf("PlatformControl | uploadProgram\n");
		end


	end

	methods(Access=public)

		function showGUI(obj)
			if isempty(obj.hFig) || ~isvalid(obj.hFig) % closing should be also overwritten to hide
				constructGUI(obj);
			end
			set(obj.hFig, "Visible", "on");
		end
	end

	methods(Static=true)
		function handle=getInstance()
			persistent instanceStatic

			if isempty(instanceStatic)
				disp('Is empty');
				instanceStatic = platformControl(); % XXX is it possible to get rid of the name space
			end
			handle=instanceStatic;
		end
	end
end

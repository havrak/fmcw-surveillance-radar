classdef preferences < handle
	properties (Access = public)
		
		% GUI %
		hFig;                   % uifugre - main figure
		hBtnApply;              % uibutton - check validity
		hBtnClose;              % uibutton - close window
		hBtnRefresh;            % uibutton - refresh serial
		hBtnReload;             % uibutton - reload config from file
		hBtnStore;              % uibutton - permit storing to file
		hDropRadarPort;         % uidropdown - radar serial port
		hDropRadarBaudrate;     % uidropdown - radar serial baudrate 
		hDropPlatformPort;      % uidropdown - platform serial port
		hDropPlatformBaudrate;  % uidropdown - platofrm serial baudrate
		 
		hEditOffsetD;           % uicontrol/edit - distance offset
		hEditOffsetT;          % uicontrol/edit - angle offset tilt
		hEditOffsetH;           % uicontrol/edit - angle offset horizontal

		% OTHER VARS % 
		availableBaudrates = [ 9600 19200 115200 230400 1000000];
		configStruct;
		configFilePath;
	end

	methods(Access = public)

		function obj = preferences()

			obj.configStruct.radar.port='none';
			obj.configStruct.radar.baudrate='0';
			obj.configStruct.platform.port='none';
			obj.configStruct.platform.baudrate='0';
			obj.configStruct.platform.distanceOffset=0;
			obj.configStruct.platform.angleOffsetT=0;
			obj.configStruct.platform.angleOffsetH=0;
			
			obj.configStruct.programs=[];

			if isunix
				path=fullfile(getenv("HOME"), ".config", "fmcw" );
			elseif ispc
				[~,cmdout] = system('echo %APPDATA%');
				path=fullfile(cmdout, "Local", "fmcw");
			else
				path="placeholder";
				fprintf('Storing config is not supported on this platform');
			end

			if ~isfolder(path)
				mkdir(path)
			end

			obj.configFilePath=fullfile(path, "fmcw.conf");


		end

		function showGUI(obj)
			fprintf('prefrences | showGUI');
			if isempty(obj.hFig)
				constructGUI(obj);
			end
			set(obj.hFig, 'Visible', 'on');
		end

		function [programs] = getPrograms(obj)
			% getPrograms: return loaded programs from permanent confi
			%
			% Output:
			% programs ... struct will all programs
			programs = obj.configStruct.programs;
		end

		function [port, baudrate] = getConnectionPlatform(obj)
			% getConnectionPlatform: return paramters of serial connection to the Platform
			%
			% Output:
			% port ... serial port to use
			% baudrate ... serial baudrate
			port = obj.configStruct.Platform.PlatformPort;
			baudrate = obj.configStruct.Platform.baudrate;
		end

		function [port, baudrate] = getConnectionRadar(obj)
			% getConnectionRadar: return paramters of serial connection to the
			% radar
			%
			% Output:
			% port ... serial port to use
			% baudrate ... serial baudrate
			port = obj.configStruct.radar.PlatformPort;
			baudrate = obj.configStruct.radar.baudrate;
		end

		function storeConfig(obj)
			if get(obj.hBtnStore, 'Value')
				struct2ini(obj.configFilePath, obj.configStruct);
			end
		end

		function loadConfig(obj)
			fprintf('Prefernces | loadConfig | loading config\n');
			if ~isfile(obj.configFilePath)
				return;
			end

			struct = ini2struct(obj.configFilePath);
			% not everything might be present in the read struct
			sections = fieldnames(obj.configStruct);
			list=cat(2, 'none', serialportlist);
			for k=1:length(sections)
				section = char(sections(k));

				obj.configStruct.programs = [];


				if (strcmp(section,'programs') && isfield(struct, 'programs'))
					items=fieldnames(struct.(section)); % load all variables in section
				else
					items=fieldnames(obj.configStruct.(section)); % load section names from configStruct, ignore all non present
				end


				if ~isfield(struct,section)
					fprintf('Section %s not found in config\n', section);
					continue;
				end
				for l=1:length(items)
					item=char(items(l));
					if ~isfield(struct.(section),item)
						fprintf('Prefernces | loadConfig | Item %s not found in configs section %s\n', item, section);
						continue;
					end
					% check for baudrate
					if numel(strfind(item, 'baudrate')) ~=0 && ~any(obj.availableBaudrates == struct.(section).(item))
						fprintf('Prefernces | loadConfig | Unsupported baudrate wrong in config file\n');
						struct.(section).(item) = obj.availableBaudrates(1);
						continue;
					end

					% check for devices ports
						if numel(strfind(item, 'port')) ~=0 && ~any(list == struct.(section).(item))
						fprintf('Prefernces | loadConfig | Port in config not found\n');
						struct.(section).(item) = list(1);
						continue;
					end

					obj.configStruct.(section).(item) = struct.(section).(item);
				end
			end

			fprintf('Prefernces | loadConfig | config loaded');
			
			set(obj.hEditOffsetD, 'String', obj.configStruct.platform.distanceOffset);
			
			if any(obj.availableBaudrates == obj.configStruct.platform.baudrate)
				set(obj.hDropPlatformBaudrate, 'Value', num2str(obj.configStruct.platform.baudrate));
			end

			if any(obj.availableBaudrates == obj.configStruct.radar.baudrate)
				set(obj.hDropRadarBaudrate, 'Value', num2str(obj.configStruct.radar.baudrate));
			end
			set(obj.hEditOffsetT, 'String', obj.configStruct.platform.angleOffsetT);
			set(obj.hEditOffsetH, 'String', obj.configStruct.platform.angleOffsetH);
		end
	end
	methods (Access = private)

		function constructGUI(obj)
			fprintf('Preferences | constructGUI | starting gui constructions');
			figSize = [500, 500];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = uifigure('Name','Preferences', ...
				'Position', [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar', 'none', ...
				'NumberTitle', 'off', ...
				'Resize', 'off', ...
				'Visible', 'off');

			uilabel(obj.hFig, 'Position', [150, figSize(2)-50, 140, 25], 'Text', 'Baudrate');
			uilabel(obj.hFig, 'Position', [240, figSize(2)-50, 140, 25], 'Text', 'Port' );
			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-100, 140, 25], ...
				'Text', 'Radar serial setting:');

			obj.hDropRadarBaudrate = uidropdown(obj.hFig, ...
				'Position', [150, figSize(2)-100, 80, 25], ...
				'Items', {'9600','19200', '115200', '230400','1000000'});

			list=cat(2, 'none', serialportlist);
			obj.hDropRadarPort = uidropdown(obj.hFig, ...
				'Position', [240, figSize(2)-100, 120, 25], ...
				'Items', list);

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-70, 140, 25], ...
				'Text', 'Platform serial setting:');

			obj.hDropPlatformBaudrate = uidropdown(obj.hFig, ...
				'Position', [150, figSize(2)-70, 80, 25], ...
				'Items', {'9600', '19200', '115200', '230400','1000000'});

			obj.hDropPlatformPort = uidropdown(obj.hFig, ...
				'Position', [240, figSize(2)-70, 120, 25], ...
				'Items', list);

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-140, 140, 25], ...
				'Text', 'Platform offset [mm]:');
			
			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-170, 140, 25], ...
				'Text', 'Tilt offset [deg]:');

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-200, 140, 25], ...
				'Text', 'Horizontal offset [deg]:');
			
			obj.hEditOffsetD=uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-140, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			obj.hEditOffsetT=uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-170, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			obj.hEditOffsetH=uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-200, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			obj.hBtnStore=uibutton(obj.hFig, 'state', ...
				'Position', [figSize(1)-200, figSize(2)-450, 80, 25], ...
				'Text', 'Store Config', ...
				'Value', true);

		obj.hBtnReload = uibutton(obj.hFig, ...
				'Text', 'Reload', ...
				'Position', [figSize(1)-300, figSize(2)-480, 80, 25], ...
				'ButtonPushedFcn', @(src,event) obj.loadConfig());
			
			obj.hBtnRefresh = uibutton(obj.hFig, ...
				'Text', 'Refresh', ...
				'Position', [375, figSize(2)-85, 80, 25], ...
				'ButtonPushedFcn', @(src,event) obj.refreshSerial());
			
			obj.hBtnApply = uibutton(obj.hFig, ...
				'Text', 'Apply', ...
				'Position', [figSize(1)-200, figSize(2)-480, 80, 25], ...
				'ButtonPushedFcn',  @(src, event)obj.apply() );
			
			obj.hBtnClose = uibutton(obj.hFig, ...
				'Text', 'Close', ...
				'Position', [figSize(1)-100, figSize(2)-480, 80, 25], ...
				'ButtonPushedFcn',@(src,event) set(obj.hFig, 'Visible', 'off'));

			loadConfig(obj);
		end

		function refreshSerial(obj)
			list=cat(2, 'none', serialportlist);
			set(obj.hDropPlatformPort, 'Items', list);
			set(obj.hDropPlatformBaudrate, 'Items', list);
		end

		function apply(obj)

			err=false;
			obj.configStruct.platform.platformBaudrate=str2double(obj.hDropPlatformBaudrate.Value);
			obj.configStruct.radar.baudrate=str2double(obj.hDropRadarBaudrate.Value);


			tmp = get(obj.hEditOffsetD, 'String');
			if isnan(str2double(tmp)) warndlg('Offset must be numerical');
			else obj.configStruct.platform.distanceOffset=tmp;
			end

			tmp = get(obj.hEditOffsetT, 'String');
			if isnan(str2double(tmp)) warndlg('Offset must be numerical');
			else obj.configStruct.platform.angleOffsetT=tmp;
			end
			
			tmp = get(obj.hEditOffsetH, 'String');
			if isnan(str2double(tmp)) warndlg('Offset must be numerical');
			else obj.configStruct.platform.angleOffsetH=tmp;
			end			

			if strcmp(obj.hDropPlatformPort.Value,obj.hDropRadarPort.Value) == 1 && obj.hDropRadarPort.Value ~= "none"
				err=true;
				uialert(obj.hFig, 'Both serial ports are same', 'Serial port', 'icon', 'error');
			else
				obj.configStruct.platform.platformPort=obj.hDropPlatformPort.Value;
				obj.configStruct.radar.port=obj.hDropRadarPort.Value;
			end

			if ~err
				uialert(obj.hFig, 'Config applied', 'Config', 'icon', 'info', 'CloseFcn','uiresume(gcbf)');
				uiwait(gcbf);
				set(obj.hFig, 'Visible', 'off');
			end

			storeConfig(obj);
		end
	end
end



function struct2ini(filename,Structure)
	%==========================================================================
	% Author:      Dirk Lohse ( dirklohse@web.de )
	% Version:     0.1a
	% Last change: 2008-11-13
	%==========================================================================
	%
	% struct2ini converts a given structure into an ini-file.
	% It's the opposite to Andriy Nych's ini2struct. Only
	% creating an ini-file is implemented. To modify an existing
	% file load it with ini2struct.m from:
	%       Andriy Nych ( nych.andriy@gmail.com )
	% change the structure and write it with struct2ini.
	%
	% Open file, or create new file, for writing
	% discard existing contents, if any.
	fid = fopen(filename,'w');
	sections = fieldnames(Structure);                     % returns the sections
	for k=1:length(sections)
		section = char(sections(k));

		fprintf(fid,'\n[%s]\n',section);

		member_struct = Structure.(section);
		if ~isempty(member_struct)
			member_names = fieldnames(member_struct);
			for j=1:length(member_names)
				member_name = char(member_names(j));
				member_value = Structure.(section).(member_name);
				if isnumeric(member_value)
					member_value=num2str(member_value);
				end

				fprintf(fid,'%s=%s\n',member_name,member_value); % output member name and value

			end % for-END (Members)
		end % if-END
	end % for-END (sections)
	fclose(fid);

end


function Result = ini2struct(filename)
	%==========================================================================
	% Author: Andriy Nych ( nych.andriy@gmail.com )
	% Version:        733341.4155741782200
	%==========================================================================
	%
	% INI = ini2struct(filename)
	%
	% This function parses INI file filename and returns it as a structure with
	% section names and keys as fields.
	%
	% sections from INI file are returned as fields of INI structure.
	% Each fiels (section of INI file) in turn is structure.
	% It's fields are variables from the corrPlatformonding section of the INI file.
	%
	% If INI file contains "oprhan" variables at the beginning, they will be
	% added as fields to INI structure.
	%
	% Lines starting with ';' and '#' are ignored (comments).
	%
	% See example below for more information.
	%
	% Usually, INI files allow to put spaces and numbers in section names
	% without restrictions as long as section name is between '[' and ']'.
	% It makes people crazy to convert them to valid Matlab variables.
	% For this purpose Matlab provides GENVARNAME function, which does
	%  "Construct a valid MATLAB variable name from a given candidate".
	% See 'help genvarname' for more information.
	%
	% The INI2STRUCT function uses the GENVARNAME to convert strange INI
	% file string into valid Matlab field names.
	%
	Result = [];
	CurrMainField = '';
	f = fopen(filename,'r');
	while ~feof(f)
		s = strtrim(fgetl(f));
		if isempty(s)
			continue;
		end
		if (s(1)==';') || (s(1)=='#')
			continue;
		end
		if ( s(1)=='[' ) && (s(end)==']' )
			CurrMainField = genvarname(s(2:end-1));
			Result.(CurrMainField) = [];
		else
			[par,val] = strtok(s, '=');
			val = CleanValue(val);

			if ~isnan(str2double(val))
				val=str2double(val);
			end

			if ~isempty(CurrMainField)
				Result.(CurrMainField).(genvarname(par)) = val;
			else
				Result.(genvarname(par)) = val;
			end
		end
	end
	fclose(f);
	return;
	function res = CleanValue(s)
		res = strtrim(s);
		if strcmpi(res(1),'=')
			res(1)=[];
		end
		res = strtrim(res);
		return;
	end
end

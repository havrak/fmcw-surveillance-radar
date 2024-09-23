classdef preferences < handle
    properties (Access = private)
        hFig
        hEdit
        hButt
        hClose

        hStoreConfig
        hLabelRadar
        hLabelEsp
        hRadarPort
        hEspPort
        hRadarBaudrate
        hEspBaudrate
        guiConstructed
        result
        hStatusLabel
        
        availableBaudrates
        configStruct
        configFilePath
    end
    methods
        function constructGUI(obj)



            figSize = [500, 500];
            screenSize = get(groot, 'ScreenSize');
            obj.hFig = uifigure('Name', 'Preferences', 'Position', [(screenSize(3:4) - figSize)/2, figSize], 'Visible', 'off');

            uilabel(obj.hFig, "Position", [140, figSize(2)-50, 140, 25], "Text", "Baudrate");
            uilabel(obj.hFig, "Position", [230, figSize(2)-50, 140, 25], "Text", "Port" );
            obj.hLabelRadar=uilabel(obj.hFig, "Position", [20, figSize(2)-100, 140, 25], "Text", "Radar serial setting:");

            obj.hRadarBaudrate = uidropdown(obj.hFig, 'Position', [140, figSize(2)-100, 80, 25], ...
                'Items', {'9600','19200','100000', '115200', '230400'});
            list=cat(2, 'none', serialportlist);
            obj.hRadarPort = uidropdown(obj.hFig, 'Position', [230, figSize(2)-100, 120, 25], ...
                'Items', list);

            obj.hLabelEsp=uilabel(obj.hFig, "Position", [20, figSize(2)-70, 140, 25], "Text", "ESP serial setting:");
            obj.hEspBaudrate = uidropdown(obj.hFig, 'Position', [140, figSize(2)-70, 80, 25], ...
                'Items', {'9600', '19200', '100000', '115200', '230400'});
            obj.hEspPort = uidropdown(obj.hFig, 'Position', [230, figSize(2)-70, 120, 25], ...
                'Items', list);

            obj.hStatusLabel=uilabel(obj.hFig, ...
                'Position', [20, figSize(2)-480, 80, 25]);

            obj.hStoreConfig=uibutton(obj.hFig, "state", "Position", [figSize(1)-200, figSize(2)-450, 80, 25], ...
                "Text", "Store Config", "Value", true);

            obj.hButt = uibutton(obj.hFig, 'Text', 'Apply', ...
                'Position', [figSize(1)-200, figSize(2)-480, 80, 25]);
            obj.hClose = uibutton(obj.hFig, 'Text', 'Close', ...
                'Position', [figSize(1)-100, figSize(2)-480, 80, 25]);
            obj.guiConstructed=true;

            obj.hButt.ButtonPushedFcn = @(src, event)obj.apply();
            obj.hClose.ButtonPushedFcn=@(src,event) set(obj.hFig, "Visible", "off");
        end

        function obj = preferences()

            obj.guiConstructed=false;
            obj.configStruct.radar.radarPort='none';
            obj.configStruct.radar.radarBaudrate='0';
            obj.configStruct.esp.espPort='none';
            obj.configStruct.esp.espBaudrate='0';
            obj.availableBaudrates = [ 9600 19200 100000 115200 230400];
            if isunix
                path=fullfile(getenv("HOME"), ".config", "fmcw" );
                disp(path);
            elseif ispc
                [~,cmdout] = system('echo %APPDATA%')
                path=fullfile(cmdout, "Local", "fmcw")
            else
                path="lorem impsum dolor sit ament";
                fprintf("Storing config is not supported on this platform");
            end

            if ~isfolder(path)
                mkdir(path)
            end

            obj.configFilePath=fullfile(path, "fmcw.conf");

            loadConfig(obj);
        end


        function showGUI(obj)
            if ~obj.guiConstructed
                constructGUI(obj);
            end
            set(obj.hFig, "Visible", "on");
        end

        function storeConfig(obj)
            if get(obj.hStoreConfig, "Value")
                struct2ini(obj.configFilePath, obj.configStruct);
            end
        end

        function loadConfig(obj)
            if isfile(obj.configFilePath)
                struct = ini2struct(obj.configFilePath);
                disp(struct.radar);
                % not everything might be present in the read struct
                sections = fieldnames(obj.configStruct);
                for k=1:length(sections)
                    section = char(sections(k));
                    items=fieldnames(obj.(section))
                    for l=1:length(items)
                        item=char(items(l));
                        disp(item);
                    end
                end
                if ~any(obj.availableBaudrates == struct.radar.radarBaudrate) || ...
                   ~any(obj.availableBaudrates == struct.esp.espBaudrate)
                     uialert(obj.hFig, 'Baudrate wrong in config file', 'Config loading erro', 'icon', 'error');
                     return;
                end

            end
        end

    end
    methods (Access = private)

        function apply(obj)
            err=false;
            obj.configStruct.esp.espBaudrate=str2double(obj.hEspBaudrate.Value);
            obj.configStruct.radar.radarBaudrate=str2double(obj.hRadarBaudrate.Value);

            if strcmp(obj.hEspPort.Value,obj.hRadarPort.Value) == 1 && obj.hRadarPort.Value ~= "none"
                err=true;
                uialert(obj.hFig, 'Both serial ports are same', 'Serial port', 'icon', 'error');
            else
                obj.configStruct.esp.espPort=obj.hEspPort.Value;
                obj.configStruct.radar.radarPort=obj.hRadarPort.Value;
            end

            if ~err
                uialert(obj.hFig, 'Config applied', 'Config', 'icon', 'info', 'CloseFcn','uiresume(gcbf)');
                uiwait(gcbf);
                set(obj.hFig, "Visible", "off");
            end
            obj.storeConfig
            disp(obj.configStruct.radar);

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
%  Author: Andriy Nych ( nych.andriy@gmail.com )
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
% It's fields are variables from the corresponding section of the INI file.
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
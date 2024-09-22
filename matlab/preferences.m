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
        radarPort
        espPort
        radarBaudrate
        espBaudrate
        storingConfigEnabled
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
                "Text", "Store Config", "Value", obj.storingConfigEnabled);

            obj.hButt = uibutton(obj.hFig, 'Text', 'Apply', ...
            'Position', [figSize(1)-200, figSize(2)-480, 80, 25]);
            obj.hClose = uibutton(obj.hFig, 'Text', 'Close', ...
            'Position', [figSize(1)-100, figSize(2)-480, 80, 25]);
            obj.guiConstructed=true;
            
            obj.hButt.ButtonPushedFcn = @(src, event)obj.apply();
            obj.hClose.ButtonPushedFcn=@(src,event) set(obj.hFig, "Visible", "off");
        end

        function obj = preferences()
            obj.radarPort='none';
            obj.guiConstructed=false;
            obj.radarBaudrate='0';
            obj.espPort='none';
            obj.espBaudrate='0';
            obj.storingConfigEnabled=false;

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

            if isdir(path)
                mkdir(path)
            end

            obj.configFilePath=fullfile(path, "fmcw.conf");

            if isfile(obj.configFilePath)
                loadConfig();
            end

            
        end
    
        
        function showGUI(obj)
            if ~obj.guiConstructed
                constructGUI(obj);
            end
            set(obj.hFig, "Visible", "on");
        end

        function storeConfig(obj)
            if obj.storingConfigEnabled
                return;
            end
            fprintf("This function is unhandled");
        end

        function loadConfig(obj)
            
            fprintf("This function is unhandled");
        end
            
    end
        methods (Access = private)

        function apply(obj)
            err=false;
            obj.espBaudrate=str2num(obj.hEspBaudrate.Value);
            obj.radarBaudrate=str2num(obj.hRadarBaudrate.Value);
            
            if strcmp(obj.hEspPort.Value,obj.hRadarPort.Value) == 1 && obj.hRadarPort.Value ~= "none"
                err=true;
                uialert(obj.hFig, 'Both serial ports are same', 'Serial port', 'icon', 'error');
            else
                obj.espPort=obj.hEspPort.Value;
                obj.radarPort=obj.hRadarPort.Value;
            end

            if ~err
                uialert(obj.hFig, 'Config applied', 'Config', 'icon', 'info', 'CloseFcn','uiresume(gcbf)');
                uiwait(gcbf);
                set(obj.hFig, "Visible", "off");
            end
        
        end
    end
end

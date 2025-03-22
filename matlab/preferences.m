classdef preferences < handle
	properties (Access = private)

		% GUI %
		hFig;                   % uifugre - main figure
		hBtnApply;              % uibutton - check validity
		hBtnClose;              % uibutton - close window
		hBtnRefresh;            % uibutton - refresh serial
		hBtnReload;             % uibutton - reload config from file

		hDropRadarPort;         % uidropdown - radar serial port
		hDropRadarBaudrate;     % uidropdown - radar serial baudrate
		hDropPlatformPort;      % uidropdown - platform serial port
		hDropPlatformBaudrate;  % uidropdown - platform serial baudrate

		hSwitchPlatformDebug;   % uiswitch - display debug messages from platform
		hEditOffsetD;           % uicontrol/edit - distance offset
		hEditOffsetP;           % uicontrol/edit - angle offset pitch
		hEditOffsetY;           % uicontrol/edit - angle offset yaw

		hDropRadarSample       % number of FFT samples for chirp
		hSwitchRadarHeader;     % uiswitch - pick between 122 and 24 GHz header
		hEditRadarBandwith;     % uicontrol/edit - distance offset
		hEditRadPatternPitch;
		hEditRadPatternYaw;
		hEditTriggerPeriod;
		hDropRadarADC;
		hTextRampTime;

		hDropProVisualization;
		hEditProSpeedBins;
		hCheckProCalcSpeed;


		% OTHER VARS %
		availableBaudrates = [ 9600 19200 115200 230400 1000000];
		availableSamples = [32 64 128 256 512 1024 2048 ];
		availableADC = [2571 2400 2118 1800 1125 487 186 59];

		availableVisualization = {'Range-Azimuth', 'Range-Doppler', 'Target-2D', 'Target-3D'};
		binaryMap = ['000'; '001'; '010'; '011'; '100'; '101'; '110'; '111'];
		adcSamplingTime = [14 15 17 20 32 74 194 614];
		configStruct;
		configFilePath;
	end

	events
		newConfigEvent
	end

	methods(Access = public)

		function obj = preferences()
			% preferences: constructor for preferences class

			obj.configStruct.radar.port='none';
			obj.configStruct.radar.baudrate=obj.availableBaudrates(1);
			obj.configStruct.radar.header='';
			obj.configStruct.radar.bandwidth=0;
			obj.configStruct.radar.samples=128;
			obj.configStruct.radar.radPatternYaw=0;
			obj.configStruct.radar.radPatternPitch=0;
			obj.configStruct.radar.adc=obj.availableADC(1);
			obj.configStruct.radar.trigger=0;

			obj.configStruct.platform.port='none';
			obj.configStruct.platform.baudrate=obj.availableBaudrates(1);
			obj.configStruct.platform.offsetDistance=0;
			obj.configStruct.platform.offsetPitch=0;
			obj.configStruct.platform.offsetYaw=0;
			obj.configStruct.platform.debug=1;

			obj.configStruct.processing.visualization=obj.availableVisualization(1);
			obj.configStruct.processing.maxSpeedBins=0;
			obj.configStruct.processing.calcspeed = 1;


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

			obj.loadConfig();
		end

		function showGUI(obj)
			% showGUI: displays generated GUI that is hidden

			if isempty(obj.hFig) | ~isvalid(obj.hFig)
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

		function setPrograms(obj, programs)
			obj.configStruct.programs = programs;
		end

		function visualization =  getProcessingVisualization(obj)
			visualization = obj.configStruct.processing.visualization;
		end

		function [calcSpeed, speedBins] = getProcessingSpeedParamters(obj)
			calcSpeed = 	obj.configStruct.processing.calcspeed;
			speedBins = obj.configStruct.processing.maxSpeedBins;
		end

		function [port, baudrate] = getConnectionPlatform(obj)
			% getConnectionPlatform: return paramters of serial connection to the Platform
			%
			% Output:
			% port ... serial port to use
			% baudrate ... serial baudrate
			port = obj.configStruct.platform.port;
			baudrate = obj.configStruct.platform.baudrate;
		end

		function [port, baudrate] = getConnectionRadar(obj)
			% getConnectionRadar: return paramters of serial connection to the
			% radar
			%
			% Output:
			% port ... serial port to use
			% baudrate ... serial baudrate
			port = obj.configStruct.radar.port;
			baudrate = obj.configStruct.radar.baudrate;
		end

		function [debug] = getPlatformDebug(obj)
			% getPlatformDebug: return 1 if debug from platform should be
			% displayed
			%
			% Output:
			% debug ... 0 or 1
			debug = obj.configStruct.platform.debug;
		end


		function header = getRadarFrontend(obj)
			% getRadarFrontend: return header used on the radar
			% radar
			%
			% Output:
			% header ... either 122 or 24
			header = obj.configStruct.radar.header;
		end

		function [radPatterYaw, radPatterPitch] = getRadarRadiationParamters(obj)
			radPatterYaw = obj.configStruct.radar.radPatternYaw;
			radPatterPitch = obj.configStruct.radar.radPatternPitch;
		end

		function period = getRadarTriggerPeriod(obj)
			period = obj.configStruct.radar.trigger;
		end

		function bandwith = getRadarBandwidth(obj)
			% getRadarBandwidth: return bandwidth used on the radar
			% radar
			%
			% Output:
			% bandwidth ... number in MHz
			bandwith = obj.configStruct.radar.bandwidth;
		end

		function [samplesReg, samplesBin, adc ] = getRadarBasebandParameters(obj)
			% getRadarSamples: return number of samples will take and ADC
			% setting. Both in binary format to be pasted into baseband command
			%
			% Output:
			% samplesReg ... number of samples
			% samplesBin ... string with binary
			% adc ... string with binary
			samplesReg = obj.configStruct.radar.samples;
			samplesBin = obj.binaryMap(obj.availableSamples==obj.configStruct.radar.samples, :);
			adc = obj.binaryMap(obj.availableADC == obj.configStruct.radar.adc, :);
		end

		function [offsetYaw, offsetPitch, offsetDistance] = getPlatformParamters(obj)
			% getPlatformParamters: return mounting paramters of the platform
			%
			%
			% Output:
			% offsetYaw ... yaw angle at which radar is poiting straight
			% offsetPitch ... pitch angle at which radar is poiting straight (PCB is perpendicular to the ground)
			% offsetDistance ... distance from axis of rotation to the center of
			% radar PCB

			offsetDistance =  obj.configStruct.platform.offsetDistance;
			offsetPitch = obj.configStruct.platform.offsetPitch;
			offsetYaw = obj.configStruct.platform.offsetYaw;
		end



		function time = getRampTime(obj)
			samples = obj.configStruct.radar.samples;
			adc = obj.adcSamplingTime(obj.configStruct.radar.adc == obj.availableADC);
			time = adc*(samples+85)/36e3;

		end

		function binWidth = getDistanceBinWidth(obj)
			binWidth = physconst('LightSpeed')*(obj.configStruct.radar.samples+85) ...
			/(2*obj.configStruct.radar.bandwidth*obj.configStruct.radar.samples);
		end

		function storeConfig(obj)
			%storeConfig: stores current config to file

			struct2ini(obj.configFilePath, obj.configStruct);
		end

		function loadConfig(obj)
			% loadConfig: loads config from file to internal structure

			fprintf('Prefernces | loadConfig | loading config\n');
			if ~isfile(obj.configFilePath)
				return;
			end

			struct = ini2struct(obj.configFilePath);
			list=cat(2, 'none', serialportlist);
			% not everything might be present in the read struct
			sections = fieldnames(obj.configStruct);

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

					if (numel(strfind(item, 'baudrate')) ~=0) && (~any(obj.availableBaudrates == struct.(section).(item)))
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

			fprintf('Prefernces | loadConfig | config loaded\n');


		end

	end
	methods (Access = private)

		function constructGUI(obj)
			% constructGUI: initializes all GUI elements

			platformConfigOffset = 140;
			radarConfigOffset = 280;
			processingOffset = 520;

			fprintf('Preferences | constructGUI | starting gui constructions');
			figSize = [500, 700];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = uifigure('Name','Preferences', ...
				'Position', [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar', 'none', ...
				'NumberTitle', 'off', ...
				'Resize', 'off', ...
				'Visible', 'off');

			%% Serial config
			uilabel(obj.hFig, 'Position', [20, figSize(2)-50, 140, 25], 'Text', 'SERIAL CONFIG', 'FontWeight','bold');
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


			obj.hBtnRefresh = uibutton(obj.hFig, ...
				'Text', 'Refresh', ...
				'Position', [375, figSize(2)-85, 80, 25], ...
				'ButtonPushedFcn', @(src,event) obj.refreshSerial());


			%% PlatformConfig
			uilabel(obj.hFig, 'Position', [20, figSize(2)-platformConfigOffset, 140, 25], 'Text', 'PLATFORM CONFIG', 'FontWeight','bold');

			% Distance offset
			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-platformConfigOffset-20, 140, 25], ...
				'Text', 'Platform offset [mm]:');
			obj.hEditOffsetD=uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-platformConfigOffset-20, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			% Pitch offset
			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-platformConfigOffset-50, 140, 25], ...
				'Text', 'Pitch offset [deg]:');

			obj.hEditOffsetP=uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-platformConfigOffset-50, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			% Yaw offset
			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-platformConfigOffset-80, 140, 25], ...
				'Text', 'Yaw offset [deg]:');

			obj.hEditOffsetY=uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-platformConfigOffset-80, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-platformConfigOffset-110, 140, 25], ...
				'Text', 'Platform debug: ');

			obj.hSwitchPlatformDebug=uiswitch('Parent', obj.hFig, ...
				'Position', [180, figSize(2)-platformConfigOffset-110, 140, 25], ...
				'Items', {'On', 'Off'}, ...
				'Orientation', 'horizontal');

			%% Radar setting

			uilabel(obj.hFig, 'Position', [20, figSize(2)-radarConfigOffset, 140, 25], 'Text', 'RADAR CONFIG', 'FontWeight','bold');

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-20, 140, 25], ...
				'Text', 'Radar frequency [GHz]: ');

			obj.hSwitchRadarHeader=uiswitch('Parent', obj.hFig, ...
				'Position', [180, figSize(2)-radarConfigOffset-20, 140, 25], ...
				'Items', {'24', '122'}, ...
				'Orientation', 'horizontal' ...
				);

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-50, 140, 25], ...
				'Text', 'Radar Bandwith [MHz]: ');

			obj.hEditRadarBandwith = uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-radarConfigOffset-50, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');


			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-80, 140, 25], ...
				'Text', 'Chirp samples: ');

				uilabel(obj.hFig, ...
				'Position', [250, figSize(2)-radarConfigOffset-95, 140, 25], ...
				'Text', 'Chirp time [ms]: ');

			obj.hTextRampTime = uicontrol('Style', 'text', ...
				'Parent',obj.hFig,  ...
				'Position', [350, figSize(2)-radarConfigOffset-100, 170, 25], ...
				'Max',1, ...
				'String',"Time", ...
				'HorizontalAlignment', 'left');

			obj.hDropRadarSample = uidropdown(obj.hFig, ...
				'Position', [150, figSize(2)-radarConfigOffset-80, 80, 25], ...
				'Items', {'32' '64' '128' '256' '512' '1024' '2048'});

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-110, 140, 25], ...
				'Text', 'ADC ClkDiv [MS/s]: ');

			obj.hDropRadarADC = uidropdown(obj.hFig, ...
				'Position', [150, figSize(2)-radarConfigOffset-110, 80, 25], ...
				'Items', {'2571' '2400' '2118' '1800' '1125' '487' '186' '59'});


			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-140, 140, 25], ...
				'Text', 'Lobe width yaw: ');

			obj.hEditRadPatternYaw = uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-radarConfigOffset-140, 140, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-170, 170, 25], ...
				'Text', 'Lobe width pitch: ');
			obj.hEditRadPatternPitch = uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-radarConfigOffset-170, 170, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-radarConfigOffset-200, 140, 25], ...
				'Text', 'Trigger period [ms]:');

			obj.hEditTriggerPeriod = uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-radarConfigOffset-200, 170, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			%% Processing settings


			uilabel(obj.hFig, 'Position', [20, figSize(2)-processingOffset, 240, 25], 'Text', 'VISUALIZATION CONFIG', 'FontWeight','bold');

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-processingOffset-20, 140, 25], ...
				'Text', 'Visualization:');

			obj.hDropProVisualization = uidropdown(obj.hFig, ...
				'Position', [150, figSize(2)-processingOffset-20, 200, 25], ...
				'Items', obj.availableVisualization);

			uilabel(obj.hFig, ...
				'Position', [20, figSize(2)-processingOffset-50, 170, 25], ...
				'Text', 'Speed resolution:');

			obj.hEditProSpeedBins = uicontrol('Style', 'edit', ...
				'Parent',obj.hFig,  ...
				'Position', [150, figSize(2)-processingOffset-50, 170, 25], ...
				'Max',1, ...
				'String',"", ...
				'HorizontalAlignment', 'left');

			obj.hCheckProCalcSpeed = uicheckbox('Parent', obj.hFig, ...
				'Position',[250, figSize(2)-processingOffset-50, 170, 25], ...
				"Text","Calculate Speed");



			%% Buttons
			function reloadConfig(obj)
				obj.loadConfig();
				obj.configToGUI();
			end

			obj.hBtnReload = uibutton(obj.hFig, ...
				'Text', 'Reload', ...
				'Position', [figSize(1)-300, 40, 80, 25], ...
				'ButtonPushedFcn', @(src,event) reloadConfig(obj) );


			obj.hBtnApply = uibutton(obj.hFig, ...
				'Text', 'Apply', ...
				'Position', [figSize(1)-200, 40, 80, 25], ...
				'ButtonPushedFcn',  @(src, event)obj.processConfig() );

			obj.hBtnClose = uibutton(obj.hFig, ...
				'Text', 'Close', ...
				'Position', [figSize(1)-100, 40, 80, 25], ...
				'ButtonPushedFcn',@(src,event) set(obj.hFig, 'Visible', 'off'));

			obj.configToGUI();

		end



		function configToGUI(obj)
			% configToGUI: sets values of GUI elements to correspond to
			% those saved in internal config structure



			%% Serial config
			list=cat(2, 'none', serialportlist);
			if any(list == obj.configStruct.platform.port)
				set(obj.hDropPlatformPort, 'Value', obj.configStruct.platform.port);
			end

			if any(list == obj.configStruct.radar.port)
				set(obj.hDropRadarPort, 'Value', obj.configStruct.radar.port);
			end


			if any(obj.availableBaudrates == obj.configStruct.platform.baudrate)
				set(obj.hDropPlatformBaudrate, 'Value', num2str(obj.configStruct.platform.baudrate));
			end

			if any(obj.availableBaudrates == obj.configStruct.radar.baudrate)
				set(obj.hDropRadarBaudrate, 'Value', num2str(obj.configStruct.radar.baudrate));
			end

			%% Platform config


			set(obj.hEditOffsetD, 'String', obj.configStruct.platform.offsetDistance);
			set(obj.hEditOffsetP, 'String', obj.configStruct.platform.offsetPitch);
			set(obj.hEditOffsetY, 'String', obj.configStruct.platform.offsetYaw);


			if(obj.configStruct.platform.debug == 1)
				set(obj.hSwitchPlatformDebug, 'Value', 'On');
			else
				set(obj.hSwitchPlatformDebug, 'Value', 'Off');
			end

			%% Radar config

			set(obj.hEditRadarBandwith, 'String', obj.configStruct.radar.bandwidth);

			set(obj.hEditRadPatternYaw, 'String', obj.configStruct.radar.radPatternYaw);
			set(obj.hEditRadPatternPitch, 'String', obj.configStruct.radar.radPatternPitch);
			set(obj.hEditTriggerPeriod, 'String', obj.configStruct.radar.trigger);

			if any(obj.availableSamples == obj.configStruct.radar.samples)
				set(obj.hDropRadarSample, 'Value', num2str(obj.configStruct.radar.samples))
			end

			if any(obj.availableADC == obj.configStruct.radar.adc)

				set(obj.hDropRadarADC, 'Value', num2str(obj.configStruct.radar.adc))
			end

			if ismember(num2str(obj.configStruct.radar.header),obj.hSwitchRadarHeader.Items)
				set(obj.hSwitchRadarHeader, 'Value', num2str(obj.configStruct.radar.header));
			end

			time = obj.getRampTime();
			set(obj.hTextRampTime, 'String', time);
			%% Processing config

			if any(matches(obj.configStruct.processing.visualization,obj.availableVisualization))
				set(obj.hDropProVisualization, 'Value', obj.configStruct.processing.visualization);
			end

			set(obj.hEditProSpeedBins, 'String', obj.configStruct.processing.maxSpeedBins);

			set(obj.hCheckProCalcSpeed, 'Value', obj.configStruct.processing.calcspeed);

		end

		function refreshSerial(obj)
			% refreshSerial: refreshes list of available serial ports

			list=cat(2, 'none', serialportlist);
			set(obj.hDropPlatformPort, 'Items', list);
			set(obj.hDropRadarPort, 'Items', list);
		end

		function  processConfig(obj)
			% processConfig: gathers all values from GUI elements and stores them
			% into config structure

			%% Serial config
			err=false;
			obj.configStruct.platform.baudrate=str2double(obj.hDropPlatformBaudrate.Value);

			disp(obj.hSwitchPlatformDebug.Value);
			if(strcmp(obj.hSwitchPlatformDebug.Value,'On'))
				obj.configStruct.platform.debug = 1;
			else
				obj.configStruct.platform.debug = 0;
			end

			if strcmp(obj.hDropPlatformPort.Value,obj.hDropRadarPort.Value) == 1 && obj.hDropRadarPort.Value ~= "none"
				err=true;
				uialert(obj.hFig, 'Both serial ports are same', 'Serial port', 'icon', 'error');
			else
				obj.configStruct.platform.port=obj.hDropPlatformPort.Value;
				obj.configStruct.radar.port=obj.hDropRadarPort.Value;
			end



			%% Platform config
			tmp = get(obj.hEditOffsetD, 'String');
			if isnan(str2double(tmp)) warndlg('Offset must be numerical');
			else obj.configStruct.platform.offsetDistance=str2double(tmp);
			end

			tmp = get(obj.hEditOffsetP, 'String');
			if isnan(str2double(tmp)) warndlg('Offset must be numerical');
			else obj.configStruct.platform.offsetPitch=str2double(tmp);
			end

			tmp = get(obj.hEditOffsetY, 'String');
			if isnan(str2double(tmp)) warndlg('Offset must be numerical');
			else obj.configStruct.platform.offsetYaw=str2double(tmp);
			end



			%% Radar config

			obj.configStruct.radar.baudrate=str2double(obj.hDropRadarBaudrate.Value);
			obj.configStruct.radar.header=obj.hSwitchRadarHeader.Value;
			obj.configStruct.radar.samples=str2double(obj.hDropRadarSample.Value);
			obj.configStruct.radar.adc=str2double(obj.hDropRadarADC.Value);


			tmp = get(obj.hEditRadPatternYaw, 'String');
			if isnan(str2double(tmp)) warndlg('Lobe width must be numerical');
			else obj.configStruct.radar.radPatternYaw=str2double(tmp);
			end

			tmp = get(obj.hEditRadPatternPitch, 'String');
			if isnan(str2double(tmp)) warndlg('Lobe width must be numerical');
			else obj.configStruct.radar.radPatternPitch=str2double(tmp);
			end

			tmp = get(obj.hEditTriggerPeriod, 'String');
			if (isnan(str2double(tmp)) || any(tmp <0)) warndlg('Trigger period must be numerical');
			else obj.configStruct.radar.trigger=str2double(tmp);
			end


			tmp = get(obj.hEditRadarBandwith, 'String');
			if isnan(str2double(tmp)) warndlg('Bandwidth must be numerical');
			else obj.configStruct.radar.bandwidth=str2double(tmp);
			end


			time = obj.getRampTime();
			set(obj.hTextRampTime, 'String', time);

			%% Processing

			obj.configStruct.processing.visualization = obj.hDropProVisualization.Value;

			if ~err
				uialert(obj.hFig, 'Config applied', 'Config', 'icon', 'info', 'CloseFcn','uiresume(gcbf)');
				uiwait(gcbf);
				% XXX set(obj.hFig, 'Visible', 'off');
			end

			tmp = get(obj.hEditProSpeedBins, 'String');
			if isnan(str2double(tmp)) warndlg('Bandwidth must be numerical');
			else obj.configStruct.processing.maxSpeedBins=str2double(tmp);
			end

			obj.configStruct.processing.calcspeed = obj.hCheckProCalcSpeed.Value;

			storeConfig(obj);
			notify(obj, 'newConfigEvent');
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

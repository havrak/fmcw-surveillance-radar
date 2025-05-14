classdef radar < handle
	% radar: Manages radar hardware communication, data acquisition, and configuration
	%
	% Handles serial communication with a radar device,  processes incoming
	% data streams, and dynamically updates configurations via a preferences
	% object. Supports real-time data buffering and event-driven processing.
	%

	properties(Access = public)

		hPreferences preferences;  % Handle to preferences object for configuration management
		hSerial = [];              % Serial port object for radar communication
		processData = true;        % Flag to enable/disable data processing

		chunkLengths = [];         % Stores lengths of incoming data chunks for buffer management
		rawBuffer = [];            % Temporary storage for raw serial data before processing
		samples = 256;             % Number of samples per radar chirp
		startTime uint64;          % Base timestamp (uint64) for calculating relative timestamps
		triggerTimer;

		bufferI;                   % Circular buffer for I
		bufferQ;                   % Circular buffer for Q
		bufferTime = [];           % Circular buffer for timestamps
		bufferSize = 100;          % Max buffer size
		writeIdx = 1;              % Index for next write
	end


	events
		newDataAvailable           % even triggered when chirp is received and buffered
	end

	methods(Static, Access=private)
		function hexCode = bin2hex(bin)
			% bin2hex: Takes binary code in string and converts it to hex
			%
			% Inputs:
			%    bin ... String with binary code
			% Outputs:
			%    hexCode ... String with hex code
			nParts = length(bin)/4;
			hexCode = repmat('0', 1, nParts);
			for iPart = 1:nParts
				thisBin = bin((iPart-1)*4+1:iPart*4);
				hexCode(iPart) = dec2hex(bin2dec(thisBin));
			end
		end

	end

	methods (Access=private)

		function processIncomingData(obj, src)
			% processIncomingData: Processes raw serial data into I/Q components and timestamps
			% Triggers newDataAvailable event when a full chirp is received
			%
			% Inputs:
			%   src ... Serial port object with incoming data
			frameLen = (4 * obj.samples + 11);

			newChunk = fgets(src);
			newChunk = uint8(newChunk);
			obj.rawBuffer = [obj.rawBuffer, newChunk];
			obj.chunkLengths = [obj.chunkLengths, numel(newChunk)];


			while numel(obj.rawBuffer) > frameLen && ~isempty(obj.chunkLengths) % in case we overflow size we start dumping older contributions
				obj.rawBuffer(1:obj.chunkLengths(1)) = [];
				obj.chunkLengths(1) = [];
			end

			if numel(obj.rawBuffer) == frameLen
				if obj.rawBuffer(5) == 77
					dataCount = obj.samples * 2;
					lowBytes = obj.rawBuffer(10:2:(9 + dataCount * 2));
					highBytes = obj.rawBuffer(11:2:(9 + dataCount * 2));
					tmp = uint16(highBytes) * 256 + uint16(lowBytes);
					data = typecast(tmp, 'int16');

					obj.bufferI(:, obj.writeIdx) = data(1:2:end);
					obj.bufferQ(:, obj.writeIdx) = data(2:2:end);
					obj.bufferTime(obj.writeIdx) = toc(obj.startTime);

					obj.writeIdx = mod(obj.writeIdx, obj.bufferSize) + 1;
					notify(obj, 'newDataAvailable');

					obj.rawBuffer = [];
					obj.chunkLengths = [];
				else % TODO: this probably actually isn't needed
					if ~isempty(obj.chunkLengths)
						obj.rawBuffer(1:obj.chunkLengths(1)) = [];
						obj.chunkLengths(1) = [];
					end
				end
			end
		end



		function sysConfig = generateSystemConfig(obj)
			% generateSystemConfig: Generates system configuration command (hex) for the radar
			%
			% Base forms of function was provided by CTU FEL courtesy of Ing. Viktor Adler, Ph.D.
			%
			% Output:
			%   sysConfig ... Hex command string
			SelfTrigDelay='000';  % 0 ms delay
			reserved='0';
			LOG='1';              % MAG 0 log | 1-linear
			FMT='0';              % 0-mm | 1-cm
			LED='01';             % 00-off | 01-1st trget rainbow
			reserved2='0000';
			protocol='010';       % 001 TSV output | 010 binary | 000 webgui
			AGC='0';              % auto gain 0-off | 1-on
			gain = obj.hPreferences.getRadarSystemParamters();
			SER2='1';             % usb connect 0-off | 1-on
			SER1='0';             % wifi 0-off | 1-on
			SLF='0';              % 0-ext trig mode | 1-standard
			PRE='0';              % pretrigger

			% data frames sent to PC
			ERR='0';
			ST='0';               % status
			TL='0';               % target list
			C='0';                % CFAR
			R='0';                % magnitude
			P='0';                % phase
			CPL='0';              % complex FFT
			RAW='1';              % raw ADC

			sys1=append(SelfTrigDelay,reserved);
			sys2=append(LOG,FMT,LED);
			sys3=reserved2;
			sys4=append(protocol,AGC);
			sys5=append(gain,SER2,SER1);
			sys6=append(ERR,ST,TL,C);
			sys7=append(R,P,CPL,RAW);
			sys8=append(reserved,reserved,SLF,PRE);
			sysCommand=radar.bin2hex(append(sys1,sys2,sys3,sys4,sys5,sys6,sys7,sys8));
			sysConfig = append('!S', sysCommand);
			disp(sysConfig);
		end

		function basebandCommand = generateBasebandCommand(obj)
			% generateBasebandCommand: Generates baseband configuration command for the radar
			%
			% Base forms of function was provided by CTU FEL courtesy of Ing. Viktor Adler, Ph.D.
			%
			% Output:
			%   basebandCommand ... Hex command string

			WIN='0';              % windowing before FFT
			FIR='0';              % FIR filter 0-of | 1-on
			DC='1';               % DCcancel 1-on | 0-off
			CFAR='00';            % 00-CA_CFAR | 01-CFFAR_GO | 10-CFAR_SO | 11 res
			CFARthreshold='0000'; % 0-30, step 2
			CFARsize='0000';      % 0000-0 | 1111-15|  n of
			CFARgrd='00';         % guard cells range 0-3
			AVERAGEn='00';        % n FFTs averaged range 0-3
			FFTsize='000';        % 000-32,64,128...,111-2048
			DOWNsample='000';     % downsampling factor 000-0,1,2,4,8..111-64
		
			[~, samplesBin, adcBin, rampsBin] = obj.hPreferences.getRadarBasebandParameters();
			baseband=append(WIN,FIR,DC,CFAR,CFARthreshold,CFARsize,CFARgrd,AVERAGEn,FFTsize,DOWNsample,rampsBin,samplesBin,adcBin);
			basebandHEX=radar.bin2hex(baseband);
			basebandCommand=append('!B',basebandHEX);
			disp(basebandCommand);
		end

		function frontendCommand = generateFrontendCommand(obj)
			% generateFrontendCommand: Generates frontend frequency configuration command
			%
			% Output:
			%   frontendCommand ... Hex command string
			FreqReserved='00000000000';
			if obj.hPreferences.getRadarFrontend() == 122
				fprintf("radar | frontendCommand | setting frontend to 122 GHz\n");
				OperatingFreq='001110101001100000000';
			else
				fprintf("radar | frontendCommand | setting frontend to 24 GHz\n");
				OperatingFreq='000010111011100000000';
			end

			frontendCommand=radar.bin2hex(append(FreqReserved, OperatingFreq));
			frontendCommand=append('!F', frontendCommand);
			disp(frontendCommand);
		end

		function pllCommand = generatePLLCommand(obj)
			% generatePLLCommand: Generates PLL configuration command based on bandwidth
			%
			% Output:
			%   pllCommand ... Hex command string
			bandwidth = floor(obj.hPreferences.getRadarBandwidth()/2);
			bandwidth = num2str(bandwidth,'%04X');
			pllCommand=append('!P0000',bandwidth);
			disp(pllCommand);
		end

		function configureRadar(obj)
			% Sends all configuration commands to the radar via serial
			%
			% Flushes buffers and ensures commands are executed in sequence
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateSystemConfig());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateBasebandCommand());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateFrontendCommand());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generatePLLCommand());
			flush(obj.hSerial);
		end

	end

	methods (Access = public)

		function obj = radar(hPreferences, startTime)
			% radar: Initializes radar object with preferences and timing reference
			%
			% Inputs:
			%   hPreferences ... Handle to preferences object
			%   startTime    ... Base timestamp (output of `tic`)
			%
			% Output:
			%   obj ... Initialized radar instance

			fprintf('Radar | radar | constructing object\n');
			obj.hPreferences = hPreferences;
			obj.startTime = startTime;

			[obj.samples, ~, ~, ~] = obj.hPreferences.getRadarBasebandParameters();
			obj.bufferI=zeros(obj.samples, obj.bufferSize);
			obj.bufferQ=zeros(obj.samples, obj.bufferSize);

			addlistener(hPreferences, 'newConfigEvent', @(~,~) obj.onNewConfigAvailable());
		end

		function endProcesses(obj)
			% endProcesses: Safely stops serial communication and timers
			%
			% Releases hardware resources and halts data acquisition

			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
				stop(obj.triggerTimer);
			end
		end

		function onNewConfigAvailable(obj)
			% onNewConfigAvailable: Updates radar configuration dynamically when preferences change
			%
			% Function is called by preference's newConfigEvent event
			% Re configures radar via serial commands and trigger timer

			[obj.samples, ~, ~, ~] = obj.hPreferences.getRadarBasebandParameters();
			obj.bufferI=zeros(obj.samples, obj.bufferSize);
			obj.bufferQ=zeros(obj.samples, obj.bufferSize);


			if ~isempty(obj.hSerial)
				stop(obj.triggerTimer);
				obj.triggerTimer.Period = obj.hPreferences.getRadarTriggerPeriod()/1000;
				start(obj.triggerTimer);
				obj.configureRadar();
			end
		end

		function status = setupSerial(obj)
			% setupSerial: Establishes serial connection to the radar hardware
			%
			% Output:
			%   status ... true if connection succeeded, false otherwise

			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
				delete(obj.triggerTimer);
				obj.hSerial = [];
				obj.triggerTimer = [];
				status = false;
				return
			end
			[port, baudrate] = obj.hPreferences.getConnectionRadar();
			try
				fprintf("radar | setupSerial | port: %s, baud: %f\n", port, baudrate)
				obj.hSerial = serialport(port, baudrate, "Timeout", 5);
				configureTerminator(obj.hSerial,"CR/LF");
				flush(obj.hSerial);
				obj.configureRadar();
				fprintf("radar | setupSerial | starting thread\n");
				configureCallback(obj.hSerial, "terminator", @(src, ~) obj.processIncomingData(src))
				status = true;

				obj.triggerTimer = timer;
				obj.triggerTimer.StartDelay = 2;
				obj.triggerTimer.Period = obj.hPreferences.getRadarTriggerPeriod()/1000;
				obj.triggerTimer.ExecutionMode = 'fixedSpacing';
				obj.triggerTimer.UserData = 0;
				obj.triggerTimer.TimerFcn = @(~,~) writeline(obj.hSerial, '!N');
				start(obj.triggerTimer);
			catch ME
				fprintf("Radar | setupSerial | Failed to setup serial")
				obj.hSerial = [];
				status = false;
			end
		end
	end
end


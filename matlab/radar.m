classdef radar < handle
	properties(Access = public)
		
		hPreferences preferences;
		hSerial;
		processData = true;

		oldBuf = [];
		samples = 256;
    startTime uint64;
		triggerTimer;
		
	  bufferI;              % Circular buffer for I
    bufferQ;              % Circular buffer for Q
    bufferTime = [];           % Circular buffer for timestamps
    bufferSize = 100;          % Max buffer size
    writeIdx = 1;              % Index for next write
   end


	events 
		newDataAvailable
	end

	methods (Access=private)

		function processIncommingData(obj,src)
			% processIncommingData: reads next line on serial and parses the data
			
			buf = fgets(src);
			if(length(buf) == 40)
				return;
			end
			process = [obj.oldBuf buf];
			
			if length(process) ~= (4*obj.samples+11)
				if length(process) > (4*obj.samples+11)
					obj.oldBuf = [];
				else
					obj.oldBuf = buf;
				end
				return;
			end
			obj.oldBuf = [];

			if(process(5) ~= 77)
				return;
			end
			dataCount = process(9)*256+process(8);
			tmp = process(11:2:(9+dataCount*2)) * 256 + process(10:2:(9+dataCount*2));
			data = typecast(uint16(tmp), 'int16');
			

			obj.bufferI(obj.writeIdx, :) = data(1:2:length(data));
      obj.bufferQ(obj.writeIdx, :) = data(2:2:length(data));
      obj.bufferTime(obj.writeIdx) = toc(obj.startTime);
            
      obj.writeIdx = mod(obj.writeIdx, obj.bufferSize) + 1;
      notify(obj, 'newDataAvailable');
		end
		

		function sysConfig = generateSystemConfig(obj)
			% generateSystemConfig: generate system config command for the radar
			%
			% OUTPUT:
			% sysConfig ... hex string to be sent to the radar

			SelfTrigDelay='000';  % 0 ms delay
			reserved='0';
			LOG='0';              % MAG 0 log | 1-linear
			FMT='0';              % 0-mm | 1-cm
			LED='01';             % 00-off | 01-1st trget rainbow
			reserved2='0000';
			protocol='010';       % 001 TSV output | 010 binary | 000 webgui
			AGC='0';              % auto gain 0-off | 1-on
			gain='10';            % 00-8dB | 10-21dB | 10-43dB | 11-56dB
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
			sysCommand=bin2hex(append(sys1,sys2,sys3,sys4,sys5,sys6,sys7,sys8));
			sysConfig = append('!S', sysCommand);
			disp(sysConfig);
		end

		function basebandCommand = generateBasebandCommand(obj)
			% generateBasebandCommand: generate baseband config command for the radar
			%
			% OUTPUT:
			% basebandCommand ... hex string to be sent to the radar

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
			RAMPS='000';          % ramps per measurement
			
			[samplesReg, samplesBin, adc] = obj.hPreferences.getRadarBasebandParameters();
			obj.samples = samplesReg;
			obj.bufferI=zeros(obj.bufferSize, obj.samples);
			obj.bufferQ=zeros(obj.bufferSize, obj.samples);

			baseband=append(WIN,FIR,DC,CFAR,CFARthreshold,CFARsize,CFARgrd,AVERAGEn,FFTsize,DOWNsample,RAMPS,samplesBin,adc);
			basebandHEX=bin2hex(baseband);
			basebandCommand=append('!B',basebandHEX);
		end

		function frontendCommand = generateFrontendCommand(obj)
			% generateBasebandCommand: generate frontend config command for the radar
			% command gets current frontend type from preferences
			%
			% OUTPUT:
			% frontendCommand ... hex string to be sent to the radar

			FreqReserved='00000000000';
			if obj.hPreferences.getRadarFrontend() == 122 
				fprintf("radar | frontendCommand | setting frontend to 122 GHz\n");
				OperatingFreq='001110101001100011000';
			else 
				fprintf("radar | frontendCommand | setting frontend to 24 GHz\n");
				OperatingFreq='000010111011100000000';
			end

			frontendCommand=bin2hex(append(FreqReserved, OperatingFreq));
			frontendCommand=append('!F', frontendCommand);
		end

		function pllCommand = generatePLLCommand(obj)
			% generatePLLCommand: generate PLL config command for the radar
			% desired bandwith is read from preferences
			%
			% OUTPUT:
			% pllCommand ... hex string to be sent to the radar

			bandwidth = obj.hPreferences.getRadarBandwidth();
			bandwidth=dec2hex(bandwidth);
			pllCommand=append('!P0000',bandwidth);
			disp(pllCommand);
		end
		
		function configureRadar(obj)
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateSystemConfig());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateBasebandCommand());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateFrontendCommand());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generatePLLCommand());
			flush(obj.hSerial); 
		end

	end

	methods (Access = public)
		
		function obj = radar(hPreferences, startTime)
			% radar: constructor for the radar class 
			%
			% INPUT:
			% hPreferences ... handle to preferences object 
			% startTime ... output of tic command, timestamp to which all others
			% are calculated from

			fprintf('Radar | radar | constructing object\n');
			obj.hPreferences = hPreferences;
      obj.startTime = startTime;
			bufferTime = zeros(obj.bufferSize);
		end
		
		function endProcesses(obj)
			% endProcesses: safely stops all class processes

			if ~isempty(obj.hSerial)
				configureCallback(obj.hSerial, "off");
				delete(obj.hSerial)
				stop(obj.triggerTimer);
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
			[port, baudrate] = obj.hPreferences.getConnectionRadar();
			%try 
				fprintf("radar | setupSerial | port: %s, baud: %f\n", port, baudrate)
				obj.hSerial = serialport(port, baudrate, "Timeout", 5);
				configureTerminator(obj.hSerial,"CR/LF");
				flush(obj.hSerial);								
				obj.configureRadar();
				fprintf("radar | setupSerial | starting thread\n");			
				configureCallback(obj.hSerial, "terminator", @(src, ~) obj.processIncommingData(src))
				status = true;

				obj.triggerTimer = timer;
				obj.triggerTimer.StartDelay = 4;
				obj.triggerTimer.Period = obj.hPreferences.getRadarTriggerPeriod()/1000;
				obj.triggerTimer.ExecutionMode = 'fixedSpacing';
				obj.triggerTimer.UserData = 0;
				obj.triggerTimer.TimerFcn = @(~,~) writeline(obj.hSerial, '!N');
				start(obj.triggerTimer);
			%catch ME
			%	fprintf("Radar | setupSerial | Failed to setup serial")
			%	status = false;
			%end
		end
	end
end


function hexCode = bin2hex(bin)
    nBin = length(bin);
    nParts = nBin/4;
    hexCode = repmat('0', 1, nParts);
    for iPart = 1:nParts
        thisBin = bin((iPart-1)*4+1:iPart*4);
        hexCode(iPart) = dec2hex(bin2dec(thisBin));
    end
end

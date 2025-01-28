classdef radar < handle
	properties(Access = public)
		
		hPreferences preferences;
		hSerial;
		processData = true;

		dataTimestamp;
		dataI;
		dataQ;
		oldBuf;
		samples = 512;
	end

	methods (Access=private)

		function processIncommingData(obj,src)

			buf = fgets(src);
			process = [obj.oldBuf buf];
			if length(process) ~= (4*obj.samples+11)
				obj.oldBuf = buf;
				return;
			end
			obj.oldBuf = [];

			if(process(5) ~= 77)
				return;
			end
			dataCount = process(9)*256+process(8);
			tmp = process(11:2:(9+dataCount*2)) * 256 + process(10:2:(9+dataCount*2));
			data = typecast(uint16(tmp), 'int16');
			nData = numel(data);
			obj.dataI = data(1:2:nData);
			obj.dataQ = data(2:2:nData);
		end
		

		function sysConfig = generateSystemConfig(obj)
			% DEFAULT: !S00032012
			%(only change is TSV mode and led)
			SelfTrigDelay='000';  % 0 ms delay
			reserved='0';
			LOG='0';              % MAG 0 log | 1-linear
			FMT='0';              % 0-mm | 1-cm
			LED='01';             % 00-off |01-1st trget rainbow
			reserved2='0000';
			protocol='010';       % 001 TSV output | 010 binary |000 webgui
			AGC='0';              % auto gain 0-off|1-on
			gain='11';            % 00-8dB|10-21|10-43|11-56dB
			SER2='1';             % usb connect 0-off|1-on
			SER1='0';             % wifi 0-off|1-on
			%data frames sent to PC
			ERR='0';
			ST='0';               % status
			TL='0';               % target list
			C='0';                % CFAR
			R='0';                % magnitude
			P='0';                % phase
			CPL='0';              % complex FFT
			RAW='1';              % raw ADC
			% then 2x reserved
			SLF='1';              % 0-ext trig mode |1-standard
			PRE='0';              % pretrigger 
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
			% DEFAULT: !BA252C125
			WIN='0';              % windowing before FFT
			FIR='0';              % FIR filter 0-of|1-on
			DC='1';               % DCcancel 1-on|0-off
			CFAR='00';            % 00-CA_CFAR|01-CFFAR_GO|10-CFAR_SO|11 res
			CFARthreshold='0100'; % 0-30, step 2
			CFARsize='1010';      % 0000-0|1111-15| n of
			CFARgrd='01';         % guard cells range 0-3
			AVERAGEn='01';        % n FFTs averaged range 0-3
			FFTsize='100';        % 000-32,64,128...,111-2048
			DOWNsample='000';     % downsampling factor 000-0,1,2,4,8..111-64
			RAMPS='100';          % ramps per measurement
			NofSAMPLES='100';     % samples per measurement
			ADCclkDIV='100';      % sampling freq
			baseband=append(WIN,FIR,DC,CFAR,CFARthreshold,CFARsize,CFARgrd,AVERAGEn,FFTsize,DOWNsample,RAMPS,NofSAMPLES,ADCclkDIV);
			basebandHEX=bin2hex(baseband);
			basebandCommand=append('!B',basebandHEX);
		end

		function frontendCommand = generateFrontendCommand(obj)
			% DEFAULT: !F00075300
			%21 bit freq in lsb=250kHz
			FreqReserved='00000000000';
			if obj.hPreferences.getRadarHeaderType() == 122 
				fprintf("radar | frontendCommand | setting frontend to 122 GHz\n");
				OperatingFreq='001110101001100011000';
			else 
				fprintf("radar | frontendCommand | setting frontend to 24 GHz\n");
				OperatingFreq='000010111011100000000';
			end

			%1st 11 bits are reserved
			frontendCommand=bin2hex(append(FreqReserved, OperatingFreq));
			frontendCommand=append('!F', frontendCommand);
		end

		function pllCommand = generatePLLCommand(obj)
			%max bandwidth by sending '!K'
			%16bit, MSB bit is sign 1=minus|0=plus
			%example 100...001 = -65536MHz | 1111...111= -2MHz
			PLLreserved='0000000000000000';%1 at the start to save starting 0s
			
			if obj.hPreferences.getRadarHeaderType() == 122
				fprintf("radar | pllCommand | setting bandwidth to 122 GHz setting\n");
				bandwidth='0000100111000100'; % 5000 Hz 
			else 
				fprintf("radar | pllCommand | setting bandwidth to 24 GHz setting\n");
				bandwidth='0000000111110100'; % 1000 MHz
			end
			pllCommand = bin2hex(append(PLLreserved, bandwidth));
			pllCommand=append('!P',pllCommand);
			disp(pllCommand);
    end

	end

	methods (Access = public)
			function obj = radar(hPreferences)
			fprintf('Radar | radar | constructing object\n');
			obj.hPreferences = hPreferences;
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
			[port, baudrate] = obj.hPreferences.getConnectionRadar();
			try 
				fprintf("radar | setupSerial | port: %s, baud: %f\n", port, baudrate)
				obj.hSerial = serialport(port, baudrate, "Timeout", 5);
				configureTerminator(obj.hSerial,"CR/LF");
				flush(obj.hSerial);								
				obj.configureRadar();
				fprintf("radar | setupSerial | starting thread\n");			
				configureCallback(obj.hSerial, "terminator", @(src, ~) obj.processIncommingData(src))
				status = true;
			catch ME
				fprintf("Radar | setupSerial | Failed to setup serial")
				status = false;
			end

			end


		function configureRadar(obj)
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateSystemConfig());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateBasebandCommand());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generateFrontendCommand());
			flush(obj.hSerial); writeline(obj.hSerial, obj.generatePLLCommand());
			flush(obj.hSerial); 
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

%Set desired parametres and call the  function in your code
%functions return command for SiRad radar
%SiRad sets all to default after restart!
%more information in Protocol_Description file (Protocol_Description_Easy_Simple_V2.4)
%for info from SiRad, send '!I' for info,E for errors, 



function [SysCommand, BasebandCommand, FrontendCommand, PLLCommand]  = funkceSiRad()


SysCommand = SystemConfig();%obligatory, else webgui mode
% flush(SerialName); writeline(SerialName, SysCommand);
BasebandCommand = BasebandConfig();%optional
%flush(SerialName); writeline(SerialName, BasebandCommand);
FrontendCommand = FrontEndConfig();%optional
%flush(SerialName); writeline(SerialName, FrontendCommand);
PLLCommand = PLLConfig();%optional
%flush(SerialName); writeline(SerialName, PLLCommand);

%flush(SerialName); writeline(SerialName, '!E'); %ErrorList
%readline(SerialName) %approx 3 times
%flush(SerialName); writeline(SerialName, '!I'); %systeminfo



end

function sysCommand = SystemConfig() %returns sysConfig Command ready for radar
    SelfTrigDelay='000';
    reserved='0';
    LOG='0'; %MAG 0 log | 1-linear
    FMT='0'; %0-mm | 1-cm
    LED='00'; %00-off |01-1st trget rainbow
    reserved2='0000';
    protocol='001'; %001 TSV output(ideal) | 010 binary |000 webgui
    AGC='0'; %auto gain 0-off|1-on
    gain='01';%00-8dB|01-21|10-43|11-56dB
    SER2='1';%usb connect 0-off|1-on
    SER1='0';%wifi 0-off|1-on
    %data frames sent to PC
    ERR='0';
    ST='0';%status
    TL='0';%target list
    C='0';%CFAR
    R='0';%magnitude
    P='0';%phase
    CPL='0';%complex FFT
    RAW='1';%raw ADC 
    % then 2x reserved
    SLF='0';%0-ext trig mode |1-standard
    PRE='0';%pretrigger
   
    
    sys1=append(SelfTrigDelay,reserved);
    sys2=append(LOG,FMT,LED);
    sys3=reserved2;
    sys4=append(protocol,AGC);
    sys5=append(gain,SER2,SER1);
    sys6=append(ERR,ST,TL,C);
    sys7=append(R,P,CPL,RAW);
    sys8=append(reserved,reserved,SLF,PRE);
    sysCommand=bin2hex(append(sys1,sys2,sys3,sys4,sys5,sys6,sys7,sys8));
    sysCommand = append('!S', sysCommand);
    
%     '!S',
end
function basebandCommand = BasebandConfig() %returns a baseband command, NESMI BYT 4 NULY NA ZACATKU
    basebandDEFAULT='!BA252C125';
    WIN='0';%windowing before FFT
    FIR='0';%FIR filter 0-of|1-on
    DC='0'; %DCcancel 1-on|0-off
    CFAR='00';%00-CA_CFAR|01-CFFAR_GO|10-CFAR_SO|11 res
    CFARthreshold='0000'; %0-30, step 2
    CFARsize='0000';%0000-0|1111-15| n of 
    CFARgrd='00'; %guard cells range 0-3
    AVERAGEn='00'; %n FFTs averaged range 0-3
    FFTsize='000';% 000-32,64,128...,111-2048 100 for 512
    DOWNsample='000';%downsampling factor 000-0,1,2,4,8..111-64
    
		
		% RAMPS='100';%ramps per measurement 
    % NofSAMPLES='011';%samples per measurement 512
    % ADCclkDIV='101';%sampling freq 000-2.571,2.4,2.118,1.8,1.125,0.487,0.186,0.059
    

		RAMPS='000';%ramps per measurement 
    NofSAMPLES='011';%samples per measurement 512
    ADCclkDIV='101';%sampling freq 000-2.571,2.4,2.118,1.8,1.125,0.487,0.186,0.059
    

		baseband=append(WIN,FIR,DC,CFAR,CFARthreshold,CFARsize,CFARgrd,AVERAGEn,FFTsize,DOWNsample,RAMPS,NofSAMPLES,ADCclkDIV);
    basebandHEX=bin2hex(baseband);
    basebandCommand=append('!B',basebandHEX);
end

function frontendCommand = FrontEndConfig()
   defaultFrontEnd='!F00075300';
   %21 bit freq in lsb=250kHz
   OperatingFreq='000010111011100000000';
	 FreqReserved='00000000000';
   %1st 11 bits are reserved
   frontendCommand=bin2hex(append(FreqReserved, OperatingFreq));
   frontendCommand=append('!F', frontendCommand);
   
   
%    Freqstr=num2str(FreqHEX-'80000000');%compensate for 1 in FreqReserved
%    frontendCommand=append('!F',Freqstr);
%    frontendCommand = frontendCommand(frontendCommand~=' ');
end

function pllCommand = PLLConfig()
    %max bandwidth by sending '!K'
    %16bit, MSB bit is sign 1=minus|0=plus
    %example 100...001 = -65536MHz | 1111...111= -2MHz
    PLLreserved='0000000000000000';%1 at the start to save starting 0s
		bandwidth='0000000111110100'; % 1000 MHz
		% bandwidth= '0000000001111101'; % 250 MHz
    pllCommand = bin2hex(append(PLLreserved, bandwidth));
    pllCommand=append('!P',pllCommand);
    % pllhex=dec2hex(bin2dec(append(PLLreserved,bandwidth)));
    % PLLhex=num2str(pllhex-'80000000');
    % pllCommand=append('!P',PLLhex);
    % pllCommand = pllCommand(pllCommand~=' ');
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
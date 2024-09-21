%% SiRad_Demo_MATLAB.m
% @author Jana Krimmling
% @version 1.0-798cfbbd6a
% @date 2020-02-17
% ************************************************************************
% Copyright (C) 2020 Silicon Radar - ALL RIGHTS RESERVED
%
% The names SiRad Easy(R) and SiRad Simple(R) belong to Silicon Radar. 
% The name MATLAB(R) belongs to MathWorks.
%
% BY DOWNLOADING, COPYING, INSTALLING, OR USING THIS SOFTWARE AND ANY
% RELATED DOCUMENTATION YOU OR ANY ENTITY BY WHICH YOU ARE EMPLOYED AND/OR
% ENGAGED AGREES TO BE BOUND BY THIS LICENSE AGREEMENT.
%
% Use in source form and re-distribution are permitted provided that the 
% following conditions are met:
%   1. This software must be used only with radar front ends made by or
%      for Silicon Radar.
%   2. Re-distributions of this code must reproduce the above copyright 
%      notice, this list of conditions and the following disclaimer in 
%      the documentation and/or other materials provided with the 
%      distribution.
%   3. Re-distributions of this code with modifications are prohibited.
%   4. The name of Silicon Radar may not be used to endorse or promote
%      products derived from this software without the prior written
%      permission of Silicon Radar.
%   5. Silicon Radar has no obligation to provide support, maintenance or
%      updates for this software.
%
% THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS 'AS IS' AND
% ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
% IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
% PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
% LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
% CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
% SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
% INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
% CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
% ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
% THE POSSIBILITY OF SUCH DAMAGE.
% ************************************************************************

% ------------------------------------------------------------------------
%                           CLEAR ENVIRONEMNT
% ------------------------------------------------------------------------
clear;
close all;
clc;

% ------------------------------------------------------------------------
%                             USER SETTINGS
% ------------------------------------------------------------------------
% set Serial Port (modify number after "COM")


% changes 
if isunix
    serialPort_string = '/dev/ttyACM0';
elseif ispc
    serialPort_string = 'COM4';
else
    disp('Platform not supported')
end

baudrate=230400;

% set fixed graph size and position to ON or OFF
fixedGraphSizeAndPosition = 1;  % or 0
% graph size
figureW = 1600; %1920;
figureH = 800; %954;
% graph position on screen
figureX = 0;
figureY = 42; % leave space for windows 10 task bar

% ------------------------------------------------------------------------
%                                SCRIPT
% ------------------------------------------------------------------------
% open com port
serialPort = [];
comPort = serialport(serialPort_string, baudrate, 'DataBits', 8, 'FlowControl', 'none', 'StopBits', 1, 'Parity', 'none', 'Timeout', 1);
serialPort = comPort;
configureTerminator(serialPort, 'CR');
flush(serialPort);
pause(0.1);

% system and processing settings
% ------------------------------------------------------------------------
SYS_CONFIG = '!S00013A08';  % set to external trigger mode
writeline(serialPort, SYS_CONFIG);
flush(serialPort);

BB_CONFIG = '!BA0000025';  % set to 1 ramp
writeline(serialPort, BB_CONFIG);
flush(serialPort);
% ------------------------------------------------------------------------

% automatic frequency settings
% ------------------------------------------------------------------------
FSCAN = '!J';  % auto detect frequency
writeline(serialPort, FSCAN);
flush(serialPort);
pause(1);

MAXBW = '!K';  % set to max bandwidth
writeline(serialPort, MAXBW);
flush(serialPort);
% ------------------------------------------------------------------------

% manual frequency settings
% ------------------------------------------------------------------------
% RFE_CONFIG = '!F0201DA9C';  % 121.5 GHz, TRX_120_002
% %RFE_CONFIG = '!F0201D4C0';  % 120 GHz, TRX_120_001
% writeline(serialPort, RFE_CONFIG);
% flush(serialPort);
%
% PLL_CONFIG = '!P00001388';  % bandwidth 5 GHz
% writeline(serialPort, PLL_CONFIG);
% flush(serialPort);
% ------------------------------------------------------------------------
pause(0.5);

% create plot before using it to be faster
dataI = [];
dataQ = [];
tiledlayout(2,2);
ax1 = nexttile;
plot(ax1,dataI);
title('I channel');
xlabel("n (sample)");
ylabel("ADC value");
ax2 = nexttile;
plot(ax2,dataQ);
title('Q channel');
xlabel("n (sample)");
ylabel("ADC value");
ax3 = nexttile;
plot(ax3, log(abs(fft(dataI))));
title('I channel - FFT');
xlabel("[bin]");
ylabel("[dB]");
ax4 = nexttile;
plot(ax4, log(abs(fft(dataQ))));
title('Q channel - FFT');
xlabel("[bin]");
ylabel("[dB]");
axis([ax1 ax2],[0 600 -80 80]);
axis([ax3 ax4],[0 600 2 10]);
if (fixedGraphSizeAndPosition)
    set(gcf, 'position', [figureX, figureY, figureW, figureH]);
end

while (1)
    lastwarn('');
    try
        TRIGGER = '!M';
        writeline(serialPort, TRIGGER);
        loop = 0;
        while (loop < 4)
            lastwarn('');
            buf = readline(serialPort);
            if (~isempty(buf))
                loop = loop + 1;
                if (strfind(buf, "M") == 1)
                    % we found data already, go out of loop
                    break;
                end
                if (strfind(buf, "R") == 2)
                    % we got STD data, device restarted, resend config
                    disp("Resend config...");
                    
                    % system and processing settings
                    % ----------------------------------------------------
                    SYS_CONFIG = '!S00013A08';  % set to ext trigger mode
                    writeline(serialPort, SYS_CONFIG);
                    flush(serialPort);
                    
                    BB_CONFIG = '!BA0000025';  % set to 1 ramp
                    writeline(serialPort, BB_CONFIG);
                    flush(serialPort);
                    % ----------------------------------------------------
                    
                    % automatic frequency settings
                    % ----------------------------------------------------
                    FSCAN = '!J';  % auto detect frequency
                    writeline(serialPort, FSCAN);
                    flush(serialPort);
                    pause(1);
                    
                    MAXBW = '!K';  % set to max bandwidth
                    writeline(serialPort, MAXBW);
                    flush(serialPort);
                    % ----------------------------------------------------
                    
                    % manual frequency settings
                    % ----------------------------------------------------
                    % RFE_CONFIG = '!F0201DA9C';  % 121.5 GHz, TRX_120_002
                    % %RFE_CONFIG = '!F0201D4C0';  % 120 GHz, TRX_120_001
                    % writeline(serialPort, RFE_CONFIG);
                    % flush(serialPort);
                    %
                    % PLL_CONFIG = '!P00001388';  % bandwidth 5 GHz
                    % writeline(serialPort, PLL_CONFIG);
                    % flush(serialPort);
                    % ----------------------------------------------------
                    pause(0.5);
                    break;
                end
            else
                error(lastwarn);
            end
        end
        if (~isempty(lastwarn))
            error(lastwarn);
        end
    catch err
        % serial timeout detected, reset port, resend config
        disp("Serial timeout. Try to recover...");
        serialPort = [];
        delete(comPort);
        pause(0.1);
        comPort = serialport(serialPort_string, baudrate, 'DataBits', 8, 'FlowControl', 'none', 'StopBits', 1, 'Parity', 'none', 'Timeout', 2);
        serialPort = comPort;
        configureTerminator(serialPort, 'CR/LF');
        flush(serialPort);
        pause(0.1);
        
        % system and processing settings
        % ----------------------------------------------------------------
        SYS_CONFIG = '!S00013A08';  % set to external trigger mode
        writeline(serialPort, SYS_CONFIG);
        flush(serialPort);
        
        BB_CONFIG = '!BA0000025';  % set to 1 ramp
        writeline(serialPort, BB_CONFIG);
        flush(serialPort);
        % ----------------------------------------------------------------
        
        % automatic frequency settings
        % ----------------------------------------------------------------
        FSCAN = '!J';  % auto detect frequency
        writeline(serialPort, FSCAN);
        flush(serialPort);
        pause(1);
        
        MAXBW = '!K';  % set to max bandwidth
        writeline(serialPort, MAXBW);
        flush(serialPort);
        % ----------------------------------------------------------------
        
        % manual frequency settings
        % ----------------------------------------------------------------
        % RFE_CONFIG = '!F0201DA9C';  % 121.5 GHz, TRX_120_002
        % %RFE_CONFIG = '!F0201D4C0';  % 120 GHz, TRX_120_001
        % writeline(serialPort, RFE_CONFIG);
        % flush(serialPort);
        %
        % PLL_CONFIG = '!P00001388';  % bandwidth 5 GHz
        % writeline(serialPort, PLL_CONFIG);
        % flush(serialPort);
        % ----------------------------------------------------------------
        pause(0.5);
    end
    
    if (isempty(buf))
        continue;
    end
    
    % convert to string
    frames = char(buf);
    
    % frame format of M frame:
    % !M0400/0001/-FFED/0005/-FFF9/...|
    % !: start marker
    % M: ADC raw data frame marker
    % 0400: number of samples, 4 chars
    % /: data delimiter
    % 0001: frame counter, 4 chars
    % /: data delimiter
    % -: optional signed indicator
    % FFED, 0005 and the like: data
    % |: frame end delimiter
    
    % find the start of the M frame, the 'M' marker
    positionOfM = strfind(frames, "M");
    if (isempty(positionOfM))
        continue;
    end
    % find the end of the M frame, the '|' marker
    positionOfEnd = strfind(frames, "|");
    if (isempty(positionOfEnd))
        continue;
    end
    
    % cut the M frame out of the other frames, omit the 'M', omit length
    % information, omit frame counter and some delimiters in the M frame
    positionOfData = positionOfM(1) + 11;
    frameM = frames(positionOfData:(positionOfEnd - 1));
    
    % replace all delimiters and optional sign chars before conversion
    % to numbers
    dataStr = split(frameM, ["/-","/"]);
    data = typecast(uint16(hex2dec(dataStr)), "int16");
    dataI = data(1:2:end);
    dataQ = data(2:2:end);
    disp(dataI);
    disp(dataQ);
    
    % plot the data
    plot(ax1,dataI);
    plot(ax2,dataQ);
    plot(ax3, log(abs(fft(dataI))));
    plot(ax4, log(abs(fft(dataQ))));
    axis([ax1 ax2],[0 600 -200 200]);
    axis([ax3 ax4],[0 600 2 10]);
    
    % wait a little before sending next trigger to not confuse device
    pause(0.005);
end

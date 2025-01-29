

function SerialRead()
% graph size
% figureW = 1000; %1920;
% figureH = 500; %954;
% % graph position on screen
% figureX = 15;
% figureY = 18; % leave space for windows 10 task bar
% dataI = [];
% dataQ = [];
% 
% hFig = figure(1);
% hFig.Position= [figureX figureY figureW figureH];
% hFig.Name = 'The data';
% tiledlayout(2,2, 'TileSpacing', 'tight', 'Padding', 'tight');
% ax1 = nexttile;
% hLine = line(ax1,'Color', 'r');
% %hLine1= line(ax1);
% title('FFT Q channel');
% xlabel("[bin]");
% ylabel("[dB]");
% ax2 = nexttile;
% % plot(ax2,dataQ);
% hLine2 = line(ax2,'Color', 'r');
% hLine22= line(ax2);
% title('I+Q channel');
% xlabel("n (sample)");
% ylabel("ADC value");
% 
% ax3 = nexttile;
% %plot(ax3, log(abs(fft(dataI))));
% hLine3 = line(ax3, 'Color','g');
% title('I channel - FFT');
% xlabel("[bin]");
% ylabel("[dB]");
% ax4 = nexttile;
% % plot(ax4, log(abs(fft(dataQ))));
% hLine4 = line(ax4, 'Color', 'r');
% title('Qchannel - FFT');
% xlabel("[bin]");
% ylabel("[dB]");
%axis([ax1 ax2],[0 600 -80 80]);
%axis([ax3 ax4],[0 600 2 10]);


SerialName = "/dev/ttyACM0";
SerialObj = serialport(SerialName,230400,"Timeout",5);
% WINDOWS configureTerminator(SerialObj,"CR/LF");
configureTerminator(SerialObj,"CR/LF");
[SysCommand, BasebandCommand, FrontendCommand, PLLCommand] = funkceSiRad();

flush(SerialObj); writeline(SerialObj, SysCommand);
flush(SerialObj); writeline(SerialObj, BasebandCommand);
flush(SerialObj); writeline(SerialObj, FrontendCommand);
flush(SerialObj); writeline(SerialObj, PLLCommand);


% serialPort = serialport("COM16",230400,"Timeout",5);
% configureTerminator(serialPort,"CR/LF");
% SYS_CONFIG = '!S00032012';  % set to
% writeline(serialPort, SYS_CONFIG);
%Base_CONFIG = '!B00032012';  % set to
%writeline(serialPort, Base_CONFIG);
flush(SerialObj);% cte jenom do prvniho \n
% paket=readline(serialPort);
%configureCallback(serialPort,"terminator",@(x, y)callbackFcn(serialPort, hLine));
i=1;
oldBuf = [];

% TRIGGER = '!M';
% writeline(SerialObj, TRIGGER);
dataTimestamp=posixtime(datetime('now'));

        
while 1 
    
   % buf = char(readline(SerialObj));
		% 
    % indStartData = strfind(buf, '1024')+5;
    % if isempty(indStartData)
    %    continue;
    % end
		% 
    % charData = buf(16:end);
    % cislavCelle = split(charData, char(9));
    % cisla = cellfun(@str2double, cislavCelle);
		% 
    % nData = length(cisla);
    % DataI = cisla(1:2:nData);
    % DataQ = cisla(2:2:nData);
		% 
			buf = fgets(SerialObj);
		%	fprintf("%f ms\n", (posixtime(datetime('now'))-dataTimestamp) * 1000);
			%dataTimestamp = posixtime(datetime('now'));
		
			process = [oldBuf buf];
			
			if length(process) > (4*256+11)
				oldBuf = [];
				continue;
			end

			if length(process) ~= (4*256+11)
				oldBuf = buf;
				continue;
			end
			oldBuf = [];

			if(process(5) ~= 77)
				continue;
			end
				dataCount = process(9)*256+process(8);
				tmp = process(10:2:(9+dataCount*2)) * 256 + process(11:2:(9+dataCount*2));
				data = typecast(uint16(tmp), 'int16');
				nData = numel(data);
				DataI = data(1:2:nData);
				DataQ = data(2:2:nData);



		fprintf("%f ms\n", (posixtime(datetime('now'))-dataTimestamp) * 1000);
		dataTimestamp = posixtime(datetime('now'));
		%toc;
		%tic;
		%fftI = 20*log10(abs(fft(DataI)));
		%fftQ= 20*log10(abs(fft(DataQ)));
    %set(hLine2, 'XData', 1:2:nData, 'YData', DataI);
    %set(hLine22, 'XData', 2:2:nData, 'YData', DataQ);
    %set(hLine3, 'XData', 1:(numel(fftI)/2), 'YData', fftI(1:(numel(fftI)/2)));

		
    %set(hLine4, 'XData', 1:(numel(fftQ)/2), 'YData', fftQ(1:(numel(fftQ)/2)));
    

end


delete(SerialObj); %lepsi delete



%     function callbackFcn(ser)
%         buf = readline(ser);
%
%         if (contains(buf,"M"))
%             data = "Mam paket";
%         end
%
%     end
end
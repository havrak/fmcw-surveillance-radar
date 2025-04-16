clc;
% graph size
figureW = 1000; %1920;
figureH = 500; %954;
% graph position on screen
figureX = 15;
figureY = 18;
dataI = [];
dataQ = [];

hFig = figure(1);
hFig.Position= [figureX figureY figureW figureH];
hFig.Name = 'The data';
tiledlayout(2,2, 'TileSpacing', 'tight', 'Padding', 'tight');

ax1 = nexttile;
hLine1 = line(ax1,'Color', 'r');
title('FFT I+Q channel');
xlabel("[bin]");
ylabel("[dB]");

ax2 = nexttile;
hLine2 = line(ax2,'Color', 'r');
hLine22= line(ax2);
title('I+Q channel');
xlabel("n (sample)");
ylabel("ADC value");

ax3 = nexttile;
hLine3 = line(ax3, 'Color','g');
title('I channel - FFT');
xlabel("[bin]");
ylabel("[dB]");

ax4 = nexttile;
hLine4 = line(ax4, 'Color', 'r');
title('Q channel - FFT');
xlabel("[bin]");
ylabel("[dB]");



SerialName = "/dev/ttyACM0";
SerialObj = serialport(SerialName,1000000,"Timeout",5);
configureTerminator(SerialObj,"CR/LF");
[SysCommand, BasebandCommand, FrontendCommand, PLLCommand] = funkceSiRad();

flush(SerialObj); writeline(SerialObj, SysCommand);
% flush(SerialObj); writeline(SerialObj, BasebandCommand);
% flush(SerialObj); writeline(SerialObj, FrontendCommand);
% flush(SerialObj); writeline(SerialObj, PLLCommand);
% flush(SerialObj); writeline(SerialObj, "!S01046010");
flush(SerialObj); writeline(SerialObj, "!B20000016");
flush(SerialObj); writeline(SerialObj, "!F00075300");
flush(SerialObj); writeline(SerialObj, "!P000009C4");
flush(SerialObj);

function timerUpdate(~, ~, serial)
TRIGGER = '!N';
writeline(serial, TRIGGER);
end



triggerTimer = timer;
triggerTimer.StartDelay = 3;
triggerTimer.Period = 0.3;
triggerTimer.ExecutionMode = 'fixedSpacing';
triggerTimer.UserData = 0;

triggerTimer.TimerFcn = {@timerUpdate, SerialObj};
start(triggerTimer);

hFig.CloseRequestFcn = 'stop(triggerTimer); delete(SerialObj); closereq;';

oldBuf = [];
distanceBinWidth = 0.0416;
distanceNFFT = 128;
distance = (0:distanceNFFT-1)*distanceBinWidth;
distance = distance.^2;



while true
	buf = [];
	% buf = char(readline(SerialObj));
	% disp(buf);
	% 
	% if isempty(buf)
	% 	continue;
	% end
	% 
	% if ~strcmp(buf(1:2),'!M')
	% 	continue;
	% end
	% 
	% charData = buf(13:end);
	% cislavCelle = split(charData, char(9));
	% cisla = cellfun(@str2double, cislavCelle);
	% 
	% nData = length(cisla);
	% DataI = cisla(1:2:nData);
	% DataQ = cisla(2:2:nData);
	% disp(size(DataI));
	%buf = 
	buf = fgets(SerialObj);
	%disp(size(lineWW));
	
	
	if(length(buf) == 40)
		continue;
	end

	process = [oldBuf buf];
	if length(process) ~= (4*distanceNFFT+11)
		if length(process) > (4*distanceNFFT+11)
			obj.oldBuf = [];
		else
			obj.oldBuf = buf;
		end
		continue;
	end
	obj.oldBuf = [];

	if(process(5) ~= 77)
		continue;
	end
	% disp(size(process));
	% dataCount = process(9)*256+process(8);
	% disp(dataCount);
	dataCount = distanceNFFT*2;
	tmp = process(11:2:(9+dataCount*2)) * 256+ process(10:2:(9+dataCount*2)) ;
	data = typecast(uint16(tmp), 'int16');
	nData = numel(data);
	DataI = double(data(1:2:nData));
	DataQ = double(data(2:2:nData));
	% disp(DataI);

	% 
	% DataIQ = DataI+1j*DataQ;
	% 
	% fftIQ = abs(fft(DataIQ));

	% time(index) =  (posixtime(datetime('now'))-dataTimestamp) * 1000;
	%dataTimestamp = posixtime(datetime('now'));
	% fprintf("Data %f\n", time(index) )

	fftI = abs(fft(DataI));
	fftQ= abs(fft(DataQ));
	%limI=floor((numel(fftI)/2));
	%limQ=floor((numel(fftQ)/2));

	set(hLine2, 'XData', 1:2:nData, 'YData', DataI);
	set(hLine22, 'XData', 2:2:nData, 'YData', DataQ);

	% set(hLine1, 'XData', 1:distanceNFFT, 'YData', fftIQ);
	set(hLine3, 'XData', 1:distanceNFFT, 'YData', fftI);
	set(hLine4, 'XData', 1:distanceNFFT, 'YData', fftQ);

	% index=index+1;
end

%stop(triggerTimer);
%delete(SerialObj);

%timeSorted=sort(time(10:end));
%onep=ceil(numel(timeSorted)/100);

% fprintf("Max time: %f\n", max(timeSorted));
% fprintf("Min time: %f\n", min(timeSorted));
% fprintf("Mean time: %f ms\n", mean(timeSorted));
% fprintf("Deviation: %f ms\n", std(timeSorted));
% fprintf("1 percent low %f ms\n", mean(timeSorted(1:onep)));
% fprintf("1 percent high %f ms\n", mean(timeSorted(numel(timeSorted)-onep:end)));
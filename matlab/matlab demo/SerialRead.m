clc;
% graph size
figureW = 1000; %1920;
figureH = 500; %954;
% graph position on screen
figureX = 15;
figureY = 18; % leave space for windows 10 task bar
dataI = [];
dataQ = [];

hFig = figure(1);
hFig.Position= [figureX figureY figureW figureH];
hFig.Name = 'The data';
tiledlayout(2,2, 'TileSpacing', 'tight', 'Padding', 'tight');
ax1 = nexttile;
hLine = line(ax1,'Color', 'r');
%hLine1= line(ax1);
title('FFT Q channel');
xlabel("[bin]");
ylabel("[dB]");
ax2 = nexttile;
% plot(ax2,dataQ);
hLine2 = line(ax2,'Color', 'r');
hLine22= line(ax2);
title('I+Q channel');
xlabel("n (sample)");
ylabel("ADC value");

ax3 = nexttile;
%plot(ax3, log(abs(fft(dataI))));
hLine3 = line(ax3, 'Color','g');
title('I channel - FFT');
xlabel("[bin]");
ylabel("[dB]");
ax4 = nexttile;
% plot(ax4, log(abs(fft(dataQ))));
hLine4 = line(ax4, 'Color', 'r');
title('Q channel - FFT');
xlabel("[bin]");
ylabel("[dB]");
% axis([ax1 ax2],[0 600 -80 80]);
% axis([ax3 ax4],[0 600 2 10]);



SerialName = "/dev/ttyACM0";
SerialObj = serialport(SerialName,1000000,"Timeout",5);
configureTerminator(SerialObj,"CR/LF");
[SysCommand, BasebandCommand, FrontendCommand, PLLCommand] = funkceSiRad();

flush(SerialObj); writeline(SerialObj, SysCommand);
flush(SerialObj); writeline(SerialObj, BasebandCommand);
flush(SerialObj); writeline(SerialObj, FrontendCommand);
flush(SerialObj); writeline(SerialObj, PLLCommand);
flush(SerialObj);% cte jenom do prvniho \n

function timerUpdate(~, ~, serial)
TRIGGER = '!N';
writeline(serial, TRIGGER);
end



triggerTimer = timer;
triggerTimer.StartDelay = 5;
triggerTimer.Period = 0.04;
triggerTimer.ExecutionMode = 'fixedSpacing';
triggerTimer.UserData = 0;

triggerTimer.TimerFcn = {@timerUpdate, SerialObj};
% start(triggerTimer);

hFig.CloseRequestFcn = 'stop(triggerTimer); delete(SerialObj); closereq;';

oldBuf = [];

dataTimestamp=posixtime(datetime('now'));

N=5000;
time = zeros(1,N);
index=1;
while index <= N
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
	
	%buf = fgets(SerialObj); % old API
	buf = [char(readline(SerialObj)) char(13) char(10)];

	if(length(buf) == 40)
		continue;
	end
	
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
	tmp = process(11:2:(9+dataCount*2)) * 256 + process(10:2:(9+dataCount*2));
	data = typecast(uint16(tmp), 'int16');
	nData = numel(data);
	DataI = data(1:2:nData);
	DataQ = data(2:2:nData);


	% time(index) =  (posixtime(datetime('now'))-dataTimestamp) * 1000;
	%dataTimestamp = posixtime(datetime('now'));
    % fprintf("Data %f\n", time(index) )

	fftI = 20*log10(abs(fft(DataI)));
	fftQ= 20*log10(abs(fft(DataQ)));
	limI=floor((numel(fftI)/2));
	limQ=floor((numel(fftQ)/2));
	set(hLine2, 'XData', 1:2:nData, 'YData', DataI);
	set(hLine22, 'XData', 2:2:nData, 'YData', DataQ);
	set(hLine3, 'XData', 1:limI, 'YData', fftI(1:limI));
	set(hLine4, 'XData', 1:limQ, 'YData', fftQ(1:limQ));

	index=index+1;
end

stop(triggerTimer);
delete(SerialObj);

timeSorted=sort(time(10:end));
onep=ceil(numel(timeSorted)/100);

fprintf("Max time: %f\n", max(timeSorted));
fprintf("Min time: %f\n", min(timeSorted));
fprintf("Mean time: %f ms\n", mean(timeSorted));
fprintf("Deviation: %f ms\n", std(timeSorted));
fprintf("1 percent low %f ms\n", mean(timeSorted(1:onep)));
fprintf("1 percent high %f ms\n", mean(timeSorted(numel(timeSorted)-onep:end)));
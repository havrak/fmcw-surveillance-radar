PARSE_DATA=0;

Nframes=1e4;
NFFT=1001;
frequencies=zeros(NFFT);
spectrograms=zeros(Nframes, NFFT);
segLen=82e-6; % 82 us
time=(0:Nframes-1)*segLen;

chunkLen=1003;

if PARSE_DATA ==1
    fprintf("Parsing from scratch\n");
    file = fopen("../data/250105-spectrogram_start.DAT");


    preambleLines = 29;
    for i = 1:preambleLines
        fgetl(file);
    end


    i=1;
    while ~feof(file)    
        chunk = textscan(file,"%s", chunkLen, "Delimiter", '\n'); 
        data = split(chunk{1}, ';');
        values=str2double(data(3:end, 1:2));
        frequencies=values(:,1);
        spectrograms(i,:) = values(:,2)';
        i = i+1;
    end
    save('spectrograms_start.mat', 'frequencies', 'spectrograms')
else
    fprintf("Loading parsed data from file\n");
    load('spectrograms_start.mat');
end


fprintf("Data loaded\n");

figure;
timeWindow = 150e-3; 
step = round(timeWindow / segLen / 10);
idx = time < timeWindow; 

figure;
imagesc(frequencies/1e6, time(idx)*1e3, spectrograms(idx, :)); 
axis xy;
colormap(jet); 
colorbar; 
xlabel('Frequency (MHz)');
ylabel('Time (ms)');
title('Waterfall Diagram');


%% Processing
figure;

axis xy;
colormap(jet); 
colorbar; 
xlabel('Frequency (MHz)');
ylabel('Time (ms)');
title('Waterfall Diagram');




frequenciesRange = frequencies < 24.0005e9 & frequencies > 23.999e9;

specStart=spectrograms(:, frequenciesRange);
specStart(specStart < -40) = 0;

imagesc(frequencies(frequenciesRange,:)/1e6, time(:)*1e3, specStart(:, :)); 

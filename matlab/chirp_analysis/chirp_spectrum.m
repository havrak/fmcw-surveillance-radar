file = readlines("../data/250105-chirp_spectrum.DAT");

data = split(file(31:end-1), ';');

frequency=str2double(data(:,1))/1e6;
data1=str2double(data(:,2));
data2=str2double(data(:,3));


tiledlayout(2,1);
nexttile;
plot(frequency, data1);
title("Power [dB]");
xlabel("Frequency (MHz)");

nexttile;
plot(frequency, data2);
title("Data 2");
xlabel("Frequency (MHz)");
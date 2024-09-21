clc; clear;
serialFound=[dir(fullfile("/dev/",'ttyACM*')); dir(fullfile("/dev/",'ttyUSB*'))];
serialFound(:).name
names={{serialFound(:).name}  {serialFound(:).folder}};

names{1,:}

%con(1:numel(dirs))=dirs{1:numel(dirs)}+names{1:numel(dirs)}
classdef radarBuffer < handle
    properties
        bufferSize = 32       % Number of samples per sliding batch
        FFTData               % Complex FFT data [bufferSize x numSamplesPerChirp]
				timestamps            % Timestamps for each sample [bufferSize x 1]
        currentIdx = 1        % Index for circular buffer (index, of next item)
    end

    methods
        function obj = radarBuffer(bufferSize, numSamplesPerChirp)
            obj.bufferSize = bufferSize;
            obj.FFTData = complex(zeros(bufferSize, numSamplesPerChirp));
            obj.timestamps = zeros(bufferSize, 1);
        end

        function addChirp(obj, I, Q, timestamp)
            obj.FFTData(obj.currentIdx, :) = fft(I + 1j*Q);
            obj.timestamps(obj.currentIdx) = timestamp;
            obj.currentIdx = mod(obj.currentIdx, obj.bufferSize) + 1;
        end

        function [batchFFTs, batchTimes] = getSlidingBatch(obj)
            idxs = mod((obj.currentIdx-1 : obj.currentIdx+obj.bufferSize-2), obj.bufferSize) + 1;
            batchFFTs = obj.FFTData(idxs, :);
            batchTimes = obj.timestamps(idxs);
				end
    end
end
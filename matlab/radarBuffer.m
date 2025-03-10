classdef radarBuffer < handle
    properties
        bufferSize = 32       % Number of samples per sliding batch
        IQData                % Complex I/Q data [bufferSize x numSamplesPerChirp]
        Timestamps            % Timestamps for each sample [bufferSize x 1]
        currentIdx = 1        % Index for circular buffer
    end

    methods
        function obj = radarBuffer(bufferSize, numSamplesPerChirp)
            obj.bufferSize = bufferSize;
            obj.IQData = complex(zeros(bufferSize, numSamplesPerChirp));
            obj.Timestamps = zeros(bufferSize, 1);
        end

        function addChirp(obj, I, Q, timestamp)
            % Slide the buffer: Overwrite oldest entry with new chirp
            obj.IQData(obj.currentIdx, :) = I + 1j*Q;
            obj.Timestamps(obj.currentIdx) = timestamp;
            obj.currentIdx = mod(obj.currentIdx, obj.bufferSize) + 1;
        end

        function [batchIQ, batchTimes] = getSlidingBatch(obj)
            % Retrieve the latest contiguous batch of size bufferSize
            idxs = mod((obj.currentIdx-1 : obj.currentIdx+obj.bufferSize-2), obj.bufferSize) + 1;
            batchIQ = obj.IQData(idxs, :);
            batchTimes = obj.Timestamps(idxs);
        end
    end
end
classdef radarBuffer < handle
    properties
        bufferSize = 12       % Number of samples per sliding batch
        FFTData               % Complex FFT data [bufferSize x numSamplesPerChirp]
				timestamps            % Timestamps for each sample [bufferSize x 1]
        currentIdx = 1        % Index for circular buffer (index, of next item)
				rangeNFFT;
				wn;

		end
		properties(Access=public)
				lastProcesingYaw = 0;
				lastProcesingPitch = 0;
		end

    methods
        function obj = radarBuffer(bufferSize, rangeNFFT)
            obj.bufferSize = bufferSize;
						obj.rangeNFFT = rangeNFFT;
						obj.wn=hamming(rangeNFFT);
            obj.FFTData = complex(zeros(rangeNFFT,bufferSize));
            obj.timestamps = zeros(bufferSize, 1);
        end

        function addChirp(obj, I, Q, timestamp)
            obj.FFTData(:, obj.currentIdx) = fft((I + 1j*Q).*obj.wn, obj.rangeNFFT);
						obj.timestamps(obj.currentIdx) = timestamp;
            obj.currentIdx = mod(obj.currentIdx, obj.bufferSize) + 1;
        end

        function [batchFFTs, batchTimes] = getSlidingBatch(obj)
            idxs = mod((obj.currentIdx-1 : obj.currentIdx+obj.bufferSize-2), obj.bufferSize) + 1;
            batchFFTs = obj.FFTData(:,idxs);
            batchTimes = obj.timestamps(idxs);
				end

				function [minTime, maxTime] = getTimeInterval(obj)
					minTime = min(obj.timestamps);
					maxTime = max(obj.timestamps);
				end

    end
end

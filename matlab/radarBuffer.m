classdef radarBuffer < handle
	% radarBuffer: Manages circular buffer for radar FFT data and timestamps
	%
	% Stores chirp data (I/Q components) as windowed FFTs, tracks timestamps,
	% and provides access to this data.

	properties
		bufferSize = 12       % Number of samples per sliding batch
		FFTData               % Complex FFT data [bufferSize x rangeNFFT]
		timestamps            % Timestamps for each sample [bufferSize x 1]
		currentIdx = 1        % Index for circular buffer (index, of next item)
		rangeNFFT;            % Number of FFT points
		wn;                   % weighting window, currently hamming
	end

	methods
		function obj = radarBuffer(bufferSize, rangeNFFT)
			% radarBuffer: Initializes the radarBuffer with specified buffer size and FFT parameters
			%
			% Inputs:
			%   bufferSize ... Size of the circular buffer
			%   rangeNFFT ... Number of FFT points for range processing
			%
			% Output:
			%   obj ... Initialized radarBuffer instance
			obj.bufferSize = bufferSize;
			obj.rangeNFFT = rangeNFFT;
			obj.wn=hamming(rangeNFFT);
			obj.FFTData = complex(zeros(rangeNFFT,bufferSize));
			obj.timestamps = zeros(bufferSize, 1);
		end

		function addChirp(obj, I, Q, timestamp)
			% addChirp: Processes and adds a chirp signal to the buffer
			%
			% Inputs:
			%   I ... In-phase component of the chirp signal
			%   Q ... Quadrature component of the chirp signal
			%   timestamp ... Timestamp associated with the chirp
			obj.FFTData(:, obj.currentIdx) = fft((I + 1j*Q).*obj.wn, obj.rangeNFFT);
			obj.timestamps(obj.currentIdx) = timestamp;
			obj.currentIdx = mod(obj.currentIdx, obj.bufferSize) + 1;
		end

		function [batchFFTs, batchTimes] = getSlidingBatch(obj)
			% getSlidingBatch: Retrieves the latest contiguous batch of FFT data and timestamps
			%
			% Outputs:
			%   batchFFTs ... FFT data matrix [rangeNFFT x bufferSize]
			%   batchTimes ... Timestamps vector [bufferSize x 1]
			idxs = mod((obj.currentIdx-1 : obj.currentIdx+obj.bufferSize-2), obj.bufferSize) + 1;
			batchFFTs = obj.FFTData(:,idxs);
			batchTimes = obj.timestamps(idxs);
		end

		function [batchFFTs, batchTimes] = getSlidingBatchOld(obj)
			% getSlidingBatchOld: Retrieves the latest contiguous batch of FFT data and
			% timestamps, last added spectrum will not be present
			%
			% Outputs:
			%   batchFFTs ... FFT data matrix [rangeNFFT x bufferSize]
			%   batchTimes ... Timestamps vector [bufferSize x 1]
			idxs = mod((obj.currentIdx : obj.currentIdx+obj.bufferSize-2), obj.bufferSize) + 1;
			batchFFTs = obj.FFTData(:,idxs);
			batchTimes = obj.timestamps(idxs);
		end

		function [minTime, maxTime] = getTimeInterval(obj)
			% getTimeInterval: Returns the minimum and maximum timestamps in the buffer
			%
			% Outputs:
			%   minTime ... Earliest timestamp in the buffer
			%   maxTime ... Latest timestamp in the buffer
			minTime = min(obj.timestamps);
			maxTime = max(obj.timestamps);
		end

	end
end

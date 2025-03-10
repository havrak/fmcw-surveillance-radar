classdef dataProcessor < handle
	properties
		hRadar                  % Radar object
		hPlatform               % Platform object
		parallelPool            % Parallel pool handle
		future                  % Asynchronous task handle
		isProcessing = false    % Flag to prevent overlap
		readIdx = 0;
	end

	methods
		function obj = dataProcessor(radarObj, platformObj)
			obj.hRadar = radarObj;
			obj.hPlatform = platformObj;
			obj.parallelPool = gcp('nocreate'); % Start parallel pool
			if isempty(obj.parallelPool)
				obj.parallelPool = parpool(1); % Start pool with 1 worker
			end

			 addlistener(radarObj, 'newDataAvailable', @(~,~) obj.onNewDataAvailable());
		end

		function onNewDataAvailable(obj)
			% Called automatically when radar data arrives
			% TODO: add chirp to buffer -> run processing on buffer 
			% on addition to chirps processing will also get data about positions


			%
			if ~obj.isProcessing
				obj.isProcessing = true; % Lock processing

				I = obj.hRadar.bufferI(obj.readIdx);
				Q = obj.hRadar.bufferQ(obj.readIdx);
				timestamp = obj.hRadar.bufferTime(obj.readIdx);

				obj.future = parfeval(obj.parallelPool, ...
					@obj.backgroundProcess, 3, I, Q, timestamp);

				afterAll(obj.future, @(varargin) obj.postProcess(varargin{:}), 0);
			end

		end

		function backgroundProcess(~, I, Q, t)
			% here run generic processing that will be agnostic to visualization
			% method
			% pair data with positions, run CFAR
			sig = I + 1i*Q;

			end

		function postProcess(obj, readIdx)
			% run CFAR, do visiaulization every once in a while
			
			obj.hRadar.readIdx = mod(readIdx, obj.hRadar.bufferSize) + 1;
			obj.isProcessing = false;

			% Update plots  
		end
	end
end
classdef radarDataCube < handle
	% RADARDATACUBE Manages 4D radar data cube  for raw and CFAR-processed data
	%
	% Stores radar data in a 4D matrix (Yaw x Pitch x Fast Time x Slow Time),
	% handles batch processing, and applies spreading/decay patterns.
	% Similarly CFAR data is stored in 3D matrix (Range x Yaw x Pitch).

	properties(Access=private)
		spreadPattern;         % Weighting matrix for data spreading [Yaw x Pitch]
		bufferA = struct(...   % Active buffer for batch data
			'timestamp', [], 'yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []);
		bufferB = struct(...    % Secondary buffer for processing
			'timestamp', [], 'yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []);
		bufferActive;          % Currently active buffer (A/B)
		bufferActiveWriteIdx = 1; % Write index for active buffer
		batchSize = 6;          % Number of samples per batch

		decay = true;           % Enable/disable data decay over time
		requestToZero = false;  % Flag to zero cubes after processing
		isProcessing = false;   % Batch processing status flag
		overflow = false;       % Buffer overflow flag
		keepRaw;                % Flag to retain raw data
		keepCFAR;               % Flag to retain CFAR data
		parallelPool;           % Thread pool
		lastYaw                 % last updated yaw angle
		lastPitch               % last updated pitch angle
		relativeTimestamp;           % relative timestamp to measure against
	end

	properties(Access=public)
		yawBinMin=0;           % Minimum yaw angle (degrees)
		yawBinMax=359;         % Maximum yaw angle (degrees)
		yawBins;               % Yaw angle bins with 1 deg resolution
		pitchBinMin=-20;       % Minimum pitch angle (degrees)
		pitchBinMax=60;        % Maximum pitch angle (degrees)
		pitchBins;             % Pitch angle bins with 1 deg resolution

		rawCube = [];          % 4D raw data matrix [Yaw x Pitch x (Fast Time x Slow Time)]
		rawCubeMap = [];       % Memory map for rawCube data
		rawCubeSize = [];      % Dimensions of rawCube [Range x Doppler x Yaw x Pitch]

		cfarCube = [];         % 4D CFAR data matrix [Yaw x Pitch x (Fast Time x Slow Time)]
		cfarCubeMap = [];      % Memory map for cfarCube data
		cfarCubeSize = [];     % Dimensions of cfarCube [Range x Yaw x Pitch]

	end

	events
		updateFinished          % called after processBatch function finishes
	end

	methods(Static)

		function mask = createSectorMask(diffYaw, diffPitch, patternSize, speed)
			% CREATESECTORMASK Generates a mask that will keep data in area we are
			% moving from and use just new in the area we are moving to
			%
			% Inputs:
			%   diffYaw ... Yaw angle difference from previous batch
			%   diffPitch ... Pitch angle difference from previous batch
			%   patternSize ... Size of the pattern matrix [rows x cols]
			%   speed ... Motion speed (for dynamic pattern adjustment)
			%
			% Output:
			%   mask ... Sector mask matrix [patternSize x patternSize]

			if speed == 0
				mask = zeros(patternSize);
				return;
			end
			epsilon = 1e-6;
			effectiveSpeed = max(speed, epsilon);

			[yawGrid, pitchGrid] = meshgrid(...
				linspace(-1, 1, patternSize(1)), ...
				linspace(-1, 1, patternSize(2)) ...
				);

			moveAngle = atan2d(diffPitch, -diffYaw);
			sectorWidth = 30 + 150 * exp(-speed/5); % upon stationary we should get 360 -> null whole area

			gridAngles = atan2d(pitchGrid, yawGrid);
			angleDiff = abs(mod(gridAngles - moveAngle + 180, 360) - 180);

			lineVector = [cosd(moveAngle), sind(moveAngle)];
			perpendicularDistances = abs(pitchGrid * lineVector(1) - yawGrid * lineVector(2));

			mask = zeros(patternSize);
			sectorMask = angleDiff <= sectorWidth;
			dropOffFactor = 1 / (effectiveSpeed^2);

			mask(sectorMask) = exp(- (perpendicularDistances(sectorMask).^2) * dropOffFactor);

			maxVal = max(mask(:));
			if maxVal > 0
				mask = mask ./ maxVal;
			end
		end

		function allocateRadarCubeFile(sizeArray, fileName)
			% ALLOCATERADARCUBEFILE Ensure binary file exists with sufficient space for radar data
			%
			% Creates a binary file with space for 1.2×proud(sizeArray) elements
			%
			% Inputs:
			%   sizeArray  - Base dimensions of the data cube [numRangeBins, numDopplerBins, ...]
			%   fileName   - Path to the output binary file
			%

			expandedSize = sizeArray;
			expandedSize(1) = floor(sizeArray(1) * 1.2);

			% Check file status
			fileExists = exist(fileName, 'file') == 2;
			needsCreation = ~fileExists;

			if fileExists
				fileInfo = dir(fileName);
				needsCreation = (fileInfo.bytes < prod(expandedSize)*4);
			end

			% Create or recreate file if necessary
			if needsCreation
				dummyData = randn(expandedSize, 'single'); % Generate random data (prevents filesystem compression)

				fid = fopen(fileName, 'w');
				fwrite(fid, dummyData, 'single');
				fclose(fid);
			end
		end

		function [lastYawIdx, lastPitchIdx] = processBatch(buffer, spreadPattern, rawCubeSize, yawBins, pitchBins, processRaw, processCFAR, decay)
			% PROCESSBATCH Applies batch updates to raw/CFAR cubes with spreading/decay
			%
			% in case spread pattern is enabled range-doppler map will be spread over a
			% larger area, otherwise just single angle is updated. This is the only
			% behavior for CFAR.
			%
			% Keep in mind this function accesses same memory space for cube objects as
			% the main thread, ensure that no functions access cubes when processBatch runs
			%
			% Inputs:
			%   buffer ... Batch data structure
			%   spreadPattern ... Weighting pattern for data spreading
			%   rawCubeSize ... Dimensions of rawCube
			%   yawBins ... Yaw angle bins
			%   pitchBins ...Pitch angle bins
			%   processRaw ... Flag to process raw data
			%   processCFAR ... Flag to process CFAR data
			%   decay ... Flag to enable decay
			% Outputs
			%   lastYawIdx ... index of last yaw position in the buffer
			%   lastPitchIdx ... index of last pitch position in the buffer

			timestamps = [buffer.timestamp];

			% Check if the buffer is not empty
			if ~isempty(timestamps)
				% Sort timestamps in ascending order (oldest to latest) and get indices
				[~, sortedIndices] = sort(timestamps);
			else
				sortedIndices = [];
			end

			lastYawIdx = buffer.yawIdx(sortedIndices(end));
			lastPitchIdx = buffer.pitchIdx(sortedIndices(end));

			%% Updating cube for raw data
			if(processRaw && isempty(spreadPattern))

				rawCube = memmapfile('rawCube.dat', ...
					'Format', {'single', rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				if(decay)
					batchDecay = single(prod([buffer.decay]));
					decayCube_avx2(rawCube.Data.rawCube, batchDecay);
				end

				for i = sortedIndices
					yaw = buffer.yawIdx(i);
					pitch = buffer.pitchIdx(i);
					contribution = buffer.rangeDoppler(:, :, i);

					if(decay)
						batchDecay = single(prod(buffer.decay(i:end)));
						decayCube_avx2(contribution, batchDecay);
					end


					rawCube.Data.rawCube(:, :, yaw, pitch) = contribution;
				end

			elseif(processRaw)

				rawCube = memmapfile('rawCube.dat', ...
					'Format', {'single', rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				% --- 1. Calculate composite region ---
				halfYaw = floor(size(spreadPattern, 1)/2);
				halfPitch = floor(size(spreadPattern, 2)/2);
				numYawBins = length(yawBins);

				% Get raw min/max indices
				% --- 1.1 Expand Yar range for each update ---
				%  we need to find most distant elements -> let's label them min and
				%  max (don't correspond to actual minimums)
				%  max + halfYaw and min - halfYaw are ok -> great
				%  max + halfYaw results in overflow, min-halfYaw doesn't -> create array max-halfYaw->end and 1-> min+halfYaw
				%  ...
				%  -> calculate all intervals we want to fill in
				%  -> contract into larger array and remove duplicate elements, mode
				%  that array
				%  find breaks in this array (wrap fro 360 to 1) -> fill in elements
				expandedYaws = arrayfun(@(y) [(y-halfYaw):(y+halfYaw)], [buffer.yawIdx], 'UniformOutput', false);
				allYaws = unique(mod(cat(2, expandedYaws{:}) - 1, numYawBins) + 1);
				% Split into contiguous segments if wrap-around exists
				wrapIdx = find(diff(allYaws) < 0);
				if ~isempty(wrapIdx)
					yawIndices = [allYaws(1:wrapIdx), allYaws(wrapIdx+1:end)];
				else
					yawIndices = allYaws;
				end

				% Clamp pitch indices
				minPitch = max(1, min([buffer.pitchIdx]) - halfPitch);
				maxPitch = min(length(pitchBins), max([buffer.pitchIdx]) + halfPitch);
				pitchIndices = minPitch:maxPitch;


				% --- 2. Initialize subrawCube for new contributions ---
				subCube = zeros(...
					rawCubeSize(1), ...
					rawCubeSize(2), ...
					length(yawIndices), ...
					length(pitchIndices), ...
					'single' ...
					);

				% --- 3. Apply updates into subrawCube ---
				yawMap = containers.Map('KeyType', 'double', 'ValueType', 'double');
				for i = 1:length(yawIndices)
					yawMap(yawIndices(i)) = i; % Maps original yaw index to subrawCube position
				end

				for i = sortedIndices

					yaw = buffer.yawIdx(i);
					pitch = buffer.pitchIdx(i);

					% --- 3.1 Reindex from rawCube into subrawCube ---
					validYaw = mod((yaw - halfYaw : yaw + halfYaw) - 1, length(yawBins)) + 1;
					localYaw = arrayfun(@(x) yawMap(x), validYaw);

					validPitch = max(1, pitch - halfPitch):min(length(pitchBins), pitch + halfPitch);
					localPitchOffset = validPitch(1) - minPitch + 1;
					localPitch = localPitchOffset : (localPitchOffset + length(validPitch) - 1);

					% --- 3.2 Adjust pattern ---
					startPitchPat = max(1,(halfPitch+1)-(pitch-validPitch(1)));
					endPitchPat = min(size(spreadPattern, 2), startPitchPat + length(validPitch) - 1);
					adjPattern = spreadPattern(:, startPitchPat:endPitchPat)*prod(buffer.decay(i:end));

					% --- 3.3 Spread contribution into 4D with pattern ---
					rangerDoppler = buffer.rangeDoppler(:, :, i);
					if(decay)
						batchDecay = single(prod([buffer.decay(i:end)]));
						decayCube_avx2(rangerDoppler, batchDecay);
					end
					contribution = applyPattern(adjPattern, rangerDoppler);


					% --- 3.4 Update subrawCube with contribution ---
					%%subCube(:, :, localYaw, localPitch) = ...
					%	subCube( :, :, localYaw, localPitch) + contribution;
					updateCube(subCube, contribution, localYaw, localPitch);

				end

				% --- 4. Decay rawCube ---
				if(decay)
					batchDecay = single(prod([buffer.decay]));
					decayCube_avx2(rawCube.Data.rawCube, batchDecay); % speed varies widely, probalby due to non consistent memory managment
				end

				% --- 5. Merge data ---
				updateCube(rawCube.Data.rawCube, subCube, yawIndices, pitchIndices);
				% m.Data.rawCube(:, :,yawIndices, pitchIndices) = m.Data.rawCube( :, :,yawIndices, pitchIndices) + subCube;
			end

			%% Updating cube for CFAR data
			if(processCFAR)

				cfarCubeSize=rawCubeSize([1 3 4]);
				cfarCube = memmapfile('cfarCube.dat', ...
					'Format', {'single', cfarCubeSize, 'cfarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				if(decay)
					batchDecay = single(prod([buffer.decay]));
					decayCube_avx2(cfarCube.Data.cfarCube, batchDecay);
				end


				for i = sortedIndices
					%			fprintf("CFAR | YAW=%f, PITCH=%f, SUM=%f\n",buffer.yawIdx(i), buffer.pitchIdx(i), sum(buffer.cfar(:, i)));
					contribution =  buffer.cfar(:, i);
					if(decay)
						batchDecay = single(prod(buffer.decay(i:end)));
						decayCube_avx2(contribution, batchDecay);
					end
					cfarCube.Data.cfarCube(:, buffer.yawIdx(i), buffer.pitchIdx(i)) = contribution;
				end
			end


		end
	end

	methods(Access=private)
		function afterBatchProcessing(obj, lastYawIdx, lastPitchIdx)
			% AFTERBATCHPROCESSING Post-batch processing callback
			%
			% In case zero was called while thread was processing cubes will be zeroed
			% form here

			obj.lastYaw = obj.yawBins(lastYawIdx);
			obj.lastPitch = obj.pitchBins(lastPitchIdx);
			obj.isProcessing = false;
			if obj.requestToZero
				obj.requestToZero = false;
				obj.zeroCubes();
			end

			fprintf("radarDataCube | updateFinished\n");
			notify(obj, 'updateFinished');
		end

		function generateSpreadPattern(obj, spreadPatternYaw, spreadPatternPitch)
			% GENERATESPREADPATTERN generate pattern used to sprtead range-doppelr map
			% over a larger area
			%
			% output pattern is (2*spreadPatternYaw+1) x (2*spreadPatternPitch+1)
			%
			% Inputs:
			%   spreadPatternYaw .... Yaw pattern half-width
			%   spreadPatternPitch ... Pitch pattern half-width

			dimensionsYaw = -spreadPatternYaw:spreadPatternYaw;
			dimensionsPitch = -spreadPatternPitch:spreadPatternPitch;
			yawSigma = 3*spreadPatternYaw / (sqrt(8*log(2)));
			pitchSigma = 1.5*spreadPatternPitch / (sqrt(8*log(2)));
			[yawMash, pitchMesh] = meshgrid(dimensionsPitch,dimensionsYaw);
			obj.spreadPattern = single(exp(-0.5*( (yawMash/yawSigma).^2 + (pitchMesh/pitchSigma).^2 )));
			%imagesc(dimensionsYaw, dimensionsPitch, pattern);
		end

	end

	methods(Access=public)


		function obj = radarDataCube(numRangeBins, numDopplerBins, batchSize,  spreadPatternYaw, spreadPatternPitch, keepRaw, keepCFAR, decay)
			% RADARDATACUBE Initializes radar data cube and associated buffers
			%
			% Inputs:
			%   numRangeBins ... Number of range bins
			%   numDopplerBins ... Number of Doppler bins
			%   batchSize ... Samples per processing batch
			%   spreadPatternYaw ... Yaw spreading pattern width
			%   spreadPatternPitch ... Pitch spreading pattern width
			%   keepRaw ... Flag to retain raw data
			%   keepCFAR ... Flag to retain CFAR data
			%   decay ... Enable/disable data decay

			obj.yawBins = obj.yawBinMin:obj.yawBinMax;     % 1° resolution
			obj.pitchBins = obj.pitchBinMin:obj.pitchBinMax;          % 1° resolution
			obj.batchSize = batchSize;
			obj.bufferActive = obj.bufferA;
			obj.parallelPool = gcp('nocreate');
			obj.relativeTimestamp = tic;
			if(spreadPatternYaw == 0 || spreadPatternPitch == 0)
				obj.spreadPattern = [];
			else
				obj.generateSpreadPattern(spreadPatternYaw, spreadPatternPitch);
			end
			obj.keepRaw = keepRaw;
			obj.keepCFAR = keepCFAR;
			obj.decay = decay;
			obj.rawCubeSize = [ ...
				numRangeBins, ...
				numDopplerBins ...
				length(obj.yawBins), ...
				length(obj.pitchBins), ...
				];
			%% Initialize radar cube for raw data

			if obj.keepRaw
				radarDataCube.allocateRadarCubeFile(obj.rawCubeSize, 'rawCube.dat');
				fprintf("radarDataCube | radarDataCube | Initializing rawCube with yaw %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)

				obj.bufferA.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');
				obj.bufferB.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');

				% Create memory map
				obj.rawCubeMap = memmapfile('rawCube.dat', ...
					'Format', {'single', obj.rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);
				zeroCube(obj.rawCubeMap.Data.rawCube);
				obj.rawCube = obj.rawCubeMap.Data.rawCube;
			end

			%% Initialize radar cube for cfar

			if obj.keepCFAR
				obj.bufferA.cfar = zeros([numRangeBins, obj.batchSize], 'single');
				obj.bufferB.cfar = zeros([numRangeBins, obj.batchSize], 'single');
				obj.cfarCubeSize = obj.rawCubeSize([1 3 4]);
				radarDataCube.allocateRadarCubeFile(obj.cfarCubeSize, 'cfarCube.dat');

				% Create memory map
				obj.cfarCubeMap = memmapfile('cfarCube.dat', ...
					'Format', {'single', obj.rawCubeSize([1 3 4]), 'cfarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);


				zeroCube(obj.cfarCubeMap.Data.cfarCube);
				obj.cfarCube = obj.cfarCubeMap.Data.cfarCube;
			end
		end

		function addData(obj, yaw, pitch, cfar, rangeDoppler, speed)
			% ADDDATA Adds radar detection to the active buffer
			%
			% Inputs:
			%   yaw ... Yaw angle of detection (degrees)
			%   pitch ... Pitch angle of detection (degrees)
			%   cfar ... CFAR detection vector
			%   rangeDoppler ... Range-Doppler matrix
			%   speed .... Motion speed for decay calculation (optional)

			if nargin < 6
				speed = 0.01;
			end

			% disp(sum(cfar))
			[~, yawIdx] = min(abs(obj.yawBins - yaw));
			[~, pitchIdx] = min(abs(obj.pitchBins - pitch));
			decayCoef = exp(-speed/500);
			% fprintf("radarDataCube | addData | adding to rawCube %d: yaw %f, pitch %f, decay %f\n", obj.bufferActiveWriteIdx, yaw, pitch, decayCoef);

			obj.bufferA.timestamp(obj.bufferActiveWriteIdx) = toc(obj.relativeTimestamp);
			obj.bufferA.decay(obj.bufferActiveWriteIdx) = decayCoef;
			obj.bufferA.yawIdx(obj.bufferActiveWriteIdx) = yawIdx;
			obj.bufferA.pitchIdx(obj.bufferActiveWriteIdx) = pitchIdx;

			if(obj.keepRaw)
				obj.bufferA.rangeDoppler(:, :, obj.bufferActiveWriteIdx) = single(rangeDoppler);
			end

			if(obj.keepCFAR)
				obj.bufferA.cfar(:, obj.bufferActiveWriteIdx) = single(cfar);
			end

			obj.bufferActiveWriteIdx = obj.bufferActiveWriteIdx + 1;
			if obj.bufferActiveWriteIdx > obj.batchSize
				obj.overflow = true;
				obj.bufferActiveWriteIdx = 1; % this means buffer will not be ordered
			end

		end

		function [yaw, pitch] = getLastPosition(obj)
			% GETLASTPOSITION return position of the last update
			%
			% Outputs:
			%   yaw ... yaw angle  (0-360°)
			%   pitch ... Pitch angle (-90°-90°)

			yaw = obj.lastYaw;
			pitch = obj.lastPitch;
		end
		function status = isBatchFull(obj)
			% ISBATCHFULL Checks if the active buffer is ready for processing
			%
			% Output:
			%   status ... True if buffer is full or overflowed

			status = obj.overflow;
		end

		function startBatchProcessing(obj)
			% STARTBATCHPROCESSING Initiates asynchronous batch processing
			%
			% internal buffers switch their place - one used for buffering will now be
			% processed while new data will be coming into second buffer

			if obj.isProcessing
				fprintf("radarDataCube | startBatchProcessing | already processing\n");
				return;
			end



			obj.isProcessing = true;
			obj.overflow = false;

			processingBuffer = obj.bufferA;
			obj.bufferA = obj.bufferB; % Reset active buffer
			obj.bufferB = processingBuffer; % Assign to processing buffer
			obj.bufferActiveWriteIdx = 1;


			if obj.parallelPool.NumWorkers > obj.parallelPool.Busy

				fprintf("radarDataCube | startBatchProcessing | starting processing\n");
				% NOTE: main thread execution for debug

				future = parfeval(gcp, ...
					@radarDataCube.processBatch, ...
					2, ...
					processingBuffer, ...
					obj.spreadPattern, ...
					obj.rawCubeSize, ...
					obj.yawBins, ...
					obj.pitchBins, ...
					obj.keepRaw, ...
					obj.keepCFAR, ...
					obj.decay);

				afterAll(future, @(varargin) obj.afterBatchProcessing(varargin{:}), 0);

				% [lastYawIdx, lastPitchIdx] = radarDataCube.processBatch( ...
				% 	processingBuffer, ...
				% 	obj.spreadPattern, ...
				% 	obj.rawCubeSize, ...
				% 	obj.yawBins, ...
				% 	obj.pitchBins, ...
				% 	obj.keepRaw, ...
				% 	obj.keepCFAR, ...
				% 	obj.decay);
				% obj.afterBatchProcessing(lastYawIdx, lastPitchIdx);

			else
				fprintf("radarDataCube | startBatchProcessing | pool empty\n");

				obj.afterBatchProcessing();
			end


		end

		function zeroCubes(obj)
			% ZEROCUBES Resets rawCube and cfarCube to zero
			%
			% If processing is ongoing, queues zeroing after completion

			if (obj.isProcessing)
				obj.requestToZero = true;
				return;
			end

			if obj.keepCFAR
				zeroCube(obj.cfarCube)
			end
			if obj.keepRaw
				zeroCube(obj.rawCube)
			end
		end

	end
end

classdef radarDataCube < handle
	% radarDataCube: Manages 4D radar data cube  for raw and CFAR-processed data
	%
	% Stores radar data in a 4D matrix (Yaw x Pitch x Fast Time x Slow Time),
	% handles batch processing, and applies spreading/decay patterns.
	% Similarly CFAR data is stored in 3D matrix (Range x Yaw x Pitch).

	properties(Access=public)
		yawBinMin=0;           % Minimum yaw angle (degrees)
		yawBinMax=359;         % Maximum yaw angle (degrees)
		yawBins;               % Yaw angle bins with 1 deg resolution
		pitchBinMin=-20;       % Minimum pitch angle (degrees)
		pitchBinMax=60;        % Maximum pitch angle (degrees)
		pitchBins;             % Pitch angle bins with 1 deg resolution

		rawCube;               % 4D raw data matrix [Yaw x Pitch x (Fast Time x Slow Time)]
		rawCubeMap;            % Memory map for rawCube data
		rawCubeSize;           % Dimensions of rawCube [Range x Doppler x Yaw x Pitch]

		cfarCube;              % 4D CFAR data matrix [Yaw x Pitch x (Fast Time x Slow Time)]
		cfarCubeMap;           % Memory map for cfarCube data
		cfarCubeSize;          % Dimensions of cfarCube [Range x Yaw x Pitch]

		spreadPattern;         % Weighting matrix for data spreading [Yaw x Pitch]
		bufferA = struct(...   % Active buffer for batch data
			'yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []);
		bufferB = struct(...    % Secondary buffer for processing
			'yawIdx', [], 'pitchIdx', [], 'rangeDoppler', [], 'cfar', [], 'decay', []);
		bufferActive;          % Currently active buffer (A/B)
		bufferActiveWriteIdx = 1; % Write index for active buffer
		batchSize = 6;          % Number of samples per batch

		decay = true;           % Enable/disable data decay over time
		requestToZero = false;  % Flag to zero cubes after processing
		isProcessing = false;   % Batch processing status flag
		overflow = false;       % Buffer overflow flag
		keepRaw;                % Flag to retain raw data
		keepCFAR;               % Flag to retain CFAR data
	end

	events
		updateFinished          % called after processBatch function finishes
	end

	methods(Static)

		function mask = createSectorMask(diffYaw, diffPitch, patternSize, speed)
			% createSectorMask: Generates a mask that will keep data in area we are
			% moving from and use just new in the area we are moving to
			%
			% NOT USED ANYWHERE
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

		%function testFunction()
		%	% testFunction: placeholder function to execute in parfev
		%	disp("Test");
		%end

		function processBatch(buffer, spreadPattern, rawCubeSize, yawBins, pitchBins, processRaw, processCFAR, decay)
			% processBatch: Applies batch updates to raw/CFAR cubes with spreading/decay
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

			%% Updating cube for raw data
			if(processRaw && isempty(spreadPattern))
				m = memmapfile('rawCube.dat', ...
					'Format', {'single', rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				if(decay)
					batchDecay = single(prod([buffer.decay]));
					scripts.decayCube_avx2(m.Data.rawCube, batchDecay);
				end

				for i = 1:numel(buffer.yawIdx)
					yaw = buffer.yawIdx(i);
					pitch = buffer.pitchIdx(i);
					contribution = buffer.rangeDoppler(:, :, i);

					if(decay)
						batchDecay = single(prod(buffer.decay(i:end)));
						scripts.decayCube_avx2(contribution, batchDecay);
					end
					m.Data.rawCube(:, :, yaw, pitch) = contribution;
				end
			elseif(processRaw)
				m = memmapfile('rawCube.dat', ...
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
				%  max (don't correspond to actuall minimums)
				%  max + halfYaw and min - halfYaw are ok -> great
				%  max + halfYaw results in overflow, min-halfYaw doesn't -> create array max-halfYaw->end and 1-> min+halfYaw
				%  ...
				%  -> calculate all intervals we want to fill in
				%  -> concart into larger array and remove duplicat elements, mode
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

				for i = 1:numel(buffer.yawIdx)
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
						scripts.decayCube_avx2(rangerDoppler, batchDecay);
					end
					contribution = scripts.applyPattern(adjPattern, rangerDoppler);



					% --- 3.4 Update subrawCube with contribution ---
					%%subCube(:, :, localYaw, localPitch) = ...
					%	subCube( :, :, localYaw, localPitch) + contribution;
					scripts.updateCube(subCube, contribution, localYaw, localPitch);
				end


				% --- 4. Decay rawCube ---
				if(decay)
					batchDecay = single(prod([buffer.decay]));
					scripts.decayCube_avx2(m.Data.rawCube, batchDecay); % speed varies widely, probalby due to non consistent memory managment
				end

				% --- 5. Merge data ---
				scripts.updateCube(m.Data.rawCube, subCube, yawIndices, pitchIndices);
				% m.Data.rawCube(:, :,yawIndices, pitchIndices) = m.Data.rawCube( :, :,yawIndices, pitchIndices) + subCube;
			end

			%% Updating cube for CFAR data
			if(processCFAR)
				cfarCubeSize=rawCubeSize([1 3 4]);
				m = memmapfile('cfarCube.dat', ...
					'Format', {'single', cfarCubeSize, 'cfarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				if(decay)
					batchDecay = single(prod([buffer.decay]));
					scripts.decayCube_avx2(m.Data.cfarCube, batchDecay);
				end


				for i = 1:numel(buffer.yawIdx)
					% fprintf("CFAR | idx=%f, sum=%f", buffer.pitchIdx(i), sum(buffer.cfar(:, i)));
					contribution =  buffer.cfar(:, i);
					if(decay)
						batchDecay = single(prod(buffer.decay(i:end)));
						scripts.decayCube_avx2(contribution, batchDecay);
					end
					m.Data.cfarCube(:, buffer.yawIdx(i), buffer.pitchIdx(i)) = contribution;
				end

			end
		end
	end

	methods(Access=private)
		function afterBatchProcessing(obj)
			% afterBatchProcessing: Post-batch processing callback
			%
			% In case zero was called while thread was processing cubes will be zeroed
			% form here

			obj.isProcessing = false;
			if obj.requestToZero
				obj.requestToZero = false;
				obj.zeroCubes();
			end
			notify(obj, 'updateFinished');
			fprintf("radarDataCube | updateFinished\n");
		end

		function generateSpreadPattern(obj, spreadPatternYaw, spreadPatternPitch)
			% generateSpreadPattern: generate pattern used to sprtead range-doppelr map
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
			% radarDataCube: Initializes radar data cube and associated buffers
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
				if ~exist('rawCube.dat', 'file') % this can more easily be handled bu fsutil (win), fallocate (linux)
					mocDataSize = obj.rawCubeSize;
					mocDataSize(1) = floor(mocDataSize(1)*1.2); % there needs to be a little reserve
					data = randn(mocDataSize, 'single');  % matlab applyes some sort of compression, so we need data to be random in order to correctly allocate enough space
					save('rawCube.dat', 'data');
				end

				fprintf("radarDataCube | radarDataCube | Initializing rawCube with yaw %f, pitch %f, range %d, doppler %f\n", length(obj.yawBins), length(obj.pitchBins), numRangeBins, numDopplerBins)

				obj.bufferA.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');
				obj.bufferB.rangeDoppler = zeros([numRangeBins, numDopplerBins, obj.batchSize], 'single');


				% Create memory map
				obj.rawCubeMap = memmapfile('rawCube.dat', ...
					'Format', {'single', obj.rawCubeSize, 'rawCube'}, ...
					'Writable', true, ...
					'Repeat', 1);


				scripts.zeroCube(obj.rawCubeMap.Data.rawCube);
				obj.rawCube = obj.rawCubeMap.Data.rawCube;
			end

			%% Initialize radar cube for cfar

			if obj.keepCFAR
				obj.bufferA.cfar = zeros([numRangeBins, obj.batchSize], 'single');
				obj.bufferB.cfar = zeros([numRangeBins, obj.batchSize], 'single');

				if ~exist('cfarCube.dat', 'file')
					mocDataSize = obj.rawCubeSize([1 3 4]);
					mocDataSize(1) = floor(mocDataSize(1)*1.2);
					data = randn(mocDataSize, 'single');
					save('cfarCube.dat', 'data');
				end

				% Create memory map
				obj.cfarCubeMap = memmapfile('cfarCube.dat', ...
					'Format', {'single', obj.rawCubeSize([1 3 4]), 'cfarCube'}, ...
					'Writable', true, ...
					'Repeat', 1);

				scripts.zeroCube(obj.cfarCubeMap.Data.cfarCube);
				obj.cfarCube = obj.cfarCubeMap.Data.cfarCube;
			end
		end

		function addData(obj, yaw, pitch, cfar, rangeDoppler, speed)
			% addData: Adds radar detection to the active buffer
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
			decayCoef = exp(-speed/1000);
			% fprintf("radarDataCube | addData | adding to rawCube %d: yaw %f, pitch %f, decay %f\n", obj.bufferActiveWriteIdx, yaw, pitch, decayCoef);

			obj.bufferA.decay(obj.bufferActiveWriteIdx) = decayCoef;

			obj.bufferA.yawIdx(obj.bufferActiveWriteIdx) = yawIdx;
			obj.bufferA.pitchIdx(obj.bufferActiveWriteIdx) = pitchIdx;
			if(obj.keepRaw)
				obj.bufferA.rangeDoppler(:, :, obj.bufferActiveWriteIdx) = single(rangeDoppler);
			end

			if(obj.keepCFAR)
				obj.bufferA.cfar(:, obj.bufferActiveWriteIdx) = single(cfar);
				%	fprintf("AFTER SINGLE\n");
				%disp(sum(obj.bufferA.cfar(:, obj.bufferActiveWriteIdx)));
			end

			if obj.bufferActiveWriteIdx > obj.batchSize
				obj.overflow = true;
				obj.bufferActiveWriteIdx = 1;
			else
				obj.bufferActiveWriteIdx = obj.bufferActiveWriteIdx + 1;
			end


		end

		function status = isBatchFull(obj)
			% isBatchFull: Checks if the active buffer is ready for processing
			%
			% Output:
			%   status ... True if buffer is full or overflowed
			status = (obj.bufferActiveWriteIdx > obj.batchSize) || obj.overflow;
		end

		function startBatchProcessing(obj)
			% startBatchProcessing: Initiates asynchronous batch processing
			%
			% internal buffers switch their place - one used for buffering will now be
			% processed while new data will be coming into second buffer

			if obj.isProcessing
				fprintf("radarDataCube | startBatchProcessing | already processing\n");
				return;
			end


			fprintf("radarDataCube | startBatchProcessing | starting processing\n");

			obj.isProcessing = true;
			obj.overflow = false;

			processingBuffer = obj.bufferA;
			obj.bufferA = obj.bufferB; % Reset active buffer
			obj.bufferB = processingBuffer; % Assign to processing buffer
			obj.bufferActiveWriteIdx = 1;

			future = parfeval(gcp, ...
				@radarDataCube.processBatch, ...
				0, ...
				processingBuffer, ...
				obj.spreadPattern, ...
				obj.rawCubeSize, ...
				obj.yawBins, ...
				obj.pitchBins, ...
				obj.keepRaw, ...
				obj.keepCFAR, ...
				obj.decay);

			afterAll(future, @(varargin) obj.afterBatchProcessing(varargin{:}), 0);

			% NOTE: main thread execution for debug
			% radarDataCube.processBatch(processingBuffer, ...
			% 	obj.spreadPattern, ...
			% 	obj.rawCubeSize, ...
			% 	obj.yawBins, ...
			% 	obj.pitchBins, ...
			% 	obj.keepRaw, ...
			% 	obj.keepCFAR, ...
			% 	obj.decay);
			% obj.afterBatchProcessing();


		end

		function zeroCubes(obj)
			% zeroCubes: Resets rawCube and cfarCube to zero
			%
			% If processing is ongoing, queues zeroing after completion
			if (obj.isProcessing)
				obj.requestToZero = true;
				return;
			end

			if obj.keepCFAR
				scripts.zeroCube(obj.cfarCube)
			end
			if obj.keepRaw
				scripts.zeroCube(obj.rawCube)
			end
		end

	end
end

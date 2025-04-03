#include "mex.h"
#include <immintrin.h>


void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    // Validate inputs
    if (nrhs != 4) {
        mexErrMsgTxt("Four inputs required: cube, weightedContribution, yawIndexes, pitchIndexes");
    }

    // Get input arrays
    if (!mxIsSingle(prhs[0]) || !mxIsSingle(prhs[1])) {
        mexErrMsgTxt("cube and weightedContribution must be single precision.");
    }

    float *cube = (float *)mxGetData(prhs[0]);
    float *contrib = (float *)mxGetData(prhs[1]);
    double *yawIndexes = mxGetPr(prhs[2]);
    double *pitchIndexes = mxGetPr(prhs[3]);

    // Get dimensions
    const mwSize *cubeDims = mxGetDimensions(prhs[0]);
    const mwSize *contribDims = mxGetDimensions(prhs[1]);

    // Actual dimensions
    mwSize rangeDim = cubeDims[0];       // First dimension: rangeBins
    mwSize dopplerDim = cubeDims[1];     // Second dimension: dopplerBins
    mwSize yawDim = cubeDims[2];         // Third dimension: yawIndices
    mwSize pitchDim = cubeDims[3];       // Fourth dimension: pitchIndices

    // Number of indices
    mwSize numYaw = mxGetNumberOfElements(prhs[2]);
    mwSize numPitch = mxGetNumberOfElements(prhs[3]);

    // Debug information
    // mexPrintf("UpdateSubcubeAVX: Processing data\n");
    // mexPrintf("  SubCube dimensions: [%d, %d, %d, %d] (range, doppler, yaw, pitch)\n",
    //           (int)rangeDim, (int)dopplerDim, (int)yawDim, (int)pitchDim);
    // mexPrintf("  Contribution dimensions: [%d, %d, %d, %d]\n",
    //           (int)contribDims[0], (int)contribDims[1], (int)contribDims[2], (int)contribDims[3]);
    // mexPrintf("  yawIndexes size: %d, pitchIndexes size: %d\n", (int)numYaw, (int)numPitch);

		mwSize rgMapSize = rangeDim*dopplerDim;

    // For each yaw and pitch index
    for (mwSize p = 0; p < numPitch; p++) {
        mwSize pitchIdx = (mwSize)pitchIndexes[p] - 1;

        for (mwSize y = 0; y < numYaw; y++) {
            mwSize yawIdx = (mwSize)yawIndexes[y] - 1;

            // Calculate base offset for this yaw-pitch combination
            mwSize baseOffset = rgMapSize * (yawIdx + pitchIdx * yawDim); // wrap around whole cube size, not just numYaw

            // Calculate contribution offset
            mwSize contribOffset = rgMapSize * (y + p * numYaw);

            // Process 8 elements at a time with AVX2
            for (mwSize i = 0; i < rgMapSize; i += 8) { // ranger doppler should be continous in memory
                // Load 8 values from cube and contribution
                __m256 cubeVec = _mm256_loadu_ps(&cube[baseOffset + i]);
                __m256 contribVec = _mm256_loadu_ps(&contrib[contribOffset + i]);
                __m256 resultVec = _mm256_add_ps(cubeVec, contribVec);
                _mm256_storeu_ps(&cube[baseOffset + i], resultVec);

                // if (p == 0 && y == 0 && i == 0) {
                //     mexPrintf("  First AVX2 operation: cube[%d:%d] += contrib[%d:%d]\n",
                //              (int)baseOffset, (int)(baseOffset+7),
                //              (int)contribOffset, (int)(contribOffset+7));
                // }
            }
        }
    }
}

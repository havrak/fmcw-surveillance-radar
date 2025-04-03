#include "mex.h"
#include <immintrin.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    // Validate inputs
    if (nrhs != 2) {
        mexErrMsgTxt("Two inputs required: adjPattern and contribution.");
    }
    if (!mxIsSingle(prhs[0]) || !mxIsSingle(prhs[1])) {
        mexErrMsgTxt("Inputs must be single-precision.");
    }

    // Get dimensions of adjPattern (MxN) and contribution (PxQ)
    const mwSize *adjDims = mxGetDimensions(prhs[0]);
    mwSize M = adjDims[0];  // Yaw dimension
    mwSize N = adjDims[1];  // Pitch dimension
    const mwSize *contribDims = mxGetDimensions(prhs[1]);
    mwSize P = contribDims[0];  // Fast time (range) dimension
    mwSize Q = contribDims[1];  // Slow time (doppler) dimension

    // Create 4D output array [P, Q, M, N] - Fast time x Slow time x Yaw x Pitch
    mwSize outDims[4] = {P, Q, M, N};
    plhs[0] = mxCreateNumericArray(4, outDims, mxSINGLE_CLASS, mxREAL);
    float *output = (float *)mxGetData(plhs[0]);

    // Get input data pointers
    float *adjPattern = (float *)mxGetData(prhs[0]);
    float *contribution = (float *)mxGetData(prhs[1]);

    // Pre-calculate the total size of the range-doppler contribution
    mwSize rdSize = P * Q;

    // Iterate over each antenna pattern position (yaw, pitch)
    for (mwSize j = 0; j < N; ++j) {
        for (mwSize i = 0; i < M; ++i) {
            // Get pattern value for this yaw/pitch combination
            float patternVal = adjPattern[i + j * M];

            // Calculate base index in the output array
            mwSize baseIdx = i * rdSize + j * rdSize * M;

            // Process all range-doppler points at once with vectorized operations
            // This leverages the contiguous memory of range and doppler dimensions
            mwSize idx = 0;

            // Use SIMD for vectorized operations on contiguous memory
            __m256 pattern_vec = _mm256_set1_ps(patternVal);
            for (; idx + 7 < rdSize; idx += 8) {
                __m256 contrib_vec = _mm256_loadu_ps(&contribution[idx]);
                __m256 result = _mm256_mul_ps(contrib_vec, pattern_vec);
                _mm256_storeu_ps(&output[baseIdx + idx], result);
            }

						// there should never be a case where P*Q is not multiple of 8
            // for (; idx < rdSize; ++idx) {
            //     output[baseIdx + idx] = contribution[idx] * patternVal;
            // }
        }
    }
}

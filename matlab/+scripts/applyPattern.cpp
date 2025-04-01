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
    mwSize M = adjDims[0];
    mwSize N = adjDims[1];
    const mwSize *contribDims = mxGetDimensions(prhs[1]);
    mwSize P = contribDims[0];
    mwSize Q = contribDims[1];

    // Create 4D output array [M, N, P, Q]
    mwSize outDims[4] = {M, N, P, Q};
    plhs[0] = mxCreateNumericArray(4, outDims, mxSINGLE_CLASS, mxREAL);
    float *output = (float *)mxGetData(plhs[0]);

    // Get input data pointers
    float *adjPattern = (float *)mxGetData(prhs[0]);
    float *contribution = (float *)mxGetData(prhs[1]);

    // Iterate over each element in contribution (k,l)
		// we are processing in retarded column-major order
		// order in memory
		//		1. M - Varies fastest (continuos segment in memory).
		//    2. N
		//    3. P
		//    4. Q
		// we can combine M and N into a single loop
    for (mwSize l = 0; l < Q; ++l) {
        for (mwSize k = 0; k < P; ++k) {

            // current contribution
            float c = contribution[k + l * P];
            __m256 c_vec = _mm256_set1_ps(c);
            mwSize base = k * M * N + l * M * N * P;
            float *outSlice = output + base;

            // Vectorized multiplication for contiguous blocks
            mwSize numElements = M * N;
            mwSize i = 0;
            for (; i + 7 < numElements; i += 8) {
                __m256 a = _mm256_loadu_ps(adjPattern + i);
                __m256 res = _mm256_mul_ps(a, c_vec);
                _mm256_storeu_ps(outSlice + i, res);
            }

						// if matlab was normal and use row-major this wouldn't be neccesary
						// but it isn't and pattern size is not by 8 divisible
            for (; i < numElements; ++i) {
                outSlice[i] = adjPattern[i] * c;
            }
        }
    }
}

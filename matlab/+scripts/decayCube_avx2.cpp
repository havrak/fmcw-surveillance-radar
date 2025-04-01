#include "mex.h"
#include <immintrin.h>
#include <cstdlib>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    // Validate inputs
    if (nrhs != 2) {
        mexErrMsgTxt("Two inputs required: (1) cube data, (2) decay factor.");
    }

    // Get input data pointer and decay factor
    float *cubeData = (float*)mxGetData(prhs[0]);
    float decay = *((float*)mxGetData(prhs[1]));

    // Get number of elements
    mwSize numElements = mxGetNumberOfElements(prhs[0]);

		// mexPrintf("Updating:  %lld\n", (unsigned long int) numElements);

		__m256 f = _mm256_set1_ps(decay);
		mwSize  i = 0;
		for (; i + 7 < numElements; i += 8) {
			__m256 x = _mm256_loadu_ps(&cubeData[i]);
			x = _mm256_mul_ps(x, f);
			_mm256_storeu_ps(&cubeData[i], x);
		}

}

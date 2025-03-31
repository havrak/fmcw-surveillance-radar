#include "mex.h"
#include <cstring> // For memset"
#include <omp.h> // OpenMP header
#include <stdint.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    // Validate inputs
    if (nrhs != 1) {
        mexErrMsgTxt("One inputs required: (1) cube data");
    }

    float *cubeData = (float*)mxGetData(prhs[0]);
    mwSize numElements = mxGetNumberOfElements(prhs[0]);
		memset(cubeData, 0, numElements * sizeof(float)); // Set all elements to zero
}

#include "mex.h"
#include <omp.h> // OpenMP header

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

    // Parallelize decay operation using OpenMP
    #pragma omp parallel for
    for (mwSize i = 0; i < numElements; i++) {
        cubeData[i] *= decay;
    }
}

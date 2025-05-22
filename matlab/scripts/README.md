# Mex scripts
Collection of CPP scripts that are compiled with MATLAB supplied mex tools in order to enable their use withing MATLAB code.
* OpenMP compilation:
	* `matlab-mex COMPFLAGS="/openmp $COMPFLAGS" CXXOPTIMFLAGS="-O3 -DNDEBUG -fno-predictive-commoning" -v decayCube_omp.cpp`

* AVX2 compilation
	* `matlab-mex  COMPFLAGS="$COMPFLAGS" CFLAGS="$CFLAGS -mavx2" CXXOPTIMFLAGS="-O3 -DNDEBUG -fno-predictive-commoning"  -v decayCube_avx2.cpp`


Compiled version of these scripts in submited thesis were build under Arch Linux on x86 architecture (Intel i7-10210U)



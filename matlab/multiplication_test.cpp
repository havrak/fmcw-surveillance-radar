#include <chrono>
#include <cstdlib>
#include <immintrin.h>
#include <iostream>
#include <omp.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

// Simple utility to initialize data
void initArray(float* arr, size_t numElements, float value)
{
	for (size_t i = 0; i < numElements; ++i) {
		arr[i] = value;
	}
}

void multiplyOpenMP(float* arr, size_t numElements, float factor)
{
#pragma omp parallel for
	for (size_t i = 0; i < numElements; ++i) {
		arr[i] *= factor;
	}
}

void multiplyAVX2(float* arr, size_t numElements, float factor)
{
	__m256 f = _mm256_set1_ps(factor);
	size_t i = 0;
	for (; i + 7 < numElements; i += 8) {
		__m256 x = _mm256_loadu_ps(&arr[i]);
		x = _mm256_mul_ps(x, f);
		_mm256_storeu_ps(&arr[i], x);
	}
}

void multiplySSE(float* arr, size_t numElements, float factor)
{
	__m128 f = _mm_set1_ps(factor);
	size_t i = 0;
	for (; i + 3 < numElements; i += 4) {
		__m128 x = _mm_loadu_ps(&arr[i]);
		x = _mm_mul_ps(x, f);
		_mm_storeu_ps(&arr[i], x);
	}
}

void multiplyAVX2OpenPM(float* arr, size_t numElements, float factor)
{
	// __m256 f = ;
#pragma omp parallel for
	for (size_t i = 0; i < numElements; i += 8) {
		// Load 8 floats into a 256-bit register
		__m256 vals = _mm256_loadu_ps(&arr[i]);
		// Multiply using AVX
		vals = _mm256_mul_ps(vals, _mm256_set1_ps(factor));
		// Store multiplied results back to memory
		_mm256_storeu_ps(&arr[i], vals);
	}
}

void multiplyAVX2TBB(float* arr, size_t numElements, float factor) {
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, numElements, 1024),
        [arr, factor](const tbb::blocked_range<size_t>& r) {
            __m256 f = _mm256_set1_ps(factor);
            for (size_t i = r.begin(); i < r.end(); i += 8) {
                __m256 x = _mm256_loadu_ps(&arr[i]);
                x = _mm256_mul_ps(x, f);
                _mm256_storeu_ps(&arr[i], x);
            }
        }
    );
}

int main()
{
	const size_t numElements = 0x1FFFFFF0; // adjust as needed
	float* data = new float[numElements];

	// Prepare
	initArray(data, numElements, 1.0f);

	// Measure OpenMP
	auto start = std::chrono::high_resolution_clock::now();
	multiplyOpenMP(data, numElements, 0.95f);
	auto end = std::chrono::high_resolution_clock::now();
	double openmpTime = std::chrono::duration<double>(end - start).count();
	std::cout << "OpenMP time:  " << openmpTime << " seconds\n";

	initArray(data, numElements, 1.0f);

	// Measure AVX2
	start = std::chrono::high_resolution_clock::now();
	multiplyAVX2(data, numElements, 0.95f);
	end = std::chrono::high_resolution_clock::now();
	double avx2Time = std::chrono::duration<double>(end - start).count();
	std::cout << "AVX2 time:    " << avx2Time << " seconds\n";

	initArray(data, numElements, 1.0f);

	// Measure SSE
	start = std::chrono::high_resolution_clock::now();
	multiplySSE(data, numElements, 0.95f);
	end = std::chrono::high_resolution_clock::now();
	double sseTime = std::chrono::duration<double>(end - start).count();
	std::cout << "SSE time:     " << sseTime << " seconds\n";

	initArray(data, numElements, 1.0f);

	// Measure AVX2 + OpenMP
	start = std::chrono::high_resolution_clock::now();
	multiplyAVX2OpenPM(data, numElements, 0.95f);
	end = std::chrono::high_resolution_clock::now();
	double avx2openTime = std::chrono::duration<double>(end - start).count();
	std::cout << "AVX2+OMP:     " << avx2openTime << " seconds\n";

	// Measure AVX2 + TBB
	start = std::chrono::high_resolution_clock::now();
	multiplyAVX2TBB(data, numElements, 0.95f);
	end = std::chrono::high_resolution_clock::now();
	double avx2TBBTime = std::chrono::duration<double>(end - start).count();
	std::cout << "AVX2+TBB:     " << avx2TBBTime << " seconds\n";

	delete[] data;
	return 0;
}

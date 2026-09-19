#include <iostream>
#include <chrono>
#include <vector>
#include <cmath>
#include <emmintrin.h>
#include <smmintrin.h>

inline float fastExp2_SSE(float x) noexcept
{
    __m128 x_vec = _mm_set1_ps(x);
    __m128 n = _mm_round_ps(x_vec, _MM_FROUND_TO_NEAREST_INT |_MM_FROUND_NO_EXC);
    __m128 f = _mm_sub_ps(x_vec, n);

    const __m128 c0 = _mm_set1_ps(1.00000005f);
    const __m128 c1 = _mm_set1_ps(0.69312727f);
    const __m128 c2 = _mm_set1_ps(0.24022212f);
    const __m128 c3 = _mm_set1_ps(0.05587551f);
    const __m128 c4 = _mm_set1_ps(0.00967076f);

    __m128 poly = _mm_add_ps(c3, _mm_mul_ps(f, c4));
    poly = _mm_add_ps(c2, _mm_mul_ps(f, poly));
    poly = _mm_add_ps(c1, _mm_mul_ps(f, poly));
    poly = _mm_add_ps(c0, _mm_mul_ps(f, poly));

    __m128i n_int = _mm_cvtps_epi32(n);
    __m128i exp_bits = _mm_add_epi32(n_int, _mm_set1_epi32(127));
    __m128i pow2_bits = _mm_slli_epi32(exp_bits, 23);
    __m128 pow2_n = _mm_castsi128_ps(pow2_bits);

    __m128 res = _mm_mul_ps(poly, pow2_n);
    float out;
    _mm_store_ss(&out, res);
    return out;
}

inline __m128 fastExp2_SSE_Vector4(__m128 x_vec) noexcept
{
    __m128 n = _mm_round_ps(x_vec, _MM_FROUND_TO_NEAREST_INT |_MM_FROUND_NO_EXC);
    __m128 f = _mm_sub_ps(x_vec, n);

    const __m128 c0 = _mm_set1_ps(1.00000005f);
    const __m128 c1 = _mm_set1_ps(0.69312727f);
    const __m128 c2 = _mm_set1_ps(0.24022212f);
    const __m128 c3 = _mm_set1_ps(0.05587551f);
    const __m128 c4 = _mm_set1_ps(0.00967076f);

    __m128 poly = _mm_add_ps(c3, _mm_mul_ps(f, c4));
    poly = _mm_add_ps(c2, _mm_mul_ps(f, poly));
    poly = _mm_add_ps(c1, _mm_mul_ps(f, poly));
    poly = _mm_add_ps(c0, _mm_mul_ps(f, poly));

    __m128i n_int = _mm_cvtps_epi32(n);
    __m128i exp_bits = _mm_add_epi32(n_int, _mm_set1_epi32(127));
    __m128i pow2_bits = _mm_slli_epi32(exp_bits, 23);
    __m128 pow2_n = _mm_castsi128_ps(pow2_bits);

    return _mm_mul_ps(poly, pow2_n);
}

int main()
{
    constexpr size_t N = 10000000;
    std::vector<float> input(N);
    for (size_t i = 0; i < N; ++i) {
        input[i] = -4.0f + 8.0f * (static_cast<float>(i % 10000) / 10000.0f);
    }
    std::vector<float> output(N);

    // 1. std::exp2f
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < N; ++i) {
        output[i] = std::exp2f(input[i]);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double msScalar = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // 2. fastExp2_SSE_Vector4 (Process 4 elements per iteration)
    auto t2 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < N; i += 4) {
        __m128 in_v = _mm_loadu_ps(&input[i]);
        __m128 out_v = fastExp2_SSE_Vector4(in_v);
        _mm_storeu_ps(&output[i], out_v);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double msSIMD = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::cout << "BENCHMARK RESULTS (N = " << N << "):\n";
    std::cout << "std::exp2f: " << msScalar << " ms (" << (msScalar / N * 1e6) << " ns/op)\n";
    std::cout << "fastExp2 SIMD (4 lanes): " << msSIMD << " ms (" << (msSIMD / N * 1e6) << " ns/op)\n";
    std::cout << "Speedup: " << (msScalar / msSIMD) << "x\n";

    return 0;
}

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>
#include <emmintrin.h> // SSE2
#include <smmintrin.h> // SSE4.1

// -------------------------------------------------------------
// Implementation 1: fastExp2 (SSE intrinsic version from simd_optimization.md)
// -------------------------------------------------------------
inline float fastExp2_SSE(float x) noexcept
{
    __m128 x_vec = _mm_set1_ps(x);
    // SSE round: round to nearest
    __m128 n = _mm_round_ps(x_vec, _MM_FROUND_TO_NEAREST_INT |_MM_FROUND_NO_EXC);
    __m128 f = _mm_sub_ps(x_vec, n);

    const __m128 c0 = _mm_set1_ps(1.00000005f);
    const __m128 c1 = _mm_set1_ps(0.69312727f);
    const __m128 c2 = _mm_set1_ps(0.24022212f);
    const __m128 c3 = _mm_set1_ps(0.05587551f);
    const __m128 c4 = _mm_set1_ps(0.00967076f);

    // Horner form: c0 + f * (c1 + f * (c2 + f * (c3 + f * c4)))
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

// -------------------------------------------------------------
// Implementation 2: fastExp2 (Fallback #else version from simd_optimization.md)
// -------------------------------------------------------------
inline float fastExp2_Fallback(float x) noexcept
{
    int ni = static_cast<int>(std::round(x));
    float fi = x - static_cast<float>(ni);
    float p = 1.0f + fi * (0.69314718f + fi * (0.24022651f + fi * (0.05550411f + fi * 0.00961813f)));
    uint32_t bits = static_cast<uint32_t>(ni + 127) << 23;
    float pow2_n;
    std::memcpy(&pow2_n, &bits, sizeof(float));
    return p * pow2_n;
}

// -------------------------------------------------------------
// Implementation 3: fastGSIMD (SSE intrinsic version from simd_optimization.md)
// -------------------------------------------------------------
struct FastGResult {
    float G;
    float num;
    float den;
    float p;
    float q;
    float z;
    bool isUpper;
};

inline FastGResult fastGSIMD_Detailed(float wd) noexcept
{
    const __m128 halfPi = _mm_set1_ps(1.57079632679f);
    const __m128 piDiv4 = _mm_set1_ps(0.78539816339f);
    __m128 wd_vec = _mm_set1_ps(wd);

    __m128 isUpper = _mm_cmpgt_ps(wd_vec, piDiv4);
    __m128 z = _mm_or_ps(_mm_and_ps(isUpper, _mm_sub_ps(halfPi, wd_vec)),
                         _mm_andnot_ps(isUpper, wd_vec));

    __m128 z2 = _mm_mul_ps(z, z);
    __m128 z4 = _mm_mul_ps(z2, z2);

    const __m128 c945 = _mm_set1_ps(945.0f);
    const __m128 c105 = _mm_set1_ps(105.0f);
    const __m128 c420 = _mm_set1_ps(420.0f);
    const __m128 c15  = _mm_set1_ps(15.0f);

    __m128 p = _mm_mul_ps(z, _mm_add_ps(_mm_sub_ps(c945, _mm_mul_ps(c105, z2)), z4));
    __m128 q = _mm_add_ps(_mm_sub_ps(c945, _mm_mul_ps(c420, z2)), _mm_mul_ps(c15, z4));

    __m128 num = _mm_or_ps(_mm_and_ps(isUpper, q),
                           _mm_andnot_ps(isUpper, p));
    __m128 den = _mm_add_ps(p, q);
    __m128 G = _mm_div_ps(num, den);

    FastGResult res;
    _mm_store_ss(&res.G, G);
    _mm_store_ss(&res.num, num);
    _mm_store_ss(&res.den, den);
    _mm_store_ss(&res.p, p);
    _mm_store_ss(&res.q, q);
    _mm_store_ss(&res.z, z);
    int mask = _mm_movemask_ps(isUpper);
    res.isUpper = (mask & 1) != 0;
    return res;
}

// -------------------------------------------------------------
// Implementation 4: Circular phase interpolation from PROJECT.md
// -------------------------------------------------------------
inline float interpolatePhase(float phi1, float phi2, float frac, float& outDiff) noexcept
{
    float diff = std::fmod(phi2 - phi1 + 540.0f, 360.0f) - 180.0f;
    outDiff = diff;
    float outPhaseDegrees = phi1 + frac * diff;
    if (outPhaseDegrees < 0.0f) outPhaseDegrees += 360.0f;
    if (outPhaseDegrees >= 360.0f) outPhaseDegrees -= 360.0f;
    return outPhaseDegrees;
}

int main()
{
    std::cout << "=========================================================\n";
    std::cout << "  EMPIRICAL CHALLENGER 1: DSP MATH BOUNDARY TEST HARNESS \n";
    std::cout << "=========================================================\n\n";

    // ---------------------------------------------------------
    // TEST SUITE 1: fastExp2 Extreme & Boundary Stress Tests
    // ---------------------------------------------------------
    std::cout << "--- TEST SUITE 1: fastExp2 Extreme Inputs & Boundary Analysis ---\n";
    std::vector<float> testX = {
        0.0f, 1.0f, -1.0f, 4.0f, -4.0f,
        15.0f, -20.0f,
        126.0f, 127.0f, 128.0f, 129.0f, 130.0f,
        -125.0f, -126.0f, -127.0f, -128.0f, -129.0f, -140.0f, -150.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()
    };

    std::cout << std::left 
              << std::setw(10) << "Input x" 
              << std::setw(16) << "std::exp2f" 
              << std::setw(16) << "fastExp2_SSE" 
              << std::setw(16) << "fastExp2_Fallb" 
              << std::setw(16) << "Rel Error (%)" 
              << "Hex (SSE)\n";
    std::cout << std::string(85, '-') << "\n";

    for (float x : testX)
    {
        float trueVal = std::exp2(x);
        float sseVal = fastExp2_SSE(x);
        float fallVal = fastExp2_Fallback(x);
        
        uint32_t sseBits;
        std::memcpy(&sseBits, &sseVal, sizeof(float));

        double relErr = 0.0;
        if (std::isfinite(trueVal) && trueVal != 0.0f && std::isfinite(sseVal)) {
            relErr = std::abs((double)sseVal - (double)trueVal) / (double)trueVal * 100.0;
        }

        std::cout << std::left 
                  << std::setw(10) << x 
                  << std::setw(16) << trueVal 
                  << std::setw(16) << sseVal 
                  << std::setw(16) << fallVal;
        if (std::isnan(trueVal) || std::isnan(sseVal) || !std::isfinite(trueVal))
            std::cout << std::setw(16) << "N/A";
        else
            std::cout << std::setw(16) << relErr;
        
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << sseBits 
                  << std::dec << std::setfill(' ') << "\n";
    }

    // ---------------------------------------------------------
    // TEST SUITE 2: fastGSIMD Nyquist & Boundary Stress Tests
    // ---------------------------------------------------------
    std::cout << "\n--- TEST SUITE 2: fastGSIMD Nyquist Proximity & Boundary Analysis ---\n";
    const float fs = 44100.0f;
    std::vector<float> testFc = {
        20.0f, 60.0f, 1000.0f, 10000.0f, 20000.0f, 21950.0f, 22000.0f,
        22049.0f, 22050.0f, // Exact Nyquist
        22051.0f, 25000.0f  // Beyond Nyquist
    };

    std::cout << std::left 
              << std::setw(10) << "fc (Hz)" 
              << std::setw(12) << "wd (rad)" 
              << std::setw(12) << "z (rad)" 
              << std::setw(12) << "num" 
              << std::setw(12) << "den (p+q)" 
              << std::setw(12) << "G (approx)" 
              << std::setw(12) << "G (exact)" 
              << "Rel Err (ppm)\n";
    std::cout << std::string(95, '-') << "\n";

    for (float fc : testFc)
    {
        float wd = 3.14159265358979323846f * fc / fs;
        FastGResult res = fastGSIMD_Detailed(wd);
        float gExact = std::tan(wd);
        float GExact = gExact / (1.0f + gExact);

        double relErrPpm = std::abs((double)res.G - (double)GExact) / (double)GExact * 1e6;

        std::cout << std::left 
                  << std::setw(10) << fc 
                  << std::setw(12) << wd 
                  << std::setw(12) << res.z 
                  << std::setw(12) << res.num 
                  << std::setw(12) << res.den 
                  << std::setw(12) << res.G 
                  << std::setw(12) << GExact 
                  << relErrPpm << " ppm\n";
    }

    // Additional extreme wd tests for fastGSIMD
    std::cout << "\nTesting extreme wd inputs to fastGSIMD:\n";
    std::vector<float> extremeWd = {
        0.0f, -0.1f, -1.0f, 1.57079632679f, 1.5708f, 2.0f, 3.14159f, 10.0f,
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()
    };
    for (float wd : extremeWd) {
        FastGResult res = fastGSIMD_Detailed(wd);
        std::cout << "wd=" << wd << " -> z=" << res.z << " num=" << res.num << " den=" << res.den << " G=" << res.G << "\n";
    }

    // ---------------------------------------------------------
    // TEST SUITE 3: Circular Phase Interpolation Edge Cases
    // ---------------------------------------------------------
    std::cout << "\n--- TEST SUITE 3: Circular Phase Interpolation Edge Cases ---\n";
    
    // Case 3.1: phi1 = 359.9, phi2 = 0.1
    std::cout << "Case 3.1: phi1 = 359.9 deg, phi2 = 0.1 deg\n";
    float diff1 = 0.0f;
    for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float phiOut = interpolatePhase(359.9f, 0.1f, frac, diff1);
        std::cout << "  frac=" << frac << " -> outPhase=" << phiOut << " deg (diff=" << diff1 << " deg)\n";
    }

    // Case 3.2: Reverse: phi1 = 0.1, phi2 = 359.9
    std::cout << "Case 3.2: phi1 = 0.1 deg, phi2 = 359.9 deg\n";
    float diff2 = 0.0f;
    for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float phiOut = interpolatePhase(0.1f, 359.9f, frac, diff2);
        std::cout << "  frac=" << frac << " -> outPhase=" << phiOut << " deg (diff=" << diff2 << " deg)\n";
    }

    // Case 3.3: Antipodal: phi1 = 0.0, phi2 = 180.0
    std::cout << "Case 3.3: phi1 = 0.0 deg, phi2 = 180.0 deg (Exact Antipodal)\n";
    float diff3 = 0.0f;
    for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float phiOut = interpolatePhase(0.0f, 180.0f, frac, diff3);
        std::cout << "  frac=" << frac << " -> outPhase=" << phiOut << " deg (diff=" << diff3 << " deg)\n";
    }

    // Case 3.4: Reverse Antipodal: phi1 = 180.0, phi2 = 0.0
    std::cout << "Case 3.4: phi1 = 180.0 deg, phi2 = 0.0 deg\n";
    float diff4 = 0.0f;
    for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float phiOut = interpolatePhase(180.0f, 0.0f, frac, diff4);
        std::cout << "  frac=" << frac << " -> outPhase=" << phiOut << " deg (diff=" << diff4 << " deg)\n";
    }

    // Case 3.5: Epsilon perturbation around antipodal (179.999 vs 180.001)
    std::cout << "Case 3.5: Perturbation around antipodal:\n";
    float diffA = 0.0f, diffB = 0.0f;
    float phiA = interpolatePhase(0.0f, 179.999f, 0.5f, diffA);
    float phiB = interpolatePhase(0.0f, 180.001f, 0.5f, diffB);
    std::cout << "  phi2 = 179.999 deg -> diff = " << diffA << " deg, midpoint = " << phiA << " deg\n";
    std::cout << "  phi2 = 180.001 deg -> diff = " << diffB << " deg, midpoint = " << phiB << " deg\n";

    return 0;
}

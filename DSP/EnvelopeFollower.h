#pragma once
#include <JuceHeader.h>
#include "SimdMath.h"
#include <cmath>
#include <algorithm>

class EnvelopeFollower
{
public:
    EnvelopeFollower() = default;

    void prepare(double sampleRate) noexcept;
    void setTimeConstants(float attackMs, float releaseMs) noexcept;

    // Scalar per-sample fallback
    [[nodiscard]] float processSample(float inputSample) noexcept;

    // PASS 1: Scalar L1 cache pre-pass over block
    void processBlock(const float* input, float* envOut, int numSamples) noexcept;

    // PASS 2 & 3: SIMD batch expansion using Chebyshev minimax fastExp2SIMD and fastGSIMD
    static void expandModulationArrays(const float* envArray,
                                       float* gOutArray,
                                       float* eqGainOutArray,
                                       int numSamples,
                                       float baseCutoff,
                                       float dynPhaseAmount,
                                       float eqCutDepthDb,
                                       float sampleRate) noexcept;

    void reset() noexcept;

private:
    float currentEnv = 0.0f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;
    float lastAttackMs = -999.0f;  // FIX: Changed from -1.0f to avoid matching valid input
    float lastReleaseMs = -999.0f;  // FIX: Changed from -1.0f to avoid matching valid input
    double sr = 44100.0;
    double lastSampleRate = 0.0;     // FIX: Track sample rate for dirty check
};
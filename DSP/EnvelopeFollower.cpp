#include "EnvelopeFollower.h"

void EnvelopeFollower::prepare(double sampleRate) noexcept
{
    sr = std::max(1.0, sampleRate);
    setTimeConstants(2.0f, 100.0f); // Default 2 ms attack, 100 ms release
    reset();
}

void EnvelopeFollower::setTimeConstants(float attackMs, float releaseMs) noexcept
{
    if (attackMs == lastAttackMs && releaseMs == lastReleaseMs)
        return;

    lastAttackMs = attackMs;
    lastReleaseMs = releaseMs;

    attackMs = std::max(0.1f, attackMs);
    releaseMs = std::max(0.1f, releaseMs);

    attackCoef = std::exp(-1.0f / (attackMs * 0.001f * static_cast<float>(sr)));
    releaseCoef = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sr)));
}

float EnvelopeFollower::processSample(float inputSample) noexcept
{
    if (!std::isfinite(currentEnv)) currentEnv = 0.0f;
    const float inVal = std::isfinite(inputSample) ? inputSample : 0.0f;
    float rectVal = std::abs(inVal);

    if (rectVal > currentEnv)
        currentEnv = attackCoef * currentEnv + (1.0f - attackCoef) * rectVal;
    else
        currentEnv = releaseCoef * currentEnv + (1.0f - releaseCoef) * rectVal;

    if (!std::isfinite(currentEnv)) currentEnv = 0.0f;
    return currentEnv;
}

void EnvelopeFollower::processBlock(const float* input, float* envOut, int numSamples) noexcept
{
    if (input == nullptr || envOut == nullptr || numSamples <= 0)
        return;

    if (!std::isfinite(currentEnv)) currentEnv = 0.0f;
    float env = currentEnv;
    const float aCoef = attackCoef;
    const float rCoef = releaseCoef;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inVal = std::isfinite(input[i]) ? input[i] : 0.0f;
        const float rectVal = std::abs(inVal);
        if (rectVal > env)
            env = aCoef * env + (1.0f - aCoef) * rectVal;
        else
            env = rCoef * env + (1.0f - rCoef) * rectVal;

        if (!std::isfinite(env)) env = 0.0f;
        envOut[i] = env;
    }

    currentEnv = std::isfinite(env) ? env : 0.0f;
}

void EnvelopeFollower::expandModulationArrays(const float* envArray,
                                            float* gOutArray,
                                            float* eqGainOutArray,
                                            int numSamples,
                                            float baseCutoff,
                                            float dynPhaseAmount,
                                            float eqCutDepthDb,
                                            float sampleRate) noexcept
{
    using FreakPhase::DSP::vFloat;
    using FreakPhase::DSP::fastExp2SIMD;
    using FreakPhase::DSP::fastGSIMD;
    using FreakPhase::DSP::fastExp2Scalar;
    using FreakPhase::DSP::fastGScalar;

    if (envArray == nullptr || gOutArray == nullptr || numSamples <= 0)
        return;

    const float nyquist = sampleRate * 0.5f;
    const float maxCutoff = nyquist * 0.49f;
    const float piOverFs = juce::MathConstants<float>::pi / sampleRate;
    const float eqDepthCoeff = eqCutDepthDb * 0.1660964f; // (eqCutDepthDb / 20) * log2(10)

    const int simdWidth = static_cast<int>(vFloat::size());
    const int simdLimit = numSamples - (numSamples % simdWidth);

    const vFloat vBaseCutoff (baseCutoff);
    const vFloat vDynPhase (dynPhaseAmount * 2.0f);
    const vFloat vPiOverFs (piOverFs);
    const vFloat vMaxCutoff (maxCutoff);
    const vFloat vMinCutoff (10.0f);
    const vFloat vEqCoeff (eqDepthCoeff);

    for (int i = 0; i < simdLimit; i += simdWidth)
    {
        vFloat env = vFloat::fromRawArray(envArray + i);
        #if FP_USE_SSE2
        vFloat x = vFloat::fromNative(_mm_max_ps(_mm_set1_ps(-10.0f), _mm_min_ps(_mm_set1_ps(10.0f), (env * vDynPhase).value)));
        #else
        vFloat x = env * vDynPhase;
        for (size_t k = 0; k < vFloat::size(); ++k)
            x.set(k, std::clamp(x.get(k), -10.0f, 10.0f));
        #endif
        vFloat octScale = fastExp2SIMD(x);

        #if FP_USE_SSE2
        vFloat fc = vFloat::fromNative(_mm_max_ps(vMinCutoff.value,
                                       _mm_min_ps(vMaxCutoff.value,
                                       _mm_mul_ps(vBaseCutoff.value, octScale.value))));
        #else
        vFloat fc = vBaseCutoff * octScale;
        for (size_t k = 0; k < vFloat::size(); ++k)
            fc.set(k, std::clamp(fc.get(k), 10.0f, maxCutoff));
        #endif

        vFloat wd = fc * vPiOverFs;
        vFloat G = fastGSIMD(wd);
        G.copyToRawArray(gOutArray + i);

        if (eqGainOutArray != nullptr)
        {
            #if FP_USE_SSE2
            vFloat eqX = vFloat::fromNative(_mm_max_ps(_mm_set1_ps(-10.0f), _mm_min_ps(_mm_set1_ps(10.0f), (env * vEqCoeff).value)));
            #else
            vFloat eqX = env * vEqCoeff;
            for (size_t k = 0; k < vFloat::size(); ++k)
                eqX.set(k, std::clamp(eqX.get(k), -10.0f, 10.0f));
            #endif
            vFloat vGain = fastExp2SIMD(eqX);
            vGain.copyToRawArray(eqGainOutArray + i);
        }
    }

    // Scalar Remainder Loop
    for (int i = simdLimit; i < numSamples; ++i)
    {
        const float envVal = envArray[i];
        const float x = std::clamp(envVal * dynPhaseAmount * 2.0f, -10.0f, 10.0f);
        const float octScale = fastExp2Scalar(x);
        const float fc = std::clamp(baseCutoff * octScale, 10.0f, maxCutoff);
        const float wd = std::clamp(fc * piOverFs, 0.0001f, 1.5393804f);

        gOutArray[i] = fastGScalar(wd);
        if (eqGainOutArray != nullptr)
        {
            const float eqX = std::clamp(envVal * eqDepthCoeff, -10.0f, 10.0f);
            eqGainOutArray[i] = fastExp2Scalar(eqX);
        }
    }
}

void EnvelopeFollower::reset() noexcept
{
    currentEnv = 0.0f;
}
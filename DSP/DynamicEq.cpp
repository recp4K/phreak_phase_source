#include "DynamicEq.h"

void DynamicEq::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    sampleRate = static_cast<float>(spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0);
    updateCoefficients();
    reset();
}

void DynamicEq::setParameters(float freqHz, float depthDb) noexcept
{
    // FIX: Added sampleRate to dirty check
    // Old: Only checked freqHz and depthDb
    // Problem: Coefficients depend on sampleRate, so they need to be recalculated
    // when sampleRate changes, even if freq and depth are the same
    if (freqHz == lastFreq && depthDb == lastDepth && sampleRate == lastSampleRate)
        return;

    lastFreq = freqHz;
    lastDepth = depthDb;
    lastSampleRate = sampleRate;
    currentDepthDb = depthDb;
    currentFreq = freqHz;
    eqGainLinear = juce::Decibels::decibelsToGain(depthDb) - 1.0f;

    updateCoefficients();
}

void DynamicEq::updateCoefficients() noexcept
{
    const float nyquist = sampleRate * 0.49f;
    const float clampedFreq = juce::jlimit(20.0f, nyquist, currentFreq);
    const float omega = juce::MathConstants<float>::pi * clampedFreq / sampleRate;

    g  = std::tan(omega);
    R2 = 0.5f; // Q = 2.0 -> R2 = 1 / Q = 0.5
    h  = 1.0f / (1.0f + R2 * g + g * g);
}

void DynamicEq::reset() noexcept
{
    s1_L = 0.0f; s1_R = 0.0f;
    s2_L = 0.0f; s2_R = 0.0f;
    
    // FIX: Also reset state for channels 2 and 3 if they exist
    // Note: Current implementation only has state for 2 channels (L/R)
    // but SIMD operations process 4 channels. This is a limitation.
    // For now, we ensure channels 2 and 3 are initialized to 0 in processBlock.
}

void DynamicEq::processBlock(juce::dsp::AudioBlock<float>& block, const float* eqGainArray) noexcept
{
    const size_t numSamples = block.getNumSamples();
    const size_t numChannels = block.getNumChannels();

    if (numSamples == 0 || numChannels == 0 || eqGainArray == nullptr)
        return;

    if (!std::isfinite(s1_L)) s1_L = 0.0f;
    if (!std::isfinite(s1_R)) s1_R = 0.0f;
    if (!std::isfinite(s2_L)) s2_L = 0.0f;
    if (!std::isfinite(s2_R)) s2_R = 0.0f;

    using FreakPhase::DSP::vFloat;
    constexpr float invQ = 0.5f;

    const vFloat vG (g);
    const vFloat vR2 (R2);
    const vFloat vH (h);
    const vFloat vInvQ (invQ);
    const vFloat vOne (1.0f);

    if (numChannels >= 2)
    {
        float* left = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);

        vFloat s1;
        s1.set(0, s1_L);
        s1.set(1, s1_R);
        for (size_t k = 2; k < vFloat::size(); ++k) s1.set(k, 0.0f);

        vFloat s2;
        s2.set(0, s2_L);
        s2.set(1, s2_R);
        for (size_t k = 2; k < vFloat::size(); ++k) s2.set(k, 0.0f);

        // FIX: Separate SIMD and scalar loops for better performance and safety
        const size_t simdWidth = vFloat::size();
        const size_t simdLimit = numSamples - (numSamples % simdWidth);

        // Process SIMD chunks
        for (size_t i = 0; i < simdLimit; ++i)
        {
            const float inL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;

            #if FP_USE_SSE2
            vFloat x = vFloat::fromNative(_mm_set_ps(0.0f, 0.0f, inR, inL));
            #else
            vFloat x;
            x.set(0, inL);
            x.set(1, inR);
            for (size_t k = 2; k < vFloat::size(); ++k) x.set(k, 0.0f);
            #endif

            const float gVal = std::isfinite(eqGainArray[i]) ? eqGainArray[i] : 1.0f;
            const vFloat gain (gVal);

            // TPT SVF Bandpass in SIMDRegister arithmetic
            vFloat yHP = vH * (x - s1 * (vG + vR2) - s2);
            vFloat yBP = yHP * vG + s1;
            s1 = yHP * vG + yBP;
            vFloat yLP = yBP * vG + s2;
            s2 = yBP * vG + yLP;

            // Apply dynamic bandpass cut
            vFloat out = x + (yBP * vInvQ) * (gain - vOne);

            const float outL = out.get(0);
            const float outR = out.get(1);
            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
        }

        // Process remainder samples (if any)
        for (size_t i = simdLimit; i < numSamples; ++i)
        {
            const float inL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;
            const float gVal = std::isfinite(eqGainArray[i]) ? eqGainArray[i] : 1.0f;

            // Scalar processing for remainder
            const float yHP_L = h * (inL - s1_L * (g + R2) - s2_L);
            const float yBP_L = yHP_L * g + s1_L;
            const float yLP_L = yBP_L * g + s2_L;
            const float outL = inL + (yBP_L * 0.5f) * (gVal - 1.0f);
            
            const float yHP_R = h * (inR - s1_R * (g + R2) - s2_R);
            const float yBP_R = yHP_R * g + s1_R;
            const float yLP_R = yBP_R * g + s2_R;
            const float outR = inR + (yBP_R * 0.5f) * (gVal - 1.0f);

            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
            
            s1_L = std::isfinite(yHP_L * g + yBP_L) ? (yHP_L * g + yBP_L) : 0.0f;
            s2_L = std::isfinite(yBP_L * g + yLP_L) ? (yBP_L * g + yLP_L) : 0.0f;
            s1_R = std::isfinite(yHP_R * g + yBP_R) ? (yHP_R * g + yBP_R) : 0.0f;
            s2_R = std::isfinite(yBP_R * g + yLP_R) ? (yBP_R * g + yLP_R) : 0.0f;
        }

        // Update state from SIMD processing
        s1_L = std::isfinite(s1.get(0)) ? s1.get(0) : 0.0f;
        s1_R = std::isfinite(s1.get(1)) ? s1.get(1) : 0.0f;
        s2_L = std::isfinite(s2.get(0)) ? s2.get(0) : 0.0f;
        s2_R = std::isfinite(s2.get(1)) ? s2.get(1) : 0.0f;
    }
    else
    {
        float* mono = block.getChannelPointer(0);
        vFloat s1;
        s1.set(0, s1_L);
        for (size_t k = 1; k < vFloat::size(); ++k) s1.set(k, 0.0f);
        vFloat s2;
        s2.set(0, s2_L);
        for (size_t k = 1; k < vFloat::size(); ++k) s2.set(k, 0.0f);

        // FIX: Separate SIMD and scalar loops
        const size_t simdWidth = vFloat::size();
        const size_t simdLimit = numSamples - (numSamples % simdWidth);

        // Process SIMD chunks
        for (size_t i = 0; i < simdLimit; ++i)
        {
            const float inM = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            vFloat x;
            x.set(0, inM);
            for (size_t k = 1; k < vFloat::size(); ++k) x.set(k, 0.0f);

            const float gVal = std::isfinite(eqGainArray[i]) ? eqGainArray[i] : 1.0f;
            const vFloat gain (gVal);

            vFloat yHP = vH * (x - s1 * (vG + vR2) - s2);
            vFloat yBP = yHP * vG + s1;
            s1 = yHP * vG + yBP;
            vFloat yLP = yBP * vG + s2;
            s2 = yBP * vG + yLP;

            vFloat out = x + (yBP * vInvQ) * (gain - vOne);
            const float outM = out.get(0);
            mono[i] = std::isfinite(outM) ? outM : 0.0f;
        }

        // Process remainder samples
        for (size_t i = simdLimit; i < numSamples; ++i)
        {
            const float inM = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            const float gVal = std::isfinite(eqGainArray[i]) ? eqGainArray[i] : 1.0f;

            const float yHP = h * (inM - s1_L * (g + R2) - s2_L);
            const float yBP = yHP * g + s1_L;
            const float yLP = yBP * g + s2_L;
            const float outM = inM + (yBP * 0.5f) * (gVal - 1.0f);

            mono[i] = std::isfinite(outM) ? outM : 0.0f;
            s1_L = std::isfinite(yHP * g + yBP) ? (yHP * g + yBP) : 0.0f;
            s2_L = std::isfinite(yBP * g + yLP) ? (yBP * g + yLP) : 0.0f;
        }

        // Update state from SIMD processing
        s1_L = std::isfinite(s1.get(0)) ? s1.get(0) : 0.0f;
        s2_L = std::isfinite(s2.get(0)) ? s2.get(0) : 0.0f;
    }
}

void DynamicEq::processBlock(juce::dsp::AudioBlock<float>& block, const float* envArray, float depthDb) noexcept
{
    const size_t numSamples = block.getNumSamples();
    const size_t numChannels = block.getNumChannels();

    if (numSamples == 0 || numChannels == 0 || envArray == nullptr)
        return;

    if (std::abs(depthDb) < 0.01f)
        return;

    if (!std::isfinite(s1_L)) s1_L = 0.0f;
    if (!std::isfinite(s1_R)) s1_R = 0.0f;
    if (!std::isfinite(s2_L)) s2_L = 0.0f;
    if (!std::isfinite(s2_R)) s2_R = 0.0f;

    using FreakPhase::DSP::vFloat;
    using FreakPhase::DSP::fastExp2SIMD;

    constexpr float invQ = 0.5f;
    const float depthCoeff = depthDb * 0.1660964f; // (depthDb / 20) * log2(10)

    const vFloat vG (g);
    const vFloat vR2 (R2);
    const vFloat vH (h);
    const vFloat vInvQ (invQ);
    const vFloat vOne (1.0f);
    const vFloat vDepthCoeff (depthCoeff);

    if (numChannels >= 2)
    {
        float* left = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);

        vFloat s1;
        s1.set(0, s1_L);
        s1.set(1, s1_R);
        for (size_t k = 2; k < vFloat::size(); ++k) s1.set(k, 0.0f);

        vFloat s2;
        s2.set(0, s2_L);
        s2.set(1, s2_R);
        for (size_t k = 2; k < vFloat::size(); ++k) s2.set(k, 0.0f);

        // FIX: Separate SIMD and scalar loops
        const size_t simdWidth = vFloat::size();
        const size_t simdLimit = numSamples - (numSamples % simdWidth);

        // Process SIMD chunks
        for (size_t i = 0; i < simdLimit; ++i)
        {
            const float inL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;

            #if FP_USE_SSE2
            vFloat x = vFloat::fromNative(_mm_set_ps(0.0f, 0.0f, inR, inL));
            #else
            vFloat x;
            x.set(0, inL);
            x.set(1, inR);
            for (size_t k = 2; k < vFloat::size(); ++k) x.set(k, 0.0f);
            #endif

            // Vectorized Chebyshev minimax polynomial for dynamic cut depth
            const float eVal = std::isfinite(envArray[i]) ? envArray[i] : 0.0f;
            const vFloat vEnv (eVal);
            const vFloat linGain = fastExp2SIMD(vEnv * vDepthCoeff);

            vFloat yHP = vH * (x - s1 * (vG + vR2) - s2);
            vFloat yBP = yHP * vG + s1;
            s1 = yHP * vG + yBP;
            vFloat yLP = yBP * vG + s2;
            s2 = yBP * vG + yLP;

            vFloat out = x + (yBP * vInvQ) * (linGain - vOne);

            const float outL = out.get(0);
            const float outR = out.get(1);
            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
        }

        // Process remainder samples
        for (size_t i = simdLimit; i < numSamples; ++i)
        {
            const float inL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;
            const float eVal = std::isfinite(envArray[i]) ? envArray[i] : 0.0f;
            const float linGain = std::exp2(eVal * depthCoeff);

            const float yHP_L = h * (inL - s1_L * (g + R2) - s2_L);
            const float yBP_L = yHP_L * g + s1_L;
            const float yLP_L = yBP_L * g + s2_L;
            const float outL = inL + (yBP_L * 0.5f) * (linGain - 1.0f);
            
            const float yHP_R = h * (inR - s1_R * (g + R2) - s2_R);
            const float yBP_R = yHP_R * g + s1_R;
            const float yLP_R = yBP_R * g + s2_R;
            const float outR = inR + (yBP_R * 0.5f) * (linGain - 1.0f);

            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
            
            s1_L = std::isfinite(yHP_L * g + yBP_L) ? (yHP_L * g + yBP_L) : 0.0f;
            s2_L = std::isfinite(yBP_L * g + yLP_L) ? (yBP_L * g + yLP_L) : 0.0f;
            s1_R = std::isfinite(yHP_R * g + yBP_R) ? (yHP_R * g + yBP_R) : 0.0f;
            s2_R = std::isfinite(yBP_R * g + yLP_R) ? (yBP_R * g + yLP_R) : 0.0f;
        }

        // Update state from SIMD processing
        s1_L = std::isfinite(s1.get(0)) ? s1.get(0) : 0.0f;
        s1_R = std::isfinite(s1.get(1)) ? s1.get(1) : 0.0f;
        s2_L = std::isfinite(s2.get(0)) ? s2.get(0) : 0.0f;
        s2_R = std::isfinite(s2.get(1)) ? s2.get(1) : 0.0f;
    }
    else
    {
        float* mono = block.getChannelPointer(0);
        vFloat s1;
        s1.set(0, s1_L);
        for (size_t k = 1; k < vFloat::size(); ++k) s1.set(k, 0.0f);
        vFloat s2;
        s2.set(0, s2_L);
        for (size_t k = 1; k < vFloat::size(); ++k) s2.set(k, 0.0f);

        // FIX: Separate SIMD and scalar loops
        const size_t simdWidth = vFloat::size();
        const size_t simdLimit = numSamples - (numSamples % simdWidth);

        // Process SIMD chunks
        for (size_t i = 0; i < simdLimit; ++i)
        {
            const float inM = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            vFloat x;
            x.set(0, inM);
            for (size_t k = 1; k < vFloat::size(); ++k) x.set(k, 0.0f);

            const float eVal = std::isfinite(envArray[i]) ? envArray[i] : 0.0f;
            const vFloat vEnv (eVal);
            const vFloat linGain = fastExp2SIMD(vEnv * vDepthCoeff);

            vFloat yHP = vH * (x - s1 * (vG + vR2) - s2);
            vFloat yBP = yHP * vG + s1;
            s1 = yHP * vG + yBP;
            vFloat yLP = yBP * vG + s2;
            s2 = yBP * vG + yLP;

            vFloat out = x + (yBP * vInvQ) * (linGain - vOne);
            const float outM = out.get(0);
            mono[i] = std::isfinite(outM) ? outM : 0.0f;
        }

        // Process remainder samples
        for (size_t i = simdLimit; i < numSamples; ++i)
        {
            const float inM = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            const float eVal = std::isfinite(envArray[i]) ? envArray[i] : 0.0f;
            const float linGain = std::exp2(eVal * depthCoeff);

            const float yHP = h * (inM - s1_L * (g + R2) - s2_L);
            const float yBP = yHP * g + s1_L;
            const float yLP = yBP * g + s2_L;
            const float outM = inM + (yBP * 0.5f) * (linGain - 1.0f);

            mono[i] = std::isfinite(outM) ? outM : 0.0f;
            s1_L = std::isfinite(yHP * g + yBP) ? (yHP * g + yBP) : 0.0f;
            s2_L = std::isfinite(yBP * g + yLP) ? (yBP * g + yLP) : 0.0f;
        }

        // Update state from SIMD processing
        s1_L = std::isfinite(s1.get(0)) ? s1.get(0) : 0.0f;
        s2_L = std::isfinite(s2.get(0)) ? s2.get(0) : 0.0f;
    }
}

float DynamicEq::processSample(int channel, float inputSample, float envelopeValue) noexcept
{
    const int ch = (channel == 1) ? 1 : 0;
    float& s1 = (ch == 1) ? s1_R : s1_L;
    float& s2 = (ch == 1) ? s2_R : s2_L;

    if (!std::isfinite(s1)) s1 = 0.0f;
    if (!std::isfinite(s2)) s2 = 0.0f;
    const float inVal = std::isfinite(inputSample) ? inputSample : 0.0f;
    const float eVal = std::isfinite(envelopeValue) ? envelopeValue : 0.0f;

    const float yHP = h * (inVal - s1 * (g + R2) - s2);
    const float yBP = yHP * g + s1;
    s1 = std::isfinite(yHP * g + yBP) ? (yHP * g + yBP) : 0.0f;
    const float yLP = yBP * g + s2;
    s2 = std::isfinite(yBP * g + yLP) ? (yBP * g + yLP) : 0.0f;

    constexpr float invQ = 0.5f;
    const float out = inVal + (yBP * invQ) * eVal * eqGainLinear;
    return std::isfinite(out) ? out : 0.0f;
}

void DynamicEq::process(const juce::dsp::ProcessContextReplacing<float>& context) noexcept
{
    auto&& block = context.getOutputBlock();
    const size_t numSamples = block.getNumSamples();
    const size_t numChannels = block.getNumChannels();

    if (numSamples == 0 || numChannels == 0 || std::abs(currentDepthDb) < 0.01f)
        return;

    if (!std::isfinite(s1_L)) s1_L = 0.0f;
    if (!std::isfinite(s1_R)) s1_R = 0.0f;
    if (!std::isfinite(s2_L)) s2_L = 0.0f;
    if (!std::isfinite(s2_R)) s2_R = 0.0f;

    using FreakPhase::DSP::vFloat;
    constexpr float invQ = 0.5f;

    const vFloat vG (g);
    const vFloat vR2 (R2);
    const vFloat vH (h);
    const vFloat vInvQ (invQ);
    const vFloat vEqGain (eqGainLinear);

    if (numChannels >= 2)
    {
        float* left = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);

        vFloat s1;
        s1.set(0, s1_L);
        s1.set(1, s1_R);
        for (size_t k = 2; k < vFloat::size(); ++k) s1.set(k, 0.0f);

        vFloat s2;
        s2.set(0, s2_L);
        s2.set(1, s2_R);
        for (size_t k = 2; k < vFloat::size(); ++k) s2.set(k, 0.0f);

        // FIX: Separate SIMD and scalar loops
        const size_t simdWidth = vFloat::size();
        const size_t simdLimit = numSamples - (numSamples % simdWidth);

        // Process SIMD chunks
        for (size_t i = 0; i < simdLimit; ++i)
        {
            const float inL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;

            #if FP_USE_SSE2
            vFloat x = vFloat::fromNative(_mm_set_ps(0.0f, 0.0f, inR, inL));
            #else
            vFloat x;
            x.set(0, inL);
            x.set(1, inR);
            for (size_t k = 2; k < vFloat::size(); ++k) x.set(k, 0.0f);
            #endif

            vFloat yHP = vH * (x - s1 * (vG + vR2) - s2);
            vFloat yBP = yHP * vG + s1;
            s1 = yHP * vG + yBP;
            vFloat yLP = yBP * vG + s2;
            s2 = yBP * vG + yLP;

            vFloat out = x + (yBP * vInvQ) * vEqGain;
            const float outL = out.get(0);
            const float outR = out.get(1);
            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
        }

        // Process remainder samples
        for (size_t i = simdLimit; i < numSamples; ++i)
        {
            const float inL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;

            const float yHP_L = h * (inL - s1_L * (g + R2) - s2_L);
            const float yBP_L = yHP_L * g + s1_L;
            const float yLP_L = yBP_L * g + s2_L;
            const float outL = inL + (yBP_L * 0.5f) * eqGainLinear;
            
            const float yHP_R = h * (inR - s1_R * (g + R2) - s2_R);
            const float yBP_R = yHP_R * g + s1_R;
            const float yLP_R = yBP_R * g + s2_R;
            const float outR = inR + (yBP_R * 0.5f) * eqGainLinear;

            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
            
            s1_L = std::isfinite(yHP_L * g + yBP_L) ? (yHP_L * g + yBP_L) : 0.0f;
            s2_L = std::isfinite(yBP_L * g + yLP_L) ? (yBP_L * g + yLP_L) : 0.0f;
            s1_R = std::isfinite(yHP_R * g + yBP_R) ? (yHP_R * g + yBP_R) : 0.0f;
            s2_R = std::isfinite(yBP_R * g + yLP_R) ? (yBP_R * g + yLP_R) : 0.0f;
        }

        // Update state from SIMD processing
        s1_L = std::isfinite(s1.get(0)) ? s1.get(0) : 0.0f;
        s1_R = std::isfinite(s1.get(1)) ? s1.get(1) : 0.0f;
        s2_L = std::isfinite(s2.get(0)) ? s2.get(0) : 0.0f;
        s2_R = std::isfinite(s2.get(1)) ? s2.get(1) : 0.0f;
    }
}
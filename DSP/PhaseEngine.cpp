#include "PhaseEngine.h"

void PhaseEngine::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    sampleRate = static_cast<float>(spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0);
    nyquist = sampleRate * 0.5f;
    coeffDirty = true; // sample rate changed -> cached rotation coefficients are stale
    reset();
}

void PhaseEngine::reset() noexcept
{
    s1_L = 0.0f; s1_R = 0.0f;
    s2_L = 0.0f; s2_R = 0.0f;
}

void PhaseEngine::setBaseRotation(float rotationDegrees, float centerFreq) noexcept
{
    // Non-finite args must never touch the cache or the coefficients: under
    // /fp:fast the clamp below folds NaN to garbage and the unordered ==
    // cache compare can then pin it forever (challenger F2). Keep the last
    // valid coefficients instead.
    if (! std::isfinite(rotationDegrees) || ! std::isfinite(centerFreq))
        return;

    // Exact-equality cache (no epsilon): knob/LUT values arrive as identical float bits
    // while static, so a recompute would produce bit-identical coefficients.
    if (! coeffDirty && rotationDegrees == lastRotation && centerFreq == lastCenter)
        return;

    lastRotation = rotationDegrees;
    lastCenter = centerFreq;
    coeffDirty = false;

    centerFreq = std::clamp(centerFreq, 20.0f, nyquist * 0.49f);
    rotationDegrees = std::clamp(rotationDegrees, -180.0f, 180.0f);

    float targetPhase = rotationDegrees;
    if (targetPhase > 0.0f) {
        targetPhase -= 360.0f; // All-pass filters delay phase (negative shift)
    }
    if (targetPhase > -1.0f) {
        targetPhase = -1.0f; // Guard against division by zero at 0 degrees
    }

    // TPT Bilinear Transform for precise discrete-time APF frequency mapping
    const float phiRad = targetPhase * juce::MathConstants<float>::pi / 180.0f;
    const float num = std::tan(juce::MathConstants<float>::pi * centerFreq / sampleRate);
    const float den = std::tan(-phiRad / 4.0f);
    
    const float g = num / den;
    baseCutoff = (sampleRate / juce::MathConstants<float>::pi) * std::atan(g);
    baseCutoff = std::clamp(baseCutoff, 10.0f, nyquist * 0.49f);
    tanFactor = baseCutoff / centerFreq;
}

void PhaseEngine::updateCenterFreq(float centerFreq) noexcept
{
    // F2: non-finite pitch-track input must not fold to clamp-hi under /fp:fast —
    // keep the last valid baseCutoff.
    if (! std::isfinite(centerFreq))
        return;

    coeffDirty = true; // pitch-track write to baseCutoff: next setBaseRotation must recompute
    centerFreq = std::clamp(centerFreq, 20.0f, nyquist * 0.49f);
    baseCutoff = std::clamp(centerFreq * tanFactor, 10.0f, nyquist * 0.49f);
}

void PhaseEngine::processBlock(juce::dsp::AudioBlock<float>& block, const float* gArray) noexcept
{
    const size_t numChannels = block.getNumChannels();
    const size_t numSamples = block.getNumSamples();

    if (numChannels == 0 || numSamples == 0 || gArray == nullptr)
        return;

    // Sanitize filter states to guarantee instant recovery from past NaN/Inf
    if (!std::isfinite(s1_L)) s1_L = 0.0f;
    if (!std::isfinite(s1_R)) s1_R = 0.0f;
    if (!std::isfinite(s2_L)) s2_L = 0.0f;
    if (!std::isfinite(s2_R)) s2_R = 0.0f;

    using FreakPhase::DSP::vFloat;

    if (numChannels >= 2)
    {
        float* left = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);

        vFloat state1;
        state1.set(0, s1_L);
        state1.set(1, s1_R);
        for (size_t k = 2; k < vFloat::size(); ++k) state1.set(k, 0.0f);

        vFloat state2;
        state2.set(0, s2_L);
        state2.set(1, s2_R);
        for (size_t k = 2; k < vFloat::size(); ++k) state2.set(k, 0.0f);

        const vFloat two (2.0f);

        for (size_t i = 0; i < numSamples; ++i)
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

            const float gVal = std::isfinite(gArray[i]) ? gArray[i] : 0.5f;
            const vFloat G (gVal);

            // Stage 1 TPT All-Pass using juce::dsp::SIMDRegister operators
            vFloat v0 = G * (x - state1);
            vFloat y_lp0 = v0 + state1;
            state1 = y_lp0 + v0;
            x = two * y_lp0 - x;

            // Stage 2 TPT All-Pass using juce::dsp::SIMDRegister operators
            vFloat v1 = G * (x - state2);
            vFloat y_lp1 = v1 + state2;
            state2 = y_lp1 + v1;
            x = two * y_lp1 - x;

            const float outL = x.get(0);
            const float outR = x.get(1);
            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
        }

        s1_L = std::isfinite(state1.get(0)) ? state1.get(0) : 0.0f;
        s1_R = std::isfinite(state1.get(1)) ? state1.get(1) : 0.0f;
        s2_L = std::isfinite(state2.get(0)) ? state2.get(0) : 0.0f;
        s2_R = std::isfinite(state2.get(1)) ? state2.get(1) : 0.0f;
    }
    else
    {
        // Mono fallback using juce::dsp::SIMDRegister
        float* mono = block.getChannelPointer(0);
        vFloat state1;
        state1.set(0, s1_L);
        for (size_t k = 1; k < vFloat::size(); ++k) state1.set(k, 0.0f);

        vFloat state2;
        state2.set(0, s2_L);
        for (size_t k = 1; k < vFloat::size(); ++k) state2.set(k, 0.0f);

        const vFloat two (2.0f);

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float inM = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            vFloat x;
            x.set(0, inM);
            for (size_t k = 1; k < vFloat::size(); ++k) x.set(k, 0.0f);

            const float gVal = std::isfinite(gArray[i]) ? gArray[i] : 0.5f;
            const vFloat G (gVal);

            vFloat v0 = G * (x - state1);
            vFloat y_lp0 = v0 + state1;
            state1 = y_lp0 + v0;
            x = two * y_lp0 - x;

            vFloat v1 = G * (x - state2);
            vFloat y_lp1 = v1 + state2;
            state2 = y_lp1 + v1;
            x = two * y_lp1 - x;

            const float outM = x.get(0);
            mono[i] = std::isfinite(outM) ? outM : 0.0f;
        }

        s1_L = std::isfinite(state1.get(0)) ? state1.get(0) : 0.0f;
        s2_L = std::isfinite(state2.get(0)) ? state2.get(0) : 0.0f;
    }
}

void PhaseEngine::processBlockStatic(juce::dsp::AudioBlock<float>& block, float staticG) noexcept
{
    const size_t numSamples = block.getNumSamples();
    const size_t numChannels = block.getNumChannels();
    if (numSamples == 0 || numChannels == 0) return;

    if (!std::isfinite(s1_L)) s1_L = 0.0f;
    if (!std::isfinite(s1_R)) s1_R = 0.0f;
    if (!std::isfinite(s2_L)) s2_L = 0.0f;
    if (!std::isfinite(s2_R)) s2_R = 0.0f;

    using FreakPhase::DSP::vFloat;

    const float cleanG = std::isfinite(staticG) ? staticG : 0.5f;

    if (numChannels >= 2)
    {
        float* left  = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);

        vFloat state1;
        state1.set(0, s1_L);
        state1.set(1, s1_R);
        for (size_t k = 2; k < vFloat::size(); ++k) state1.set(k, 0.0f);

        vFloat state2;
        state2.set(0, s2_L);
        state2.set(1, s2_R);
        for (size_t k = 2; k < vFloat::size(); ++k) state2.set(k, 0.0f);

        const vFloat two (2.0f);
        const vFloat G (cleanG);

        for (size_t i = 0; i < numSamples; ++i)
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

            vFloat v0 = G * (x - state1);
            vFloat y_lp0 = v0 + state1;
            state1 = y_lp0 + v0;
            x = two * y_lp0 - x;

            vFloat v1 = G * (x - state2);
            vFloat y_lp1 = v1 + state2;
            state2 = y_lp1 + v1;
            x = two * y_lp1 - x;

            const float outL = x.get(0);
            const float outR = x.get(1);
            left[i]  = std::isfinite(outL) ? outL : 0.0f;
            right[i] = std::isfinite(outR) ? outR : 0.0f;
        }

        s1_L = std::isfinite(state1.get(0)) ? state1.get(0) : 0.0f;
        s1_R = std::isfinite(state1.get(1)) ? state1.get(1) : 0.0f;
        s2_L = std::isfinite(state2.get(0)) ? state2.get(0) : 0.0f;
        s2_R = std::isfinite(state2.get(1)) ? state2.get(1) : 0.0f;
    }
    else
    {
        float* mono = block.getChannelPointer(0);
        vFloat state1;
        state1.set(0, s1_L);
        for (size_t k = 1; k < vFloat::size(); ++k) state1.set(k, 0.0f);

        vFloat state2;
        state2.set(0, s2_L);
        for (size_t k = 1; k < vFloat::size(); ++k) state2.set(k, 0.0f);

        const vFloat two (2.0f);
        const vFloat G (cleanG);

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float inM = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            vFloat x;
            x.set(0, inM);
            for (size_t k = 1; k < vFloat::size(); ++k) x.set(k, 0.0f);

            vFloat v0 = G * (x - state1);
            vFloat y_lp0 = v0 + state1;
            state1 = y_lp0 + v0;
            x = two * y_lp0 - x;

            vFloat v1 = G * (x - state2);
            vFloat y_lp1 = v1 + state2;
            state2 = y_lp1 + v1;
            x = two * y_lp1 - x;

            const float outM = x.get(0);
            mono[i] = std::isfinite(outM) ? outM : 0.0f;
        }

        s1_L = std::isfinite(state1.get(0)) ? state1.get(0) : 0.0f;
        s2_L = std::isfinite(state2.get(0)) ? state2.get(0) : 0.0f;
    }
}

void PhaseEngine::processBlock(juce::dsp::AudioBlock<float>& block) noexcept
{
    const float omega = juce::MathConstants<float>::pi * baseCutoff / sampleRate;
    const float G = FreakPhase::DSP::fastGScalar(omega);
    processBlockStatic(block, G);
}

void PhaseEngine::process(const juce::dsp::ProcessContextReplacing<float>& context) noexcept
{
    auto&& block = context.getOutputBlock();
    processBlock(block);
}

float PhaseEngine::processSample(int channel, float inputSample, float modulationAmount) noexcept
{
    const int ch = (channel == 1) ? 1 : 0;
    float& s1 = (ch == 1) ? s1_R : s1_L;
    float& s2 = (ch == 1) ? s2_R : s2_L;

    // Dynamically modulate cutoff frequency exponentially in pitch octaves around baseCutoff
    const float octScale = FreakPhase::DSP::fastExp2Scalar(modulationAmount * 2.0f);
    const float currentCutoff = std::clamp(baseCutoff * octScale, 10.0f, nyquist * 0.49f);

    const float wd = juce::MathConstants<float>::pi * currentCutoff / sampleRate;
    const float G = FreakPhase::DSP::fastGScalar(wd);

    float x = inputSample;

    // Stage 1 TPT All-Pass
    const float v0 = G * (x - s1);
    const float y_lp0 = v0 + s1;
    s1 = y_lp0 + v0;
    x = 2.0f * y_lp0 - x;

    // Stage 2 TPT All-Pass
    const float v1 = G * (x - s2);
    const float y_lp1 = v1 + s2;
    s2 = y_lp1 + v1;
    x = 2.0f * y_lp1 - x;

    return x;
}

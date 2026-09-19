#pragma once
#include <JuceHeader.h>
#include "SimdMath.h"
#include <algorithm>

class PhaseEngine
{
public:
    PhaseEngine() = default;

    void prepare(const juce::dsp::ProcessSpec& spec) noexcept;
    void reset() noexcept;

    // Calculates static base cutoff based on target rotation (-180 to +180 deg) and center frequency
    void setBaseRotation(float rotationDegrees, float centerFreq = 60.0f) noexcept;

    // Smoothly updates center frequency (e.g. from real-time pitch tracker) with zero trigonometry
    void updateCenterFreq(float centerFreq) noexcept;

    [[nodiscard]] float getBaseCutoff() const noexcept { return baseCutoff; }

    // =========================================================================
    // SIMD Block-Processing Engine using juce::dsp::AudioBlock<float>
    // =========================================================================
    // Modulated block processing: gArray contains per-sample Bilinear G factors
    void processBlock(juce::dsp::AudioBlock<float>& block, const float* gArray) noexcept;

    // Static unmodulated phase rotation across block
    void processBlock(juce::dsp::AudioBlock<float>& block) noexcept;
    void processBlockStatic(juce::dsp::AudioBlock<float>& block, float staticG) noexcept;

    // Standard JUCE DSP Process Context overload
    void process(const juce::dsp::ProcessContextReplacing<float>& context) noexcept;

    // Backward-compatible per-sample processor
    [[nodiscard]] float processSample(int channel, float inputSample, float modulationAmount) noexcept;

private:
    float sampleRate = 44100.0f;
    float nyquist = 22050.0f;
    float baseCutoff = 60.0f;
    float tanFactor = 1.0f;

    // Coefficient cache for setBaseRotation: exact bit equality (same call sequence
    // must recompute after updateCenterFreq/prepare touch baseCutoff or sample rate).
    float lastRotation = 0.0f;
    float lastCenter = 0.0f;
    bool coeffDirty = true;

    // Custom 1st-order TPT All-Pass Filter states for Stereo [stage 1, stage 2]
    float s1_L = 0.0f, s1_R = 0.0f;
    float s2_L = 0.0f, s2_R = 0.0f;
};
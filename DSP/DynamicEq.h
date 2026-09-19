#pragma once
#include <JuceHeader.h>
#include "SimdMath.h"

class DynamicEq
{
public:
    DynamicEq() = default;

    void prepare(const juce::dsp::ProcessSpec& spec) noexcept;
    void setParameters(float frequency, float depthDb) noexcept;
    void reset() noexcept;

    // SIMD Block-Processing with pre-calculated linear EQ gains using juce::dsp::SIMDRegister<float>
    void processBlock(juce::dsp::AudioBlock<float>& block, const float* eqGainArray) noexcept;

    // SIMD Block-Processing with raw envelope array and dynamic cut depth in dB using fastExp2SIMD
    void processBlock(juce::dsp::AudioBlock<float>& block, const float* envArray, float depthDb) noexcept;

    void process(const juce::dsp::ProcessContextReplacing<float>& context) noexcept;

    [[nodiscard]] float processSample(int channel, float inputSample, float envelopeValue) noexcept;

private:
    void updateCoefficients() noexcept;

    float sampleRate = 44100.0f;
    float currentFreq = 60.0f;
    float currentDepthDb = 0.0f;
    float eqGainLinear = 0.0f;
    float lastFreq = -1.0f;
    float lastDepth = -999.0f;

    // TPT SVF Filter coefficients
    float g = 0.0f;
    float R2 = 0.5f; // Resonance 2.0 -> R2 = 1 / Q = 0.5
    float h = 0.0f;

    // Stereo TPT SVF State Variables: Lane 0 = Left, Lane 1 = Right
    float s1_L = 0.0f, s1_R = 0.0f;
    float s2_L = 0.0f, s2_R = 0.0f;
};
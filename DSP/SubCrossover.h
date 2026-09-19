#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

/**
 * @class SubCrossover
 * @brief 4th-Order Linkwitz-Riley (LR4, 24 dB/oct) stereo crossover filter.
 *
 * Cascades two 2nd-order Butterworth filters in series for both Low-Pass and High-Pass branches.
 * At the crossover frequency, the outputs sum to an all-pass flat magnitude response (0 dB ripple).
 * Real-time safe: Zero memory allocation inside processBlock/process.
 */
class SubCrossover
{
public:
    SubCrossover() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = static_cast<float>(spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0);

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        for (int ch = 0; ch < 2; ++ch)
        {
            lpFilter1[ch].prepare(monoSpec);
            lpFilter2[ch].prepare(monoSpec);
            hpFilter1[ch].prepare(monoSpec);
            hpFilter2[ch].prepare(monoSpec);
            
            lpFilter1[ch].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            lpFilter2[ch].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            hpFilter1[ch].setType(juce::dsp::StateVariableTPTFilterType::highpass);
            hpFilter2[ch].setType(juce::dsp::StateVariableTPTFilterType::highpass);
        }

        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            lpFilter1[ch].reset();
            lpFilter2[ch].reset();
            hpFilter1[ch].reset();
            hpFilter2[ch].reset();
        }
    }

    void setCrossoverFrequency(float freqHz) noexcept
    {
        float clamped = std::clamp(freqHz, 40.0f, std::min(350.0f, sampleRate * 0.45f));
        if (std::abs(clamped - cutoffFreq) > 0.1f)
        {
            cutoffFreq = clamped;
            updateCoefficients();
        }
    }

    float getCrossoverFrequency() const noexcept { return cutoffFreq; }

    /**
     * Splits inputBuffer into subBuffer (<= cutoffFreq) and highBuffer (> cutoffFreq).
     * Buffers must have sufficient channel and sample counts.
     */
    void process(const juce::AudioBuffer<float>& inputBuffer,
                 juce::AudioBuffer<float>& subBuffer,
                 juce::AudioBuffer<float>& highBuffer,
                 int numSamples) noexcept
    {
        const int numChannels = std::min(2, inputBuffer.getNumChannels());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in = inputBuffer.getReadPointer(ch);
            float* sub = subBuffer.getWritePointer(ch);
            float* high = highBuffer.getWritePointer(ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float x = in[i];

                // Low-Pass LR4: 2 cascaded 2nd-order Butterworth LP stages
                float lp1 = lpFilter1[ch].processSample(0, x);
                float lp2 = lpFilter2[ch].processSample(0, lp1);
                
                sub[i] = lp2;

                // High-Pass LR4: 2 cascaded 2nd-order Butterworth HP stages
                float hp1 = hpFilter1[ch].processSample(0, x);
                float hp2 = hpFilter2[ch].processSample(0, hp1);
                
                high[i] = hp2;
            }
        }
        if (numChannels == 1 && subBuffer.getNumChannels() > 1 && highBuffer.getNumChannels() > 1)
        {
            subBuffer.copyFrom(1, 0, subBuffer, 0, 0, numSamples);
            highBuffer.copyFrom(1, 0, highBuffer, 0, 0, numSamples);
        }
    }

private:
    void updateCoefficients() noexcept
    {
        // 2nd-order Butterworth Q factor = 1 / sqrt(2) ~ 0.70710678
        constexpr float butterworthQ = 0.7071067811865475f;

        for (int ch = 0; ch < 2; ++ch)
        {
            lpFilter1[ch].setCutoffFrequency(cutoffFreq);
            lpFilter1[ch].setResonance(butterworthQ);
            lpFilter2[ch].setCutoffFrequency(cutoffFreq);
            lpFilter2[ch].setResonance(butterworthQ);
            
            hpFilter1[ch].setCutoffFrequency(cutoffFreq);
            hpFilter1[ch].setResonance(butterworthQ);
            hpFilter2[ch].setCutoffFrequency(cutoffFreq);
            hpFilter2[ch].setResonance(butterworthQ);
        }
    }

    float sampleRate = 44100.0f;
    float cutoffFreq = 100.0f;

    // Stereo LR4 filter stages using TPT Filters (zero allocation)
    juce::dsp::StateVariableTPTFilter<float> lpFilter1[2];
    juce::dsp::StateVariableTPTFilter<float> lpFilter2[2];
    juce::dsp::StateVariableTPTFilter<float> hpFilter1[2];
    juce::dsp::StateVariableTPTFilter<float> hpFilter2[2];
};


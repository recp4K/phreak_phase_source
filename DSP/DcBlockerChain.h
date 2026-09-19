#pragma once
#include <JuceHeader.h>

class DcBlockerChain
{
public:
    void prepare(double sampleRate) noexcept
    {
        mainL.setSampleRate(sampleRate);
        mainR.setSampleRate(sampleRate);
        sideL.setSampleRate(sampleRate);
        sideR.setSampleRate(sampleRate);
        reset();
    }

    void process(juce::AudioBuffer<float>& mainBus, juce::AudioBuffer<float>& sideBus, int dcMode, int numSamples) noexcept
    {
        // dcMode: 0 = Off, 1 = Main, 2 = SC, 3 = Both
        if (dcMode == 1 || dcMode == 3)
        {
            // Process only the live block: buses are pre-allocated to full scratch capacity,
            // so advancing the one-pole state over stale tail samples would be block-history dependent.
            const int liveSamples = juce::jmin(numSamples, mainBus.getNumSamples());
            if (mainBus.getNumChannels() > 0)
            {
                auto* mL = mainBus.getWritePointer(0);
                for (int i = 0; i < liveSamples; ++i)
                    mL[i] = mainL.process(mL[i]);
            }
            if (mainBus.getNumChannels() > 1)
            {
                auto* mR = mainBus.getWritePointer(1);
                for (int i = 0; i < liveSamples; ++i)
                    mR[i] = mainR.process(mR[i]);
            }
        }

        if (dcMode == 2 || dcMode == 3)
        {
            const int liveSamples = juce::jmin(numSamples, sideBus.getNumSamples());
            if (sideBus.getNumChannels() > 0)
            {
                auto* sL = sideBus.getWritePointer(0);
                for (int i = 0; i < liveSamples; ++i)
                    sL[i] = sideL.process(sL[i]);
            }
            if (sideBus.getNumChannels() > 1)
            {
                auto* sR = sideBus.getWritePointer(1);
                for (int i = 0; i < liveSamples; ++i)
                    sR[i] = sideR.process(sR[i]);
            }
        }
    }

    void reset() noexcept
    {
        mainL.reset();
        mainR.reset();
        sideL.reset();
        sideR.reset();
    }

private:
    struct TrueDCBlocker {
        float R = 0.9995f, x1 = 0.0f, y1 = 0.0f;
        void setSampleRate(double sampleRate) noexcept {
            if (sampleRate > 1000.0) {
                // Cutoff at ~3.5 Hz to preserve all sub-bass and 808 fundamental frequencies
                R = 1.0f - static_cast<float>(2.0 * juce::MathConstants<double>::pi * 3.5 / sampleRate);
            }
        }
        float process(float x) noexcept {
            if (!std::isfinite(x)) x = 0.0f;
            if (!std::isfinite(x1)) x1 = 0.0f;
            if (!std::isfinite(y1)) y1 = 0.0f;
            float y = x - x1 + R * y1;
            y += 1e-15f;
            y -= 1e-15f;
            x1 = x;
            y1 = std::isfinite(y) ? y : 0.0f;
            return y1;
        }
        void reset() noexcept {
            x1 = 0.0f; y1 = 0.0f;
        }
    };
    TrueDCBlocker mainL, mainR, sideL, sideR;
};
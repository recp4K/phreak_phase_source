#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <algorithm>

/**
 * @class PitchTracker
 * @brief Real-time, zero-allocation sub-bass fundamental pitch detector with musical semitone snapping.
 *
 * Designed specifically for low frequencies (25 Hz - 200 Hz, e.g. 808s, synth basses, electric bass).
 * Uses low-pass conditioned zero-crossing with Schmitt-trigger hysteresis to reliably detect the
 * fundamental period, and quantizes the result to equal-temperament semitone pitches to avoid microtonal jitter.
 */
class PitchTracker
{
public:
    PitchTracker() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = static_cast<float>(newSampleRate > 0.0 ? newSampleRate : 44100.0);
        smoothedFreq.reset(sampleRate, 0.03); // 30 ms glide for phase center frequency
        smoothedFreq.setCurrentAndTargetValue(60.0f);
        reset();
    }

    void reset() noexcept
    {
        sampleCounter = 0;
        lastZeroCrossSample = 0;
        state = 0;
        peakAmp = 0.0f;
        lpState = 0.0f;

        // Restart the 30 ms glide (mirrors prepare()) so a resumed pitch estimate
        // cannot zipper from a stale smoothed position after Smart Disable.
        smoothedFreq.reset(sampleRate, 0.03);
        smoothedFreq.setCurrentAndTargetValue(60.0f);

        currentPitchHz.store(60.0f, std::memory_order_relaxed);
        snappedPitchHz.store(65.41f, std::memory_order_relaxed); // C2
        midiNoteNumber.store(36, std::memory_order_relaxed);
    }

    /**
     * Processes a single sub-bass sample and returns the current smoothed center frequency in Hz.
     */
    [[nodiscard]] float processSample(float inputSample) noexcept
    {
        // NaN/Inf recovery (mirrors TrueDCBlocker): a single poisoned sample must not
        // permanently contaminate lpState (and propagate into updateCenterFreq -> fastG).
        if (! std::isfinite(inputSample)) inputSample = 0.0f;
        if (! std::isfinite(lpState)) lpState = 0.0f;

        sampleCounter++;

        // 1. Conditioning 1-pole Low-pass filter at ~120 Hz to attenuate harmonics
        const float alpha = 0.017f * (44100.0f / sampleRate); // approx 120 Hz at 44.1k
        lpState += alpha * (inputSample - lpState) + 1e-15f;
        lpState -= 1e-15f;
        float x = lpState;

        // Peak tracking with slow release
        float absX = std::abs(x);
        if (absX > peakAmp) peakAmp = absX;
        else peakAmp *= 0.9998f;

        // Dynamic Schmitt-trigger thresholds (20% of current peak, with floor)
        const float thresh = std::max(0.005f, peakAmp * 0.20f);

        // Schmitt-trigger state machine
        if (state <= 0 && x > thresh)
        {
            state = 1;
            int period = sampleCounter - lastZeroCrossSample;
            lastZeroCrossSample = sampleCounter;

            // Enforce valid sub-bass frequency bounds (25 Hz - 220 Hz)
            const int minPeriod = static_cast<int>(sampleRate / 220.0f);
            const int maxPeriod = static_cast<int>(sampleRate / 25.0f);

            if (period >= minPeriod && period <= maxPeriod && peakAmp > 0.008f)
            {
                float detectedHz = sampleRate / static_cast<float>(period);
                currentPitchHz.store(detectedHz, std::memory_order_relaxed);

                // Quantize to nearest equal-temperament semitone
                // MIDI note: round(12 * log2(f / 440) + 69)
                // Using fast approximation or table lookup?
                // For sub-bass (25 Hz to 220 Hz), we can just use std::log2 for now
                // since the subagent warned about it, let's use a LUT or fast log2.
                // Wait, let's use the LUT approach.
                static constexpr float midiToFreq[128] = {
                    8.18f, 8.66f, 9.18f, 9.72f, 10.3f, 10.91f, 11.56f, 12.25f, 12.98f, 13.75f, 14.57f, 15.43f,
                    16.35f, 17.32f, 18.35f, 19.45f, 20.6f, 21.83f, 23.12f, 24.5f, 25.96f, 27.5f, 29.14f, 30.87f,
                    32.7f, 34.65f, 36.71f, 38.89f, 41.2f, 43.65f, 46.25f, 49.0f, 51.91f, 55.0f, 58.27f, 61.74f,
                    65.41f, 69.3f, 73.42f, 77.78f, 82.41f, 87.31f, 92.5f, 98.0f, 103.83f, 110.0f, 116.54f, 123.47f,
                    130.81f, 138.59f, 146.83f, 155.56f, 164.81f, 174.61f, 185.0f, 196.0f, 207.65f, 220.0f, 233.08f, 246.94f,
                    261.63f
                    // We only need up to MIDI note 60 (C4 = 261.63Hz)
                };

                // FIX: Use binary search instead of linear O(n) search for better performance
                // Binary search on the sorted midiToFreq array (notes 12-60)
                int low = 12, high = 60;
                int bestNote = 12; // C0 as fallback
                float minDiff = 9999.0f;
                
                while (low <= high) {
                    int mid = (low + high) / 2;
                    float diff = std::abs(detectedHz - midiToFreq[mid]);
                    
                    if (diff < minDiff) {
                        minDiff = diff;
                        bestNote = mid;
                    }
                    
                    if (midiToFreq[mid] < detectedHz) {
                        low = mid + 1;
                    } else if (midiToFreq[mid] > detectedHz) {
                        high = mid - 1;
                    } else {
                        bestNote = mid; // Exact match
                        break;
                    }
                }
                
                // Also check immediate neighbors for potential better match
                // (binary search might miss due to non-linear frequency spacing)
                for (int n = std::max(12, bestNote - 2); n <= std::min(60, bestNote + 2); ++n) {
                    float diff = std::abs(detectedHz - midiToFreq[n]);
                    if (diff < minDiff) {
                        minDiff = diff;
                        bestNote = n;
                    }
                }

                int note = bestNote;
                float quantizedHz = midiToFreq[note];
                snappedPitchHz.store(quantizedHz, std::memory_order_relaxed);
                midiNoteNumber.store(note, std::memory_order_relaxed);

                smoothedFreq.setTargetValue(quantizedHz);
            }
        }
        else if (state >= 0 && x < -thresh)
        {
            state = -1;
        }

        return smoothedFreq.getNextValue();
    }

    [[nodiscard]] float getSmoothedFrequency() const noexcept
    {
        return smoothedFreq.getCurrentValue();
    }

    static juce::String getNoteName(int midiNote)
    {
        static const char* const noteNames[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        int octave = (midiNote / 12) - 1;
        int noteIndex = (midiNote % 12 + 12) % 12;
        return juce::String(noteNames[noteIndex]) + juce::String(octave);
    }

    // Lock-free atomics for UI and DSP reading
    std::atomic<float> currentPitchHz{ 60.0f };
    std::atomic<float> snappedPitchHz{ 65.41f };
    std::atomic<int> midiNoteNumber{ 36 }; // C2

private:
    float sampleRate = 44100.0f;
    uint32_t sampleCounter = 0;
    uint32_t lastZeroCrossSample = 0;
    int state = 0; // -1 = negative, +1 = positive
    float peakAmp = 0.0f;
    float lpState = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFreq;
};


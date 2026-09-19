// SignalAnalyzer.h
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

class SignalAnalyzer
{
public:
    SignalAnalyzer() = default;

    void prepare(double sampleRate) noexcept;
    void setResponseMs(float ms) noexcept;
    void reset() noexcept;

    void processMeters(juce::AudioBuffer<float>& buffer, float& envRms, float& envPeak,
        std::atomic<float>& outRms, std::atomic<float>& outPeak,
        std::atomic<float>& persistentPeak, int numSamplesToScan) noexcept;

    void processCorrelation(juce::AudioBuffer<float>& mainBus, juce::AudioBuffer<float>& sideBus,
        std::atomic<float>& outCorrelation, std::atomic<float>& persistentMaxCorr,
        int trackBChannelMode, int numSamplesToScan) noexcept;

    // Hilbert-derived instantaneous phase processing (Zero allocation, real-time safe)
    void processInstantPhase(float mainSample, float scSample) noexcept;
    void processInstantPhase(const float* mainData, const float* scData, int numSamples) noexcept;
    void processInstantPhase(const float* mainData, const float* scDataL, const float* scDataR, int numSamples) noexcept;

    // Public atomics for instantaneous phase monitoring (polled by Phase Wheel / GUI)
    std::atomic<float> instantPhaseMain{ 0.0f };
    std::atomic<float> instantPhaseSc{ 0.0f };
    std::atomic<float> phaseDiffDeg{ 0.0f };
    std::atomic<float> phaseVectorMag{ 0.0f };

private:
    double sr = 44100.0;
    float targetMs = 50.0f;
    float corrEmaDot = 0.0f;
    float corrEmaMainSq = 0.0f;
    float corrEmaSideSq = 0.0f;

    // Hilbert All-Pass Filter state & coefficient
    float apfStateMain = 0.0f;
    float apfStateSc = 0.0f;
    float apfCoeff = 0.0f;
    float smoothedPhaseDiffDeg = 0.0f;
};
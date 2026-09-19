#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <complex>
#include <limits>
#include <algorithm>

struct SegmentAlignmentResult
{
    float delayMs { 0.0f };
    float delaySamples { 0.0f };
    float rotateDeg { 0.0f };
    bool polarityFlip { false };
    float correlationBefore { 0.0f };
    float correlationAfter { 0.0f };
    float detectedFreq { 60.0f };
    float suggestedEqCutDb { 0.0f };
    bool hasAudio { false };
};

class AutoAlignerThread : public juce::Thread
{
public:
    // FIX: Reduced FFT size for better performance (2048 points is sufficient for sub-bass alignment)
    static constexpr int fftOrder = 11; // 2048 points (was 13 = 8192)
    static constexpr int fftSize = 1 << fftOrder;

    AutoAlignerThread(juce::AudioBuffer<float>& buffer, std::atomic<int>& writeIdx, std::atomic<double>& sampleRate);
    ~AutoAlignerThread() override;

    void prepare();
    void run() override;

    SegmentAlignmentResult analyzeSlice(const float* mainData, const float* scData, int numSamples, double sampleRate);

    std::atomic<float> bestSubSampleDelay{ 0.0f };
    std::atomic<float> bestRotate{ 0.0f };
    std::atomic<float> bestDelay{ 0.0f };
    std::atomic<float> bestDelaySamples{ 0.0f };
    std::atomic<bool> bestFlip{ false };
    std::atomic<float> bestGain{ 0.0f };
    std::atomic<float> detectedFreq{ 60.0f };
    std::atomic<float> suggestedEqCutDb{ 0.0f };
    std::atomic<float> suggestedEqFreqHz{ 60.0f };
    std::atomic<float> progress{ 0.0f };
    std::atomic<float> initialCorrelation{ 0.0f };
    std::atomic<float> peakCorrelation{ 0.0f };
    std::atomic<bool> isFinished{ false };
    std::atomic<bool> autoAlignActive{ false };
    std::atomic<bool> timedOut{ false };
    std::atomic<int> alignTrackMode{ 1 }; // 0 = Continuous, 1 = Transient

    void triggerAnalysis(int trackMode = 1) noexcept;

private:
    juce::AudioBuffer<float>& captureBuffer;
    std::atomic<int>& writeIndex;
    std::atomic<double>& sr;

    // GCC-PHAT DSP scratch members & private snapshot
    juce::dsp::FFT fft{ fftOrder };
    std::vector<std::complex<float>> fftBufMain;
    std::vector<std::complex<float>> fftBufSc;
    std::vector<std::complex<float>> fftScratch;
    std::vector<float> hannWindow;
    std::vector<float> corrOutput;
    juce::AudioBuffer<float> snapshotBuffer;
    
    // FIX: Add mutex for thread-safe access to snapshotBuffer
    std::mutex snapshotMutex;
    bool lastFlipState{ false };

    juce::int64 scanStartTimeMs{ 0 };
    float continuousSmoothedDelMs{ 0.0f };
    float continuousSmoothedRotateDeg{ 0.0f };
    bool hasContinuousHistory{ false };
};

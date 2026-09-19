#pragma once
#include <JuceHeader.h>
#include "Core/ParameterCache.h"
#include "DSP/DcBlockerChain.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/PhaseEngine.h"
#include "DSP/DynamicEq.h"
#include "DSP/AutoAlignerThread.h"
#include "DSP/SubCrossover.h"
#include "DSP/PitchTracker.h"
#include "Analysis/SignalAnalyzer.h"
#include "Timeline/PlayheadTracker.h"
#include "Timeline/BakedTimelineLUT.h"
#include "Timeline/TimelineAudioCaptureFifo.h"
#include "Timeline/TimelineAnalysisThread.h"

class FreakPhaseAudioProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    FreakPhaseAudioProcessor();
    ~FreakPhaseAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // APVTS
    juce::AudioProcessorValueTreeState apvts;

    // Meters & Correlation (Lock-free atomic variables polled by GUI via juce::Timer)
    std::atomic<float> phaseCorrelation{ 0.0f };
    std::atomic<float> persistentMaxCorr{ 0.0f };

    std::atomic<float> outRmsM{ -60.0f };
    std::atomic<float> outPeakM{ -60.0f };
    std::atomic<float> persistentPeakM{ -60.0f };

    std::atomic<float> outRmsS{ -60.0f };
    std::atomic<float> outPeakS{ -60.0f };
    std::atomic<float> persistentPeakS{ -60.0f };

    std::atomic<float> outRmsMix{ -60.0f };
    std::atomic<float> outPeakMix{ -60.0f };
    std::atomic<float> persistentPeakMix{ -60.0f };

    std::atomic<bool> clearPeaksFlag{ false };
    std::atomic<bool> isAutoTracking{ false };
    std::atomic<bool> isTimelineArmed{ false };        // ARM state: ready to capture when DAW starts playing
    std::atomic<bool> isTimelineCaptureActive{ false }; // Gates timelineFifo.write during playback
    std::atomic<bool> wasPlayingLastBlock{ false };     // Detects DAW stop edge to trigger auto-finalization
    std::atomic<bool> isTimelineAnalyzing{ false };     // True during background GCC-PHAT computation
    std::atomic<float> timelineAnalysisProgress{ 0.0f }; // Progress 0.0 - 1.0
    std::atomic<uint32_t> lastDiscontinuityEpoch{ 0 };  // HIGH 3: detects seek/scrub discontinuities

    // Capture buffer for waveform visualization and AutoAligner
    static constexpr int captureBufferSize = 8 * 48000;
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int> captureWriteIndex{ 0 };

    // Atomic: shared with background worker threads (relaxed reads; a one-scan-stale
    // rate is behaviorally equivalent and removes the data race / torn reads).
    std::atomic<double> currentSampleRate { 44100.0 };
    AutoAlignerThread alignerThread;
    PitchTracker pitchTracker;

    void updatePdcLatency(int mode);
    SignalAnalyzer& getAnalyzer() noexcept { return analyzer; }

    float getPhaseSubBaseCutoff() const noexcept;
    float getPhaseHighBaseCutoff() const noexcept;
    float getPhaseMainBaseCutoff() const noexcept { return getPhaseSubBaseCutoff(); }
    float getPhaseSideBaseCutoff() const noexcept { return getPhaseHighBaseCutoff(); }

    FreakPhase::Timeline::PlayheadTracker& getPlayheadTracker() noexcept { return playheadTracker; }
    FreakPhase::Timeline::RcuTimelineManager& getTimelineManager() noexcept { return rcuTimelineManager; }
    FreakPhase::Timeline::TimelineAudioCaptureFifo& getTimelineFifo() noexcept { return timelineFifo; }
    FreakPhase::Timeline::TimelineAnalysisThread* getTimelineAnalysisThread() noexcept { return timelineAnalysisThread.get(); }

    struct TimelineSegmentData
    {
        int64_t startSample { 0 };
        int64_t endSample { 0 };
        float delaySamples { 0.0f };
        float rotateDeg { 0.0f };
        float eqCutDb { 0.0f };
        bool polarityFlip { false };
        bool bypassed { false };
        float correlationBefore { 0.0f };
        float correlationAfter { 0.0f };
    };

    void armTimelineLearn(bool arm) noexcept;
    void bakeTimeline(int64_t startSample, int64_t endSample, float delaySamples, float phaseDeg, float eqCutDb);
    void bakeTimelineSegments(const std::vector<TimelineSegmentData>& segments);
    void clearTimeline();

private:
    void handleAsyncUpdate() override;

    ParameterCache paramCache;

    DcBlockerChain dcBlocker;
    EnvelopeFollower envFollower;
    PhaseEngine phaseSub, phaseHigh;
    DynamicEq dynEq;
    SignalAnalyzer analyzer;
    SubCrossover subCrossover;

    juce::dsp::DelayLine<float> delaySub{ 96000 }, delayHigh{ 96000 };
    juce::dsp::DelayLine<float> lookaheadDelay{ 96000 };
    juce::dsp::DelayLine<float> highDelay{ 96000 };
    juce::dsp::DelayLine<float> delayTrackB{ 96000 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSubFlipGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSubDelay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTrackBDelay;

    juce::AudioBuffer<float> mixBuffer;
    juce::AudioBuffer<float> subBuffer;
    juce::AudioBuffer<float> highBuffer;
    juce::AudioBuffer<float> trackABuffer;
    juce::AudioBuffer<float> trackBBuffer;

    // SIMD scratch buffers pre-allocated in prepareToPlay (>= 8192 samples)
    juce::HeapBlock<float> scratchEnvBuffer;
    juce::HeapBlock<float> scratchGBuffer;
    juce::HeapBlock<float> scratchEqGainBuffer;

    FreakPhase::Timeline::PlayheadTracker playheadTracker;
    FreakPhase::Timeline::RcuTimelineManager rcuTimelineManager;
    FreakPhase::Timeline::TimelineAudioCaptureFifo timelineFifo;
    std::unique_ptr<FreakPhase::Timeline::TimelineAnalysisThread> timelineAnalysisThread;

    float envRmsM = 0.0f, envPeakM = 0.0f;
    float envRmsS = 0.0f, envPeakS = 0.0f;
    float envRmsMix = 0.0f, envPeakMix = 0.0f;

    int activeLatencyMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreakPhaseAudioProcessor)
};

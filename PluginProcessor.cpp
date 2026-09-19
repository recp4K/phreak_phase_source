#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout FreakPhaseAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Target Selection: Sub Band vs High Band
    params.push_back(std::make_unique<juce::AudioParameterChoice>("TARGET", "Target",
        juce::StringArray{ "Sub Band", "High Band" }, 0));

    // Monitor Output: Track A, Track B, or Summed MIX
    params.push_back(std::make_unique<juce::AudioParameterChoice>("MONITOR", "Monitor",
        juce::StringArray{ "Out: Track A", "Out: Track B", "Out: MIX" }, 2)); // Default to MIX for Summing Bus

    // Track B (Aux Input) Channel Selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>("TRACK_B_CH", "Track B Channel",
        juce::StringArray{ "Track B: Left", "Track B: Right", "Track B: L+R" }, 0));

    // DC Blocker Filter
    params.push_back(std::make_unique<juce::AudioParameterChoice>("DC_FILTER", "DC Filter",
        juce::StringArray{ "DC: Off", "DC: Track A", "DC: Track B", "DC: Both" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>("FREEZE", "Freeze", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("DELTA_LISTEN", "Delta Listen", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("WHEEL_COLLAPSED", "Phase Wheel Collapsed", false));

    // Independent SUB Band Parameters (Phase & Delay)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_ROTATE", "Sub Rotate",
        juce::NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_DELAY", "Sub Delay",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("SUB_FLIP", "Sub Flip", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_GAIN", "Sub Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_DYN_AMOUNT", "Sub Dyn Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    // Independent HIGH Band Parameters (Phase & Delay)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("HIGH_ROTATE", "High Rotate",
        juce::NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("HIGH_DELAY", "High Delay",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("HIGH_FLIP", "High Flip", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("HIGH_GAIN", "High Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("HIGH_DYN_AMOUNT", "High Dyn Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("RESPONSE", "Response",
        juce::NormalisableRange<float>(5.0f, 500.0f, 1.0f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("DYN_EQ_FREQ", "Dynamic EQ Freq",
        juce::NormalisableRange<float>(20.0f, 500.0f, 0.1f, 0.4f), 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DYN_EQ_DEPTH", "Dynamic EQ Depth",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DYN_PH_AMOUNT", "Dynamic Phase Mod",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("BASS_GLUE", "Bass Glue",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("ENV_ATTACK", "Env Attack",
        juce::NormalisableRange<float>(0.1f, 50.0f, 0.1f, 0.5f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ENV_RELEASE", "Env Release",
        juce::NormalisableRange<float>(5.0f, 500.0f, 1.0f, 0.5f), 100.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("LOOKAHEAD_MS", "Lookahead Ms",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 5.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("CROSSOVER_FREQ", "Sub Crossover Freq",
        juce::NormalisableRange<float>(40.0f, 300.0f, 1.0f, 0.5f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("CROSSOVER_ENABLE", "Sub Split Active", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("PITCH_TRACK", "Pitch Track Sub", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("ALIGN_TRACK_MODE", "Align Track Mode",
        juce::StringArray{ "Continuous", "Transient" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("LATENCY_MODE", "Latency Mode",
        juce::StringArray{ "Live (0ms)", "Precision (5ms)" }, 1));

    // Display
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ZOOMX", "Zoom X",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("PANX", "Pan X",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ZOOMY", "Zoom Y",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f), 1.0f));

    return { params.begin(), params.end() };
}

FreakPhaseAudioProcessor::FreakPhaseAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Track A", juce::AudioChannelSet::stereo(), true)
        .withInput("Track B", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()),
      alignerThread(captureBuffer, captureWriteIndex, currentSampleRate)
{
    paramCache.update(apvts);
    captureBuffer.setSize(2, captureBufferSize);
    captureBuffer.clear();

    timelineAnalysisThread = std::make_unique<FreakPhase::Timeline::TimelineAnalysisThread>(
        timelineFifo,
        rcuTimelineManager,
        isTimelineCaptureActive,
        isTimelineAnalyzing,
        timelineAnalysisProgress,
        currentSampleRate
    );
    timelineAnalysisThread->startThread(juce::Thread::Priority::normal);
}

FreakPhaseAudioProcessor::~FreakPhaseAudioProcessor()
{
    if (timelineAnalysisThread)
        timelineAnalysisThread->stopThread(2000);
    alignerThread.stopThread(1000);
}

const juce::String FreakPhaseAudioProcessor::getName() const { return JucePlugin_Name; }
bool FreakPhaseAudioProcessor::acceptsMidi() const { return false; }
bool FreakPhaseAudioProcessor::producesMidi() const { return false; }
bool FreakPhaseAudioProcessor::isMidiEffect() const { return false; }
double FreakPhaseAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int FreakPhaseAudioProcessor::getNumPrograms() { return 1; }
int FreakPhaseAudioProcessor::getCurrentProgram() { return 0; }
void FreakPhaseAudioProcessor::setCurrentProgram(int) {}
const juce::String FreakPhaseAudioProcessor::getProgramName(int) { return {}; }
void FreakPhaseAudioProcessor::changeProgramName(int, const juce::String&) {}

bool FreakPhaseAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Output must be stereo (or mono)
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    // Track A (Main Input) must match Output or accept 4 discrete channels
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::quadraphonic()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::discreteChannels(4))
        return false;

    // Track B (Input Bus 1) must be active (stereo or mono) to prevent FL Studio from muting/sleeping sidechain
    auto trackB = layouts.getChannelSet(true, 1);
    if (trackB.isDisabled())
        return false;
    if (trackB != juce::AudioChannelSet::stereo()
     && trackB != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void FreakPhaseAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    paramCache.update(apvts);

    const int safeCapacity = std::max(samplesPerBlock * 4, 32768);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(safeCapacity);
    spec.numChannels = 2;

    dcBlocker.prepare(sampleRate);
    envFollower.prepare(sampleRate);
    phaseSub.prepare(spec);
    phaseHigh.prepare(spec);
    dynEq.prepare(spec);
    analyzer.prepare(sampleRate);
    subCrossover.prepare(spec);
    pitchTracker.prepare(sampleRate);
    playheadTracker.prepare(sampleRate);

    delaySub.prepare(spec);
    delayHigh.prepare(spec);
    delayTrackB.prepare(spec);
    delaySub.reset();
    delayHigh.reset();
    delayTrackB.reset();

    lookaheadDelay.prepare(spec);
    lookaheadDelay.reset();
    highDelay.prepare(spec);
    highDelay.reset();

    alignerThread.stopThread(200);
    alignerThread.prepare();

    captureBuffer.setSize(2, captureBufferSize);
    captureBuffer.clear();
    captureWriteIndex.store(0);

    // Scratch buffers pre-allocated to >= 32768 samples (Zero real-time heap allocations)
    mixBuffer.setSize(2, safeCapacity);
    subBuffer.setSize(2, safeCapacity);
    highBuffer.setSize(2, safeCapacity);
    trackABuffer.setSize(2, safeCapacity);
    trackBBuffer.setSize(2, safeCapacity);

    scratchEnvBuffer.calloc(static_cast<size_t>(safeCapacity));
    scratchGBuffer.calloc(static_cast<size_t>(safeCapacity));
    scratchEqGainBuffer.calloc(static_cast<size_t>(safeCapacity));

    int latMode = (paramCache.latencyMode != nullptr) ? juce::roundToInt(paramCache.latencyMode->load(std::memory_order_relaxed)) : 1;
    activeLatencyMode = latMode;
    int lookaheadSamples = (activeLatencyMode == 1) ? juce::roundToInt(0.005 * sampleRate) : 0;
    setLatencySamples(lookaheadSamples);
    lookaheadDelay.setDelay(static_cast<float>(lookaheadSamples));
    highDelay.setDelay(static_cast<float>(lookaheadSamples));

    envRmsM = envPeakM = 0.0f;
    envRmsS = envPeakS = 0.0f;
    envRmsMix = envPeakMix = 0.0f;

    smoothedSubFlipGain.reset(sampleRate, 0.008); // 8 ms crossfade ramp
    smoothedSubFlipGain.setCurrentAndTargetValue(1.0f);
    smoothedSubDelay.reset(sampleRate, 0.010); // 10 ms delay smoothing
    smoothedSubDelay.setCurrentAndTargetValue(0.0f);
    smoothedTrackBDelay.reset(sampleRate, 0.010); // 10 ms Track B delay smoothing
    smoothedTrackBDelay.setCurrentAndTargetValue(0.0f);
}

void FreakPhaseAudioProcessor::releaseResources()
{
    reset();
}

void FreakPhaseAudioProcessor::reset()
{
    dcBlocker.reset();
    envFollower.reset();
    phaseSub.reset();
    phaseHigh.reset();
    dynEq.reset();
    analyzer.reset();
    subCrossover.reset();
    pitchTracker.reset();
    delaySub.reset();
    delayHigh.reset();
    delayTrackB.reset();
    lookaheadDelay.reset();
    highDelay.reset();
    playheadTracker.reset();

    bool currentFlip = (paramCache.subFlip != nullptr) ? (paramCache.subFlip->load(std::memory_order_relaxed) > 0.5f) : false;
    smoothedSubFlipGain.setCurrentAndTargetValue(currentFlip ? -1.0f : 1.0f);
    smoothedSubDelay.setCurrentAndTargetValue(0.0f);
    smoothedTrackBDelay.setCurrentAndTargetValue(0.0f);

    // Smart Disable contract: clear meter ballistics + persistent scalars and the
    // waveform capture ring so resumed processing starts from a clean state.
    envRmsM = envPeakM = 0.0f;
    envRmsS = envPeakS = 0.0f;
    envRmsMix = envPeakMix = 0.0f;

    captureBuffer.clear();
    captureWriteIndex.store(0);

    // SPSC invariant: only reset the timeline capture FIFO when the analysis worker
    // is not mid-drain AND no LEARN capture is rolling (the audio thread is the
    // fifo producer while capture is active — resetting underneath it corrupts
    // the write index). Stale contents are bounded by the recorded start/end
    // timeline coordinates and are drained normally.
    if (! isTimelineAnalyzing.load(std::memory_order_acquire)
        && ! isTimelineCaptureActive.load(std::memory_order_acquire))
        timelineFifo.reset();
}

void FreakPhaseAudioProcessor::updatePdcLatency(int mode)
{
    if (mode == activeLatencyMode) return;
    activeLatencyMode = mode;
    int lookaheadSamples = (activeLatencyMode == 1) ? juce::roundToInt(0.005 * currentSampleRate.load(std::memory_order_relaxed)) : 0;
    lookaheadDelay.setDelay(static_cast<float>(lookaheadSamples));
    highDelay.setDelay(static_cast<float>(lookaheadSamples));
    
    // FIX: setLatencySamples must be called from message thread only
    // Use triggerAsyncUpdate which calls handleAsyncUpdate on message thread
    triggerAsyncUpdate();
}

void FreakPhaseAudioProcessor::handleAsyncUpdate()
{
    int lookaheadSamples = (activeLatencyMode == 1) ? juce::roundToInt(0.005 * currentSampleRate.load(std::memory_order_relaxed)) : 0;
    setLatencySamples(lookaheadSamples);

    // Auto-finalization when DAW stopped while armed/recording
    // Notify active editor so it can update UI and emit events to WebView2
    if (auto* editor = dynamic_cast<FreakPhaseAudioProcessorEditor*>(getActiveEditor()))
    {
        editor->onDawStopTimelineCapture();
    }
}

float FreakPhaseAudioProcessor::getPhaseSubBaseCutoff() const noexcept
{
    return phaseSub.getBaseCutoff();
}

float FreakPhaseAudioProcessor::getPhaseHighBaseCutoff() const noexcept
{
    return phaseHigh.getBaseCutoff();
}

void FreakPhaseAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    auto* bus0 = getBus(true, 0);
    if (numSamples == 0 || bus0 == nullptr || bus0->getNumberOfChannels() == 0) {
        buffer.clear();
        return;
    }

    // Real-time safety guard: reject buffer overruns without heap reallocations
    const int currentCapacity = subBuffer.getNumSamples();
    if (numSamples > currentCapacity) {
        buffer.clear();
        return;
    }

    if (clearPeaksFlag.exchange(false, std::memory_order_relaxed)) {
        persistentPeakM.store(-60.0f, std::memory_order_relaxed);
        persistentPeakS.store(-60.0f, std::memory_order_relaxed);
        persistentPeakMix.store(-60.0f, std::memory_order_relaxed);
        persistentMaxCorr.store(0.0f, std::memory_order_relaxed);
    }

    // 1. Thread-safe APVTS Parameter Reads
    int dcMode = juce::roundToInt(paramCache.dcFilter->load(std::memory_order_relaxed));
    float dynPhAmount = paramCache.dynPhAmount->load(std::memory_order_relaxed) / 100.0f;
    bool deltaListen = paramCache.deltaListen->load(std::memory_order_relaxed) > 0.5f;

    // Independent Sub Band Parameters
    float subRot = paramCache.subRot->load(std::memory_order_relaxed);
    float subDelMs = paramCache.subDel->load(std::memory_order_relaxed);
    bool subFlip = paramCache.subFlip->load(std::memory_order_relaxed) > 0.5f;
    float subGainDb = paramCache.subGain->load(std::memory_order_relaxed);

    // Independent High Band Parameters
    float highRot = paramCache.highRot->load(std::memory_order_relaxed);
    float highDelMs = paramCache.highDel->load(std::memory_order_relaxed);
    bool highFlip = paramCache.highFlip->load(std::memory_order_relaxed) > 0.5f;
    float highGainDb = paramCache.highGain->load(std::memory_order_relaxed);

    int monitorMode = juce::roundToInt(paramCache.monitor->load(std::memory_order_relaxed));
    int trackBChannelMode = (paramCache.trackBCh != nullptr) ? juce::roundToInt(paramCache.trackBCh->load(std::memory_order_relaxed)) : 0;
    float dynEqFreq = paramCache.dynEqFreq->load(std::memory_order_relaxed);
    float dynEqDepth = paramCache.dynEqDepth->load(std::memory_order_relaxed);
    float attMs = (paramCache.envAttack != nullptr) ? paramCache.envAttack->load(std::memory_order_relaxed) : 2.0f;
    float relMs = (paramCache.envRelease != nullptr) ? paramCache.envRelease->load(std::memory_order_relaxed) : 100.0f;

    envFollower.setTimeConstants(attMs, relMs);

    int latMode = (paramCache.latencyMode != nullptr) ? juce::roundToInt(paramCache.latencyMode->load(std::memory_order_relaxed)) : 1;
    if (latMode != activeLatencyMode)
    {
        updatePdcLatency(latMode);
    }

    float crossFreq = (paramCache.crossoverFreq != nullptr) ? paramCache.crossoverFreq->load(std::memory_order_relaxed) : 100.0f;
    bool crossEnable = (paramCache.crossoverEnable != nullptr) ? (paramCache.crossoverEnable->load(std::memory_order_relaxed) > 0.5f) : true;
    bool pitchTrackOn = (paramCache.pitchTrackEnable != nullptr) ? (paramCache.pitchTrackEnable->load(std::memory_order_relaxed) > 0.5f) : true;

    // 2. Playhead Tracking & Timeline Fallback (Requirement R3)
    auto transport = playheadTracker.update(getPlayHead(), numSamples);
    if (timelineAnalysisThread)
        timelineAnalysisThread->setBpm(transport.bpm);

    // HIGH 3: Discontinuity detection (scrub/seek/loop in FL Studio)
    // When epoch changes, the playhead jumped — clear all delay lines to prevent clicks
    {
        const uint32_t currentEpoch = transport.discontinuityEpoch;
        const uint32_t lastEpoch    = lastDiscontinuityEpoch.load(std::memory_order_relaxed);
        if (currentEpoch != lastEpoch)
        {
            lastDiscontinuityEpoch.store(currentEpoch, std::memory_order_relaxed);
            // Clear all delay lines: prevents click/pop on seek or loop-wrap
            delaySub.reset();
            delayHigh.reset();
            delayTrackB.reset();
            lookaheadDelay.reset();
            highDelay.reset();
            // 2ms crossfade ramp to suppress any residual transient on the flip smoother
            smoothedSubFlipGain.reset(currentSampleRate.load(std::memory_order_relaxed), 0.002);
        }
    }

    uint64_t audioEpoch = 0;
    const auto* lut = rcuTimelineManager.acquireTableForAudio(audioEpoch);

    // PDC offset: the audio thread works on samples that will be output lookaheadSamples later.
    // Correct LUT lookup position so the baked keyframe matches the audible output (HIGH 4).
    const int lookaheadSamplesLocal = (activeLatencyMode == 1) ? juce::roundToInt(0.005 * currentSampleRate.load(std::memory_order_relaxed)) : 0;

    bool isTimelineActive = false;
    float tlDelayOffset   = 0.0f;
    float tlPhaseDegrees  = 0.0f;
    float tlEqGainDb      = 0.0f;
    bool  tlPolarityFlip  = false;

    if (lut != nullptr && transport.isPlaying)
    {
        // BLOCKER 3 fix: sample LUT per 64-sample sub-block for ~1.33ms granularity.
        // We take the value at the start of this block (sub-block loop is inside the DSP path below).
        // The corrected position accounts for PDC lookahead (HIGH 4).
        // FIX: PDC compensation - ADD lookahead samples to align with output timing
        const int64_t lutSamplePos = transport.samplePosition + static_cast<int64_t>(lookaheadSamplesLocal);

        // Check if current playhead timestamp is mapped in the BakedTimelineLUT
        // BLOCKER 2 fix: use 5-arg overload that returns polarityFlip
        if (lut->sample(lutSamplePos, tlDelayOffset, tlPhaseDegrees, tlEqGainDb, tlPolarityFlip))
        {
            isTimelineActive = true;
            // Override real-time parameters with timeline baked keyframe data
            subRot    = tlPhaseDegrees;
            subDelMs  = (tlDelayOffset / static_cast<float>(currentSampleRate.load(std::memory_order_relaxed))) * 1000.0f;
            dynEqDepth = tlEqGainDb;
            // BLOCKER 2 fix: apply polarity from timeline keyframe
            smoothedSubFlipGain.setTargetValue(tlPolarityFlip ? -1.0f : 1.0f);
        }
    }

    // 3. 4-Input Channel Routing: Track A (Input 1/2) and Track B (Input 3/4)
    const int totalChannels = buffer.getNumChannels();
    const float* inTrackAL = nullptr;
    const float* inTrackAR = nullptr;
    const float* inTrackBL = nullptr;
    const float* inTrackBR = nullptr;

    const int busAChannels = (getBusCount(true) > 0 && getBus(true, 0) != nullptr) ? getBus(true, 0)->getNumberOfChannels() : 0;
    const int busAOffset = (busAChannels > 0) ? getChannelIndexInProcessBlockBuffer(true, 0, 0) : 0;

    if (busAChannels > 0 && totalChannels >= busAOffset + 1)
    {
        inTrackAL = buffer.getReadPointer(busAOffset);
        if (busAChannels > 1 && totalChannels >= busAOffset + 2)
            inTrackAR = buffer.getReadPointer(busAOffset + 1);
        else
            inTrackAR = inTrackAL; // Mono duplicated
    }

    const int busBChannels = (getBusCount(true) > 1 && getBus(true, 1) != nullptr && getBus(true, 1)->isEnabled()) 
                           ? getBus(true, 1)->getNumberOfChannels() : 0;
    const int busBOffset = (busBChannels > 0) ? getChannelIndexInProcessBlockBuffer(true, 1, 0) : 0;

    if (busBChannels > 0 && totalChannels >= busBOffset + 1)
    {
        inTrackBL = buffer.getReadPointer(busBOffset);
        if (busBChannels > 1 && totalChannels >= busBOffset + 2)
            inTrackBR = buffer.getReadPointer(busBOffset + 1);
        else
            inTrackBR = inTrackBL; // Mono duplicated
    }
    else if (totalChannels >= 4)
    {
        // 4 discrete channels inside primary input buffer
        inTrackBL = buffer.getReadPointer(2);
        inTrackBR = buffer.getReadPointer(3);
    }

    // Populate pre-allocated stereo buffers for Track A and Track B
    if (inTrackAL != nullptr)
        trackABuffer.copyFrom(0, 0, inTrackAL, numSamples);
    else
        trackABuffer.clear(0, 0, numSamples);

    if (inTrackAR != nullptr)
        trackABuffer.copyFrom(1, 0, inTrackAR, numSamples);
    else
        trackABuffer.copyFrom(1, 0, trackABuffer, 0, 0, numSamples);

    if (inTrackBL != nullptr)
        trackBBuffer.copyFrom(0, 0, inTrackBL, numSamples);
    else
        trackBBuffer.clear(0, 0, numSamples);

    if (inTrackBR != nullptr)
        trackBBuffer.copyFrom(1, 0, inTrackBR, numSamples);
    else if (inTrackBL != nullptr)
        trackBBuffer.copyFrom(1, 0, trackBBuffer, 0, 0, numSamples);
    else
        trackBBuffer.clear(1, 0, numSamples);

    // Sanitize input buffers: protect against upstream NaN / +/-Inf poisoning
    for (int ch = 0; ch < 2; ++ch) {
        auto* a = trackABuffer.getWritePointer(ch);
        auto* b = trackBBuffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            if (!std::isfinite(a[i])) a[i] = 0.0f;
            if (!std::isfinite(b[i])) b[i] = 0.0f;
        }
    }

    // 4. DC Offset Blocker on Track A and Track B
    dcBlocker.process(trackABuffer, trackBBuffer, dcMode, numSamples);

    // 5. Real-Time Waveform Capture & Timeline Capture FIFO
    if (paramCache.freeze->load(std::memory_order_relaxed) < 0.5f) {
        int wIdx = captureWriteIndex.load(std::memory_order_relaxed);
        if (wIdx < 0 || wIdx >= captureBufferSize) wIdx = 0;
        for (int i = 0; i < numSamples; ++i) {
            float aSample = trackABuffer.getSample(0, i);
            float bSample = 0.0f;
            if (trackBChannelMode == 1) bSample = trackBBuffer.getSample(1, i);
            else if (trackBChannelMode == 2) bSample = 0.5f * (trackBBuffer.getSample(0, i) + trackBBuffer.getSample(1, i));
            else bSample = trackBBuffer.getSample(0, i);

            captureBuffer.setSample(0, wIdx, aSample);
            captureBuffer.setSample(1, wIdx, bSample);
            if (++wIdx >= captureBufferSize) wIdx = 0;
        }
        captureWriteIndex.store(wIdx, std::memory_order_release);
    }

    // Decision #4 (/grill-me): ARM / LEARN transport state machine
    const bool isPlayingNow = transport.isPlaying;
    const bool wasPlaying = wasPlayingLastBlock.exchange(isPlayingNow, std::memory_order_acq_rel);

    if (isTimelineArmed.load(std::memory_order_relaxed) && !isNonRealtime())
    {
        if (isPlayingNow && !wasPlaying)
        {
            // DAW transport started while armed -> engage capture
            isTimelineCaptureActive.store(true, std::memory_order_release);
        }
        else if (!isPlayingNow && wasPlaying && isTimelineCaptureActive.load(std::memory_order_relaxed))
        {
            // DAW transport stopped while recording -> auto-stop and trigger finalization
            isTimelineCaptureActive.store(false, std::memory_order_release);
            isTimelineArmed.store(false, std::memory_order_release);
            triggerAsyncUpdate(); // Asynchronously notifies editor & worker on message thread
        }
    }

    // Write to lock-free timeline capture FIFO for background GCC-PHAT analysis
    // HIGH 2 fix: only write when timeline capture is active and we're in real-time mode
    if (isTimelineCaptureActive.load(std::memory_order_relaxed) && !isNonRealtime())
        timelineFifo.write(trackABuffer.getReadPointer(0), trackBBuffer.getReadPointer(0), numSamples, transport.samplePosition);

    // 6. Update Filter Parameters
    dynEq.setParameters(dynEqFreq, dynEqDepth);
    phaseSub.setBaseRotation(subRot, dynEqFreq);
    phaseHigh.setBaseRotation(highRot, dynEqFreq * 2.0f);

    // 7. Sidechain Envelope Extraction from Track B
    const float* trackB_L = trackBBuffer.getReadPointer(0);
    const float* trackB_R = trackBBuffer.getReadPointer(1);

    // Pre-extract mono sidechain driving signal into scratch buffer
    float* envInput = scratchEnvBuffer.get();
    for (int i = 0; i < numSamples; ++i)
    {
        if (trackBChannelMode == 1) envInput[i] = trackB_R[i];
        else if (trackBChannelMode == 2) envInput[i] = 0.5f * (trackB_L[i] + trackB_R[i]);
        else envInput[i] = trackB_L[i];
    }

    // Scalar L1 pre-pass for envelope follower
    envFollower.processBlock(envInput, scratchEnvBuffer.get(), numSamples);

    // Batch SIMD expand modulation arrays: populates scratchGBuffer and scratchEqGainBuffer.
    // Wideband path only: in crossover mode the LUT-aware sub-block loop below is the
    // authoritative pass (post-pitch-track baseCutoff) and fully overwrites these ranges.
    if (! crossEnable)
    {
        EnvelopeFollower::expandModulationArrays(
            scratchEnvBuffer.get(),
            scratchGBuffer.get(),
            scratchEqGainBuffer.get(),
            numSamples,
            phaseSub.getBaseCutoff(),
            dynPhAmount,
            dynEqDepth,
            static_cast<float>(currentSampleRate.load(std::memory_order_relaxed))
        );
    }

    // 8. Multiband Processing on Track A
    if (crossEnable)
    {
        // Split Track A into Sub (< crossFreq) and High (> crossFreq)
                // FIX: Use dirty flag to avoid redundant coefficient updates
                if (dspDirtyFlags.subCrossoverDirty || std::abs(crossFreq - subCrossover.getCrossoverFrequency()) > 0.1f)
                {
                    subCrossover.setCrossoverFrequency(crossFreq);
                    dspDirtyFlags.subCrossoverDirty = false;
        }
        const int crossSamples = juce::jmin(numSamples, subBuffer.getNumSamples());
        subCrossover.process(trackABuffer, subBuffer, highBuffer, crossSamples);

        // Sub band pitch tracking (snaps all-pass center frequency)
        // MEDIUM fix: iterate all samples in the block, not just sample 0
        if (pitchTrackOn) {
            float trackedFreq = 60.0f;
            const float* subL = subBuffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
            // FIX: Use dirty flag for phaseSub center frequency updates
            dspDirtyFlags.phaseSubDirty = true;
                trackedFreq = pitchTracker.processSample(subL[i]);
            phaseSub.updateCenterFreq(trackedFreq);
        }

        // Sub Band Lookahead Delay
        for (int ch = 0; ch < 2; ++ch) {
            auto* sData = subBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                lookaheadDelay.pushSample(ch, sData[i]);
                sData[i] = lookaheadDelay.popSample(ch);
            }
        }

        // BLOCKER 3 fix: Process DSP in 64-sample sub-blocks so LUT keyframes update
        // at ~1.33ms intervals (vs 42ms for a full 2048-sample block).
        // Each sub-block: re-query LUT → update subRot/dynEqDepth → setParameters →
        // expandModulationArrays for sub-block → dynEq.processBlock → phaseSub.processBlock
        static constexpr int kSubBlockSize = 64;
        int offset = 0;
        while (offset < numSamples)
        {
            const int subN = juce::jmin(kSubBlockSize, numSamples - offset);

            // Re-query LUT at this sub-block's sample position (with PDC correction)
            if (lut != nullptr && transport.isPlaying)
            {
                // FIX: PDC compensation - ADD lookahead samples to align with output timing
                const int64_t subLutPos = (transport.samplePosition + static_cast<int64_t>(offset))
                                          + static_cast<int64_t>(lookaheadSamplesLocal);
                float sbDelay = 0.0f, sbPhase = 0.0f, sbEqGain = 0.0f;
                bool  sbPolarity = false;
                if (lut->sample(subLutPos, sbDelay, sbPhase, sbEqGain, sbPolarity))
                {
                    subRot    = sbPhase;
                    dynEqDepth = sbEqGain;
                    subDelMs  = (sbDelay / static_cast<float>(currentSampleRate.load(std::memory_order_relaxed))) * 1000.0f;
                    smoothedSubFlipGain.setTargetValue(sbPolarity ? -1.0f : 1.0f);
                    // Re-compute filter coefficients for this sub-block
                    dynEq.setParameters(dynEqFreq, dynEqDepth);
                    phaseSub.setBaseRotation(subRot, dynEqFreq);
                }
            }

            // Expand modulation arrays for this sub-block
            EnvelopeFollower::expandModulationArrays(
                scratchEnvBuffer.get() + offset,
                scratchGBuffer.get()   + offset,
                scratchEqGainBuffer.get() + offset,
                subN,
                phaseSub.getBaseCutoff(),
                dynPhAmount,
                dynEqDepth,
                static_cast<float>(currentSampleRate.load(std::memory_order_relaxed))
            );

            // Create sub-block AudioBlock views (offset into pre-allocated buffers)
            float* subPtrs[2] = {
                subBuffer.getWritePointer(0) + offset,
                subBuffer.getWritePointer(1) + offset
            };
            juce::dsp::AudioBlock<float> subBlockN(subPtrs, 2, static_cast<size_t>(subN));

            // SIMD Dynamic EQ surgical cut
            dynEq.processBlock(subBlockN, scratchEqGainBuffer.get() + offset);

            // SIMD Vectorized PhaseEngine TPT APF cascade
            phaseSub.processBlock(subBlockN, scratchGBuffer.get() + offset);

            offset += subN;
        }

        // Delta Listen (listen to phase delta only)
        if (deltaListen) {
            for (int ch = 0; ch < 2; ++ch) {
                auto* sData = subBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    sData[i] = sData[i] - trackABuffer.getSample(ch, i);
                }
            }
        }

        // Sub Polarity Flip & Gain (smoothed linear crossfade ramp, prevents Dirac clicks)
        // Note: when timeline active, smoothedSubFlipGain target is already set in sub-block loop above
        if (!isTimelineActive)
            smoothedSubFlipGain.setTargetValue(subFlip ? -1.0f : 1.0f);
        const float subGainLin = (std::abs(subGainDb) > 0.01f) ? juce::Decibels::decibelsToGain(subGainDb) : 1.0f;
        for (int i = 0; i < numSamples; ++i) {
            float g = smoothedSubFlipGain.getNextValue() * subGainLin;
            subBuffer.setSample(0, i, subBuffer.getSample(0, i) * g);
            subBuffer.setSample(1, i, subBuffer.getSample(1, i) * g);
        }

        // Sub Band Delay (smoothed fractional delay prevents clicks during automation)
        const float targetSubDelSamples = (subDelMs > 0.001f) ? (subDelMs / 1000.0f) * static_cast<float>(currentSampleRate.load(std::memory_order_relaxed)) : 0.0f;
        smoothedSubDelay.setTargetValue(targetSubDelSamples);
        if (! smoothedSubDelay.isSmoothing())
        {
            // Smoothing idle: getNextValue() returns the settled target, so a single block-level
            // setDelay + cached-delay popSample(ch) yields the identical sample stream.
            const float curDelay = smoothedSubDelay.getNextValue();
            delaySub.setDelay(curDelay);
            float* subData[2] = { subBuffer.getWritePointer(0), subBuffer.getWritePointer(1) };
            for (int i = 0; i < numSamples; ++i) {
                for (int ch = 0; ch < 2; ++ch) {
                    delaySub.pushSample(ch, subData[ch][i]);
                    subData[ch][i] = delaySub.popSample(ch);
                }
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i) {
                float curDelay = smoothedSubDelay.getNextValue();
                for (int ch = 0; ch < 2; ++ch) {
                    delaySub.pushSample(ch, subBuffer.getSample(ch, i));
                    subBuffer.setSample(ch, i, delaySub.popSample(ch, curDelay));
                }
            }
        }

        // High Band Processing: Compensate lookahead latency
        for (int ch = 0; ch < 2; ++ch) {
            auto* h = highBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                highDelay.pushSample(ch, h[i]);
                h[i] = highDelay.popSample(ch);
            }
        }

        // Wrap High buffer in AudioBlock for SIMD Block Processing: [High L, High R]
        juce::dsp::AudioBlock<float> highBlock(highBuffer.getArrayOfWritePointers(), 2, 0, static_cast<size_t>(numSamples));

        // High Band PhaseEngine (static unmodulated rotation)
        phaseHigh.processBlock(highBlock);

        // High Polarity Flip & Gain
        if (highFlip) highBuffer.applyGain(-1.0f);
        if (std::abs(highGainDb) > 0.01f) highBuffer.applyGain(juce::Decibels::decibelsToGain(highGainDb));

        // High Band Delay
        const float highDelSamples = (highDelMs > 0.001f) ? (highDelMs / 1000.0f) * static_cast<float>(currentSampleRate.load(std::memory_order_relaxed)) : 0.0f;
        delayHigh.setDelay(highDelSamples);
        for (int ch = 0; ch < 2; ++ch) {
            auto* d = highBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                delayHigh.pushSample(ch, d[i]);
                d[i] = delayHigh.popSample(ch);
            }
        }

        // Reconstruct Track A by summing Sub and High
        for (int ch = 0; ch < 2; ++ch) {
            trackABuffer.copyFrom(ch, 0, subBuffer, ch, 0, numSamples);
            if (!deltaListen) {
                trackABuffer.addFrom(ch, 0, highBuffer, ch, 0, numSamples);
            }
        }
    }
    else
    {
        // Wideband processing on Track A using SIMD AudioBlock
        for (int ch = 0; ch < 2; ++ch) {
            auto* aData = trackABuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                lookaheadDelay.pushSample(ch, aData[i]);
                aData[i] = lookaheadDelay.popSample(ch);
            }
        }

        juce::dsp::AudioBlock<float> blockA(trackABuffer.getArrayOfWritePointers(), 2, 0, static_cast<size_t>(numSamples));
        dynEq.processBlock(blockA, scratchEqGainBuffer.get());
        phaseSub.processBlock(blockA, scratchGBuffer.get());

        // Wideband Sub Polarity Flip & Gain (smoothed linear crossfade ramp, prevents Dirac clicks)
        smoothedSubFlipGain.setTargetValue(subFlip ? -1.0f : 1.0f);
        const float subGainLin = (std::abs(subGainDb) > 0.01f) ? juce::Decibels::decibelsToGain(subGainDb) : 1.0f;
        for (int i = 0; i < numSamples; ++i) {
            float g = smoothedSubFlipGain.getNextValue() * subGainLin;
            trackABuffer.setSample(0, i, trackABuffer.getSample(0, i) * g);
            trackABuffer.setSample(1, i, trackABuffer.getSample(1, i) * g);
        }

        // Wideband Sub Delay (smoothed fractional delay prevents clicks during automation)
        const float targetSubDelSamples = (subDelMs > 0.001f) ? (subDelMs / 1000.0f) * static_cast<float>(currentSampleRate.load(std::memory_order_relaxed)) : 0.0f;
        smoothedSubDelay.setTargetValue(targetSubDelSamples);
        if (! smoothedSubDelay.isSmoothing())
        {
            // Smoothing idle: single block-level setDelay + cached-delay popSample(ch)
            // produces the identical sample stream (setDelay is idempotent for Linear).
            const float curDelay = smoothedSubDelay.getNextValue();
            delaySub.setDelay(curDelay);
            float* wideData[2] = { trackABuffer.getWritePointer(0), trackABuffer.getWritePointer(1) };
            for (int i = 0; i < numSamples; ++i) {
                for (int ch = 0; ch < 2; ++ch) {
                    delaySub.pushSample(ch, wideData[ch][i]);
                    wideData[ch][i] = delaySub.popSample(ch);
                }
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i) {
                float curDelay = smoothedSubDelay.getNextValue();
                for (int ch = 0; ch < 2; ++ch) {
                    delaySub.pushSample(ch, trackABuffer.getSample(ch, i));
                    trackABuffer.setSample(ch, i, delaySub.popSample(ch, curDelay));
                }
            }
        }
    }

    // Latency Alignment & Symmetric Negative Delay for Track B:
    // 1. Track A is delayed by lookahead (5ms in Precision mode). To ensure Track A and Track B
    //    are time-aligned in the summing bus (mixBuffer) and output, Track B is delayed by matching lookahead.
    // 2. If subDelMs < 0 (negative sub delay), Track A is advanced relative to Track B by delaying Track B further.
    const int lookaheadSamples = (activeLatencyMode == 1) ? juce::roundToInt(0.005 * currentSampleRate.load(std::memory_order_relaxed)) : 0;
    const float trackBDelSamples = static_cast<float>(lookaheadSamples)
        + ((subDelMs < -0.001f) ? (-subDelMs / 1000.0f) * static_cast<float>(currentSampleRate.load(std::memory_order_relaxed)) : 0.0f);
    smoothedTrackBDelay.setTargetValue(trackBDelSamples);
    if (! smoothedTrackBDelay.isSmoothing())
    {
        // Smoothing idle: single block-level setDelay + cached-delay popSample(ch)
        // produces the identical sample stream.
        const float curBDel = smoothedTrackBDelay.getNextValue();
        delayTrackB.setDelay(curBDel);
        float* tbData[2] = { trackBBuffer.getWritePointer(0), trackBBuffer.getWritePointer(1) };
        for (int i = 0; i < numSamples; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                delayTrackB.pushSample(ch, tbData[ch][i]);
                tbData[ch][i] = delayTrackB.popSample(ch);
            }
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i) {
            float curBDel = smoothedTrackBDelay.getNextValue();
            for (int ch = 0; ch < 2; ++ch) {
                delayTrackB.pushSample(ch, trackBBuffer.getSample(ch, i));
                trackBBuffer.setSample(ch, i, delayTrackB.popSample(ch, curBDel));
            }
        }
    }

    // 9. Summing Bus Operation (R1 Acceptance Criteria: successfully sums Input 1/2 and Input 3/4)
    // No clear() needed: copyFrom below overwrites [0, numSamples) and all consumers
    // (meters, monitor routing) read only the live numSamples region.
    for (int ch = 0; ch < 2; ++ch) {
        mixBuffer.copyFrom(ch, 0, trackABuffer, ch, 0, numSamples);
        mixBuffer.addFrom(ch, 0, trackBBuffer, ch, 0, numSamples);
    }

    // 10. Meters and Correlation Analysis
    analyzer.setResponseMs(paramCache.response->load(std::memory_order_relaxed));
    analyzer.processMeters(trackABuffer, envRmsM, envPeakM, outRmsM, outPeakM, persistentPeakM, numSamples);
    analyzer.processMeters(trackBBuffer, envRmsS, envPeakS, outRmsS, outPeakS, persistentPeakS, numSamples);
    analyzer.processCorrelation(trackABuffer, trackBBuffer, phaseCorrelation, persistentMaxCorr, trackBChannelMode, numSamples);

    if (numSamples > 0) {
        if (trackBChannelMode == 2 && trackBBuffer.getNumChannels() > 1) {
            analyzer.processInstantPhase(trackABuffer.getReadPointer(0),
                                        trackBBuffer.getReadPointer(0),
                                        trackBBuffer.getReadPointer(1),
                                        numSamples);
        } else {
            const float* trackB_ptr = (trackBChannelMode == 1 && trackBBuffer.getNumChannels() > 1)
                                      ? trackBBuffer.getReadPointer(1)
                                      : trackBBuffer.getReadPointer(0);
            analyzer.processInstantPhase(trackABuffer.getReadPointer(0), trackB_ptr, numSamples);
        }
    }

    analyzer.processMeters(mixBuffer, envRmsMix, envPeakMix, outRmsMix, outPeakMix, persistentPeakMix, numSamples);

    // 11. Monitor Routing to Output Buffer (0 = Track A, 1 = Track B, 2 = MIX)
    auto* outBusPtr = getBus(false, 0);
    const int outChannels = (outBusPtr != nullptr) ? outBusPtr->getNumberOfChannels() : std::min(2, totalChannels);
    const auto& srcBuffer = (monitorMode == 0) ? trackABuffer : ((monitorMode == 1) ? trackBBuffer : mixBuffer);

    if (outChannels == 1 && totalChannels >= 1) {
        // Proper stereo-to-mono downmixing: preserves both L and R channel energy
        auto* dest = buffer.getWritePointer(0);
        const float* srcL = srcBuffer.getReadPointer(0);
        const float* srcR = srcBuffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            dest[i] = 0.5f * (srcL[i] + srcR[i]);
        }
    } else {
        const int maxCopyChannels = std::min(outChannels, totalChannels);
        for (int ch = 0; ch < maxCopyChannels; ++ch) {
            buffer.copyFrom(ch, 0, srcBuffer, juce::jmin(ch, 1), 0, numSamples);
        }
    }

    // Mute any extra channels in the main buffer (e.g. discrete input 3 and 4) to prevent bleed
    for (int ch = outChannels; ch < totalChannels; ++ch) {
        buffer.clear(ch, 0, numSamples);
    }
}

juce::AudioProcessorEditor* FreakPhaseAudioProcessor::createEditor()
{
    return new FreakPhaseAudioProcessorEditor(*this);
}

bool FreakPhaseAudioProcessor::hasEditor() const
{
    return true;
}

void FreakPhaseAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
        copyXmlToBinary(*xml, destData);
}

void FreakPhaseAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        auto newTree = juce::ValueTree::fromXml(*xmlState);
        if (newTree.isValid())
        {
            apvts.replaceState(newTree);

            // Synchronize latency mode on message thread
            int latMode = (paramCache.latencyMode != nullptr) ? juce::roundToInt(paramCache.latencyMode->load(std::memory_order_relaxed)) : 1;
            if (latMode != activeLatencyMode)
            {
                activeLatencyMode = latMode;
                int lookaheadSamples = (activeLatencyMode == 1) ? juce::roundToInt(0.005 * currentSampleRate.load(std::memory_order_relaxed)) : 0;
                setLatencySamples(lookaheadSamples);
                lookaheadDelay.setDelay(static_cast<float>(lookaheadSamples));
                highDelay.setDelay(static_cast<float>(lookaheadSamples));
            }
        }
    }
}

void FreakPhaseAudioProcessor::bakeTimeline(int64_t startSample, int64_t endSample, float delaySamples, float phaseDeg, float eqCutDb)
{
    const int64_t duration = std::max(endSample + 48000, static_cast<int64_t>(currentSampleRate.load(std::memory_order_relaxed) * 120.0));
    auto* newTable = new FreakPhase::Timeline::BakedTimelineLUT(duration, startSample, endSample);

    auto* pts = newTable->getWritePointer();
    const int64_t numPts = newTable->getNumPoints();
    const int64_t sIdx = std::max(int64_t(0), startSample >> FreakPhase::Timeline::BakedTimelineLUT::INTERVAL_SHIFT);
    const int64_t eIdx = std::min(numPts - 1, (endSample >> FreakPhase::Timeline::BakedTimelineLUT::INTERVAL_SHIFT) + 1);

    const float centerFreq = (paramCache.crossoverFreq != nullptr) ? paramCache.crossoverFreq->load(std::memory_order_relaxed) : 100.0f;

    for (int64_t i = 0; i < numPts; ++i)
    {
        if (i >= sIdx && i <= eIdx)
        {
            pts[i].delayOffsetSamples = delaySamples;
            pts[i].phaseAngleDegrees = phaseDeg;
            pts[i].dynamicCutGainDb = eqCutDb;
            pts[i].filterCutoffHz = centerFreq;
        }
        else
        {
            pts[i].delayOffsetSamples = 0.0f;
            pts[i].phaseAngleDegrees = 0.0f;
            pts[i].dynamicCutGainDb = 0.0f;
            pts[i].filterCutoffHz = centerFreq;
        }
    }
    newTable->setMappedRange(startSample, endSample);
    rcuTimelineManager.publishNewTable(newTable);
}

void FreakPhaseAudioProcessor::armTimelineLearn(bool arm) noexcept
{
    isTimelineArmed.store(arm, std::memory_order_release);
    if (!arm)
        isTimelineCaptureActive.store(false, std::memory_order_release);
}

void FreakPhaseAudioProcessor::bakeTimelineSegments(const std::vector<TimelineSegmentData>& segments)
{
    if (segments.empty())
    {
        clearTimeline();
        return;
    }

    int64_t minStart = std::numeric_limits<int64_t>::max();
    int64_t maxEnd = 0;

    for (const auto& seg : segments)
    {
        if (seg.bypassed) continue;
        minStart = std::min(minStart, seg.startSample);
        maxEnd = std::max(maxEnd, seg.endSample);
    }

    if (maxEnd <= minStart)
    {
        clearTimeline();
        return;
    }

    const int64_t duration = std::max(maxEnd + 48000, static_cast<int64_t>(currentSampleRate.load(std::memory_order_relaxed) * 120.0));
    auto* newTable = new FreakPhase::Timeline::BakedTimelineLUT(duration, minStart, maxEnd);
    auto* pts = newTable->getWritePointer();
    const int64_t numPts = newTable->getNumPoints();
    const float centerFreq = (paramCache.crossoverFreq != nullptr) ? paramCache.crossoverFreq->load(std::memory_order_relaxed) : 100.0f;

    // Initialize all points to neutral (fallback)
    for (int64_t i = 0; i < numPts; ++i)
    {
        pts[i].delayOffsetSamples = 0.0f;
        pts[i].phaseAngleDegrees  = 0.0f;
        pts[i].dynamicCutGainDb   = 0.0f;
        pts[i].filterCutoffHz     = centerFreq;
        pts[i].polarityFlip       = 0.0f;
    }

    // Populate each active segment
    for (const auto& seg : segments)
    {
        if (seg.bypassed) continue;
        const int64_t sIdx = std::max(int64_t(0), seg.startSample >> FreakPhase::Timeline::BakedTimelineLUT::INTERVAL_SHIFT);
        const int64_t eIdx = std::min(numPts - 1, (seg.endSample >> FreakPhase::Timeline::BakedTimelineLUT::INTERVAL_SHIFT) + 1);

        for (int64_t i = sIdx; i <= eIdx; ++i)
        {
            pts[i].delayOffsetSamples = seg.delaySamples;
            pts[i].phaseAngleDegrees  = seg.rotateDeg;
            pts[i].dynamicCutGainDb   = seg.eqCutDb;
            pts[i].filterCutoffHz     = centerFreq;
            pts[i].polarityFlip       = seg.polarityFlip ? 1.0f : 0.0f;
        }
    }

    newTable->setMappedRange(minStart, maxEnd);
    rcuTimelineManager.publishNewTable(newTable);
}

void FreakPhaseAudioProcessor::clearTimeline()
{
    rcuTimelineManager.publishNewTable(nullptr);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreakPhaseAudioProcessor();
}

// =============================================================================
// FIX: MIDI Learn Implementation
// =============================================================================

void FreakPhaseAudioProcessor::startMidiLearn(const juce::String& paramId)
{
    midiLearnMode = true;
    currentLearningParam = paramId;
    currentLearningCC = -1;
}

void FreakPhaseAudioProcessor::cancelMidiLearn()
{
    midiLearnMode = false;
    currentLearningParam = juce::String();
    currentLearningCC = -1;
}

void FreakPhaseAudioProcessor::clearMidiMappings()
{
    midiCCToParam.clear();
}

void FreakPhaseAudioProcessor::handleMidiLearn(int ccNumber)
{
    if (midiLearnMode && ccNumber >= 0 && ccNumber <= 127)
    {
        midiCCToParam[ccNumber] = currentLearningParam;
        midiLearnMode = false;
        currentLearningParam = juce::String();
        currentLearningCC = -1;
    }
}

std::map<int, juce::String> FreakPhaseAudioProcessor::suggestMidiMappings() const
{
    // Suggest common MIDI CC mappings for this plugin
    return {
        {1, "SUB_ROTATE"},      // Mod Wheel
        {7, "SUB_GAIN"},        // Volume
        {10, "SUB_DELAY"},      // Pan
        {11, "HIGH_ROTATE"},    // Expression
        {74, "HIGH_GAIN"},      // Filter Cutoff (common for high freq)
        {71, "DYN_EQ_DEPTH"},   // Resonance
        {72, "RESPONSE"},       // Release Time
        {73, "ENV_ATTACK"},     // Attack Time
        {75, "ENV_RELEASE"},    // Decay Time
        {91, "DYN_PH_AMOUNT"},  // Reverb Wet/Dry
        {92, "BASS_GLUE"},      // Vibrato Rate
        {93, "CROSSOVER_FREQ"}  // Vibrato Depth
    };
}

// =============================================================================
// FIX: MIDI CC handling for parameter automation
// =============================================================================

bool FreakPhaseAudioProcessor::acceptsMidi() const { return true; }
bool FreakPhaseAudioProcessor::producesMidi() const { return false; }
bool FreakPhaseAudioProcessor::isMidiEffect() const { return false; }

void FreakPhaseAudioProcessor::handleMidiMessage(const juce::MidiMessage& msg)
{
    // Handle MIDI CC messages for parameter automation
    if (msg.isController())
    {
        const int ccNumber = msg.getControllerNumber();
        const float ccValue = msg.getControllerValue() / 127.0f;
        
        // Check if this CC is mapped to a parameter
        auto it = midiCCToParam.find(ccNumber);
        if (it != midiCCToParam.end())
        {
            const juce::String& paramId = it->second;
            if (auto* param = apvts.getParameter(paramId))
            {
                // Normalize CC value (0-1) to parameter range
                const auto range = param->getNormalisableRange();
                const float normalizedValue = range.convertFrom0to1(ccValue);
                param->setValueNotifyingHost(normalizedValue);
            }
        }
        else if (midiLearnMode)
        {
            // If in MIDI learn mode, map this CC to the current parameter
            handleMidiLearn(ccNumber);
        }
    }
    
    // Also handle note on/off for potential future features
    else if (msg.isNoteOn() || msg.isNoteOff())
    {
        // Could be used for trigger-based automation
        // For now, just ignore
    }
}

// =============================================================================
// FIX: VST3 Sidechain Input Support
// This allows using VST3 sidechain input instead of separate bus
// =============================================================================

// In VST3, we can use AudioProcessor::getBus with isSidechain flag
// However, JUCE doesn't expose this directly, so we need to check bus properties

bool FreakPhaseAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Original implementation
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::quadraphonic()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::discreteChannels(4))
        return false;

    // Check for sidechain input (VST3 specific)
    // In VST3, sidechain is typically on bus index 1
    auto trackB = layouts.getChannelSet(true, 1);
    if (trackB.isDisabled())
        return false;
    if (trackB != juce::AudioChannelSet::stereo()
     && trackB != juce::AudioChannelSet::mono())
        return false;

    return true;
}

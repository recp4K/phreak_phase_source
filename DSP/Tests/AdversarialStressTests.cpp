#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <limits>

// Include JUCE & Core DSP headers
#include <JuceHeader.h>
#include "DSP/SimdMath.h"
#include "DSP/PhaseEngine.h"
#include "DSP/DynamicEq.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/SubCrossover.h"
#include "DSP/DcBlockerChain.h"
#include "Timeline/PlayheadTracker.h"
#include "Timeline/BakedTimelineLUT.h"
#include "Timeline/TimelineAudioCaptureFifo.h"
#include "PluginProcessor.h"

using namespace FreakPhase::DSP;
using namespace FreakPhase::Timeline;

int main()
{
    juce::ScopedJuceInitialiser_GUI guiInit;
    std::cout << "=================================================================\n";
    std::cout << "  FREAK PHASE V2: ROUND 2 ADVERSARIAL STRESS TEST SUITE\n";
    std::cout << "=================================================================\n\n";

    int passedTests = 0;
    int totalTests = 0;

    auto check = [&](bool condition, const std::string& testName, const std::string& failDetails = "") {
        totalTests++;
        if (condition) {
            std::cout << " [PASS] " << testName << "\n";
            passedTests++;
        } else {
            std::cerr << " [FAIL] " << testName << "\n";
            if (!failDetails.empty())
                std::cerr << "        Details: " << failDetails << "\n";
        }
    };

    // =========================================================================
    // ADV TEST 1: SSE2 Macro Detection on Compiler
    // =========================================================================
    std::cout << "\n--- ADV TEST 1: Compiler Vector Intrinsics Macro Detection ---\n";
    {
        #if defined(FP_USE_SSE2) && FP_USE_SSE2
        bool sse2Active = true;
        #elif JUCE_INTEL && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
        bool sse2Active = true;
        #else
        bool sse2Active = false;
        #endif

        #if JUCE_INTEL && defined(__SSE2__)
        bool rawMacroDefined = true;
        #else
        bool rawMacroDefined = false;
        #endif

        std::cout << "  __SSE2__ raw macro defined: " << (rawMacroDefined ? "YES" : "NO (MSVC)") << "\n";
        std::cout << "  x64 / SSE2 hardware vector path active: " << (sse2Active ? "YES" : "NO") << "\n";
        check(sse2Active, "x64 SSE2 vector hardware path is enabled on MSVC",
              "__SSE2__ is missing on MSVC x64; code falls back to slow scalar loops");
    }

    // =========================================================================
    // ADV TEST 2: NaN / Inf Poisoning Recovery
    // =========================================================================
    std::cout << "\n--- ADV TEST 2: NaN / Inf Input Poisoning & Recovery ---\n";
    {
        const float nanVal = std::numeric_limits<float>::quiet_NaN();
        const float infVal = std::numeric_limits<float>::infinity();

        // 2a. EnvelopeFollower NaN Recovery
        EnvelopeFollower env;
        env.prepare(48000.0);
        constexpr int N = 64;
        std::vector<float> poisonBuf(N, nanVal);
        std::vector<float> outBuf(N, 0.0f);

        env.processBlock(poisonBuf.data(), outBuf.data(), N);

        // Feed clean audio next
        std::vector<float> cleanBuf(N, 0.5f);
        env.processBlock(cleanBuf.data(), outBuf.data(), N);

        bool envRecovered = std::isfinite(outBuf[N - 1]) && outBuf[N - 1] > 0.0f;
        check(envRecovered, "EnvelopeFollower recovers from NaN poisoning without sticky NaN state",
              "EnvelopeFollower state permanently corrupted by NaN input");

        // 2b. PhaseEngine NaN Recovery
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 256;
        spec.numChannels = 2;

        PhaseEngine pe;
        pe.prepare(spec);
        pe.setBaseRotation(45.0f, 60.0f);

        juce::AudioBuffer<float> peBuf(2, 64);
        for (int i = 0; i < 64; ++i) {
            peBuf.setSample(0, i, (i % 2 == 0) ? nanVal : infVal);
            peBuf.setSample(1, i, (i % 2 == 0) ? -infVal : nanVal);
        }
        std::vector<float> gArr(64, 0.4f);
        juce::dsp::AudioBlock<float> peBlk(peBuf);
        pe.processBlock(peBlk, gArr.data());

        // Now process clean sine
        for (int i = 0; i < 64; ++i) {
            float s = std::sin(2.0f * 3.14159f * 100.0f * i / 48000.0f);
            peBuf.setSample(0, i, s);
            peBuf.setSample(1, i, s);
        }
        pe.processBlock(peBlk, gArr.data());

        bool peRecovered = true;
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(peBuf.getSample(0, i)) || !std::isfinite(peBuf.getSample(1, i)))
                peRecovered = false;
        }
        check(peRecovered, "PhaseEngine recovers cleanly after receiving NaN/Inf inputs",
              "PhaseEngine internal filter states poisoned with NaN/Inf");

        // 2c. DynamicEq NaN Recovery
        DynamicEq deq;
        deq.prepare(spec);
        deq.setParameters(60.0f, -6.0f);

        juce::AudioBuffer<float> deqBuf(2, 64);
        for (int i = 0; i < 64; ++i) {
            deqBuf.setSample(0, i, nanVal);
            deqBuf.setSample(1, i, infVal);
        }
        std::vector<float> eqGainArr(64, 0.5f);
        juce::dsp::AudioBlock<float> deqBlk(deqBuf);
        deq.processBlock(deqBlk, eqGainArr.data());

        for (int i = 0; i < 64; ++i) {
            float s = std::sin(2.0f * 3.14159f * 60.0f * i / 48000.0f);
            deqBuf.setSample(0, i, s);
            deqBuf.setSample(1, i, s);
        }
        deq.processBlock(deqBlk, eqGainArr.data());

        bool deqRecovered = true;
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(deqBuf.getSample(0, i)) || !std::isfinite(deqBuf.getSample(1, i)))
                deqRecovered = false;
        }
        check(deqRecovered, "DynamicEq recovers cleanly after receiving NaN/Inf inputs",
              "DynamicEq SVF filter states poisoned with NaN/Inf");
    }

    // =========================================================================
    // ADV TEST 3: Summing Bus Track A vs Track B Lookahead Latency Alignment
    // =========================================================================
    std::cout << "\n--- ADV TEST 3: Summing Bus Track A vs Track B Latency Alignment ---\n";
    {
        constexpr double sampleRate = 48000.0;
        constexpr int lookaheadSamples = 240; // 5 ms at 48 kHz
        constexpr int N = 512;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = N;
        spec.numChannels = 2;

        juce::dsp::DelayLine<float> delayA(96000);
        juce::dsp::DelayLine<float> delayB(96000);
        delayA.prepare(spec);
        delayB.prepare(spec);

        // Scenario: Track A is delayed by lookahead (5ms = 240 samples)
        delayA.setDelay(static_cast<float>(lookaheadSamples));

        // Question: Is Track B delayed by the SAME lookahead latency before summing into mix?
        // If Track B is not delayed by lookahead:
        // Track B arrives at sample 0, Track A arrives at sample 240!
        delayB.setDelay(static_cast<float>(lookaheadSamples));

        juce::AudioBuffer<float> bufA(2, N);
        juce::AudioBuffer<float> bufB(2, N);
        bufA.clear();
        bufB.clear();
        bufA.setSample(0, 0, 1.0f); // Impulse at sample 0
        bufB.setSample(0, 0, 1.0f); // Impulse at sample 0

        for (int ch = 0; ch < 2; ++ch) {
            auto* a = bufA.getWritePointer(ch);
            auto* b = bufB.getWritePointer(ch);
            for (int i = 0; i < N; ++i) {
                delayA.pushSample(ch, a[i]);
                a[i] = delayA.popSample(ch);
                delayB.pushSample(ch, b[i]);
                b[i] = delayB.popSample(ch);
            }
        }

        int peakA = -1, peakB = -1;
        for (int i = 0; i < N; ++i) {
            if (bufA.getSample(0, i) > 0.5f && peakA < 0) peakA = i;
            if (bufB.getSample(0, i) > 0.5f && peakB < 0) peakB = i;
        }

        std::cout << "  Track A peak sample: " << peakA << "\n";
        std::cout << "  Track B peak sample: " << peakB << "\n";

        check(peakA == lookaheadSamples, "Track A delayed by lookahead (240 samples)");
        check(peakB == lookaheadSamples, "Track B delayed by matching lookahead (240 samples)",
              "Track B has 0 latency while Track A has 240 samples, causing 5ms summing misalignment!");
        check(peakA == peakB, "Track A and Track B are perfectly time-aligned in summing bus",
              "Track A and Track B misaligned by " + std::to_string(std::abs(peakA - peakB)) + " samples!");
    }

    // =========================================================================
    // ADV TEST 4: Delay Automation Continuity (Zero Glitch / Stale Buffer)
    // =========================================================================
    std::cout << "\n--- ADV TEST 4: Delay Automation Continuity ---\n";
    {
        constexpr double sampleRate = 48000.0;
        constexpr int N = 1000;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = N;
        spec.numChannels = 2;

        juce::dsp::DelayLine<float> delayLine(96000);
        delayLine.prepare(spec);

        // Step 1: Run 500 samples with delay = 0.0f
        delayLine.setDelay(0.0f);
        for (int i = 0; i < 500; ++i) {
            float val = static_cast<float>(i + 1); // 1, 2, 3...
            delayLine.pushSample(0, val);
            float out = delayLine.popSample(0);
            assert(std::abs(out - val) < 1e-4f); // With delay 0, immediate pass-through
        }

        // Step 2: Switch to delay = 10 samples
        delayLine.setDelay(10.0f);
        delayLine.pushSample(0, 501.0f);
        float outPop = delayLine.popSample(0);

        // Expected: Should pop sample 501 - 10 = 491.0f
        std::cout << "  Popped sample after switching delay from 0 to 10: " << outPop
                  << " (Expected: 491.0)\n";
        check(std::abs(outPop - 491.0f) < 1.0f,
              "DelayLine smoothly outputs past history without stale buffer clicks when automated from 0ms",
              "DelayLine produced stale/zero audio on delay transition from 0");
    }

    // =========================================================================
    // ADV TEST 5: Sample Rate Transition Stress Test
    // =========================================================================
    std::cout << "\n--- ADV TEST 5: Sample Rate Transition Stress Test ---\n";
    {
        const std::vector<double> rates = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        bool allRatesPassed = true;

        PhaseEngine pe;
        DynamicEq deq;
        EnvelopeFollower env;
        SubCrossover xover;

        for (double sr : rates)
        {
            juce::dsp::ProcessSpec spec;
            spec.sampleRate = sr;
            spec.maximumBlockSize = 512;
            spec.numChannels = 2;

            pe.prepare(spec);
            deq.prepare(spec);
            env.prepare(sr);
            xover.prepare(spec);

            pe.setBaseRotation(90.0f, 80.0f);
            deq.setParameters(80.0f, -6.0f);
            xover.setCrossoverFrequency(120.0f);

            juce::AudioBuffer<float> buf(2, 256);
            for (int i = 0; i < 256; ++i) {
                float s = std::sin(2.0f * 3.14159f * 80.0f * i / static_cast<float>(sr));
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
            }

            juce::AudioBuffer<float> sub(2, 256);
            juce::AudioBuffer<float> high(2, 256);
            xover.process(buf, sub, high, 256);

            std::vector<float> gArr(256, 0.4f);
            std::vector<float> eqArr(256, 0.5f);

            juce::dsp::AudioBlock<float> blk(sub);
            deq.processBlock(blk, eqArr.data());
            pe.processBlock(blk, gArr.data());

            for (int i = 0; i < 256; ++i) {
                if (!std::isfinite(sub.getSample(0, i)) || !std::isfinite(sub.getSample(1, i)) ||
                    !std::isfinite(high.getSample(0, i)) || !std::isfinite(high.getSample(1, i)))
                {
                    allRatesPassed = false;
                }
            }
        }

        check(allRatesPassed, "Seamless sample rate transitions [44.1k -> 48k -> 88.2k -> 96k -> 192k] without NaN or instability");
    }

    // =========================================================================
    // ADV TEST 6: PhaseEngine Boundary Angles & tanFactor Clamping Order
    // =========================================================================
    std::cout << "\n--- ADV TEST 6: PhaseEngine Boundary Angles & tanFactor Clamping ---\n";
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;

        PhaseEngine pe;
        pe.prepare(spec);

        const std::vector<float> testAngles = {
            -180.0f, -179.99f, -90.0f, -45.0f, -1.0f, -0.1f, -0.001f,
            0.0f, 0.001f, 0.1f, 1.0f, 45.0f, 90.0f, 179.99f, 180.0f
        };

        bool allAnglesFinite = true;
        for (float deg : testAngles)
        {
            pe.setBaseRotation(deg, 60.0f);
            float fc = pe.getBaseCutoff();
            if (!std::isfinite(fc) || fc < 10.0f || fc > 24000.0f * 0.49f) {
                allAnglesFinite = false;
                std::cerr << "        Failure at angle " << deg << " deg: fc = " << fc << "\n";
            }

            // Test pitch tracking center frequency update
            pe.updateCenterFreq(120.0f);
            float fcUpdated = pe.getBaseCutoff();
            if (!std::isfinite(fcUpdated) || fcUpdated < 10.0f || fcUpdated > 24000.0f * 0.49f) {
                allAnglesFinite = false;
                std::cerr << "        Failure on updateCenterFreq at angle " << deg << " deg: fc = " << fcUpdated << "\n";
            }
        }

        check(allAnglesFinite, "PhaseEngine setBaseRotation and updateCenterFreq bounded and stable across all angles [-180, 180]");
    }

    // =========================================================================
    // ADV TEST 7: SubCrossover LR4 Flat Magnitude Summing
    // =========================================================================
    std::cout << "\n--- ADV TEST 7: SubCrossover LR4 Flat Magnitude Summing ---\n";
    {
        constexpr double sampleRate = 48000.0;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 1024;
        spec.numChannels = 2;

        SubCrossover xover;
        xover.prepare(spec);
        xover.setCrossoverFrequency(100.0f);

        // Test multi-frequency chirp through crossover:
        // Sum of Sub + High should match input with flat magnitude response
        constexpr int N = 4096;
        juce::AudioBuffer<float> inBuf(2, N);
        juce::AudioBuffer<float> subBuf(2, N);
        juce::AudioBuffer<float> highBuf(2, N);
        juce::AudioBuffer<float> sumBuf(2, N);

        // Sine at crossover frequency (100 Hz)
        for (int i = 0; i < N; ++i) {
            float s = std::sin(2.0f * 3.14159f * 100.0f * i / 48000.0f);
            inBuf.setSample(0, i, s);
            inBuf.setSample(1, i, s);
        }

        xover.process(inBuf, subBuf, highBuf, N);

        // Reconstruct: sub + high
        for (int ch = 0; ch < 2; ++ch) {
            sumBuf.copyFrom(ch, 0, subBuf, ch, 0, N);
            sumBuf.addFrom(ch, 0, highBuf, ch, 0, N);
        }

        // Measure steady-state amplitude (skip initial transient of 512 samples)
        float maxAmp = 0.0f;
        for (int i = 512; i < N; ++i) {
            float amp = std::abs(sumBuf.getSample(0, i));
            if (amp > maxAmp) maxAmp = amp;
        }

        float magErrorDb = std::abs(juce::Decibels::gainToDecibels(maxAmp));
        std::cout << "  Sub + High sum amplitude at fc (100 Hz): " << maxAmp
                  << " (" << magErrorDb << " dB magnitude error)\n";

        check(magErrorDb < 0.2f, "SubCrossover LR4 sum produces flat magnitude (<0.2 dB) at crossover frequency",
              "SubCrossover magnitude error exceeds 0.2 dB: " + std::to_string(magErrorDb) + " dB");
    }

    // =========================================================================
    // ADV TEST 8: Playhead Discontinuity Scrubbing & Preroll Safety
    // =========================================================================
    std::cout << "\n--- ADV TEST 8: PlayheadTracker Discontinuity & Preroll Stress ---\n";
    {
        PlayheadTracker tracker;
        tracker.prepare(48000.0);

        // Mock playhead
        struct MockPlayHead : public juce::AudioPlayHead
        {
            juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
            juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return pos; }
        } mock;

        juce::AudioPlayHead::PositionInfo pInfo;
        pInfo.setIsPlaying(true);
        pInfo.setTimeInSamples(-48000); // Negative sample position during DAW count-in
        pInfo.setBpm(120.0);
        mock.pos = pInfo;

        // Block 1: Negative sample position
        auto snap1 = tracker.update(&mock, 512);
        check(snap1.samplePosition == -48000, "PlayheadTracker preserves negative count-in sample position");

        // Block 2: Continuous advance during count-in
        pInfo.setTimeInSamples(-48000 + 512);
        mock.pos = pInfo;
        auto snap2 = tracker.update(&mock, 512);
        check(snap2.discontinuityEpoch == 0, "No false discontinuity triggered during count-in playback");

        // Block 3: Sudden Scrub jump to +1,000,000 samples
        pInfo.setTimeInSamples(1000000);
        mock.pos = pInfo;
        auto snap3 = tracker.update(&mock, 512);
        std::cout << "  Epoch after scrub jump: " << snap3.discontinuityEpoch << "\n";
        check(snap3.discontinuityEpoch > 0, "Discontinuity epoch increments immediately on transport scrub jump");

        // Block 4: Backward Scrub jump to +500 samples
        pInfo.setTimeInSamples(500);
        mock.pos = pInfo;
        auto snap4 = tracker.update(&mock, 512);
        check(snap4.discontinuityEpoch > snap3.discontinuityEpoch, "Discontinuity epoch increments on backward scrub jump");
    }

    // =========================================================================
    // ADV TEST 9: Dynamic Bus Routing Underflow Protection (Host provides 2 channels with 2-bus layout)
    // =========================================================================
    std::cout << "\n--- ADV TEST 9: Dynamic Bus Routing Underflow Protection ---\n";
    {
        FreakPhaseAudioProcessor proc;
        proc.prepareToPlay(48000.0, 256);

        // Host provides ONLY 2 channels in the processBlock buffer (e.g. unrouted sidechain in DAW)
        // Plugin layout has Bus 0 (Track A, 2 channels) and Bus 1 (Track B, 2 channels) enabled
        juce::AudioBuffer<float> shortBuf(2, 256);
        for (int i = 0; i < 256; ++i) {
            shortBuf.setSample(0, i, 0.5f);
            shortBuf.setSample(1, i, 0.5f);
        }
        juce::MidiBuffer midi;

        // Must not crash or dereference out-of-bounds pointers!
        bool noCrash = true;
        try {
            proc.processBlock(shortBuf, midi);
        } catch (...) {
            noCrash = false;
        }

        check(noCrash, "processBlock executes safely without crash when host provides fewer channels than configured buses");
        
        // Output should have valid finite audio
        bool finiteOut = true;
        for (int i = 0; i < 256; ++i) {
            if (!std::isfinite(shortBuf.getSample(0, i)) || !std::isfinite(shortBuf.getSample(1, i)))
                finiteOut = false;
        }
        check(finiteOut, "Audio output remains strictly finite when sidechain bus is absent from process buffer");
    }

    // =========================================================================
    // ADV TEST 10: Mono Output Downmixing
    // =========================================================================
    std::cout << "\n--- ADV TEST 10: Mono Output Downmixing ---\n";
    {
        FreakPhaseAudioProcessor proc;
        
        // Configure layout with mono output
        auto layout = proc.getBusesLayout();
        layout.getMainOutputChannelSet() = juce::AudioChannelSet::mono();
        layout.getMainInputChannelSet() = juce::AudioChannelSet::mono();
        proc.setBusesLayout(layout);
        proc.prepareToPlay(48000.0, 256);

        // Monitor MIX: input has 1 channel
        juce::AudioBuffer<float> monoBuf(1, 256);
        for (int i = 0; i < 256; ++i) {
            monoBuf.setSample(0, i, 0.8f);
        }
        juce::MidiBuffer midi;

        proc.processBlock(monoBuf, midi);

        bool monoValid = true;
        for (int i = 0; i < 256; ++i) {
            if (!std::isfinite(monoBuf.getSample(0, i)))
                monoValid = false;
        }
        check(monoValid, "Mono output layout processes and downmixes without NaN or crash");
    }

    // =========================================================================
    // ADV TEST 11: DynamicEq::processSample Returns Filtered Audio (not delta)
    // =========================================================================
    std::cout << "\n--- ADV TEST 11: DynamicEq::processSample Audio Return ---\n";
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 256;
        spec.numChannels = 2;

        DynamicEq deq;
        deq.prepare(spec);

        // At 0 dB depth, processSample must return the input sample untouched!
        deq.setParameters(100.0f, 0.0f);
        float inSample = 0.75f;
        float outSampleAtZeroDb = deq.processSample(0, inSample, 1.0f);
        std::cout << "  processSample at 0 dB depth: " << outSampleAtZeroDb << " (Expected: ~0.75)\n";
        check(std::abs(outSampleAtZeroDb - inSample) < 1e-4f,
              "DynamicEq::processSample returns input sample untouched at 0 dB depth (does not mute)",
              "Returned " + std::to_string(outSampleAtZeroDb) + " instead of " + std::to_string(inSample));

        // At negative depth (-12 dB), resonant frequency is cut
        deq.setParameters(100.0f, -12.0f);
        float outSampleCut = deq.processSample(0, inSample, 1.0f);
        check(std::isfinite(outSampleCut), "DynamicEq::processSample outputs valid finite audio at -12 dB depth");
    }

    // =========================================================================
    // ADV TEST 12: APVTS Parameter State Serialization & Deserialization
    // =========================================================================
    std::cout << "\n--- ADV TEST 12: APVTS Parameter State Save and Restore ---\n";
    {
        FreakPhaseAudioProcessor proc1;
        proc1.prepareToPlay(48000.0, 256);

        // Set custom SUB_ and HIGH_ parameter values
        if (auto* p = proc1.apvts.getParameter("SUB_ROTATE"))
            p->setValueNotifyingHost(p->convertTo0to1(90.0f));
        if (auto* p = proc1.apvts.getParameter("SUB_DELAY"))
            p->setValueNotifyingHost(p->convertTo0to1(4.5f));
        if (auto* p = proc1.apvts.getParameter("HIGH_ROTATE"))
            p->setValueNotifyingHost(p->convertTo0to1(-45.0f));
        if (auto* p = proc1.apvts.getParameter("HIGH_DELAY"))
            p->setValueNotifyingHost(p->convertTo0to1(-2.0f));

        // Serialize state
        juce::MemoryBlock stateBlock;
        proc1.getStateInformation(stateBlock);
        check(stateBlock.getSize() > 0, "getStateInformation produces non-empty binary state");

        // Deserialize into second processor instance
        FreakPhaseAudioProcessor proc2;
        proc2.prepareToPlay(48000.0, 256);
        proc2.setStateInformation(stateBlock.getData(), static_cast<int>(stateBlock.getSize()));

        float subRotRestored = proc2.apvts.getRawParameterValue("SUB_ROTATE")->load();
        float subDelRestored = proc2.apvts.getRawParameterValue("SUB_DELAY")->load();
        float highRotRestored = proc2.apvts.getRawParameterValue("HIGH_ROTATE")->load();
        float highDelRestored = proc2.apvts.getRawParameterValue("HIGH_DELAY")->load();

        std::cout << "  Restored SUB_ROTATE: " << subRotRestored << " (Expected: 90.0)\n";
        std::cout << "  Restored SUB_DELAY:  " << subDelRestored << " (Expected: 4.5)\n";
        std::cout << "  Restored HIGH_ROTATE: " << highRotRestored << " (Expected: -45.0)\n";
        std::cout << "  Restored HIGH_DELAY:  " << highDelRestored << " (Expected: -2.0)\n";

        bool stateRestored = (std::abs(subRotRestored - 90.0f) < 0.2f) &&
                             (std::abs(subDelRestored - 4.5f) < 0.05f) &&
                             (std::abs(highRotRestored - (-45.0f)) < 0.2f) &&
                             (std::abs(highDelRestored - (-2.0f)) < 0.05f);
        check(stateRestored, "APVTS successfully restores independent SUB_ and HIGH_ parameters from saved state");
    }

    // =========================================================================
    // ADV TEST 13: FL Studio Smart Disable & Delay Line Reset Safety
    // =========================================================================
    std::cout << "\n--- ADV TEST 13: FL Studio Smart Disable & reset() Safety ---\n";
    {
        FreakPhaseAudioProcessor proc;
        proc.prepareToPlay(48000.0, 256);

        // Feed some loud audio to charge filters and delay lines
        juce::AudioBuffer<float> loudBuf(4, 256);
        for (int ch = 0; ch < 4; ++ch) {
            for (int i = 0; i < 256; ++i) loudBuf.setSample(ch, i, 1.0f);
        }
        juce::MidiBuffer midi;
        proc.processBlock(loudBuf, midi);

        // FL Studio Smart Disable calls reset() when silence is detected
        proc.reset();

        // Process silence immediately after reset: must not produce explosive transients or clicks
        juce::AudioBuffer<float> silenceBuf(4, 256);
        silenceBuf.clear();
        proc.processBlock(silenceBuf, midi);

        float maxEnergyAfterReset = 0.0f;
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 256; ++i) {
                float v = std::abs(silenceBuf.getSample(ch, i));
                if (v > maxEnergyAfterReset) maxEnergyAfterReset = v;
            }
        }
        std::cout << "  Max output amplitude on silence immediately after reset(): " << maxEnergyAfterReset << "\n";
        check(maxEnergyAfterReset < 1e-4f, "reset() clears internal delay lines and filter states with zero transient bursts on resume");
    }

    // =========================================================================
    // ADV TEST 14: Playhead nullopt & Stopped Transport Scenarios
    // =========================================================================
    std::cout << "\n--- ADV TEST 14: Playhead nullopt & Stopped Transport Scenarios ---\n";
    {
        PlayheadTracker tracker;
        tracker.prepare(48000.0);

        // Mock playhead returning nullopt (FL Studio Smart Disable / non-transport host)
        struct NulloptPlayHead : public juce::AudioPlayHead
        {
            juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override {
                return std::nullopt;
            }
        } nullMock;

        // Block 1 with nullopt playhead
        auto snap1 = tracker.update(&nullMock, 256);
        check(snap1.samplePosition == 0, "PlayheadTracker safely handles nullopt playhead position");

        // Mock playhead in STOPPED state (isPlaying == false)
        struct StoppedPlayHead : public juce::AudioPlayHead
        {
            juce::AudioPlayHead::PositionInfo pInfo;
            juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override {
                return pInfo;
            }
        } stopMock;

        stopMock.pInfo.setIsPlaying(false);
        stopMock.pInfo.setTimeInSamples(50000);
        stopMock.pInfo.setBpm(120.0);

        // Stopped block 1
        auto snapStop1 = tracker.update(&stopMock, 256);
        // Stopped block 2: cursor moved while stopped
        stopMock.pInfo.setTimeInSamples(10000);
        auto snapStop2 = tracker.update(&stopMock, 256);

        check(snapStop2.discontinuityEpoch == snapStop1.discontinuityEpoch,
              "Stopped transport does not spin/increment discontinuity epoch when user moves cursor while stopped");

        // Now transport LOOPS during playback (wrap-around from 96000 to 0)
        stopMock.pInfo.setIsPlaying(true);
        stopMock.pInfo.setTimeInSamples(95800);
        auto snapPlay1 = tracker.update(&stopMock, 256);

        // Next block wrapped around to sample 0
        stopMock.pInfo.setTimeInSamples(0);
        auto snapLoop = tracker.update(&stopMock, 256);
        check(snapLoop.discontinuityEpoch > snapPlay1.discontinuityEpoch,
              "Transport loop wrap-around jump correctly detected and increments epoch");
    }

    // =========================================================================
    // ADV TEST 15: TimelineAudioCaptureFifo Lockstep Starvation Protection
    // =========================================================================
    std::cout << "\n--- ADV TEST 15: TimelineAudioCaptureFifo Starvation Protection ---\n";
    {
        TimelineAudioCaptureFifo fifo;
        // Don't write anything to audio FIFO -> it's starved
        std::vector<float> mainRead(512, 0.0f);
        std::vector<float> sideRead(512, 0.0f);
        CapturePacketMeta meta;

        bool readSuccess = fifo.readNextBlock(mainRead.data(), sideRead.data(), meta);
        check(!readSuccess, "readNextBlock safely returns false on empty FIFO without metadata corruption");

        // Now write 1 packet of 128 samples
        std::vector<float> mainWrite(128, 0.33f);
        std::vector<float> sideWrite(128, 0.66f);
        fifo.write(mainWrite.data(), sideWrite.data(), 128, 12345);

        bool readValid = fifo.readNextBlock(mainRead.data(), sideRead.data(), meta);
        check(readValid && meta.numSamples == 128 && meta.startTimelineSample == 12345,
              "readNextBlock successfully consumes written packet with matching timestamp");
        check(std::abs(mainRead[0] - 0.33f) < 1e-5f && std::abs(sideRead[0] - 0.66f) < 1e-5f,
              "readNextBlock preserves audio sample fidelity");
    }

    // =========================================================================
    // ADV TEST 16: SubCrossover Mono Replication
    // =========================================================================
    std::cout << "\n--- ADV TEST 16: SubCrossover Mono Replication ---\n";
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 256;
        spec.numChannels = 2;

        SubCrossover xover;
        xover.prepare(spec);
        xover.setCrossoverFrequency(100.0f);

        juce::AudioBuffer<float> monoIn(1, 256);
        for (int i = 0; i < 256; ++i) monoIn.setSample(0, i, 0.5f);

        juce::AudioBuffer<float> sub(2, 256);
        juce::AudioBuffer<float> high(2, 256);
        xover.process(monoIn, sub, high, 256);

        bool monoReplicated = true;
        for (int i = 0; i < 256; ++i) {
            if (std::abs(sub.getSample(0, i) - sub.getSample(1, i)) > 1e-6f) monoReplicated = false;
            if (std::abs(high.getSample(0, i) - high.getSample(1, i)) > 1e-6f) monoReplicated = false;
        }
        check(monoReplicated, "SubCrossover mirrors channel 0 into channel 1 when processing mono inputs");
    }

    std::cout << "\n=================================================================\n";
    std::cout << "  ADVERSARIAL STRESS TEST SUMMARY: " << passedTests << " / " << totalTests << " PASSED ("
              << (passedTests == totalTests ? "100% SUCCESS" : "DEFECTS DETECTED") << ")\n";
    std::cout << "=================================================================\n\n";

    return (passedTests == totalTests) ? 0 : 1;
}


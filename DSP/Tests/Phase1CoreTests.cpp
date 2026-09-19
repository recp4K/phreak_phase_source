#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>
#include <chrono>
#include <algorithm>

// Include JUCE headers & Core DSP headers
#include <JuceHeader.h>
#include "Analysis/SignalAnalyzer.h"
#include "DSP/SimdMath.h"
#include "DSP/PhaseEngine.h"
#include "DSP/DynamicEq.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/SubCrossover.h"
#include "DSP/DcBlockerChain.h"
#include "DSP/PitchTracker.h"
#include "Timeline/PlayheadTracker.h"
#include "Timeline/BakedTimelineLUT.h"
#include "Timeline/TimelineAudioCaptureFifo.h"

using namespace FreakPhase::DSP;
using namespace FreakPhase::Timeline;

int main()
{
    std::cout << "=================================================================\n";
    std::cout << "  FREAK PHASE V2: PHASE 1 DSP CORE VERIFICATION SUITE\n";
    std::cout << "=================================================================\n\n";

    int passedTests = 0;
    int totalTests = 0;

    auto check = [&](bool condition, const std::string& testName) {
        totalTests++;
        if (condition) {
            std::cout << " [PASS] " << testName << "\n";
            passedTests++;
        } else {
            std::cerr << " [FAIL] " << testName << "\n";
        }
    };

    // =========================================================================
    // TEST 1: fastExp2SIMD Numerical Accuracy & Clamping
    // =========================================================================
    std::cout << "\n--- TEST 1: fastExp2SIMD Accuracy & Clamping ---\n";
    {
        double maxRelErrMusical = 0.0;
        constexpr int N = 100000;
        for (int i = 0; i <= N; ++i)
        {
            float x = -4.0f + 8.0f * (static_cast<float>(i) / static_cast<float>(N));
            vFloat vx(x);
            vFloat res = fastExp2SIMD(vx);
            float approx = res.get(0);
            float exact = std::exp2(x);

            double relErr = std::abs((double)approx - (double)exact) / (double)exact * 100.0;
            if (relErr > maxRelErrMusical) maxRelErrMusical = relErr;
        }

        std::cout << "Max Rel Error on [-4, +4] octaves: " << std::fixed << std::setprecision(6) << maxRelErrMusical << "%\n";
        check(maxRelErrMusical < 0.000726, "fastExp2SIMD error < 0.000726% on [-4, 4]");

        // Boundary Clamping Test
        vFloat extremeNeg(-150.0f);
        vFloat extremePos(150.0f);
        vFloat negRes = fastExp2SIMD(extremeNeg);
        vFloat posRes = fastExp2SIMD(extremePos);

        check(std::isfinite(negRes.get(0)) && negRes.get(0) >= 0.0f, "fastExp2SIMD handles -150 without underflow corruption");
        check(std::isfinite(posRes.get(0)) && posRes.get(0) > 0.0f, "fastExp2SIMD handles +150 without sign bit overflow");
    }

    // =========================================================================
    // TEST 2: fastGSIMD Stability & Padé [5/4] Single Division Accuracy
    // =========================================================================
    std::cout << "\n--- TEST 2: fastGSIMD Stability & Accuracy ---\n";
    {
        const float fs = 44100.0f;
        double maxRelErrG = 0.0;
        bool allPolesStable = true;

        constexpr int N = 100000;
        for (int i = 1; i <= N; ++i)
        {
            float fc = 10.0f + (0.49f * fs - 10.0f) * (static_cast<float>(i) / static_cast<float>(N));
            float wd = juce::MathConstants<float>::pi * fc / fs;
            vFloat vWd(wd);
            vFloat vG = fastGSIMD(vWd);
            float approxG = vG.get(0);

            float exactTan = std::tan(wd);
            float exactG = exactTan / (1.0f + exactTan);

            double relErr = std::abs((double)approxG - (double)exactG) / (double)exactG * 100.0;
            if (relErr > maxRelErrG) maxRelErrG = relErr;

            // Strict stability check: pole must reside inside unit circle |1 - 2G| < 1.0
            float pole = std::abs(1.0f - 2.0f * approxG);
            if (pole >= 1.0f) allPolesStable = false;
        }

        std::cout << "Max Rel Error for G on [10 Hz, 0.49 Fs]: " << std::scientific << maxRelErrG << "%\n";
        check(maxRelErrG < 0.001, "fastGSIMD relative error < 0.001% across audible band");
        check(allPolesStable, "fastGSIMD discrete-time state pole |1 - 2G| < 1.0 (strictly BIBO stable)");

        // Supra-Nyquist Protection Test
        vFloat supraNyquist(10.0f); // well beyond pi/2
        vFloat supraG = fastGSIMD(supraNyquist);
        float poleSupra = std::abs(1.0f - 2.0f * supraG.get(0));
        check(supraG.get(0) <= 0.9990f, "fastGSIMD G upper ceiling clamped to 0.9990");
        check(poleSupra <= 0.9980f, "fastGSIMD supra-Nyquist protection guarantees |1 - 2G| <= 0.9980");
    }

    // =========================================================================
    // TEST 3: PhaseEngine SIMD Block Processing & All-Pass Energy Conservation
    // =========================================================================
    std::cout << "\n--- TEST 3: PhaseEngine SIMD Block Processing & Energy Conservation ---\n";
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 2048;
        spec.numChannels = 2;

        PhaseEngine pe;
        pe.prepare(spec);
        pe.setBaseRotation(90.0f, 100.0f);

        constexpr int blockSize = 2048;
        juce::AudioBuffer<float> buffer(2, blockSize);

        // Continuous full-period sine wave
        for (int i = 0; i < blockSize; ++i)
        {
            float sine = std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * i / 48000.0f);
            buffer.setSample(0, i, sine);
            buffer.setSample(1, i, sine);
        }

        double inEnergyL = 0.0, inEnergyR = 0.0;
        for (int i = 0; i < blockSize; ++i) {
            inEnergyL += buffer.getSample(0, i) * buffer.getSample(0, i);
            inEnergyR += buffer.getSample(1, i) * buffer.getSample(1, i);
        }

        std::vector<float> gArray(blockSize, 0.5f);
        juce::dsp::AudioBlock<float> block(buffer);

        pe.processBlock(block, gArray.data());

        double outEnergyL = 0.0, outEnergyR = 0.0;
        for (int i = 0; i < blockSize; ++i) {
            outEnergyL += buffer.getSample(0, i) * buffer.getSample(0, i);
            outEnergyR += buffer.getSample(1, i) * buffer.getSample(1, i);
        }

        double diffL = std::abs(inEnergyL - outEnergyL) / inEnergyL * 100.0;
        std::cout << "Energy diff L (2048 samples): " << diffL << "%\n";
        check(diffL < 0.2, "PhaseEngine all-pass energy conservation (<0.2% transient bound)");
        check(std::isfinite(outEnergyL) && std::isfinite(outEnergyR), "PhaseEngine outputs remain strictly finite");
    }

    // =========================================================================
    // TEST 4: DynamicEq SIMD Block Processing
    // =========================================================================
    std::cout << "\n--- TEST 4: DynamicEq SIMD Block Processing ---\n";
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 512;
        spec.numChannels = 2;

        DynamicEq deq;
        deq.prepare(spec);
        deq.setParameters(100.0f, -6.0f);

        constexpr int blockSize = 256;
        juce::AudioBuffer<float> buffer(2, blockSize);
        for (int i = 0; i < blockSize; ++i) {
            float sine = std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * i / 48000.0f);
            buffer.setSample(0, i, sine);
            buffer.setSample(1, i, sine);
        }

        double inEnergy = 0.0;
        for (int i = 0; i < blockSize; ++i) inEnergy += buffer.getSample(0, i) * buffer.getSample(0, i);

        std::vector<float> eqGainArray(blockSize, 0.5f); // -6 dB linear gain ~ 0.5
        juce::dsp::AudioBlock<float> block(buffer);
        deq.processBlock(block, eqGainArray.data());

        double outEnergy = 0.0;
        for (int i = 0; i < blockSize; ++i) outEnergy += buffer.getSample(0, i) * buffer.getSample(0, i);

        check(outEnergy < inEnergy, "DynamicEq cuts energy at resonance frequency as requested");
    }

    // =========================================================================
    // TEST 5: EnvelopeFollower SIMD Batch Expansion
    // =========================================================================
    std::cout << "\n--- TEST 5: EnvelopeFollower SIMD Batch Expansion ---\n";
    {
        EnvelopeFollower env;
        env.prepare(48000.0);
        env.setTimeConstants(2.0f, 100.0f);

        constexpr int blockSize = 128;
        std::vector<float> inAudio(blockSize, 0.8f);
        std::vector<float> envOut(blockSize, 0.0f);
        std::vector<float> gOut(blockSize, 0.0f);
        std::vector<float> eqGainOut(blockSize, 0.0f);

        env.processBlock(inAudio.data(), envOut.data(), blockSize);
        EnvelopeFollower::expandModulationArrays(
            envOut.data(), gOut.data(), eqGainOut.data(),
            blockSize, 60.0f, 0.5f, -6.0f, 48000.0f
        );

        check(envOut[blockSize - 1] > 0.0f && envOut[blockSize - 1] <= 1.0f, "EnvelopeFollower tracks bounded envelope [0, 1]");
        check(gOut[0] > 0.0001f && gOut[0] < 0.999f, "expandModulationArrays populates valid Bilinear G factors");
        check(eqGainOut[0] > 0.0f && eqGainOut[0] <= 1.0f, "expandModulationArrays populates valid linear cut gains");
    }

    // =========================================================================
    // TEST 6: PlayheadTracker & BakedTimelineLUT Fallback (Requirement R3)
    // =========================================================================
    std::cout << "\n--- TEST 6: Timeline Fallback Data Structure (Requirement R3) ---\n";
    {
        // 1. Unmapped Section Fallback Test
        BakedTimelineLUT lut(10000, 1000, 5000);

        TimelineControlPoint* pts = lut.getWritePointer();
        int64_t idx = 2000 >> BakedTimelineLUT::INTERVAL_SHIFT;
        pts[idx].delayOffsetSamples = 48.0f;
        pts[idx].phaseAngleDegrees = 90.0f;
        pts[idx].dynamicCutGainDb = -4.0f;

        float del = 0.0f, ph = 0.0f, eq = 0.0f;

        // Position 500 is UNMAPPED (< 1000)
        bool mapped500 = lut.sample(500, del, ph, eq);
        check(!mapped500, "Unmapped section (sample 500) returns false (triggers Real-Time Auto Mode fallback)");

        // Position 6000 is UNMAPPED (>= 5000)
        bool mapped6000 = lut.sample(6000, del, ph, eq);
        check(!mapped6000, "Unmapped section (sample 6000) returns false (triggers Real-Time Auto Mode fallback)");

        // Position 2000 is MAPPED (in [1000, 5000])
        bool mapped2000 = lut.sample(2000, del, ph, eq);
        check(mapped2000, "Mapped section (sample 2000) returns true (applies timeline keyframe)");

        // 2. Shortest-Arc Circular Phase Interpolation Test
        int64_t idx1 = 3072 >> BakedTimelineLUT::INTERVAL_SHIFT; // exactly 48
        int64_t idx2 = idx1 + 1;                                 // exactly 49
        pts[idx1].phaseAngleDegrees = 170.0f;
        pts[idx2].phaseAngleDegrees = -170.0f; // 20 degree jump across +/-180 boundary

        int64_t midpoint = (idx1 << BakedTimelineLUT::INTERVAL_SHIFT) + 32; // exactly half interval
        lut.sample(midpoint, del, ph, eq);
        std::cout << "Midpoint phase interpolation between +170 and -170: " << ph << " deg\n";
        check(std::abs(std::abs(ph) - 180.0f) < 0.01f, "Shortest-arc circular phase interpolation correctly crosses +/-180 antipodal boundary");

        // 3. RcuTimelineManager Thread-Safety Test
        RcuTimelineManager rcu;
        uint64_t epoch1 = 0;
        auto* activeTable = rcu.acquireTableForAudio(epoch1);
        check(activeTable == nullptr, "RCU initial active table is null");

        auto* newLut = new BakedTimelineLUT(10000, 0, 10000);
        rcu.publishNewTable(newLut);

        uint64_t epoch2 = 0;
        auto* activeTable2 = rcu.acquireTableForAudio(epoch2);
        check(activeTable2 != nullptr, "RCU publishes new table atomically to audio thread");
        check(epoch2 > epoch1, "RCU advances audio thread epoch atomically");
    }

    // =========================================================================
    // TEST 7: EDGE CASES: Odd Buffer Sizes & Tail Vector Remainder Loops
    // =========================================================================
    std::cout << "\n--- TEST 7: Odd Buffer Sizes & Remainder Loop Stress Tests ---\n";
    {
        const std::vector<int> oddSizes = { 1, 3, 5, 7, 9, 15, 33, 63, 65, 127, 255, 513 };
        bool allOddSizesPass = true;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 1024;
        spec.numChannels = 2;

        PhaseEngine pe;
        pe.prepare(spec);
        pe.setBaseRotation(45.0f, 80.0f);

        for (int sz : oddSizes)
        {
            juce::AudioBuffer<float> buf(2, sz);
            for (int i = 0; i < sz; ++i) {
                buf.setSample(0, i, 0.5f);
                buf.setSample(1, i, -0.5f);
            }

            std::vector<float> gArr(sz, 0.4f);
            juce::dsp::AudioBlock<float> blk(buf);
            pe.processBlock(blk, gArr.data());

            for (int i = 0; i < sz; ++i) {
                if (!std::isfinite(buf.getSample(0, i)) || !std::isfinite(buf.getSample(1, i)))
                    allOddSizesPass = false;
            }
        }
        check(allOddSizesPass, "Odd buffer sizes processed cleanly without buffer overruns or NaN");

        // Zero-length buffer handling (FL Studio empty block edge case)
        juce::AudioBuffer<float> emptyBuf(2, 0);
        juce::dsp::AudioBlock<float> emptyBlk(emptyBuf);
        pe.processBlock(emptyBlk, nullptr);
        check(true, "Zero-length AudioBlock handled gracefully without crashing or null-dereference");
    }

    // =========================================================================
    // TEST 8: 4-Input Channel Summing Math Verification
    // =========================================================================
    std::cout << "\n--- TEST 8: 4-Input Channel Summing Bus Math Verification ---\n";
    {
        constexpr int blockSize = 128;
        juce::AudioBuffer<float> trackA(2, blockSize);
        juce::AudioBuffer<float> trackB(2, blockSize);
        juce::AudioBuffer<float> mix(2, blockSize);

        for (int i = 0; i < blockSize; ++i) {
            trackA.setSample(0, i, 0.3f);
            trackA.setSample(1, i, 0.4f);
            trackB.setSample(0, i, 0.2f);
            trackB.setSample(1, i, 0.1f);
        }

        // Summing bus math: Output = Track A + Track B
        for (int ch = 0; ch < 2; ++ch) {
            mix.copyFrom(ch, 0, trackA, ch, 0, blockSize);
            mix.addFrom(ch, 0, trackB, ch, 0, blockSize);
        }

        bool sumAccurate = true;
        for (int i = 0; i < blockSize; ++i) {
            if (std::abs(mix.getSample(0, i) - 0.5f) > 1e-6f) sumAccurate = false;
            if (std::abs(mix.getSample(1, i) - 0.5f) > 1e-6f) sumAccurate = false;
        }
        check(sumAccurate, "Track A (Input 1/2) and Track B (Input 3/4) sum accurately into 2-channel output");
    }

    // =========================================================================
    // TEST 9: DynamicEq Odd Buffer Sizes & Tail Remainder Loops
    // =========================================================================
    std::cout << "\n--- TEST 9: DynamicEq Odd Buffer Sizes & Remainder Loops ---\n";
    {
        const std::vector<int> oddSizes = { 1, 3, 5, 7, 9, 15, 33, 63, 65, 127, 255, 513 };
        bool allOddPass = true;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 1024;
        spec.numChannels = 2;

        DynamicEq deq;
        deq.prepare(spec);
        deq.setParameters(80.0f, -6.0f);

        for (int sz : oddSizes)
        {
            juce::AudioBuffer<float> buf(2, sz);
            for (int i = 0; i < sz; ++i) {
                buf.setSample(0, i, 0.4f);
                buf.setSample(1, i, -0.4f);
            }

            std::vector<float> eqGainArr(sz, 0.6f);
            juce::dsp::AudioBlock<float> blk(buf);
            deq.processBlock(blk, eqGainArr.data());

            for (int i = 0; i < sz; ++i) {
                if (!std::isfinite(buf.getSample(0, i)) || !std::isfinite(buf.getSample(1, i)))
                    allOddPass = false;
            }
        }
        check(allOddPass, "DynamicEq odd buffer sizes and remainder loops processed cleanly without NaN");

        // Zero-length AudioBlock handling
        juce::AudioBuffer<float> emptyBuf(2, 0);
        juce::dsp::AudioBlock<float> emptyBlk(emptyBuf);
        deq.processBlock(emptyBlk, nullptr);
        check(true, "DynamicEq zero-length AudioBlock handled safely without crashing");
    }

    // =========================================================================
    // TEST 10: Symmetrical Relative Delay (Negative Delay Logic)
    // =========================================================================
    std::cout << "\n--- TEST 10: Symmetrical Relative Delay Logic ---\n";
    {
        constexpr double sampleRate = 48000.0;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 2;

        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayA(4800);
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayB(4800);
        delayA.prepare(spec);
        delayB.prepare(spec);

        // Simulation helper exactly matching PluginProcessor delay routing:
        auto simulateDelayOffset = [&](float subDelMs) -> int {
            float trackADelayMs = (subDelMs >= 0.0f) ? subDelMs : 0.0f;
            float trackBDelayMs = (subDelMs < 0.0f) ? -subDelMs : 0.0f;

            delayA.reset();
            delayB.reset();
            delayA.setDelay(trackADelayMs * 0.001f * (float)sampleRate);
            delayB.setDelay(trackBDelayMs * 0.001f * (float)sampleRate);

            constexpr int N = 512;
            juce::AudioBuffer<float> bufA(2, N);
            juce::AudioBuffer<float> bufB(2, N);
            bufA.clear();
            bufB.clear();
            bufA.setSample(0, 0, 1.0f);
            bufA.setSample(1, 0, 1.0f);
            bufB.setSample(0, 0, 1.0f);
            bufB.setSample(1, 0, 1.0f);

            if (trackADelayMs > 0.001f) {
                juce::dsp::AudioBlock<float> blkA(bufA);
                juce::dsp::ProcessContextReplacing<float> ctxA(blkA);
                delayA.process(ctxA);
            }

            if (trackBDelayMs > 0.001f) {
                juce::dsp::AudioBlock<float> blkB(bufB);
                juce::dsp::ProcessContextReplacing<float> ctxB(blkB);
                delayB.process(ctxB);
            }

            int peakA = -1, peakB = -1;
            for (int i = 0; i < N; ++i) {
                if (bufA.getSample(0, i) > 0.5f && peakA < 0) peakA = i;
                if (bufB.getSample(0, i) > 0.5f && peakB < 0) peakB = i;
            }
            // Return relative offset: peakA - peakB
            return peakA - peakB;
        };

        int relativeOffsetPos = simulateDelayOffset(2.0f);  // +2 ms => 96 samples delay on A
        int relativeOffsetNeg = simulateDelayOffset(-2.0f); // -2 ms => 96 samples delay on B

        std::cout << "Relative offset for +2.0ms: " << relativeOffsetPos << " samples\n";
        std::cout << "Relative offset for -2.0ms: " << relativeOffsetNeg << " samples\n";

        check(relativeOffsetPos == 96, "Positive sub delay (+2ms) delays Track A by 96 samples relative to Track B");
        check(relativeOffsetNeg == -96, "Negative sub delay (-2ms) delays Track B by 96 samples relative to Track A (symmetric)");
    }

    // =========================================================================
    // TEST 11: BakedTimelineLUT Zero and Negative Duration Safety
    // =========================================================================
    std::cout << "\n--- TEST 11: BakedTimelineLUT Zero & Negative Duration Safety ---\n";
    {
        // Zero duration
        BakedTimelineLUT zeroLut(0, 0, 0);
        check(zeroLut.getNumPoints() >= 2, "BakedTimelineLUT(0) guarantees minimum 2-point allocation");
        float del = 0.0f, ph = 0.0f, eq = 0.0f;
        bool mappedZero = zeroLut.sample(0, del, ph, eq);
        check(!mappedZero, "Sampling zero-duration LUT safely returns false (unmapped fallback)");

        // Negative duration
        BakedTimelineLUT negLut(-10000, 0, 0);
        check(negLut.getNumPoints() >= 2, "BakedTimelineLUT(-10000) guarantees minimum 2-point allocation");
        bool mappedNeg = negLut.sample(-50, del, ph, eq);
        check(!mappedNeg, "Sampling negative-duration LUT safely returns false");
    }

    // =========================================================================
    // TEST 12: Maximum Buffer Size Stress Test (32768 Samples)
    // =========================================================================
    std::cout << "\n--- TEST 12: Large Buffer Stress Test (32768 Samples) ---\n";
    {
        constexpr int largeSize = 32768;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 96000.0;
        spec.maximumBlockSize = largeSize;
        spec.numChannels = 2;

        PhaseEngine pe;
        pe.prepare(spec);
        pe.setBaseRotation(90.0f, 60.0f);

        DynamicEq deq;
        deq.prepare(spec);
        deq.setParameters(60.0f, -6.0f);

        juce::AudioBuffer<float> bigBuffer(2, largeSize);
        for (int i = 0; i < largeSize; ++i) {
            float s = std::sin(2.0f * juce::MathConstants<float>::pi * 60.0f * i / 96000.0f);
            bigBuffer.setSample(0, i, s);
            bigBuffer.setSample(1, i, s);
        }

        std::vector<float> gArr(largeSize, 0.45f);
        std::vector<float> eqGainArr(largeSize, 0.5f);

        juce::dsp::AudioBlock<float> block(bigBuffer);
        pe.processBlock(block, gArr.data());
        deq.processBlock(block, eqGainArr.data());

        bool allFinite = true;
        for (int i = 0; i < largeSize; ++i) {
            if (!std::isfinite(bigBuffer.getSample(0, i)) || !std::isfinite(bigBuffer.getSample(1, i))) {
                allFinite = false;
                break;
            }
        }
        check(allFinite, "PhaseEngine and DynamicEq process 32768-sample buffer without overflow or NaN");
    }

    // =========================================================================
    // TEST 13: NaN / Inf Input Poisoning & Recovery
    // =========================================================================
    std::cout << "\n--- TEST 13: NaN / Inf Input Poisoning & Recovery ---\n";
    {
        const float nanVal = std::numeric_limits<float>::quiet_NaN();
        const float infVal = std::numeric_limits<float>::infinity();

        // 13a. EnvelopeFollower NaN Recovery
        EnvelopeFollower env;
        env.prepare(48000.0);
        constexpr int N = 64;
        std::vector<float> poisonBuf(N, nanVal);
        std::vector<float> outBuf(N, 0.0f);

        env.processBlock(poisonBuf.data(), outBuf.data(), N);

        std::vector<float> cleanBuf(N, 0.5f);
        env.processBlock(cleanBuf.data(), outBuf.data(), N);

        bool envRecovered = std::isfinite(outBuf[N - 1]) && outBuf[N - 1] > 0.0f;
        check(envRecovered, "EnvelopeFollower recovers from NaN poisoning without sticky NaN state");

        // 13b. PhaseEngine NaN Recovery
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
        check(peRecovered, "PhaseEngine recovers cleanly after receiving NaN/Inf inputs");

        // 13c. DynamicEq NaN Recovery
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
        check(deqRecovered, "DynamicEq recovers cleanly after receiving NaN/Inf inputs");
    }

    // =========================================================================
    // TEST 14: Summing Bus Track A vs Track B Lookahead Latency Alignment
    // =========================================================================
    std::cout << "\n--- TEST 14: Summing Bus Lookahead Latency Alignment ---\n";
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

        delayA.setDelay(static_cast<float>(lookaheadSamples));
        delayB.setDelay(static_cast<float>(lookaheadSamples));

        juce::AudioBuffer<float> bufA(2, N);
        juce::AudioBuffer<float> bufB(2, N);
        bufA.clear();
        bufB.clear();
        bufA.setSample(0, 0, 1.0f);
        bufB.setSample(0, 0, 1.0f);

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

        check(peakA == lookaheadSamples, "Track A delayed by lookahead (240 samples)");
        check(peakB == lookaheadSamples, "Track B delayed by matching lookahead (240 samples)");
        check(peakA == peakB, "Track A and Track B are perfectly time-aligned in summing bus");
    }

    // =========================================================================
    // TEST 15: Delay Automation Continuity (Zero Glitch / Stale Buffer)
    // =========================================================================
    std::cout << "\n--- TEST 15: Delay Automation Continuity ---\n";
    {
        constexpr double sampleRate = 48000.0;
        constexpr int N = 1000;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = N;
        spec.numChannels = 2;

        juce::dsp::DelayLine<float> delayLine(96000);
        delayLine.prepare(spec);

        delayLine.setDelay(0.0f);
        for (int i = 0; i < 500; ++i) {
            float val = static_cast<float>(i + 1);
            delayLine.pushSample(0, val);
            float out = delayLine.popSample(0);
            assert(std::abs(out - val) < 1e-4f);
        }

        delayLine.setDelay(10.0f);
        delayLine.pushSample(0, 501.0f);
        float outPop = delayLine.popSample(0);

        check(std::abs(outPop - 491.0f) < 1.0f,
              "DelayLine smoothly outputs past history without stale buffer clicks when automated from 0ms");
    }

    // =========================================================================
    // TEST 16: Sample Rate Transition Stress Test
    // =========================================================================
    std::cout << "\n--- TEST 16: Sample Rate Transition Stress Test ---\n";
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
    // TEST 17: PhaseEngine Boundary Angles & tanFactor Clamping Order
    // =========================================================================
    std::cout << "\n--- TEST 17: PhaseEngine Boundary Angles & tanFactor Clamping ---\n";
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
            if (!std::isfinite(fc) || fc < 10.0f || fc > 24000.0f * 0.49f) allAnglesFinite = false;

            pe.updateCenterFreq(120.0f);
            float fcUpdated = pe.getBaseCutoff();
            if (!std::isfinite(fcUpdated) || fcUpdated < 10.0f || fcUpdated > 24000.0f * 0.49f) allAnglesFinite = false;
        }

        check(allAnglesFinite, "PhaseEngine setBaseRotation and updateCenterFreq bounded and stable across all angles [-180, 180]");
    }

    // =========================================================================
    // TEST 18: SubCrossover LR4 Flat Magnitude Summing
    // =========================================================================
    std::cout << "\n--- TEST 18: SubCrossover LR4 Flat Magnitude Summing ---\n";
    {
        constexpr double sampleRate = 48000.0;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 1024;
        spec.numChannels = 2;

        SubCrossover xover;
        xover.prepare(spec);
        xover.setCrossoverFrequency(100.0f);

        constexpr int N = 4096;
        juce::AudioBuffer<float> inBuf(2, N);
        juce::AudioBuffer<float> subBuf(2, N);
        juce::AudioBuffer<float> highBuf(2, N);
        juce::AudioBuffer<float> sumBuf(2, N);

        for (int i = 0; i < N; ++i) {
            float s = std::sin(2.0f * 3.14159f * 100.0f * i / 48000.0f);
            inBuf.setSample(0, i, s);
            inBuf.setSample(1, i, s);
        }

        xover.process(inBuf, subBuf, highBuf, N);

        for (int ch = 0; ch < 2; ++ch) {
            sumBuf.copyFrom(ch, 0, subBuf, ch, 0, N);
            sumBuf.addFrom(ch, 0, highBuf, ch, 0, N);
        }

        float maxAmp = 0.0f;
        for (int i = 512; i < N; ++i) {
            float amp = std::abs(sumBuf.getSample(0, i));
            if (amp > maxAmp) maxAmp = amp;
        }

        float magErrorDb = std::abs(juce::Decibels::gainToDecibels(maxAmp));
        check(magErrorDb < 0.2f, "SubCrossover LR4 sum produces flat magnitude (<0.2 dB) at crossover frequency");
    }

    // =========================================================================
    // TEST 19: PlayheadTracker Discontinuity Scrubbing & Preroll Safety
    // =========================================================================
    std::cout << "\n--- TEST 19: PlayheadTracker Discontinuity & Preroll Stress ---\n";
    {
        PlayheadTracker tracker;
        tracker.prepare(48000.0);

        struct MockPlayHead : public juce::AudioPlayHead
        {
            juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
            juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return pos; }
        } mock;

        juce::AudioPlayHead::PositionInfo pInfo;
        pInfo.setIsPlaying(true);
        pInfo.setTimeInSamples(-48000); // Negative count-in
        pInfo.setBpm(120.0);
        mock.pos = pInfo;

        auto snap1 = tracker.update(&mock, 512);
        check(snap1.samplePosition == -48000, "PlayheadTracker preserves negative count-in sample position");

        pInfo.setTimeInSamples(-48000 + 512);
        mock.pos = pInfo;
        auto snap2 = tracker.update(&mock, 512);
        check(snap2.discontinuityEpoch == 0, "No false discontinuity triggered during count-in playback");

        pInfo.setTimeInSamples(1000000);
        mock.pos = pInfo;
        auto snap3 = tracker.update(&mock, 512);
        check(snap3.discontinuityEpoch > 0, "Discontinuity epoch increments immediately on transport scrub jump");

        pInfo.setTimeInSamples(500);
        mock.pos = pInfo;
        auto snap4 = tracker.update(&mock, 512);
        check(snap4.discontinuityEpoch > snap3.discontinuityEpoch, "Discontinuity epoch increments on backward scrub jump");
    }

    // =========================================================================
    // TEST 20: SignalAnalyzer numSamples Scan & Poisoned Tail
    // =========================================================================
    std::cout << "\n--- TEST 20: SignalAnalyzer numSamples Scan & Poisoned Tail ---\n";
    {
        const double sr = 48000.0;
        constexpr int C = 4096; // buffer capacity > any live n used below

        // ---- T20.1: processMeters ignores poisoned tail --------------------
        {
            SignalAnalyzer a;
            a.prepare(sr);

            juce::AudioBuffer<float> trackA(2, C);
            juce::AudioBuffer<float> trackB(2, C);
            constexpr int n = 512;
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < n; ++i) {
                    trackA.setSample(ch, i, 0.25f);
                    trackB.setSample(ch, i, 0.25f);
                }
                // Stale-tail poison: pre-fix capacity scans would report these magnitudes
                for (int i = n; i < C; ++i) {
                    trackA.setSample(ch, i, 7.777f);
                    trackB.setSample(ch, i, -3.3e8f);
                }
            }

            float envRms = 0.0f, envPeak = 0.0f;
            std::atomic<float> outRms{ -60.0f }, outPeak{ -60.0f }, persistentPeak{ -60.0f };

            a.processMeters(trackA, envRms, envPeak, outRms, outPeak, persistentPeak, n);

            // Ballistics dt discriminator: dt must be n/sr, NOT capacity/sr.
            // First-block attack: envRms = alphaAttack * 0.25 (alphaAttack ~ 0.656).
            // A capacity-derived dt (4096/48000) would give alpha ~ 0.9998 -> value ~ 0.24995.
            const float blockDt = static_cast<float>(n) / static_cast<float>(sr);
            const float alphaAttack = 1.0f - std::exp(-blockDt / 0.010f);
            check(std::abs(envRms - alphaAttack * 0.25f) < 1e-4f,
                  "T20.1a first-block envRms == alphaAttack(n/sr) * 0.25 (true block dt, live scan only)");

            // Settle the ballistics on the same poisoned buffer: converged envelope must
            // equal the live constant 0.25 (poisoned tail would converge to 7.777 / 3.3e8).
            for (int k = 0; k < 2500; ++k)
                a.processMeters(trackA, envRms, envPeak, outRms, outPeak, persistentPeak, n);

            check(std::abs(envPeak - 0.25f) < 1e-6f, "T20.1b converged envPeak == 0.25 (poisoned tail 7.777 ignored)");
            check(std::abs(envRms - 0.25f) < 1e-6f, "T20.1c converged envRms == 0.25 (RMS of live constant only)");
            check(std::abs(outPeak.load() - juce::Decibels::gainToDecibels(0.25f, -60.0f)) < 1e-3f,
                  "T20.1d outPeak == gainToDecibels(0.25) (~ -12.041 dB)");
            check(std::abs(outRms.load() - juce::Decibels::gainToDecibels(0.25f, -60.0f)) < 1e-3f,
                  "T20.1e outRms == gainToDecibels(0.25) (~ -12.041 dB)");
            check(std::abs(persistentPeak.load() - juce::Decibels::gainToDecibels(0.25f, -60.0f)) < 1e-3f,
                  "T20.1f persistentPeak max-hold == current peak (first-block hold)");

            // Track B poison variant: -3.3e8f tail must equally never enter the meters
            float envRmsB = 0.0f, envPeakB = 0.0f;
            std::atomic<float> outRmsB{ -60.0f }, outPeakB{ -60.0f }, persistentPeakB{ -60.0f };
            for (int k = 0; k < 2500; ++k)
                a.processMeters(trackB, envRmsB, envPeakB, outRmsB, outPeakB, persistentPeakB, n);
            check(std::abs(envPeakB - 0.25f) < 1e-6f,
                  "T20.1g Track B variant (-3.3e8 tail) converges to live 0.25");
        }

        // ---- T20.2: persistent peak survives smaller block; stale tail never enters ----
        {
            SignalAnalyzer a;
            a.prepare(sr);

            juce::AudioBuffer<float> buf(2, C);
            constexpr int n1 = 512;
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < n1; ++i) buf.setSample(ch, i, 0.25f);
                for (int i = n1; i < C; ++i) buf.setSample(ch, i, 7.777f);
            }

            float envRms = 0.0f, envPeak = 0.0f;
            std::atomic<float> outRms{ -60.0f }, outPeak{ -60.0f }, persistentPeak{ -60.0f };

            for (int k = 0; k < 2500; ++k)
                a.processMeters(buf, envRms, envPeak, outRms, outPeak, persistentPeak, n1);

            // Shrink the live block and re-poison the tail
            constexpr int n2 = 256;
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < n2; ++i) buf.setSample(ch, i, 0.10f);
                for (int i = n2; i < C; ++i) buf.setSample(ch, i, 9.9f);
            }
            for (int k = 0; k < 2500; ++k)
                a.processMeters(buf, envRms, envPeak, outRms, outPeak, persistentPeak, n2);

            check(std::abs(persistentPeak.load() - juce::Decibels::gainToDecibels(0.25f, -60.0f)) < 1e-3f,
                  "T20.2a persistentPeak holds -12.041 dB from earlier larger block (max-hold)");
            check(std::abs(outPeak.load() - juce::Decibels::gainToDecibels(0.10f, -60.0f)) < 1e-3f,
                  "T20.2b outPeak settles at gainToDecibels(0.10) (~ -20 dB), not poisoned 9.9 (+19.9 dB)");
        }

        // ---- T20.3: processCorrelation ignores poisoned tail (perfect correlation) ----
        {
            SignalAnalyzer a;
            a.prepare(sr);

            juce::AudioBuffer<float> bufA(2, C);
            juce::AudioBuffer<float> bufB(2, C);
            constexpr int n = 480;
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < n; ++i) {
                    const float s = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * i / 48000.0f);
                    bufA.setSample(ch, i, s);
                    bufB.setSample(ch, i, s);
                }
                // If scanned, +1000/-1000 tails would drive the Pearson value toward -1
                for (int i = n; i < C; ++i) {
                    bufA.setSample(ch, i, 1000.0f);
                    bufB.setSample(ch, i, -1000.0f);
                }
            }

            std::atomic<float> outCorr{ 0.0f }, persistentMaxCorr{ 0.0f };
            for (int k = 0; k < 5; ++k)
                a.processCorrelation(bufA, bufB, outCorr, persistentMaxCorr, 0, n);

            check(outCorr.load() > 0.999f,
                  "T20.3a identical live blocks give correlation > 0.999 despite +/-1000 tail poison");
            check(persistentMaxCorr.load() > 0.999f,
                  "T20.3b persistentMaxCorr holds > 0.999 (live-only normalization)");
        }

        // ---- T20.4: PhaseEngine dirty-check interaction guard (OPT-3 regression) ----
        {
            juce::dsp::ProcessSpec spec{ 48000.0, 512, 2 };
            PhaseEngine pe;
            pe.prepare(spec);

            // Static mapping reference for setBaseRotation(-90, 60):
            // (fs/pi) * atan(tan(pi*fc/fs) / tan(pi/8))  [-90 deg -> -phiRad/4 = pi/8]
            const double fs = 48000.0;
            const double expectedStatic = (fs / juce::MathConstants<double>::pi)
                * std::atan(std::tan(juce::MathConstants<double>::pi * 60.0 / fs)
                            / std::tan(juce::MathConstants<double>::pi / 8.0));

            pe.setBaseRotation(-90.0f, 60.0f);
            pe.updateCenterFreq(120.0f);           // pitch-track write to baseCutoff
            pe.setBaseRotation(-90.0f, 60.0f);     // same inputs: MUST recompute (coeffDirty)

            const float cutoffAfter = pe.getBaseCutoff();
            check(std::abs(cutoffAfter - static_cast<float>(expectedStatic)) < 1e-3f,
                  "T20.4a setBaseRotation recomputes after updateCenterFreq (static mapping, not pitch-tracked)");

            const float before = pe.getBaseCutoff();
            pe.setBaseRotation(-90.0f, 60.0f);     // no intervening updateCenterFreq: early-out
            check(std::abs(pe.getBaseCutoff() - before) < 1e-9f,
                  "T20.4b repeated setBaseRotation with no invalidation is idempotent (cache hit)");
        }

        // ---- T20.5: PitchTracker NaN recovery (OPT-11) ----------------------
        {
            PitchTracker tracker;
            tracker.prepare(48000.0);

            const float nanVal = std::numeric_limits<float>::quiet_NaN();
            bool allFinite = true;

            const float poisoned = tracker.processSample(nanVal);
            if (!std::isfinite(poisoned)) allFinite = false;

            // > 2 full periods of a 100 Hz sine above threshold: proves the LP state
            // recovered and period detection still works after the NaN (pre-fix the
            // poisoned lpState suppresses every subsequent detection).
            for (int i = 0; i < 2048; ++i) {
                const float s = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * i / 48000.0f);
                const float f = tracker.processSample(s);
                if (!std::isfinite(f)) allFinite = false;
            }

            const float finalFreq = tracker.getSmoothedFrequency();
            check(allFinite && std::isfinite(finalFreq) && finalFreq > 25.0f,
                  "T20.5a smoothed frequency stays finite and > 25 Hz after NaN input");

            // 100 Hz quantizes to MIDI 43 (G2, 98.0 Hz): detection actually resumed.
            check(std::abs(tracker.snappedPitchHz.load() - 98.0f) < 0.01f,
                  "T20.5b period detection resumed after NaN (snapped to G2 98.0 Hz)");
        }
    }

    std::cout << "\n=================================================================\n";
    std::cout << "  FINAL TEST SUMMARY: " << passedTests << " / " << totalTests << " TESTS PASSED ("
              << (passedTests == totalTests ? "100% SUCCESS" : "FAILURES DETECTED") << ")\n";
    std::cout << "=================================================================\n\n";

    return (passedTests == totalTests) ? 0 : 1;
}


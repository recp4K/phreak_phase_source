#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#include "TimelineAudioCaptureFifo.h"
#include "BakedTimelineLUT.h"
#include "../DSP/AutoAlignerThread.h"

namespace FreakPhase::Timeline
{
    struct TimelineWaveformOverviewData
    {
        int64_t startSample { 0 };
        int64_t endSample { 0 };
        std::vector<float> trackA_min;
        std::vector<float> trackA_max;
        std::vector<float> trackB_min;
        std::vector<float> trackB_max;
    };

    class TimelineAnalysisThread : public juce::Thread
    {
    public:
        TimelineAnalysisThread(TimelineAudioCaptureFifo& fifo,
                               RcuTimelineManager& rcuManager,
                               std::atomic<bool>& captureActiveFlag,
                               std::atomic<bool>& analyzingFlag,
                               std::atomic<float>& progressFlag,
                               std::atomic<double>& sampleRateRef)
            : juce::Thread("FreakPhase_TimelineAnalysisThread"),
              captureFifo(fifo),
              rcu(rcuManager),
              isCaptureActive(captureActiveFlag),
              isAnalyzing(analyzingFlag),
              analysisProgress(progressFlag),
              sr(sampleRateRef)
        {
            scratchPacketMain.resize(8192, 0.0f);
            scratchPacketSide.resize(8192, 0.0f);
        }

        ~TimelineAnalysisThread() override
        {
            stopThread(2000);
        }

        using SegmentsCallback = std::function<void(const juce::Array<juce::var>&, const TimelineWaveformOverviewData&)>;

        void setCompletionCallback(SegmentsCallback cb)
        {
            const juce::SpinLock::ScopedLockType sl(callbackLock);
            onComplete = std::move(cb);
        }

        void setBpm(double currentBpm) noexcept
        {
            bpm.store(currentBpm > 20.0 && currentBpm < 300.0 ? currentBpm : 120.0, std::memory_order_relaxed);
        }

        void run() override
        {
            juce::ScopedNoDenormals noDenormals; // background worker: covers analyzeSlice FFT work

            bool wasCapturing = false;

            while (!threadShouldExit())
            {
                const bool capturingNow = isCaptureActive.load(std::memory_order_acquire);

                // F1 race guard: hold the analyzing flag across ANY fifo access (drain
                // or analysis) so the message thread never resets the SPSC fifo while
                // this worker is mid-drain. Flag is cleared at iteration end / exit.
                const bool fifoHasData = captureFifo.getNumReady() > 0;
                if (fifoHasData)
                    isAnalyzing.store(true, std::memory_order_release);

                // Drain FIFO packets continuously while capturing or while FIFO has unread data
                CapturePacketMeta meta;
                bool readSomething = false;

                while (captureFifo.getNumReady() > 0)
                {
                    if (threadShouldExit())
                    {
                        isAnalyzing.store(false, std::memory_order_release);
                        return;
                    }

                    const int numReady = captureFifo.getNumReady();
                    if (static_cast<size_t>(numReady) > scratchPacketMain.size())
                    {
                        scratchPacketMain.resize(static_cast<size_t>(numReady) * 2);
                        scratchPacketSide.resize(static_cast<size_t>(numReady) * 2);
                    }

                    if (captureFifo.readNextBlock(scratchPacketMain.data(), scratchPacketSide.data(), meta))
                    {
                        readSomething = true;
                        if (capturedMain.empty())
                        {
                            recordingStartSample = meta.startTimelineSample;
                        }

                        // Append samples
                        const size_t oldSize = capturedMain.size();
                        capturedMain.resize(oldSize + static_cast<size_t>(meta.numSamples));
                        capturedSide.resize(oldSize + static_cast<size_t>(meta.numSamples));

                        std::copy_n(scratchPacketMain.data(), meta.numSamples, capturedMain.data() + oldSize);
                        std::copy_n(scratchPacketSide.data(), meta.numSamples, capturedSide.data() + oldSize);
                        recordingEndSample = meta.startTimelineSample + meta.numSamples;
                    }
                    else
                    {
                        break;
                    }
                }

                // Detect Transition: Capturing -> Stopped (DAW stopped after LEARN pass)
                if (wasCapturing && !capturingNow)
                {
                    processCapturedAudio();
                }

                wasCapturing = capturingNow;

                // processCapturedAudio() clears the flag itself on all exit paths;
                // the drain-hold must not outlive the iteration (idle waits must
                // not block host resets).
                if (fifoHasData)
                    isAnalyzing.store(false, std::memory_order_release);

                // Rest briefly if nothing was read and not capturing
                if (!readSomething && !capturingNow)
                {
                    wait(40);
                }
                else
                {
                    wait(10);
                }
            }
        }

    private:
        TimelineAudioCaptureFifo& captureFifo;
        RcuTimelineManager& rcu;
        std::atomic<bool>& isCaptureActive;
        std::atomic<bool>& isAnalyzing;
        std::atomic<float>& analysisProgress;
        std::atomic<double>& sr;
        std::atomic<double> bpm{ 120.0 };

        SegmentsCallback onComplete;
        juce::SpinLock callbackLock;

        std::vector<float> scratchPacketMain;
        std::vector<float> scratchPacketSide;
        std::vector<float> capturedMain;
        std::vector<float> capturedSide;
        int64_t recordingStartSample{ 0 };
        int64_t recordingEndSample{ 0 };

        // Dedicated helper aligner thread instance for FFT scratch
        juce::AudioBuffer<float> dummyBuffer{ 2, 8192 };
        std::atomic<int> dummyIdx{ 0 };
        AutoAlignerThread sliceAligner{ dummyBuffer, dummyIdx, sr };

        void processCapturedAudio()
        {
            const size_t totalSamples = capturedMain.size();
            if (totalSamples < 512 || capturedSide.size() != totalSamples)
            {
                capturedMain.clear();
                capturedSide.clear();
                return;
            }

            isAnalyzing.store(true, std::memory_order_release);
            analysisProgress.store(0.05f, std::memory_order_release);

            const double srSnapshot = sr.load(std::memory_order_relaxed);
            const double currentSr = srSnapshot > 1000.0 ? srSnapshot : 44100.0;
            const double currentBpm = bpm.load(std::memory_order_relaxed);
            const int64_t beatSamples = std::max(int64_t(512), static_cast<int64_t>((60.0 / currentBpm) * currentSr));
            const int64_t barSamples = beatSamples * 4;

            // 1. Build Downsampled Waveform Overview (512 points for UI minimap & lane backdrop)
            TimelineWaveformOverviewData overview;
            overview.startSample = recordingStartSample;
            overview.endSample = recordingEndSample;
            const size_t overviewBins = 512;
            overview.trackA_min.resize(overviewBins, 0.0f);
            overview.trackA_max.resize(overviewBins, 0.0f);
            overview.trackB_min.resize(overviewBins, 0.0f);
            overview.trackB_max.resize(overviewBins, 0.0f);

            const double binStep = static_cast<double>(totalSamples) / static_cast<double>(overviewBins);
            for (size_t b = 0; b < overviewBins; ++b)
            {
                const size_t sStart = static_cast<size_t>(b * binStep);
                const size_t sEnd = std::min(totalSamples, static_cast<size_t>((b + 1) * binStep));
                float minA = 0.0f, maxA = 0.0f, minB = 0.0f, maxB = 0.0f;
                for (size_t s = sStart; s < sEnd; ++s)
                {
                    minA = std::min(minA, capturedMain[s]);
                    maxA = std::max(maxA, capturedMain[s]);
                    minB = std::min(minB, capturedSide[s]);
                    maxB = std::max(maxB, capturedSide[s]);
                }
                overview.trackA_min[b] = minA;
                overview.trackA_max[b] = maxA;
                overview.trackB_min[b] = minB;
                overview.trackB_max[b] = maxB;
            }

            analysisProgress.store(0.2f, std::memory_order_release);

            // 2. Compute GCC-PHAT per Beat Segment (User confirmed Decision #1 & #2: tempo-synced beat grid)
            const int64_t firstBeatIndex = recordingStartSample / beatSamples;
            const int64_t lastBeatIndex = (recordingEndSample + beatSamples - 1) / beatSamples;
            const int64_t totalBeats = std::max(int64_t(1), lastBeatIndex - firstBeatIndex);

            juce::Array<juce::var> segVarArray;

            // Allocate a new BakedTimelineLUT for RCU publishing
            auto newLut = std::make_unique<BakedTimelineLUT>(recordingEndSample + beatSamples, recordingStartSample, recordingEndSample);
            auto* writePtr = newLut->getWritePointer();
            const int64_t numLutPoints = newLut->getNumPoints();

            // Neutral init
            for (int64_t p = 0; p < numLutPoints; ++p)
            {
                writePtr[p] = TimelineControlPoint{};
            }

            for (int64_t beatIdx = firstBeatIndex; beatIdx < lastBeatIndex; ++beatIdx)
            {
                if (threadShouldExit())
                {
                    isAnalyzing.store(false, std::memory_order_release);
                    return;
                }

                const int64_t segStart = beatIdx * beatSamples;
                const int64_t segEnd = segStart + beatSamples;

                // Slice indices relative to captured linear buffer
                const int64_t relStart = std::max(int64_t(0), segStart - recordingStartSample);
                const int64_t relEnd = std::min(static_cast<int64_t>(totalSamples), segEnd - recordingStartSample);
                const int sliceSamples = static_cast<int>(std::max(int64_t(0), relEnd - relStart));

                SegmentAlignmentResult alignRes;
                if (sliceSamples >= 64)
                {
                    alignRes = sliceAligner.analyzeSlice(capturedMain.data() + relStart,
                                                         capturedSide.data() + relStart,
                                                         sliceSamples,
                                                         currentSr);
                }

                const int barNum = static_cast<int>(segStart / barSamples) + 1;
                const int subBeatNum = static_cast<int>((segStart % barSamples) / beatSamples) + 1;
                const juce::String segId = "beat-" + juce::String(barNum) + "." + juce::String(subBeatNum);

                auto* obj = new juce::DynamicObject();
                obj->setProperty("id", segId);
                obj->setProperty("startSample", static_cast<double>(segStart));
                obj->setProperty("endSample", static_cast<double>(segEnd));
                obj->setProperty("delaySamples", alignRes.delaySamples);
                obj->setProperty("rotateDeg", alignRes.rotateDeg);
                obj->setProperty("eqCutDb", alignRes.suggestedEqCutDb);
                obj->setProperty("polarityFlip", alignRes.polarityFlip);
                obj->setProperty("bypassed", !alignRes.hasAudio);
                obj->setProperty("correlationBefore", alignRes.correlationBefore);
                obj->setProperty("correlationAfter", alignRes.correlationAfter);
                segVarArray.add(juce::var(obj));

                // Populate RCU LUT control points for this beat (64-sample intervals)
                if (alignRes.hasAudio)
                {
                    const int64_t ptStart = std::max(int64_t(0), segStart >> BakedTimelineLUT::INTERVAL_SHIFT);
                    const int64_t ptEnd = std::min(numLutPoints, (segEnd >> BakedTimelineLUT::INTERVAL_SHIFT) + 1);
                    for (int64_t pt = ptStart; pt < ptEnd; ++pt)
                    {
                        writePtr[pt].delayOffsetSamples = alignRes.delaySamples;
                        writePtr[pt].phaseAngleDegrees  = alignRes.rotateDeg;
                        writePtr[pt].dynamicCutGainDb   = alignRes.suggestedEqCutDb;
                        writePtr[pt].filterCutoffHz     = alignRes.detectedFreq;
                        writePtr[pt].polarityFlip       = alignRes.polarityFlip ? 1.0f : 0.0f;
                    }
                }

                const float progress = 0.2f + 0.75f * static_cast<float>(beatIdx - firstBeatIndex + 1) / static_cast<float>(totalBeats);
                analysisProgress.store(progress, std::memory_order_release);
            }

            // Publish new LUT table to RCU manager (Option 2 confirmed: Auto-bake)
            rcu.publishNewTable(newLut.release());

            analysisProgress.store(1.0f, std::memory_order_release);
            isAnalyzing.store(false, std::memory_order_release);

            // Notify UI on Message Thread
            SegmentsCallback cbCopy;
            {
                const juce::SpinLock::ScopedLockType sl(callbackLock);
                cbCopy = onComplete;
            }

            if (cbCopy)
            {
                juce::MessageManager::callAsync([cb = std::move(cbCopy), arr = segVarArray, ov = overview]() {
                    cb(arr, ov);
                });
            }

            // Free linear memory after analysis completes
            capturedMain.clear();
            capturedSide.clear();
        }
    };
}

#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace FreakPhase::Timeline
{
    struct TransportSnapshot
    {
        int64_t samplePosition { 0 };
        double  ppqPosition    { 0.0 };
        double  bpm            { 120.0 };
        int     timeSigNum     { 4 };
        int     timeSigDenom   { 4 };
        bool    isPlaying      { false };
        bool    isLooping      { false };
        int64_t loopStartSamples { 0 };
        int64_t loopEndSamples   { 0 };
        uint32_t discontinuityEpoch { 0 };
    };

    class PlayheadTracker
    {
    public:
        void prepare(double sampleRate) noexcept
        {
            currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
            reset();
        }

        void reset() noexcept
        {
            lastKnownSamplePos = 0;
            hasValidLastPos = false;
            lastBlockSamples = 0;
            epochCounter = 0;

            for (auto& slot : bufferSlots)
                slot = TransportSnapshot {};

            cleanIndex.store(0, std::memory_order_relaxed);
            readIndex.store(1, std::memory_order_relaxed);
            hasNewData.store(false, std::memory_order_relaxed);
            writeIndex = 2;
        }

        // =====================================================================
        // Audio Thread: Called at start of processBlock()
        // =====================================================================
        TransportSnapshot update(juce::AudioPlayHead* playhead, int numSamples) noexcept
        {
            TransportSnapshot snap {};
            bool hasValidHostInfo = false;

            if (playhead != nullptr)
            {
                if (auto posOpt = playhead->getPosition())
                {
                    hasValidHostInfo = true;

                    if (auto timeInSamples = posOpt->getTimeInSamples())
                        snap.samplePosition = *timeInSamples;
                    else if (auto timeInSeconds = posOpt->getTimeInSeconds())
                        snap.samplePosition = static_cast<int64_t>(*timeInSeconds * currentSampleRate);
                    else
                        snap.samplePosition = hasValidLastPos ? lastKnownSamplePos : 0;

                    if (auto ppq = posOpt->getPpqPosition()) snap.ppqPosition = *ppq;
                    if (auto bpm = posOpt->getBpm()) snap.bpm = *bpm;
                    if (auto sig = posOpt->getTimeSignature())
                    {
                        snap.timeSigNum = sig->numerator;
                        snap.timeSigDenom = sig->denominator;
                    }
                    snap.isPlaying = posOpt->getIsPlaying();
                    snap.isLooping = posOpt->getIsLooping();

                    if (auto loop = posOpt->getLoopPoints())
                    {
                        if (loop->ppqStart >= 0.0 && snap.bpm > 0.0)
                        {
                            const double samplesPerBeat = (currentSampleRate * 60.0) / snap.bpm;
                            snap.loopStartSamples = static_cast<int64_t>(loop->ppqStart * samplesPerBeat);
                            snap.loopEndSamples   = static_cast<int64_t>(loop->ppqEnd   * samplesPerBeat);
                        }
                    }
                }
            }

            // FL Studio Fallback: Continuously advance counter during playback
            if (!hasValidHostInfo)
            {
                if (hasValidLastPos && lastState.isPlaying)
                    snap.samplePosition = lastKnownSamplePos + lastBlockSamples;
                else
                    snap.samplePosition = hasValidLastPos ? lastKnownSamplePos : 0;

                snap.isPlaying = lastState.isPlaying;
                snap.bpm = lastState.bpm > 0.0 ? lastState.bpm : 120.0;
            }

            // Discontinuity Detection:
            // 1. Only increment epoch during playback if delta between actual and expected sample position exceeds 1 sample (e.g. scrub jump or loop wrap)
            // 2. When starting playback from stopped: increment epoch only if cursor position changed
            if (snap.isPlaying && hasValidHostInfo && hasValidLastPos)
            {
                if (!lastState.isPlaying)
                {
                    if (std::abs(snap.samplePosition - lastKnownSamplePos) > 1)
                    {
                        epochCounter++;
                    }
                }
                else
                {
                    const int64_t expectedPos = lastKnownSamplePos + lastBlockSamples;
                    if (std::abs(snap.samplePosition - expectedPos) > 1)
                    {
                        epochCounter++;
                    }
                }
            }
            snap.discontinuityEpoch = epochCounter;

            lastKnownSamplePos = snap.samplePosition;
            lastBlockSamples = numSamples;
            hasValidLastPos = true;
            lastState = snap;

            // Publish via Lock-Free Triple-Buffering
            bufferSlots[writeIndex] = snap;
            writeIndex = cleanIndex.exchange(writeIndex, std::memory_order_acq_rel);
            hasNewData.store(true, std::memory_order_release);

            return snap;
        }

        // =====================================================================
        // GUI / Message Thread: Called at 60 Hz to retrieve transport state
        // =====================================================================
        TransportSnapshot readSnapshot() noexcept
        {
            if (hasNewData.exchange(false, std::memory_order_acq_rel))
            {
                uint8_t prevRead = readIndex.load(std::memory_order_relaxed);
                uint8_t latestClean = cleanIndex.exchange(prevRead, std::memory_order_acq_rel);
                readIndex.store(latestClean, std::memory_order_relaxed);
                return bufferSlots[latestClean];
            }
            return bufferSlots[readIndex.load(std::memory_order_relaxed)];
        }

    private:
        double currentSampleRate = 44100.0;
        int64_t lastKnownSamplePos = 0;
        bool hasValidLastPos = false;
        int lastBlockSamples = 0;
        uint32_t epochCounter = 0;
        TransportSnapshot lastState {};

        // Lock-Free Triple-Buffer Slots
        TransportSnapshot bufferSlots[3];
        std::atomic<uint8_t> cleanIndex { 0 };
        std::atomic<uint8_t> readIndex  { 1 };
        std::atomic<bool>    hasNewData { false };
        uint8_t writeIndex { 2 };
    };
}

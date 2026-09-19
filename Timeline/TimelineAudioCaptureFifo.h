#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>

namespace FreakPhase::Timeline
{
    struct CapturePacketMeta
    {
        int64_t startTimelineSample { 0 };
        int numSamples { 0 };
        int fifoSampleOffset { 0 };
    };

    class TimelineAudioCaptureFifo
    {
    public:
        static constexpr int fifoCapacity = 262144; // ~5.9 sec at 44.1 kHz
        static constexpr int metaCapacity = 2048;   // Max blocks in flight

        TimelineAudioCaptureFifo()
            : audioFifo(fifoCapacity),
              metaFifo(metaCapacity)
        {
            bufferMain.setSize(1, fifoCapacity);
            bufferSide.setSize(1, fifoCapacity);
            bufferMain.clear();
            bufferSide.clear();
            metaQueue.resize(metaCapacity);
        }

        // =====================================================================
        // Audio Thread: Real-Time SPSC Write (Lock-Free, Zero-Allocation)
        // =====================================================================
        void write(const float* main, const float* side, int numSamples, int64_t startTimelineSample) noexcept
        {
            if (numSamples <= 0 || main == nullptr) return;

            int start1, size1, start2, size2;
            audioFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

            int mStart1, mSize1, mStart2, mSize2;
            metaFifo.prepareToWrite(1, mStart1, mSize1, mStart2, mSize2);

            // Atomic drop: drop frame if either audio or metadata lacks space
            if (size1 + size2 < numSamples || mSize1 == 0)
                return;

            // First Slice
            if (size1 > 0)
            {
                bufferMain.copyFrom(0, start1, main, size1);
                if (side != nullptr)
                    bufferSide.copyFrom(0, start1, side, size1);
                else
                    bufferSide.clear(0, start1, size1);
            }

            // Second Slice (Wrap-Around)
            // Memory Safety Fix: Clears start2 for size2 samples (NOT size1!)
            if (size2 > 0)
            {
                bufferMain.copyFrom(0, start2, main + size1, size2);
                if (side != nullptr)
                    bufferSide.copyFrom(0, start2, side + size1, size2);
                else
                    bufferSide.clear(0, start2, size2);
            }

            audioFifo.finishedWrite(size1 + size2);

            // Record Timeline Metadata Packet
            metaQueue[static_cast<size_t>(mStart1)] = CapturePacketMeta {
                startTimelineSample,
                numSamples,
                start1
            };
            metaFifo.finishedWrite(1);
        }

        // =====================================================================
        // Background Worker Thread: Read Blocks with Preserved Timeline Coordinates
        // =====================================================================
        bool readNextBlock(float* destMain, float* destSide, CapturePacketMeta& outMeta) noexcept
        {
            int mStart1, mSize1, mStart2, mSize2;
            metaFifo.prepareToRead(1, mStart1, mSize1, mStart2, mSize2);
            if (mSize1 == 0)
                return false;

            outMeta = metaQueue[static_cast<size_t>(mStart1)];
            if (audioFifo.getNumReady() < outMeta.numSamples)
                return false;

            metaFifo.finishedRead(1);

            int start1, size1, start2, size2;
            audioFifo.prepareToRead(outMeta.numSamples, start1, size1, start2, size2);

            if (size1 > 0)
            {
                if (destMain != nullptr)
                    juce::FloatVectorOperations::copy(destMain, bufferMain.getReadPointer(0, start1), size1);
                if (destSide != nullptr)
                    juce::FloatVectorOperations::copy(destSide, bufferSide.getReadPointer(0, start1), size1);
            }
            if (size2 > 0)
            {
                if (destMain != nullptr)
                    juce::FloatVectorOperations::copy(destMain + size1, bufferMain.getReadPointer(0, start2), size2);
                if (destSide != nullptr)
                    juce::FloatVectorOperations::copy(destSide + size1, bufferSide.getReadPointer(0, start2), size2);
            }

            audioFifo.finishedRead(size1 + size2);
            return true;
        }

        [[nodiscard]] int getNumReady() const noexcept { return audioFifo.getNumReady(); }

        // Message-thread only: clear captured audio + FIFO positions. Never call while the
        // analysis worker is mid-drain — resetting an SPSC fifo during consumption would
        // violate the single-producer/single-consumer invariant.
        void reset() noexcept
        {
            audioFifo.reset();
            metaFifo.reset();
            bufferMain.clear();
            bufferSide.clear();
        }

    private:
        juce::AbstractFifo audioFifo;
        juce::AudioBuffer<float> bufferMain;
        juce::AudioBuffer<float> bufferSide;

        juce::AbstractFifo metaFifo;
        std::vector<CapturePacketMeta> metaQueue;
    };
}

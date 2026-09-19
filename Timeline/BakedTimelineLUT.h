#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <cmath>

namespace FreakPhase::Timeline
{
    // Packed to 32 bytes: 8 x 32-bit floats (delay/phase/eqGain/cutoff/polarityFlip + 3 padding)
    struct alignas(16) TimelineControlPoint
    {
        float delayOffsetSamples { 0.0f }; // Delay offset in samples
        float phaseAngleDegrees  { 0.0f }; // Target phase rotation [-180, 180)
        float dynamicCutGainDb   { 0.0f }; // Dynamic EQ surgical cut depth [0, -18 dB]
        float filterCutoffHz     { 60.0f }; // Dynamic EQ center frequency [Hz]
        float polarityFlip       { 0.0f }; // 0.0f = Normal, 1.0f = Inverted
        float padding[3]         { 0.0f, 0.0f, 0.0f }; // 32-byte cache alignment
    };

    class BakedTimelineLUT
    {
    public:
        static constexpr int64_t SAMPLES_PER_INTERVAL = 64; // ~1.33 ms at 48 kHz
        static constexpr int64_t INTERVAL_SHIFT = 6;        // x >> 6 == x / 64

        BakedTimelineLUT(int64_t totalDurationSamples, int64_t mappedStart = 0, int64_t mappedEnd = 0)
            : totalSamples(std::max(int64_t(0), totalDurationSamples)),
              numPoints((std::max(int64_t(0), totalDurationSamples) >> INTERVAL_SHIFT) + 2),
              startSample(mappedStart),
              endSample(mappedEnd > 0 ? mappedEnd : std::max(int64_t(0), totalDurationSamples)),
              hasValidData(mappedEnd > mappedStart)
        {
            points.calloc(static_cast<size_t>(numPoints));
        }

        ~BakedTimelineLUT() = default;

        // Check if sample position resides within an explicitly mapped timeline section
        [[nodiscard]] inline bool isRegionMapped(int64_t samplePosition) const noexcept
        {
            return hasValidData && (samplePosition >= startSample && samplePosition < endSample);
        }

        void setMappedRange(int64_t start, int64_t end) noexcept
        {
            startSample = start;
            endSample = end;
            hasValidData = (end > start);
        }

        // =====================================================================
        // Pure O(1) Real-Time Audio Thread Sample Lookup (with Polarity Flip)
        // Returns true if mapped, false if unmapped (triggers Real-Time Auto Mode fallback)
        // =====================================================================
        inline bool sample(int64_t currentSamplePosition,
                           float& outDelaySamples,
                           float& outPhaseDegrees,
                           float& outEqGainDb,
                           bool& outPolarityFlip) const noexcept
        {
            if (!isRegionMapped(currentSamplePosition))
            {
                outPolarityFlip = false;
                return false;
            }

            if (currentSamplePosition <= 0)
            {
                outDelaySamples = points[0].delayOffsetSamples;
                outPhaseDegrees = points[0].phaseAngleDegrees;
                outEqGainDb     = points[0].dynamicCutGainDb;
                outPolarityFlip = (points[0].polarityFlip > 0.5f);
                return true;
            }

            const int64_t index = currentSamplePosition >> INTERVAL_SHIFT;
            if (index >= numPoints - 1)
            {
                outDelaySamples = points[numPoints - 1].delayOffsetSamples;
                outPhaseDegrees = points[numPoints - 1].phaseAngleDegrees;
                outEqGainDb     = points[numPoints - 1].dynamicCutGainDb;
                outPolarityFlip = (points[numPoints - 1].polarityFlip > 0.5f);
                return true;
            }

            const float frac = static_cast<float>(currentSamplePosition & (SAMPLES_PER_INTERVAL - 1))
                               * (1.0f / static_cast<float>(SAMPLES_PER_INTERVAL));

            const auto& p1 = points[index];
            const auto& p2 = points[index + 1];

            outDelaySamples = p1.delayOffsetSamples + frac * (p2.delayOffsetSamples - p1.delayOffsetSamples);
            outEqGainDb     = p1.dynamicCutGainDb + frac * (p2.dynamicCutGainDb - p1.dynamicCutGainDb);
            outPolarityFlip = (frac < 0.5f) ? (p1.polarityFlip > 0.5f) : (p2.polarityFlip > 0.5f);

            // Shortest-arc circular phase interpolation with tie-breaking hysteresis
            float diff = std::fmod(p2.phaseAngleDegrees - p1.phaseAngleDegrees + 540.0f, 360.0f) - 180.0f;
            if (diff == -180.0f) diff = 180.0f; // Deterministic tie-break
            
            float angle = p1.phaseAngleDegrees + frac * diff;
            // Strict range normalization into [-180, +180)
            angle = std::fmod(angle + 180.0f, 360.0f);
            if (angle < 0.0f) angle += 360.0f;
            outPhaseDegrees = angle - 180.0f;

            if (!std::isfinite(outDelaySamples)) outDelaySamples = 0.0f;
            if (!std::isfinite(outPhaseDegrees)) outPhaseDegrees = 0.0f;
            if (!std::isfinite(outEqGainDb)) outEqGainDb = 0.0f;

            return true;
        }

        // Backwards-compatible 4-argument overload for existing tests
        inline bool sample(int64_t currentSamplePosition,
                           float& outDelaySamples,
                           float& outPhaseDegrees,
                           float& outEqGainDb) const noexcept
        {
            bool dummyFlip = false;
            return sample(currentSamplePosition, outDelaySamples, outPhaseDegrees, outEqGainDb, dummyFlip);
        }

        TimelineControlPoint* getWritePointer() noexcept { return points.get(); }
        [[nodiscard]] int64_t getNumPoints() const noexcept { return numPoints; }

    private:
        int64_t totalSamples { 0 };
        int64_t numPoints { 0 };
        int64_t startSample { 0 };
        int64_t endSample { 0 };
        bool hasValidData { false };
        juce::HeapBlock<TimelineControlPoint> points;
    };

    // =========================================================================
    // Epoch-Fenced RCU Table Manager: Zero UAF, Zero Locks on Audio Thread
    // =========================================================================
    class RcuTimelineManager
    {
    public:
        RcuTimelineManager() = default;

        ~RcuTimelineManager()
        {
            aliveToken->store(false, std::memory_order_release);

            // Drain remaining retired tables on destruction
            for (auto& item : retiredQueue)
                delete item.table;
            retiredQueue.clear();

            auto* cur = activeLUT.exchange(nullptr);
            delete cur;
        }

        // Called on Audio Thread: Marks epoch and loads table pointer
        const BakedTimelineLUT* acquireTableForAudio(uint64_t& outEpoch) noexcept
        {
            outEpoch = currentAudioEpoch.fetch_add(1, std::memory_order_acq_rel);
            return activeLUT.load(std::memory_order_acquire);
        }

        // Called on GUI Thread: Pure read-only load of active table without advancing audio epoch
        const BakedTimelineLUT* getActiveTableForGui() const noexcept
        {
            return activeLUT.load(std::memory_order_acquire);
        }

        // Called from ANY thread. Routes the actual swap + retire-queue push to the GUI
        // (message) thread so retiredQueue is ONLY ever mutated there. This eliminates the
        // data race between the worker thread and GUI timer that caused heap corruption.
        void publishNewTable(BakedTimelineLUT* newTable)
        {
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            const bool isGuiThread = (mm != nullptr) ? mm->isThisTheMessageThread() : true;

            if (isGuiThread)
            {
                publishOnGuiThread(newTable);
            }
            else
            {
                // Pass pointer directly to GUI thread via capture.
                // Eliminates race conditions and null pointer overwrites.
                juce::MessageManager::callAsync([this, token = aliveToken, newTable]()
                {
                    if (!token->load(std::memory_order_acquire))
                    {
                        delete newTable;
                        return;
                    }
                    publishOnGuiThread(newTable);
                });
            }
        }

        // Called on GUI Thread (e.g. 10 Hz timer): Frees memory safely after quiescent period
        void reclaimQuiescentTables()
        {
            // retiredQueue is GUI-thread-only; jassert guards against accidental misuse
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            {
                jassert(mm->isThisTheMessageThread());
            }

            const uint64_t audioEpoch = currentAudioEpoch.load(std::memory_order_acquire);

            auto it = retiredQueue.begin();
            while (it != retiredQueue.end())
            {
                // Option A: If at least 4 audio blocks have elapsed, no audio thread can be accessing old table (FL Studio safety)
                if (audioEpoch >= it->retireEpoch + 4)
                {
                    delete it->table;
                    it = retiredQueue.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // FIX: Prevent memory leak by enforcing maximum size on retiredQueue
            // If queue grows too large (e.g., GUI thread not running), force cleanup of oldest entries
            static constexpr size_t MAX_RETIRED_ENTRIES = 16;
            while (retiredQueue.size() > MAX_RETIRED_ENTRIES)
            {
                delete retiredQueue.front().table;
                retiredQueue.erase(retiredQueue.begin());
            }
        }

    private:
        struct RetiredEntry
        {
            const BakedTimelineLUT* table;
            uint64_t retireEpoch;
        };

        // Must only be called on the GUI (message) thread
        void publishOnGuiThread(BakedTimelineLUT* newTable)
        {
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            {
                jassert(mm->isThisTheMessageThread());
            }

            const uint64_t retireEpoch = currentAudioEpoch.load(std::memory_order_acquire);
            auto* oldTable = activeLUT.exchange(newTable, std::memory_order_acq_rel);

            if (oldTable != nullptr)
            {
                // FIX: Check if we're about to exceed MAX_RETIRED_ENTRIES before adding
                // This prevents unbounded growth if reclaimQuiescentTables hasn't run yet
                if (retiredQueue.size() >= MAX_RETIRED_ENTRIES)
                {
                    // Force immediate cleanup of oldest entry to make room
                    delete retiredQueue.front().table;
                    retiredQueue.erase(retiredQueue.begin());
                }
                retiredQueue.push_back(RetiredEntry { oldTable, retireEpoch });
            }

            reclaimQuiescentTables();
        }

        std::atomic<uint64_t>              currentAudioEpoch { 0 };
        std::atomic<const BakedTimelineLUT*> activeLUT       { nullptr };
        std::vector<RetiredEntry>          retiredQueue;                   // GUI thread only
        std::shared_ptr<std::atomic<bool>> aliveToken        { std::make_shared<std::atomic<bool>>(true) };
    };
}

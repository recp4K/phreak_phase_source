// SignalAnalyzer.cpp
#include "SignalAnalyzer.h"

namespace
{
    // Closed-form degree wrap to (-180, 180]: no iteration, so a NaN can never
    // spin forever under /fp:fast (challenger F3). +180 maps to -180 (deterministic
    // tie-break, same convention as BakedTimelineLUT phase interpolation).
    inline float wrapDeg180(float x) noexcept
    {
        x = std::fmod(x + 180.0f, 360.0f);
        if (x < 0.0f) x += 360.0f;
        return x - 180.0f;
    }
}

void SignalAnalyzer::prepare(double sampleRate) noexcept
{
    sr = std::max(1.0, sampleRate);
    corrEmaDot = 0.0f;
    corrEmaMainSq = 0.0f;
    corrEmaSideSq = 0.0f;
    
    apfStateMain = 0.0f;
    apfStateSc = 0.0f;
    smoothedPhaseDiffDeg = 0.0f;
    
    // Calculate coefficient for a 1st order all-pass filter centered at 60 Hz 
    // to act as a pseudo-Hilbert transform (narrowband 90-degree phase shift)
    float wd = juce::MathConstants<float>::pi * 60.0f / static_cast<float>(sr);
    float tanWd = std::tan(wd);
    apfCoeff = (1.0f - tanWd) / (1.0f + tanWd);
}

void SignalAnalyzer::setResponseMs(float ms) noexcept
{
    targetMs = std::max(1.0f, ms);
}

// Smart Disable contract: clear analyzer state (sr/apfCoeff keep their prepared values)
void SignalAnalyzer::reset() noexcept
{
    corrEmaDot = 0.0f;
    corrEmaMainSq = 0.0f;
    corrEmaSideSq = 0.0f;

    apfStateMain = 0.0f;
    apfStateSc = 0.0f;
    smoothedPhaseDiffDeg = 0.0f;
}

void SignalAnalyzer::processMeters(juce::AudioBuffer<float>& buffer, float& envRms, float& envPeak,
    std::atomic<float>& outRms, std::atomic<float>& outPeak,
    std::atomic<float>& persistentPeak, int numSamplesToScan) noexcept
{
    // Scan only the live block: buffers are pre-allocated to full scratch capacity,
    // so reading beyond numSamples would average stale audio from previous larger blocks.
    const int numSamples = juce::jmin(numSamplesToScan, buffer.getNumSamples());
    if (numSamples <= 0) return;

    const int numChannels = buffer.getNumChannels();
    float bRms = buffer.getRMSLevel(0, 0, numSamples);
    if (numChannels > 1)
        bRms = std::max(bRms, buffer.getRMSLevel(1, 0, numSamples));

    float bPeak = buffer.getMagnitude(0, 0, numSamples);
    if (numChannels > 1)
        bPeak = std::max(bPeak, buffer.getMagnitude(1, 0, numSamples));

    // Dual-ballistics EMA: fast Attack (10 ms), slow Release (300 ms for RMS, 1000 ms for Peak)
    float blockDt = static_cast<float>(numSamples) / static_cast<float>(sr);
    const float alphaAttack = 1.0f - std::exp(-blockDt / 0.010f);
    const float alphaReleaseRms = 1.0f - std::exp(-blockDt / 0.300f);
    const float alphaReleasePeak = 1.0f - std::exp(-blockDt / 1.000f);

    if (bRms > envRms)
        envRms = (1.0f - alphaAttack) * envRms + alphaAttack * bRms;
    else
        envRms = (1.0f - alphaReleaseRms) * envRms + alphaReleaseRms * bRms;

    if (bPeak > envPeak)
        envPeak = (1.0f - alphaAttack) * envPeak + alphaAttack * bPeak;
    else
        envPeak = (1.0f - alphaReleasePeak) * envPeak + alphaReleasePeak * bPeak;

    float dbRms = juce::Decibels::gainToDecibels(envRms, -60.0f);
    float dbPeak = juce::Decibels::gainToDecibels(envPeak, -60.0f);

    outRms.store(dbRms, std::memory_order_relaxed);
    outPeak.store(dbPeak, std::memory_order_relaxed);

    if (dbPeak > persistentPeak.load(std::memory_order_relaxed)) {
        persistentPeak.store(dbPeak, std::memory_order_relaxed);
    }
}

void SignalAnalyzer::processCorrelation(juce::AudioBuffer<float>& mainBus, juce::AudioBuffer<float>& sideBus,
    std::atomic<float>& outCorrelation, std::atomic<float>& persistentMaxCorr,
    int trackBChannelMode, int numSamplesToScan) noexcept
{
    if (mainBus.getNumChannels() == 0 || sideBus.getNumChannels() == 0) return;

    const int numSamples = juce::jmin(numSamplesToScan, juce::jmin(mainBus.getNumSamples(), sideBus.getNumSamples()));
    if (numSamples <= 0) return; // prevents 0-length block producing 0 * invN = NaN EMA state
    const float* m0 = mainBus.getReadPointer(0);
    const float* m1 = (mainBus.getNumChannels() > 1) ? mainBus.getReadPointer(1) : m0;
    const float* s0 = sideBus.getReadPointer(0);
    const float* s1 = (sideBus.getNumChannels() > 1) ? sideBus.getReadPointer(1) : s0;

    float blockDot = 0.0f, blockMSq = 0.0f, blockSSq = 0.0f;

    // Obliczanie wektorowe dla obu kanałów stereo (L/R) z uwzględnieniem TRACK_B_CH
    for (int i = 0; i < numSamples; ++i) {
        float m = 0.5f * (m0[i] + m1[i]);
        float s = 0.0f;
        if (trackBChannelMode == 1)
            s = s1[i];
        else if (trackBChannelMode == 2)
            s = 0.5f * (s0[i] + s1[i]);
        else
            s = s0[i];

        blockDot += m * s;
        blockMSq += m * m;
        blockSSq += s * s;
    }

    // Wydłużone okno uśredniania korelacji Pearsona (200-250 ms) zgodnie z wytycznymi badawczymi
    float blockDt = static_cast<float>(numSamples) / static_cast<float>(sr);
    const float corrWindowSec = 0.220f; // 220 ms
    float blockAlpha = 1.0f - std::exp(-blockDt / corrWindowSec);

    // Filtracja dolnoprzepustowa bloków w celu stabilizacji pomiaru
    float invN = 1.0f / static_cast<float>(numSamples);
    corrEmaDot = (1.0f - blockAlpha) * corrEmaDot + blockAlpha * (blockDot * invN);
    corrEmaMainSq = (1.0f - blockAlpha) * corrEmaMainSq + blockAlpha * (blockMSq * invN);
    corrEmaSideSq = (1.0f - blockAlpha) * corrEmaSideSq + blockAlpha * (blockSSq * invN);

    float currentCorr = 0.0f;
    if (corrEmaMainSq > 0.00001f && corrEmaSideSq > 0.00001f) {
        currentCorr = std::clamp(corrEmaDot / std::sqrt(corrEmaMainSq * corrEmaSideSq), -1.0f, 1.0f);
    }

    outCorrelation.store(currentCorr, std::memory_order_relaxed);
    if (currentCorr > persistentMaxCorr.load(std::memory_order_relaxed)) {
        persistentMaxCorr.store(currentCorr, std::memory_order_relaxed);
    }
}

void SignalAnalyzer::processInstantPhase(float mainSample, float scSample) noexcept
{
    float m = mainSample;
    float s = scSample;
    processInstantPhase(&m, &s, 1);
}

void SignalAnalyzer::processInstantPhase(const float* mainData, const float* scData, int numSamples) noexcept
{
    if (mainData == nullptr || scData == nullptr || numSamples <= 0) return;

    float lastPhaseM = 0.0f;
    float lastPhaseS = 0.0f;
    float maxEnergy = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // F3: sanitize inputs — NaN would poison the APF states and make the
        // energy/atan2 results NaN (unspecified under /fp:fast)
        const float mainSample = std::isfinite(mainData[i]) ? mainData[i] : 0.0f;
        const float scSample   = std::isfinite(scData[i])   ? scData[i]   : 0.0f;

        // 1st-order APF Hilbert -90-degree phase shifter (-a feedforward)
        float mainQ = -apfCoeff * mainSample + apfStateMain;
        apfStateMain = mainSample + apfCoeff * mainQ;

        float scQ = -apfCoeff * scSample + apfStateSc;
        apfStateSc = scSample + apfCoeff * scQ;

        float energy = mainSample * mainSample + scSample * scSample;
        if (energy > maxEnergy)
        {
            maxEnergy = energy;
            lastPhaseM = std::atan2(mainQ, mainSample);
            lastPhaseS = std::atan2(scQ, scSample);
        }
    }

    // F3: sanitize persisted APF states (a poisoned state outlives the block)
    if (! std::isfinite(apfStateMain)) apfStateMain = 0.0f;
    if (! std::isfinite(apfStateSc))   apfStateSc   = 0.0f;

    if (maxEnergy > 1e-8f)
    {
        instantPhaseMain.store(lastPhaseM, std::memory_order_relaxed);
        instantPhaseSc.store(lastPhaseS, std::memory_order_relaxed);

        float diff = wrapDeg180((lastPhaseM - lastPhaseS) * (180.0f / juce::MathConstants<float>::pi));

        // Shortest-arc circular EMA smoothing (~50ms) for phaseDiffDeg
        float delta = wrapDeg180(diff - smoothedPhaseDiffDeg);

        float phaseDt = static_cast<float>(numSamples) / static_cast<float>(sr);
        float phaseAlpha = 1.0f - std::exp(-phaseDt / 0.050f);
        smoothedPhaseDiffDeg = wrapDeg180(smoothedPhaseDiffDeg + phaseAlpha * delta);

        phaseDiffDeg.store(smoothedPhaseDiffDeg, std::memory_order_relaxed);
    }

    // Vector magnitude normalized [0.0, 1.0] for dual-signal
    float mag = std::clamp(std::sqrt(maxEnergy) * 0.7071f, 0.0f, 1.0f);
    phaseVectorMag.store(mag, std::memory_order_relaxed);
}

void SignalAnalyzer::processInstantPhase(const float* mainData, const float* scDataL, const float* scDataR, int numSamples) noexcept
{
    if (mainData == nullptr || scDataL == nullptr || scDataR == nullptr || numSamples <= 0) return;

    float lastPhaseM = 0.0f;
    float lastPhaseS = 0.0f;
    float maxEnergy = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // F3: sanitize inputs — NaN would poison the APF states and make the
        // energy/atan2 results NaN (unspecified under /fp:fast)
        const float m = std::isfinite(mainData[i]) ? mainData[i] : 0.0f;
        const float sl = std::isfinite(scDataL[i]) ? scDataL[i] : 0.0f;
        const float srIn = std::isfinite(scDataR[i]) ? scDataR[i] : 0.0f;
        const float mainSample = m;
        const float scSample = 0.5f * (sl + srIn);

        // 1st-order APF Hilbert -90-degree phase shifter (-a feedforward)
        float mainQ = -apfCoeff * mainSample + apfStateMain;
        apfStateMain = mainSample + apfCoeff * mainQ;

        float scQ = -apfCoeff * scSample + apfStateSc;
        apfStateSc = scSample + apfCoeff * scQ;

        float energy = mainSample * mainSample + scSample * scSample;
        if (energy > maxEnergy)
        {
            maxEnergy = energy;
            lastPhaseM = std::atan2(mainQ, mainSample);
            lastPhaseS = std::atan2(scQ, scSample);
        }
    }

    // F3: sanitize persisted APF states (a poisoned state outlives the block)
    if (! std::isfinite(apfStateMain)) apfStateMain = 0.0f;
    if (! std::isfinite(apfStateSc))   apfStateSc   = 0.0f;

    if (maxEnergy > 1e-8f)
    {
        instantPhaseMain.store(lastPhaseM, std::memory_order_relaxed);
        instantPhaseSc.store(lastPhaseS, std::memory_order_relaxed);

        float diff = wrapDeg180((lastPhaseM - lastPhaseS) * (180.0f / juce::MathConstants<float>::pi));

        // Shortest-arc circular EMA smoothing (~50ms) for phaseDiffDeg
        float delta = wrapDeg180(diff - smoothedPhaseDiffDeg);

        float phaseDt = static_cast<float>(numSamples) / static_cast<float>(sr);
        float phaseAlpha = 1.0f - std::exp(-phaseDt / 0.050f);
        smoothedPhaseDiffDeg = wrapDeg180(smoothedPhaseDiffDeg + phaseAlpha * delta);

        phaseDiffDeg.store(smoothedPhaseDiffDeg, std::memory_order_relaxed);
    }

    // Vector magnitude normalized [0.0, 1.0] for dual-signal
    float mag = std::clamp(std::sqrt(maxEnergy) * 0.7071f, 0.0f, 1.0f);
    phaseVectorMag.store(mag, std::memory_order_relaxed);
}
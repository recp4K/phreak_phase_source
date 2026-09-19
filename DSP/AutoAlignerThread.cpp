#include "AutoAlignerThread.h"

AutoAlignerThread::AutoAlignerThread(juce::AudioBuffer<float>& buffer, std::atomic<int>& writeIdx, std::atomic<double>& sampleRate)
    : juce::Thread("AutoAlignerThread"), captureBuffer(buffer), writeIndex(writeIdx), sr(sampleRate)
{
    prepare();
}

AutoAlignerThread::~AutoAlignerThread()
{
    stopThread(1000);
}

void AutoAlignerThread::triggerAnalysis(int trackMode) noexcept
{
    timedOut.store(false, std::memory_order_release);
    isFinished.store(false, std::memory_order_release);
    progress.store(0.0f, std::memory_order_release);
    alignTrackMode.store(trackMode, std::memory_order_release);
    scanStartTimeMs = juce::Time::currentTimeMillis();
    hasContinuousHistory = false;
    lastFlipState = false;
    autoAlignActive.store(true, std::memory_order_release);
    if (!isThreadRunning())
        startThread();
    notify();
}

void AutoAlignerThread::prepare()
{
    timedOut.store(false, std::memory_order_relaxed);
    initialCorrelation.store(0.0f, std::memory_order_relaxed);
    bestDelaySamples.store(0.0f, std::memory_order_relaxed);
    hasContinuousHistory = false;
    lastFlipState = false;
    scanStartTimeMs = 0;

    fftBufMain.resize(fftSize, { 0.0f, 0.0f });
    fftBufSc.resize(fftSize, { 0.0f, 0.0f });
    fftScratch.resize(fftSize, { 0.0f, 0.0f });
    const int halfFft = fftSize / 2;
    hannWindow.resize(halfFft, 0.0f);
    corrOutput.resize(fftSize, 0.0f);
    snapshotBuffer.setSize(2, fftSize);
    snapshotBuffer.clear();

    const float twoPi = juce::MathConstants<float>::twoPi;
    for (int i = 0; i < halfFft; ++i)
    {
        hannWindow[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(twoPi * static_cast<float>(i) / static_cast<float>(halfFft - 1)));
    }
}

void AutoAlignerThread::run()
{
    juce::ScopedNoDenormals noDenormals; // background worker: covers all FFT work below

    while (!threadShouldExit())
    {
        if (!autoAlignActive.load(std::memory_order_relaxed))
        {
            scanStartTimeMs = 0;
            wait(50);
            continue;
        }

        if (scanStartTimeMs == 0)
            scanStartTimeMs = juce::Time::currentTimeMillis();

        isFinished.store(false);
        progress.store(0.0f);

        const int capSize = captureBuffer.getNumSamples();
        const int numSamplesToRead = fftSize;

        if (capSize < numSamplesToRead || captureBuffer.getNumChannels() < 2 || hannWindow.empty())
        {
            isFinished.store(true);
            progress.store(1.0f);
            wait(50);
            continue;
        }

        if (snapshotBuffer.getNumSamples() != numSamplesToRead || snapshotBuffer.getNumChannels() < 2)
            snapshotBuffer.setSize(2, numSamplesToRead);

        const int currentWriteIdx = writeIndex.load(std::memory_order_acquire);

        auto copySnapshot = [&](int offsetFromWrite)
        {
            int readIdx = currentWriteIdx - offsetFromWrite;
            while (readIdx < 0)
                readIdx += capSize;
            readIdx %= capSize;

            const int tillEnd = capSize - readIdx;
            for (int ch = 0; ch < 2; ++ch)
            {
                if (numSamplesToRead <= tillEnd)
                {
                    snapshotBuffer.copyFrom(ch, 0, captureBuffer, ch, readIdx, numSamplesToRead);
                }
                else
                {
                    snapshotBuffer.copyFrom(ch, 0, captureBuffer, ch, readIdx, tillEnd);
                    snapshotBuffer.copyFrom(ch, tillEnd, captureBuffer, ch, 0, numSamplesToRead - tillEnd);
                }
            }
        };

        // Start with recent window
        copySnapshot(numSamplesToRead);

        float peakMain = snapshotBuffer.getMagnitude(0, 0, numSamplesToRead);
        float peakSc = snapshotBuffer.getMagnitude(1, 0, numSamplesToRead);

        // Dynamic transient search: if recent window is quiet, look back across recent history (up to 1s)
        if (peakMain < 0.002f || peakSc < 0.002f)
        {
            int bestOffset = numSamplesToRead;
            float maxCombinedEnergy = peakMain * peakSc;
            const int maxSearchBack = std::min(capSize - numSamplesToRead, 48000);
            for (int offset = numSamplesToRead + 2048; offset <= maxSearchBack; offset += 2048)
            {
                copySnapshot(offset);
                float pM = snapshotBuffer.getMagnitude(0, 0, numSamplesToRead);
                float pS = snapshotBuffer.getMagnitude(1, 0, numSamplesToRead);
                float combined = pM * pS;
                if (combined > maxCombinedEnergy)
                {
                    maxCombinedEnergy = combined;
                    bestOffset = offset;
                    peakMain = pM;
                    peakSc = pS;
                    if (pM >= 0.005f && pS >= 0.005f)
                        break;
                }
            }
            if (bestOffset != numSamplesToRead)
                copySnapshot(bestOffset);
        }

        if (threadShouldExit()) return;

        // Dynamic Energy Detection & 2.5s Timeout Gate
        if (peakMain < 0.002f || peakSc < 0.002f)
        {
            const auto elapsed = juce::Time::currentTimeMillis() - scanStartTimeMs;
            if (elapsed > 2500)
            {
                // Timeout after 2.5s of no audio detected
                timedOut.store(true, std::memory_order_release);
                peakCorrelation.store(0.0f, std::memory_order_release);
                initialCorrelation.store(0.0f, std::memory_order_release);
                progress.store(1.0f, std::memory_order_release);
                isFinished.store(true, std::memory_order_release);
                autoAlignActive.store(false, std::memory_order_release);
                scanStartTimeMs = 0;
                wait(50);
                continue;
            }

            peakCorrelation.store(0.0f, std::memory_order_relaxed);
            progress.store(0.05f, std::memory_order_relaxed);
            wait(50);
            continue;
        }

        // Energy detected: clear timeout and proceed with analysis
        timedOut.store(false, std::memory_order_release);
        scanStartTimeMs = 0;

        const float* mainData = snapshotBuffer.getReadPointer(0);
        const float* scData = snapshotBuffer.getReadPointer(1);

        // Calculate unaligned initial Pearson correlation at lag 0
        float sumProd = 0.0f;
        float sumSqM = 0.0f;
        float sumSqS = 0.0f;
        for (int i = 0; i < numSamplesToRead; ++i)
        {
            float m = mainData[i];
            float s = scData[i];
            sumProd += m * s;
            sumSqM += m * m;
            sumSqS += s * s;
        }
        float denom0 = std::sqrt(sumSqM * sumSqS);
        float initCorr = (denom0 > 1e-9f) ? std::clamp(sumProd / denom0, -1.0f, 1.0f) : 0.0f;
        initialCorrelation.store(initCorr, std::memory_order_relaxed);
        const double srVal = sr.load(std::memory_order_relaxed);
        const float sampleRateFloat = static_cast<float>(srVal > 1000.0 ? srVal : 44100.0);

        int minLag = static_cast<int>(sampleRateFloat / 250.0f);
        int maxLag = static_cast<int>(sampleRateFloat / 30.0f);
        minLag = juce::jlimit(1, numSamplesToRead / 4, minLag);
        maxLag = juce::jlimit(minLag + 1, numSamplesToRead / 2, maxLag);

        int searchLength = juce::jmin(numSamplesToRead - maxLag, numSamplesToRead / 2);
        float bestAutoCorr = -std::numeric_limits<float>::infinity();
        int bestLag = minLag;

        for (int lag = minLag; lag <= maxLag; lag += 2)
        {
            if (threadShouldExit()) return;

            float dot = 0.0f;
            for (int i = 0; i < searchLength; ++i)
                dot += scData[i] * scData[i + lag];

            if (dot > bestAutoCorr)
            {
                bestAutoCorr = dot;
                bestLag = lag;
            }
        }
        
        float fundFreq = juce::jlimit(30.0f, 250.0f, sampleRateFloat / static_cast<float>(bestLag));
        detectedFreq.store(fundFreq);
        progress.store(0.1f);

        if (threadShouldExit()) return;

        std::fill(fftBufMain.begin(), fftBufMain.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(fftBufSc.begin(), fftBufSc.end(), std::complex<float>(0.0f, 0.0f));
        
        // Zero-pad the second half to avoid circular convolution wrap-around
        const int halfFft = fftSize / 2;
        for (int i = 0; i < halfFft; ++i)
        {
            fftBufMain[static_cast<size_t>(i)] = { mainData[i] * hannWindow[static_cast<size_t>(i)], 0.0f };
            fftBufSc[static_cast<size_t>(i)]   = { scData[i]   * hannWindow[static_cast<size_t>(i)], 0.0f };
        }
        for (int i = halfFft; i < fftSize; ++i)
        {
            fftBufMain[static_cast<size_t>(i)] = { 0.0f, 0.0f };
            fftBufSc[static_cast<size_t>(i)]   = { 0.0f, 0.0f };
        }
        progress.store(0.3f);

        if (threadShouldExit()) return;

        fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftBufMain.data()), reinterpret_cast<juce::dsp::Complex<float>*>(fftScratch.data()), false);
        std::copy(fftScratch.begin(), fftScratch.end(), fftBufMain.begin());
        fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftBufSc.data()), reinterpret_cast<juce::dsp::Complex<float>*>(fftScratch.data()), false);
        std::copy(fftScratch.begin(), fftScratch.end(), fftBufSc.begin());
        progress.store(0.6f);

        if (threadShouldExit()) return;

        int fundBin = juce::roundToInt(fundFreq * static_cast<float>(fftSize) / sampleRateFloat);
        fundBin = std::clamp(fundBin, 1, fftSize / 4);
        std::complex<float> rawCrossFund = fftBufMain[static_cast<size_t>(fundBin)] * std::conj(fftBufSc[static_cast<size_t>(fundBin)]);

        // Apply bandpass window to whiten only relevant sub-bass frequencies (e.g. 30 Hz - 600 Hz)
        int bin30Hz = static_cast<int>(30.0f * static_cast<float>(fftSize) / sampleRateFloat);
        int bin600Hz = static_cast<int>(600.0f * static_cast<float>(fftSize) / sampleRateFloat);
        
        for (int i = 0; i < fftSize; ++i)
        {
            auto G = fftBufMain[static_cast<size_t>(i)] * std::conj(fftBufSc[static_cast<size_t>(i)]);
            float mag = std::abs(G);
            
            // Window the whitening
            if (i < bin30Hz || (i > bin600Hz && i < (fftSize - bin600Hz)) || i > (fftSize - bin30Hz))
                mag = 0.0f;

            fftBufMain[static_cast<size_t>(i)] = (mag > 1e-9f) ? (G / mag) : std::complex<float>(0.0f, 0.0f);
        }
        progress.store(0.8f);

        if (threadShouldExit()) return;

        fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftBufMain.data()), reinterpret_cast<juce::dsp::Complex<float>*>(fftScratch.data()), true);
        std::copy(fftScratch.begin(), fftScratch.end(), fftBufMain.begin());

        float peakVal = 0.0f;
        int maxIndex = 0;
        bool isNegativePeak = false;
        const int activeBinsCount = std::max(1, 2 * (bin600Hz - bin30Hz + 1));
        const float norm = 1.0f / static_cast<float>(activeBinsCount);

        for (int i = 0; i < fftSize; ++i)
        {
            corrOutput[static_cast<size_t>(i)] = fftBufMain[static_cast<size_t>(i)].real() * norm;
            float absVal = std::abs(corrOutput[static_cast<size_t>(i)]);
            if (absVal > peakVal)
            {
                peakVal = absVal;
                maxIndex = i;
                isNegativePeak = (corrOutput[static_cast<size_t>(i)] < 0.0f);
            }
        }
        peakCorrelation.store(std::clamp(peakVal, 0.0f, 1.0f), std::memory_order_relaxed);

        float delaySamples = static_cast<float>(maxIndex);
        {
            const int prevIdx = (maxIndex - 1 + fftSize) % fftSize;
            const int nextIdx = (maxIndex + 1) % fftSize;
            float s = isNegativePeak ? -1.0f : 1.0f;
            float alphaVal = s * corrOutput[static_cast<size_t>(prevIdx)];
            float betaVal  = s * corrOutput[static_cast<size_t>(maxIndex)];
            float gammaVal = s * corrOutput[static_cast<size_t>(nextIdx)];
            float denom = 2.0f * (alphaVal - 2.0f * betaVal + gammaVal);
            if (std::abs(denom) > 1e-6f)
            {
                float subOffset = (alphaVal - gammaVal) / denom;
                delaySamples += std::clamp(subOffset, -0.5f, 0.5f);
            }
        }

        if (delaySamples > static_cast<float>(fftSize) / 2.0f)
            delaySamples -= static_cast<float>(fftSize);

        float optDelMs = (delaySamples / sampleRateFloat) * 1000.0f;
        optDelMs = juce::jlimit(-20.0f, 20.0f, optDelMs);
        
        float rawPhaseAngle = std::atan2(rawCrossFund.imag(), rawCrossFund.real());
        float linearPhase = juce::MathConstants<float>::twoPi * fundFreq * (delaySamples / sampleRateFloat);
        float residualPhase = rawPhaseAngle + linearPhase;

        float optRotateDeg = -residualPhase * (180.0f / juce::MathConstants<float>::pi);
        while (optRotateDeg > 180.0f) optRotateDeg -= 360.0f;
        while (optRotateDeg < -180.0f) optRotateDeg += 360.0f;

        // Polarity flip with strict 20% hysteresis:
        // 90 deg +/- 20% (72 deg to 108 deg) angular deadband strictly prevents chattering near orthogonal boundaries
        bool optFlip = lastFlipState;
        const float absAngle = std::abs(optRotateDeg);
        if (!lastFlipState)
        {
            // Switch from Normal to Inverted only when crossing above 108 deg (90° + 20%)
            if (absAngle > 108.0f)
                optFlip = true;
        }
        else
        {
            // Switch from Inverted back to Normal only when dropping below 72 deg (90° - 20%)
            if (absAngle < 72.0f)
                optFlip = false;
        }
        lastFlipState = optFlip;

        if (optFlip)
        {
            if (optRotateDeg > 0.0f) optRotateDeg -= 180.0f;
            else optRotateDeg += 180.0f;
        }

        // Continuous Mode EMA smoothing vs Transient Mode
        if (alignTrackMode.load(std::memory_order_relaxed) == 0)
        {
            if (!hasContinuousHistory)
            {
                continuousSmoothedDelMs = optDelMs;
                continuousSmoothedRotateDeg = optRotateDeg;
                hasContinuousHistory = true;
            }
            else
            {
                continuousSmoothedDelMs = 0.25f * optDelMs + 0.75f * continuousSmoothedDelMs;
                float rotDiff = std::fmod(optRotateDeg - continuousSmoothedRotateDeg + 540.0f, 360.0f) - 180.0f;
                continuousSmoothedRotateDeg += 0.25f * rotDiff;
                while (continuousSmoothedRotateDeg > 180.0f) continuousSmoothedRotateDeg -= 360.0f;
                while (continuousSmoothedRotateDeg < -180.0f) continuousSmoothedRotateDeg += 360.0f;
            }
            optDelMs = continuousSmoothedDelMs;
            optRotateDeg = continuousSmoothedRotateDeg;
        }
        else
        {
            hasContinuousHistory = false;
        }

        bestSubSampleDelay.store(optDelMs);
        
        // Negative delay advances the late signal
        float compDelMs = -optDelMs;
        bestDelay.store(compDelMs);
        bestDelaySamples.store(-delaySamples);
        bestRotate.store(optRotateDeg);
        bestFlip.store(optFlip);
        bestGain.store(0.0f);

        float suggestedEqCut = 0.0f;
        if (initCorr < 0.45f && fundFreq >= 30.0f && fundFreq <= 220.0f)
            suggestedEqCut = juce::jlimit(-6.0f, -1.5f, -(0.5f - initCorr) * 10.0f);
        suggestedEqCutDb.store(suggestedEqCut, std::memory_order_relaxed);
        suggestedEqFreqHz.store(fundFreq, std::memory_order_relaxed);
        
        progress.store(1.0f, std::memory_order_release);
        isFinished.store(true, std::memory_order_release);

        if (alignTrackMode.load(std::memory_order_relaxed) == 1)
        {
            // Single-shot transient mode: stop scanning immediately so results are stable
            autoAlignActive.store(false, std::memory_order_release);
        }
        
        // Wait before next alignment frame to avoid maxing out CPU when auto-aligning continuously
        wait(50);
    }
}

SegmentAlignmentResult AutoAlignerThread::analyzeSlice(const float* mainData, const float* scData, int numSamples, double sampleRate)
{
    SegmentAlignmentResult res;
    if (mainData == nullptr || scData == nullptr || numSamples < 64)
        return res;

    const float sampleRateFloat = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 44100.0);
    const int N = std::min(numSamples, fftSize / 2);

    // 1. Initial Pearson correlation at lag 0
    float sumProd = 0.0f;
    float sumSqM = 0.0f;
    float sumSqS = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float m = mainData[i];
        float s = scData[i];
        sumProd += m * s;
        sumSqM += m * m;
        sumSqS += s * s;
    }
    float denom0 = std::sqrt(sumSqM * sumSqS);
    if (denom0 < 1e-7f)
    {
        res.hasAudio = false;
        return res;
    }
    res.hasAudio = true;
    res.correlationBefore = std::clamp(sumProd / denom0, -1.0f, 1.0f);

    // 2. Fundamental pitch via autocorrelation on sidechain
    int minLag = static_cast<int>(sampleRateFloat / 250.0f);
    int maxLag = static_cast<int>(sampleRateFloat / 30.0f);
    minLag = juce::jlimit(1, N / 4, minLag);
    maxLag = juce::jlimit(minLag + 1, N / 2, maxLag);

    int searchLength = juce::jmin(N - maxLag, N / 2);
    float bestAutoCorr = -std::numeric_limits<float>::infinity();
    int bestLag = minLag;

    for (int lag = minLag; lag <= maxLag; lag += 2)
    {
        float dot = 0.0f;
        for (int i = 0; i < searchLength; ++i)
            dot += scData[i] * scData[i + lag];

        if (dot > bestAutoCorr)
        {
            bestAutoCorr = dot;
            bestLag = lag;
        }
    }
    float fundFreq = juce::jlimit(30.0f, 250.0f, sampleRateFloat / static_cast<float>(bestLag));
    res.detectedFreq = fundFreq;

    // 3. FFT & Hann windowing
    std::fill(fftBufMain.begin(), fftBufMain.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(fftBufSc.begin(), fftBufSc.end(), std::complex<float>(0.0f, 0.0f));

    const float twoPi = juce::MathConstants<float>::twoPi;
    for (int i = 0; i < N; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(twoPi * static_cast<float>(i) / static_cast<float>(N - 1)));
        fftBufMain[static_cast<size_t>(i)] = { mainData[i] * w, 0.0f };
        fftBufSc[static_cast<size_t>(i)]   = { scData[i]   * w, 0.0f };
    }

    fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftBufMain.data()), reinterpret_cast<juce::dsp::Complex<float>*>(fftScratch.data()), false);
    std::copy(fftScratch.begin(), fftScratch.end(), fftBufMain.begin());
    fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftBufSc.data()), reinterpret_cast<juce::dsp::Complex<float>*>(fftScratch.data()), false);
    std::copy(fftScratch.begin(), fftScratch.end(), fftBufSc.begin());

    int fundBin = juce::roundToInt(fundFreq * static_cast<float>(fftSize) / sampleRateFloat);
    fundBin = std::clamp(fundBin, 1, fftSize / 4);
    std::complex<float> rawCrossFund = fftBufMain[static_cast<size_t>(fundBin)] * std::conj(fftBufSc[static_cast<size_t>(fundBin)]);

    // GCC-PHAT whitening within sub-bass range 30 - 600 Hz
    int bin30Hz = static_cast<int>(30.0f * static_cast<float>(fftSize) / sampleRateFloat);
    int bin600Hz = static_cast<int>(600.0f * static_cast<float>(fftSize) / sampleRateFloat);

    for (int i = 0; i < fftSize; ++i)
    {
        auto G = fftBufMain[static_cast<size_t>(i)] * std::conj(fftBufSc[static_cast<size_t>(i)]);
        float mag = std::abs(G);
        if (i < bin30Hz || (i > bin600Hz && i < (fftSize - bin600Hz)) || i > (fftSize - bin30Hz))
            mag = 0.0f;
        fftBufMain[static_cast<size_t>(i)] = (mag > 1e-9f) ? (G / mag) : std::complex<float>(0.0f, 0.0f);
    }

    fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftBufMain.data()), reinterpret_cast<juce::dsp::Complex<float>*>(fftScratch.data()), true);
    std::copy(fftScratch.begin(), fftScratch.end(), fftBufMain.begin());

    float peakVal = 0.0f;
    int maxIndex = 0;
    bool isNegativePeak = false;
    const int activeBinsCount = std::max(1, 2 * (bin600Hz - bin30Hz + 1));
    const float norm = 1.0f / static_cast<float>(activeBinsCount);

    for (int i = 0; i < fftSize; ++i)
    {
        corrOutput[static_cast<size_t>(i)] = fftBufMain[static_cast<size_t>(i)].real() * norm;
        float absVal = std::abs(corrOutput[static_cast<size_t>(i)]);
        if (absVal > peakVal)
        {
            peakVal = absVal;
            maxIndex = i;
            isNegativePeak = (corrOutput[static_cast<size_t>(i)] < 0.0f);
        }
    }

    float delaySamples = static_cast<float>(maxIndex);
    {
        const int prevIdx = (maxIndex - 1 + fftSize) % fftSize;
        const int nextIdx = (maxIndex + 1) % fftSize;
        float s = isNegativePeak ? -1.0f : 1.0f;
        float alphaVal = s * corrOutput[static_cast<size_t>(prevIdx)];
        float betaVal  = s * corrOutput[static_cast<size_t>(maxIndex)];
        float gammaVal = s * corrOutput[static_cast<size_t>(nextIdx)];
        float denom = 2.0f * (alphaVal - 2.0f * betaVal + gammaVal);
        if (std::abs(denom) > 1e-6f)
        {
            float subOffset = (alphaVal - gammaVal) / denom;
            delaySamples += std::clamp(subOffset, -0.5f, 0.5f);
        }
    }

    if (delaySamples > static_cast<float>(fftSize) / 2.0f)
        delaySamples -= static_cast<float>(fftSize);

    float optDelMs = (delaySamples / sampleRateFloat) * 1000.0f;
    optDelMs = juce::jlimit(-20.0f, 20.0f, optDelMs);

    float rawPhaseAngle = std::atan2(rawCrossFund.imag(), rawCrossFund.real());
    float linearPhase = juce::MathConstants<float>::twoPi * fundFreq * (delaySamples / sampleRateFloat);
    float residualPhase = rawPhaseAngle + linearPhase;

    float optRotateDeg = -residualPhase * (180.0f / juce::MathConstants<float>::pi);
    while (optRotateDeg > 180.0f) optRotateDeg -= 360.0f;
    while (optRotateDeg < -180.0f) optRotateDeg += 360.0f;

    bool optFlip = false;
    if (std::abs(optRotateDeg) > 90.0f)
    {
        optFlip = true;
        if (optRotateDeg > 0.0f) optRotateDeg -= 180.0f;
        else optRotateDeg += 180.0f;
    }

    res.delayMs = -optDelMs;
    res.delaySamples = -delaySamples;
    res.rotateDeg = optRotateDeg;
    res.polarityFlip = optFlip;

    // Post-alignment estimated correlation
    float estCorr = std::clamp(peakVal, 0.05f, 0.98f);
    res.correlationAfter = std::max(res.correlationBefore, estCorr);

    // Suggested EQ cut
    if (res.correlationBefore < 0.45f && fundFreq >= 30.0f && fundFreq <= 220.0f)
        res.suggestedEqCutDb = juce::jlimit(-6.0f, -1.5f, -(0.5f - res.correlationBefore) * 10.0f);
    else
        res.suggestedEqCutDb = 0.0f;

    return res;
}

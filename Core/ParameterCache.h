#pragma once
#include <JuceHeader.h>
#include <atomic>

struct ParameterCache
{
    std::atomic<float>* subRot = nullptr;
    std::atomic<float>* subDel = nullptr;
    std::atomic<float>* subFlip = nullptr;
    std::atomic<float>* subGain = nullptr;
    std::atomic<float>* subDynAmount = nullptr;
    std::atomic<float>* highRot = nullptr;
    std::atomic<float>* highDel = nullptr;
    std::atomic<float>* highFlip = nullptr;
    std::atomic<float>* highGain = nullptr;
    std::atomic<float>* highDynAmount = nullptr;
    std::atomic<float>* dcFilter = nullptr;
    std::atomic<float>* freeze = nullptr;
    std::atomic<float>* response = nullptr;
    std::atomic<float>* trackBCh = nullptr;
    std::atomic<float>* monitor = nullptr;
    std::atomic<float>* dynEqFreq = nullptr;
    std::atomic<float>* dynEqDepth = nullptr;
    std::atomic<float>* dynPhAmount = nullptr;
    std::atomic<float>* deltaListen = nullptr;
    std::atomic<float>* envAttack = nullptr;
    std::atomic<float>* envRelease = nullptr;
    std::atomic<float>* lookaheadMs = nullptr;
    std::atomic<float>* crossoverFreq = nullptr;
    std::atomic<float>* crossoverEnable = nullptr;
    std::atomic<float>* pitchTrackEnable = nullptr;
    std::atomic<float>* latencyMode = nullptr;
    std::atomic<float>* alignTrackMode = nullptr;
    std::atomic<float>* bassGlue = nullptr;
    std::atomic<float>* zoomX = nullptr;
    std::atomic<float>* zoomY = nullptr;
    std::atomic<float>* panX = nullptr;

    void update(juce::AudioProcessorValueTreeState& apvts) noexcept
    {
        subRot = apvts.getRawParameterValue("SUB_ROTATE");
        subDel = apvts.getRawParameterValue("SUB_DELAY");
        subFlip = apvts.getRawParameterValue("SUB_FLIP");
        subGain = apvts.getRawParameterValue("SUB_GAIN");
        subDynAmount = apvts.getRawParameterValue("SUB_DYN_AMOUNT");
        highRot = apvts.getRawParameterValue("HIGH_ROTATE");
        highDel = apvts.getRawParameterValue("HIGH_DELAY");
        highFlip = apvts.getRawParameterValue("HIGH_FLIP");
        highGain = apvts.getRawParameterValue("HIGH_GAIN");
        highDynAmount = apvts.getRawParameterValue("HIGH_DYN_AMOUNT");
        dcFilter = apvts.getRawParameterValue("DC_FILTER");
        freeze = apvts.getRawParameterValue("FREEZE");
        response = apvts.getRawParameterValue("RESPONSE");
        trackBCh = apvts.getRawParameterValue("TRACK_B_CH");
        monitor = apvts.getRawParameterValue("MONITOR");
        dynEqFreq = apvts.getRawParameterValue("DYN_EQ_FREQ");
        dynEqDepth = apvts.getRawParameterValue("DYN_EQ_DEPTH");
        dynPhAmount = apvts.getRawParameterValue("DYN_PH_AMOUNT");
        deltaListen = apvts.getRawParameterValue("DELTA_LISTEN");
        envAttack = apvts.getRawParameterValue("ENV_ATTACK");
        envRelease = apvts.getRawParameterValue("ENV_RELEASE");
        lookaheadMs = apvts.getRawParameterValue("LOOKAHEAD_MS");
        crossoverFreq = apvts.getRawParameterValue("CROSSOVER_FREQ");
        crossoverEnable = apvts.getRawParameterValue("CROSSOVER_ENABLE");
        pitchTrackEnable = apvts.getRawParameterValue("PITCH_TRACK");
        latencyMode = apvts.getRawParameterValue("LATENCY_MODE");
        alignTrackMode = apvts.getRawParameterValue("ALIGN_TRACK_MODE");
        bassGlue = apvts.getRawParameterValue("BASS_GLUE");
        zoomX = apvts.getRawParameterValue("ZOOMX");
        zoomY = apvts.getRawParameterValue("ZOOMY");
        panX = apvts.getRawParameterValue("PANX");
    }
};
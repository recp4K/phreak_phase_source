#pragma once
#include <JuceHeader.h>
#include <vector>

struct Preset
{
    juce::String name;
    float crossoverFreq = 100.0f;
    bool crossoverEnable = true;
    bool pitchTrack = true;
    int latencyMode = 1; // 0 = Live, 1 = Precision
    float dynEqFreq = 60.0f;
    float dynEqDepth = -6.0f;
    float dynPhAmount = 50.0f;
    float envAttack = 2.0f;
    float envRelease = 100.0f;
    float subDynAmount = 25.0f;
    float highDynAmount = 0.0f;
    float bassGlue = 0.0f;
};

class PresetManager
{
public:
    PresetManager(juce::AudioProcessorValueTreeState& state) : apvts(state)
    {
        initPresets();
    }

    const std::vector<Preset>& getPresets() const noexcept { return presets; }

    void applyPreset(int index)
    {
        if (index < 0 || index >= static_cast<int>(presets.size())) return;
        const auto& p = presets[static_cast<size_t>(index)];

        setParameter("CROSSOVER_FREQ", p.crossoverFreq);
        setParameter("CROSSOVER_ENABLE", p.crossoverEnable ? 1.0f : 0.0f);
        setParameter("PITCH_TRACK", p.pitchTrack ? 1.0f : 0.0f);
        setParameter("LATENCY_MODE", static_cast<float>(p.latencyMode));
        setParameter("DYN_EQ_FREQ", p.dynEqFreq);
        setParameter("DYN_EQ_DEPTH", p.dynEqDepth);
        setParameter("DYN_PH_AMOUNT", p.dynPhAmount);
        setParameter("ENV_ATTACK", p.envAttack);
        setParameter("ENV_RELEASE", p.envRelease);
        setParameter("SUB_DYN_AMOUNT", p.subDynAmount);
        setParameter("HIGH_DYN_AMOUNT", p.highDynAmount);
        setParameter("BASS_GLUE", p.bassGlue);
    }

    // A/B Comparison state storage
    void saveStateA()
    {
        stateA = apvts.copyState();
        hasStateA = true;
    }

    void saveStateB()
    {
        stateB = apvts.copyState();
        hasStateB = true;
    }

    void toggleAB(bool selectB)
    {
        if (selectB)
        {
            saveStateA();
            if (!hasStateB)
                saveStateB();
            else
                apvts.replaceState(stateB);
        }
        else
        {
            saveStateB();
            if (hasStateA)
                apvts.replaceState(stateA);
        }
    }

private:
    void setParameter(const juce::String& paramId, float value)
    {
        if (auto* param = apvts.getParameter(paramId))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    }

    void initPresets()
    {
        presets.push_back({ "1. Trap 808 & Kick", 90.0f, true, true, 1, 45.0f, -8.0f, 65.0f, 1.5f, 90.0f, 30.0f, 0.0f, 20.0f });
        presets.push_back({ "2. Modern House Punch", 110.0f, true, true, 1, 55.0f, -5.0f, 40.0f, 2.0f, 80.0f, 20.0f, 10.0f, 15.0f });
        presets.push_back({ "3. DnB Sub & Reese", 125.0f, true, true, 0, 65.0f, -6.0f, 50.0f, 2.5f, 120.0f, 25.0f, 15.0f, 30.0f });
        presets.push_back({ "4. Live Drum & DI Bass", 100.0f, false, false, 0, 70.0f, 0.0f, 0.0f, 3.0f, 150.0f, 0.0f, 0.0f, 0.0f });
        presets.push_back({ "5. Sub Phase Glide 808", 85.0f, true, true, 1, 40.0f, -7.0f, 80.0f, 1.0f, 100.0f, 35.0f, 0.0f, 10.0f });
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Preset> presets;

    juce::ValueTree stateA;
    juce::ValueTree stateB;
    bool hasStateA = false;
    bool hasStateB = false;
};


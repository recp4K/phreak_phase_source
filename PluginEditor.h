#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Core/PresetManager.h"
#include <juce_gui_extra/juce_gui_extra.h>

class FreakPhaseAudioProcessorEditor;

// WebBrowserComponent subclass exposing the page-load virtuals as callbacks,
// so the editor can detect when the asynchronously-created WebView2 finally
// accepts a navigation (early goToURL() calls are dropped by the platform layer).
class PhreakWebView : public juce::WebBrowserComponent
{
public:
    std::function<bool(const juce::String&)> onAboutToLoad;
    std::function<void(const juce::String&)> onFinishedLoading;
    std::function<bool(const juce::String&)> onNetworkError;

    explicit PhreakWebView(const Options& options) : WebBrowserComponent(options) {}

    bool pageAboutToLoad(const juce::String& newURL) override
    {
        return onAboutToLoad ? onAboutToLoad(newURL) : true;
    }

    void pageFinishedLoading(const juce::String& url) override
    {
        if (onFinishedLoading)
            onFinishedLoading(url);
    }

    bool pageLoadHadNetworkError(const juce::String& errorInfo) override
    {
        return onNetworkError ? onNetworkError(errorInfo) : true;
    }
};

class FreakPhaseAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    FreakPhaseAudioProcessorEditor(FreakPhaseAudioProcessor&);
    ~FreakPhaseAudioProcessorEditor() noexcept override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    void onDawStopTimelineCapture();

private:
    void handleFrontendSetParameter(const juce::var& data);
    void handleFrontendTriggerAlign();
    void handleFrontendSelectPreset(const juce::var& data);
    void handleFrontendBakeTimeline(const juce::var& data);
    void handleFrontendClearTimeline();
    void handleFrontendArmTimelineLearn(const juce::var& data);
    void handleFrontendDisarmTimelineLearn();
    void setupWebView();
    void pushWaveformEvent();
    void applySmartAlignParameters(float delayMs, float rotateDeg, bool flip, float gainDb, bool withGestures = true);

    FreakPhaseAudioProcessor& audioProcessor;
    PresetManager presetManager;

    juce::WebBrowserComponent::Options webViewOptions;
    std::unique_ptr<PhreakWebView> webView;
    juce::ComponentBoundsConstrainer resizeConstrainer;
    juce::File userDataFolder;

    // WebView2 creates the browser asynchronously: goToURL() issued before the
    // native controller exists is silently dropped, so navigation is retried
    // from the timer until the page reports back through the callbacks above.
    bool webViewNavigated = false;
    bool webViewBoundsApplied = false;
    int webViewNavigateAttempts = 0;

    // GUI-side snapshot scratch for the 30 Hz waveform event (message thread only)
    juce::AudioBuffer<float> waveformSnapshot { 2, 2048 };
    uint64_t audioFrameCounter { 0 };
    bool alignFinishedEdge { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreakPhaseAudioProcessorEditor)
};
#include "PluginEditor.h"
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace
{
#if JUCE_WINDOWS
    bool isOsProcessAlive(juce::int64 pid)
    {
        if (pid <= 0) return false;
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (hProcess == NULL)
            return false;
        DWORD exitCode = 0;
        bool alive = (GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE);
        CloseHandle(hProcess);
        return alive;
    }
#endif

    void cleanStaleWebViewUserDataFolders(const juce::File& currentFolder)
    {
        auto tempDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory);
        juce::Array<juce::File> staleDirs;
        tempDir.findChildFiles(staleDirs, juce::File::findDirectories, false, "FreakPhase_WebView2_*");
        const auto now = juce::Time::getCurrentTime();

        for (auto& dir : staleDirs)
        {
            if (dir == currentFolder)
                continue;

            auto name = dir.getFileName();
            if (name.startsWith("FreakPhase_WebView2_PID_"))
            {
                auto pidStr = name.fromFirstOccurrenceOf("FreakPhase_WebView2_PID_", false, false)
                                  .upToFirstOccurrenceOf("_", false, false);
                auto pid = pidStr.getLargeIntValue();
#if JUCE_WINDOWS
                if (pid > 0 && isOsProcessAlive(pid))
                {
                    // Belongs to an active host/DAW process — NEVER delete active data from another instance!
                    continue;
                }
#endif
                dir.deleteRecursively();
            }
            else
            {
                // Legacy or unversioned directories: delete only if older than 2 hours to avoid corrupting concurrent instances
                if (dir.getLastModificationTime() < now - juce::RelativeTime::hours(2))
                {
                    dir.deleteRecursively();
                }
            }
        }
    }
}

FreakPhaseAudioProcessorEditor::FreakPhaseAudioProcessorEditor(FreakPhaseAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      presetManager(p.apvts)
{
    // Setup Resizable Bounds (Target: 1120x650, Min: 900x520, Max: 1680x1050)
    resizeConstrainer.setSizeLimits(900, 520, 1680, 1050);
    resizeConstrainer.setFixedAspectRatio(1120.0 / 650.0);
    setConstrainer(&resizeConstrainer);
    setResizable(true, true);
    setSize(1120, 650);

    // Dedicated WebView2 User Data Folder (prevents host write permission issues
    // in Program Files). Suffixing with PID + instance ID keeps multiple plugin instances
    // isolated and prevents accidental cross-instance directory deletion.
#if JUCE_WINDOWS
    const auto currentPid = static_cast<juce::int64>(GetCurrentProcessId());
#else
    const auto currentPid = static_cast<juce::int64>(0);
#endif
    const auto instanceId = (juce::uint64) (juce::pointer_sized_int) this;

    userDataFolder = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory)
                             .getChildFile(juce::String("FreakPhase_WebView2_PID_") + juce::String(currentPid)
                                           + "_" + juce::String((juce::int64) instanceId));
    userDataFolder.createDirectory();

    // Clean up stale WebView2 directories left behind by terminated instances/crashes
    cleanStaleWebViewUserDataFolders(userDataFolder);

    // VST3 & Standalone: resolve modules relative to currentApplicationFile (VST3 DLL/bundle or Standalone EXE)
    auto moduleFile = juce::File::getSpecialLocation(juce::File::SpecialLocationType::currentApplicationFile);
    auto moduleDir = moduleFile.getParentDirectory();

    auto loaderDll = moduleDir.getChildFile("WebView2Loader.dll");
    if (!loaderDll.existsAsFile())
    {
        auto parentDll = moduleDir.getParentDirectory().getChildFile("WebView2Loader.dll");
        if (parentDll.existsAsFile())
            loaderDll = parentDll;
        else
        {
            // FIX: Search in standard WebView2 installation locations instead of hardcoded paths
            auto programFilesDll = juce::File::getSpecialLocation(juce::File::SpecialLocationType::globalApplicationsDirectory)
                .getChildFile("Microsoft").getChildFile("WebView2").getChildFile("FixedVersion")
                .getChildFile("Runtime").getChildFile("x64").getChildFile("WebView2Loader.dll");
            if (programFilesDll.existsAsFile())
                loaderDll = programFilesDll;
            else
            {
                // Try common development package locations
                auto vsPackagesDll = moduleDir.getParentDirectory().getParentDirectory()
                    .getChildFile("packages").getChildFile("Microsoft.Web.WebView2")
                    .getChildFile("build").getChildFile("native").getChildFile("x64")
                    .getChildFile("WebView2Loader.dll");
                if (vsPackagesDll.existsAsFile())
                    loaderDll = vsPackagesDll;
            }
        }
    }

    // Resource Provider: intercepts requests to https://juce.backend/ and serves local files safely
    auto resourceProvider = [](const juce::String& path) -> std::optional<juce::WebBrowserComponent::Resource>
    {
        auto appDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::currentApplicationFile).getParentDirectory();
        juce::File baseDir = appDir.getChildFile("WebUI");
        if (!baseDir.isDirectory())
            baseDir = appDir.getParentDirectory().getChildFile("Resources").getChildFile("WebUI");
        if (!baseDir.isDirectory())
            baseDir = appDir.getChildFile("Resources").getChildFile("WebUI");
        if (!baseDir.isDirectory())
            baseDir = appDir.getChildFile("Source").getChildFile("UI").getChildFile("WebUI");
        // FIX: Removed hardcoded paths - use relative paths only

        juce::String cleanPath = path;
        if (cleanPath == "/" || cleanPath.isEmpty())
            cleanPath = "index.html";
        else if (cleanPath.startsWithChar('/'))
            cleanPath = cleanPath.substring(1);

        auto targetFile = baseDir.getChildFile(cleanPath);
        if (targetFile.existsAsFile())
        {
            juce::MemoryBlock mb;
            targetFile.loadFileAsData(mb);

            std::vector<std::byte> bytes(mb.getSize());
            std::memcpy(bytes.data(), mb.getData(), mb.getSize());

            juce::String mime = "application/octet-stream";
            if (targetFile.hasFileExtension(".html") || targetFile.hasFileExtension(".htm"))
                mime = "text/html; charset=utf-8";
            else if (targetFile.hasFileExtension(".css"))
                mime = "text/css; charset=utf-8";
            else if (targetFile.hasFileExtension(".js"))
                mime = "application/javascript; charset=utf-8";
            else if (targetFile.hasFileExtension(".png"))
                mime = "image/png";
            else if (targetFile.hasFileExtension(".svg"))
                mime = "image/svg+xml";
            else if (targetFile.hasFileExtension(".woff2"))
                mime = "font/woff2";
            else if (targetFile.hasFileExtension(".woff"))
                mime = "font/woff";
            else if (targetFile.hasFileExtension(".json"))
                mime = "application/json; charset=utf-8";

            return juce::WebBrowserComponent::Resource { std::move(bytes), mime };
        }

        return std::nullopt;
    };

    auto winOptions = juce::WebBrowserComponent::Options::WinWebView2{}
        .withUserDataFolder(userDataFolder)
        .withStatusBarDisabled()
        .withBuiltInErrorPageDisabled();

    if (loaderDll.existsAsFile())
        winOptions = winOptions.withDLLLocation(loaderDll);

    // Configure WebView Options. The user script captures uncaught JS errors
    // into window.__phreakErr (harmless, aids field diagnosis).
    webViewOptions = juce::WebBrowserComponent::Options{}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(winOptions)
        .withNativeIntegrationEnabled(true)
        .withResourceProvider(resourceProvider)
        .withUserScript(
            "window.__phreakErr='';"
            "window.addEventListener('error',function(e){window.__phreakErr='err: '+e.message+' @ '+(e.filename||'?')+':'+e.lineno;});"
            "window.addEventListener('unhandledrejection',function(e){window.__phreakErr='rej: '+(e.reason&&(e.reason.stack||e.reason.message)||e.reason);});")
        .withEventListener("setParameter", [this](juce::var data) {
            handleFrontendSetParameter(data);
        })
        .withEventListener("beginGesture", [this](juce::var data) {
            if (auto* obj = data.getDynamicObject())
            {
                auto paramId = obj->getProperty("paramId").toString();
                if (auto* param = audioProcessor.apvts.getParameter(paramId))
                    param->beginChangeGesture();
            }
        })
        .withEventListener("endGesture", [this](juce::var data) {
            if (auto* obj = data.getDynamicObject())
            {
                auto paramId = obj->getProperty("paramId").toString();
                if (auto* param = audioProcessor.apvts.getParameter(paramId))
                    param->endChangeGesture();
            }
        })
        .withEventListener("triggerSmartAlign", [this](juce::var) {
            handleFrontendTriggerAlign();
        })
        .withEventListener("selectPreset", [this](juce::var data) {
            handleFrontendSelectPreset(data);
        })
        .withEventListener("bakeTimeline", [this](juce::var data) {
            handleFrontendBakeTimeline(data);
        })
        .withEventListener("clearTimeline", [this](juce::var) {
            handleFrontendClearTimeline();
        })
        .withEventListener("armTimelineLearn", [this](juce::var data) {
            handleFrontendArmTimelineLearn(data);
        })
        .withEventListener("disarmTimelineLearn", [this](juce::var) {
            handleFrontendDisarmTimelineLearn();
        })
        // FIX: MIDI Learn event listeners
        .withEventListener("startMidiLearn", [this](juce::var data) {
            handleFrontendStartMidiLearn(data);
        })
        .withEventListener("cancelMidiLearn", [this](juce::var) {
            handleFrontendCancelMidiLearn();
        })
        .withEventListener("clearMidiMappings", [this](juce::var) {
            handleFrontendClearMidiMappings();
        })
        .withEventListener("getMidiMappings", [this](juce::var data) {
            juce::var result;
            handleFrontendGetMidiMappings(result);
            if (webView) webView->emitEventIfBrowserIsVisible("midiMappings", result);
        })
        .withEventListener("getSuggestedMidiMappings", [this](juce::var data) {
            juce::var result;
            handleFrontendGetSuggestedMidiMappings(result);
            if (webView) webView->emitEventIfBrowserIsVisible("suggestedMidiMappings", result);
        });

    setupWebView();

    if (auto* worker = audioProcessor.getTimelineAnalysisThread())
    {
        worker->setCompletionCallback([safeThis = juce::Component::SafePointer<FreakPhaseAudioProcessorEditor>(this)](const juce::Array<juce::var>& segs, const FreakPhase::Timeline::TimelineWaveformOverviewData& overview)
        {
            if (safeThis == nullptr || safeThis->webView == nullptr)
                return;

            safeThis->webView->emitEventIfBrowserIsVisible("timelineSegmentsCalculated", juce::var(segs));

            auto* obj = new juce::DynamicObject();
            obj->setProperty("startSample", static_cast<double>(overview.startSample));
            obj->setProperty("endSample", static_cast<double>(overview.endSample));

            juce::Array<juce::var> aMin, aMax, bMin, bMax;
            for (size_t i = 0; i < overview.trackA_min.size(); ++i)
            {
                aMin.add(overview.trackA_min[i]);
                aMax.add(overview.trackA_max[i]);
                bMin.add(overview.trackB_min[i]);
                bMax.add(overview.trackB_max[i]);
            }
            obj->setProperty("trackA_min", aMin);
            obj->setProperty("trackA_max", aMax);
            obj->setProperty("trackB_min", bMin);
            obj->setProperty("trackB_max", bMax);

            safeThis->webView->emitEventIfBrowserIsVisible("timelineWaveformOverview", juce::var(obj));
        });
    }

    // NOTE: do NOT call goToURL here. WebView2 is created asynchronously; a URL
    // handed over before the native controller exists is silently dropped and
    // the component shows a blank page forever. Navigation is issued from
    // timerCallback() instead, retrying until pageAboutToLoad() confirms it.

    startTimerHz(60);
}

void FreakPhaseAudioProcessorEditor::setupWebView()
{
    webView = std::make_unique<PhreakWebView>(webViewOptions);
    webView->onAboutToLoad = [this](const juce::String& newURL)
    {
        if (newURL == juce::WebBrowserComponent::getResourceProviderRoot())
            webViewNavigated = true;
        return true;
    };
    webView->onFinishedLoading = [this](const juce::String&)
    {
        webViewNavigated = true;
    };
    webView->onNetworkError = [this](const juce::String&)
    {
        // Suppress the adhoc error page; the timer keeps retrying until its
        // attempt budget runs out (no attempt reset, so a hard failure stops).
        webViewNavigated = false;
        return false;
    };
    addAndMakeVisible(*webView);

    if (isVisible())
        webView->setBounds(getLocalBounds());
}

FreakPhaseAudioProcessorEditor::~FreakPhaseAudioProcessorEditor() noexcept
{
    if (auto* worker = audioProcessor.getTimelineAnalysisThread())
        worker->setCompletionCallback(nullptr);

    stopTimer();
    webView.reset();

    if (userDataFolder.isDirectory())
    {
        if (!userDataFolder.deleteRecursively())
        {
            // If WebView2 processes hold file locks momentarily during shutdown,
            // retry async on a detached background thread to prevent %TEMP% leaks.
            juce::Thread::launch([folder = userDataFolder]()
            {
                for (int attempt = 0; attempt < 15; ++attempt)
                {
                    juce::Thread::sleep(100);
                    if (!folder.isDirectory() || folder.deleteRecursively())
                        break;
                }
            });
        }
    }
}

void FreakPhaseAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff060609));
}

void FreakPhaseAudioProcessorEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds(getLocalBounds());
}

void FreakPhaseAudioProcessorEditor::handleFrontendSetParameter(const juce::var& data)
{
    if (auto* obj = data.getDynamicObject())
    {
        auto paramId = obj->getProperty("paramId").toString();
        float value = static_cast<float>(obj->getProperty("value"));

        if (auto* param = audioProcessor.apvts.getParameter(paramId))
        {
            auto range = audioProcessor.apvts.getParameterRange(paramId);
            param->setValueNotifyingHost(range.convertTo0to1(value));
        }
    }
}

void FreakPhaseAudioProcessorEditor::handleFrontendTriggerAlign()
{
    alignFinishedEdge = false;
    int mode = 1;
    if (auto* pMode = audioProcessor.apvts.getRawParameterValue("ALIGN_TRACK_MODE"))
        mode = juce::roundToInt(pMode->load(std::memory_order_relaxed));

    audioProcessor.alignerThread.triggerAnalysis(mode);
}

void FreakPhaseAudioProcessorEditor::handleFrontendBakeTimeline(const juce::var& data)
{
    if (auto* obj = data.getDynamicObject())
    {
        // Check if multi-segment array is provided (Auto-Align style)
        if (obj->hasProperty("segments"))
        {
            auto segmentsVar = obj->getProperty("segments");
            if (auto* arr = segmentsVar.getArray())
            {
                std::vector<FreakPhaseAudioProcessor::TimelineSegmentData> segs;
                segs.reserve(static_cast<size_t>(arr->size()));
                for (auto& s : *arr)
                {
                    if (auto* sObj = s.getDynamicObject())
                    {
                        FreakPhaseAudioProcessor::TimelineSegmentData seg;
                        seg.startSample = static_cast<int64_t>(static_cast<double>(sObj->getProperty("startSample")));
                        seg.endSample   = static_cast<int64_t>(static_cast<double>(sObj->getProperty("endSample")));
                        seg.delaySamples = static_cast<float>(static_cast<double>(sObj->getProperty("delaySamples")));
                        seg.rotateDeg   = static_cast<float>(static_cast<double>(sObj->getProperty("rotateDeg")));
                        seg.eqCutDb     = static_cast<float>(static_cast<double>(sObj->getProperty("eqCutDb")));
                        seg.polarityFlip = static_cast<bool>(sObj->getProperty("polarityFlip"));
                        seg.bypassed    = static_cast<bool>(sObj->getProperty("bypassed"));
                        seg.correlationBefore = static_cast<float>(static_cast<double>(sObj->getProperty("correlationBefore")));
                        seg.correlationAfter  = static_cast<float>(static_cast<double>(sObj->getProperty("correlationAfter")));
                        segs.push_back(seg);
                    }
                }
                audioProcessor.bakeTimelineSegments(segs);
                return;
            }
        }

        // Single range fallback
        int64_t startSample = static_cast<int64_t>(static_cast<double>(obj->getProperty("startSample")));
        int64_t endSample = static_cast<int64_t>(static_cast<double>(obj->getProperty("endSample")));
        float delayMs = static_cast<float>(static_cast<double>(obj->getProperty("delayMs")));
        float rotateDeg = static_cast<float>(static_cast<double>(obj->getProperty("rotateDeg")));
        float eqCutDb = static_cast<float>(static_cast<double>(obj->getProperty("eqCutDb")));

        double sr = audioProcessor.getSampleRate();
        if (sr < 1000.0) sr = 44100.0;
        float delaySamples = (delayMs / 1000.0f) * static_cast<float>(sr);

        audioProcessor.bakeTimeline(startSample, endSample, delaySamples, rotateDeg, eqCutDb);
    }
}

void FreakPhaseAudioProcessorEditor::handleFrontendClearTimeline()
{
    audioProcessor.clearTimeline();
}

void FreakPhaseAudioProcessorEditor::handleFrontendArmTimelineLearn(const juce::var& data)
{
    bool arm = true;
    if (auto* obj = data.getDynamicObject())
    {
        if (obj->hasProperty("armed"))
            arm = static_cast<bool>(obj->getProperty("armed"));
    }
    audioProcessor.armTimelineLearn(arm);
}

void FreakPhaseAudioProcessorEditor::handleFrontendDisarmTimelineLearn()
{
    audioProcessor.armTimelineLearn(false);
}

void FreakPhaseAudioProcessorEditor::onDawStopTimelineCapture()
{
    if (webView != nullptr)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("event", "dawStopTimelineCapture");
        webView->emitEventIfBrowserIsVisible("timelineCaptureStopped", juce::var(obj));
    }
}

void FreakPhaseAudioProcessorEditor::applySmartAlignParameters(float delayMs, float rotateDeg, bool flip, float gainDb, bool withGestures)
{
    auto applyParam = [this, withGestures](const juce::String& paramId, float plainVal)
    {
        if (auto* param = audioProcessor.apvts.getParameter(paramId))
        {
            auto range = audioProcessor.apvts.getParameterRange(paramId);
            float norm = range.convertTo0to1(plainVal);
            if (withGestures)
                param->beginChangeGesture();
            param->setValueNotifyingHost(norm);
            if (withGestures)
                param->endChangeGesture();
        }
    };

    applyParam("SUB_DELAY", delayMs);
    applyParam("SUB_ROTATE", rotateDeg);
    applyParam("SUB_FLIP", flip ? 1.0f : 0.0f);
    if (std::abs(gainDb) > 0.001f)
        applyParam("SUB_GAIN", gainDb);
}

void FreakPhaseAudioProcessorEditor::handleFrontendSelectPreset(const juce::var& data)
{
    if (auto* obj = data.getDynamicObject())
    {
        int index = static_cast<int>(obj->getProperty("index"));
        presetManager.applyPreset(index);
    }
}

void FreakPhaseAudioProcessorEditor::timerCallback()
{
    if (webView == nullptr)
        return;

    // WebView2 creation retry loop: attempt navigation until the browser
    // controller materialises and accepts the Resource Provider root URL
    // (60 Hz * 600 attempts = up to 10 s of retries, then give up quietly).
    if (!webViewNavigated && webViewNavigateAttempts < 600)
    {
        ++webViewNavigateAttempts;
        webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    }

    // Bounds race fix: the native WebView2 controller is created asynchronously,
    // AFTER resized() has already run — it can come up with stale/empty bounds
    // and render nothing until the user resizes the window. Re-apply bounds
    // once, right after the page confirms it loaded.
    if (webViewNavigated && !webViewBoundsApplied)
    {
        webViewBoundsApplied = true;
        webView->setBounds(getLocalBounds());
    }

    auto* obj = new juce::DynamicObject();

    // Telemetry from Processor & SignalAnalyzer
    float corr = audioProcessor.phaseCorrelation.load(std::memory_order_relaxed);
    float angle = audioProcessor.getAnalyzer().phaseDiffDeg.load(std::memory_order_relaxed);
    float rmsA = audioProcessor.outRmsM.load(std::memory_order_relaxed);
    float rmsB = audioProcessor.outRmsS.load(std::memory_order_relaxed);
    float peakA = audioProcessor.outPeakM.load(std::memory_order_relaxed);
    float peakB = audioProcessor.outPeakS.load(std::memory_order_relaxed);
    float phaseConf = audioProcessor.getAnalyzer().phaseVectorMag.load(std::memory_order_relaxed);
    float mixRms = audioProcessor.outRmsMix.load(std::memory_order_relaxed);

    obj->setProperty("correlation", corr);
    obj->setProperty("phaseAngleDeg", angle);
    obj->setProperty("rmsTrackA_dB", rmsA);
    obj->setProperty("rmsTrackB_dB", rmsB);
    obj->setProperty("peakTrackA_dB", peakA);
    obj->setProperty("peakTrackB_dB", peakB);
    obj->setProperty("phaseConfidence", phaseConf);
    obj->setProperty("mixRms_dB", mixRms);

    const auto transport = audioProcessor.getPlayheadTracker().readSnapshot();
    obj->setProperty("isPlaying", transport.isPlaying);
    obj->setProperty("samplePosition", static_cast<double>(transport.samplePosition));
    obj->setProperty("bpm", transport.bpm);
    obj->setProperty("timeSigNumerator", transport.timeSigNum);
    obj->setProperty("timeSigDenominator", transport.timeSigDenom);
    bool lutActive = (audioProcessor.getTimelineManager().getActiveTableForGui() != nullptr);
    obj->setProperty("lutActive", lutActive);
    obj->setProperty("isTimelineArmed", audioProcessor.isTimelineArmed.load(std::memory_order_relaxed));
    obj->setProperty("isTimelineCapturing", audioProcessor.isTimelineCaptureActive.load(std::memory_order_relaxed));
    obj->setProperty("isTimelineAnalyzing", audioProcessor.isTimelineAnalyzing.load(std::memory_order_relaxed));
    obj->setProperty("timelineAnalysisProgress", audioProcessor.timelineAnalysisProgress.load(std::memory_order_relaxed));

    int midiNote = audioProcessor.pitchTracker.midiNoteNumber.load(std::memory_order_relaxed);
    float hz = audioProcessor.pitchTracker.currentPitchHz.load(std::memory_order_relaxed);
    if (midiNote > 0 && hz > 20.0f)
    {
        auto noteName = PitchTracker::getNoteName(midiNote);
        obj->setProperty("detectedNote", noteName + " - " + juce::String(hz, 1) + " Hz");
        obj->setProperty("detectedFundamentalHz", hz);
    }
    else
    {
        obj->setProperty("detectedNote", "--");
        obj->setProperty("detectedFundamentalHz", 0.0f);
    }

    webView->emitEventIfBrowserIsVisible("audioFrame", juce::var(obj));

    // Reclaim quiescent timeline LUT tables (epoch-fenced RCU)
    audioProcessor.getTimelineManager().reclaimQuiescentTables();

    // Smart Align completion edge: one alignResult event per finished GCC-PHAT scan
    bool finished = audioProcessor.alignerThread.isFinished.load(std::memory_order_relaxed);
    if (finished && !alignFinishedEdge)
    {
        alignFinishedEdge = true;

        const bool timedOut = audioProcessor.alignerThread.timedOut.load(std::memory_order_relaxed);
        const float delMs = audioProcessor.alignerThread.bestDelay.load(std::memory_order_relaxed);
        const float delSamples = audioProcessor.alignerThread.bestDelaySamples.load(std::memory_order_relaxed);
        const float rotDeg = audioProcessor.alignerThread.bestRotate.load(std::memory_order_relaxed);
        const bool flip = audioProcessor.alignerThread.bestFlip.load(std::memory_order_relaxed);
        const float gain = audioProcessor.alignerThread.bestGain.load(std::memory_order_relaxed);
        const float peakCorr = audioProcessor.alignerThread.peakCorrelation.load(std::memory_order_relaxed);
        const float preCorr = audioProcessor.alignerThread.initialCorrelation.load(std::memory_order_relaxed);
        const float detFreq = audioProcessor.alignerThread.detectedFreq.load(std::memory_order_relaxed);
        const double sr = audioProcessor.getSampleRate();

        // Apply calculated alignment to APVTS parameters with full DAW automation gestures
        // (silence protection: do not overwrite settings if timed out or no correlation was found)
        if (!timedOut && peakCorr >= 0.05f)
        {
            auto* trackModeParam = audioProcessor.apvts.getRawParameterValue("ALIGN_TRACK_MODE");
            const bool isContinuous = (trackModeParam != nullptr && juce::roundToInt(trackModeParam->load(std::memory_order_relaxed)) == 0);
            applySmartAlignParameters(delMs, rotDeg, flip, gain, !isContinuous);
        }

        auto* result = new juce::DynamicObject();
        result->setProperty("delayMs", delMs);
        result->setProperty("delaySamples", delSamples);
        result->setProperty("rotateDeg", rotDeg);
        result->setProperty("flip", flip);
        result->setProperty("preCorrelation", preCorr);
        result->setProperty("peakCorrelation", peakCorr);
        result->setProperty("correlationGain", peakCorr - preCorr);
        result->setProperty("detectedFreq", detFreq);
        result->setProperty("suggestedEqCutDb", audioProcessor.alignerThread.suggestedEqCutDb.load(std::memory_order_relaxed));
        result->setProperty("suggestedEqFreqHz", audioProcessor.alignerThread.suggestedEqFreqHz.load(std::memory_order_relaxed));
        result->setProperty("sampleRate", sr > 1000.0 ? sr : 44100.0);
        result->setProperty("timedOut", timedOut);
        result->setProperty("status", timedOut ? "NO AUDIO - CHECK ROUTING" : "OK");
        webView->emitEventIfBrowserIsVisible("alignResult", juce::var(result));

        // ALIGN_TRACK_MODE: Transient (1, default) = single-shot scan — stop the
        // GCC-PHAT loop after delivering a result; Continuous (0) keeps scanning.
        auto* trackMode = audioProcessor.apvts.getRawParameterValue("ALIGN_TRACK_MODE");
        if (trackMode == nullptr
            || juce::roundToInt(trackMode->load(std::memory_order_relaxed)) != 0
            || timedOut)
        {
            audioProcessor.alignerThread.autoAlignActive.store(false, std::memory_order_relaxed);
        }
    }
    else if (!finished)
    {
        alignFinishedEdge = false;
    }

    // Waveform snapshot at 30 Hz (every 2nd audioFrame tick)
    if ((++audioFrameCounter % 2) == 0)
        pushWaveformEvent();
}

void FreakPhaseAudioProcessorEditor::pushWaveformEvent()
{
    if (webView == nullptr)
        return;

    // If scope is frozen in APVTS, do not waste CPU/IPC streaming waveform snapshots
    if (auto* freezeParam = audioProcessor.apvts.getRawParameterValue("FREEZE"))
    {
        if (freezeParam->load(std::memory_order_relaxed) > 0.5f)
            return;
    }

    auto& capture = audioProcessor.captureBuffer;
    const int capSize = capture.getNumSamples();
    const int snapshotN = waveformSnapshot.getNumSamples();

    float zoomXVal = 0.5f;
    float panXVal = 0.5f;
    if (auto* p = audioProcessor.apvts.getRawParameterValue("ZOOMX"))
        zoomXVal = p->load(std::memory_order_relaxed);
    if (auto* p = audioProcessor.apvts.getRawParameterValue("PANX"))
        panXVal = p->load(std::memory_order_relaxed);

    double currentSr = audioProcessor.getSampleRate();
    if (currentSr < 1000.0) currentSr = 44100.0;

    // Window size: min 50-100 ms at 1x zoom (zoomXVal=0.5 -> ~80 ms)
    // Range from 25ms (zoomX=1.0) to 180ms (zoomX=0.0)
    float windowMs = juce::jmap(zoomXVal, 0.0f, 1.0f, 180.0f, 25.0f);
    int windowSamples = juce::roundToInt(windowMs * 0.001f * static_cast<float>(currentSr));
    windowSamples = juce::jlimit(snapshotN, capSize / 2, windowSamples);

    int panOffset = juce::roundToInt((panXVal - 0.5f) * static_cast<float>(windowSamples) * 0.5f);
    int startIdx = audioProcessor.captureWriteIndex.load(std::memory_order_acquire) - windowSamples + panOffset;
    while (startIdx < 0)
        startIdx += capSize;
    startIdx %= capSize;

    float step = static_cast<float>(windowSamples) / static_cast<float>(snapshotN);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dest = waveformSnapshot.getWritePointer(ch);
        const auto* src = capture.getReadPointer(ch);
        for (int i = 0; i < snapshotN; ++i)
        {
            int idx = (startIdx + juce::roundToInt(static_cast<float>(i) * step)) % capSize;
            dest[i] = src[idx];
        }
    }

    auto* obj = new juce::DynamicObject();
    juce::Array<juce::var> arrA, arrB;
    arrA.ensureStorageAllocated(snapshotN);
    arrB.ensureStorageAllocated(snapshotN);
    const float* a = waveformSnapshot.getReadPointer(0);
    const float* b = waveformSnapshot.getReadPointer(1);
    for (int i = 0; i < snapshotN; ++i)
    {
        arrA.add(a[i]);
        arrB.add(b[i]);
    }
    obj->setProperty("trackA", arrA);
    obj->setProperty("trackB", arrB);
    webView->emitEventIfBrowserIsVisible("waveform", juce::var(obj));
}
// =============================================================================
// FIX: MIDI Learn handlers for frontend communication
// =============================================================================

void FreakPhaseAudioProcessorEditor::handleFrontendStartMidiLearn(const juce::var& data)
{
    if (auto* obj = data.getDynamicObject())
    {
        auto paramId = obj->getProperty("paramId").toString();
        audioProcessor.startMidiLearn(paramId);
    }
}

void FreakPhaseAudioProcessorEditor::handleFrontendCancelMidiLearn()
{
    audioProcessor.cancelMidiLearn();
}

void FreakPhaseAudioProcessorEditor::handleFrontendClearMidiMappings()
{
    audioProcessor.clearMidiMappings();
}

void FreakPhaseAudioProcessorEditor::handleFrontendGetMidiMappings(juce::var& result)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& [cc, paramId] : audioProcessor.midiCCToParam)
    {
        obj->setProperty(juce::String(cc), juce::var(paramId));
    }
    result = juce::var(obj);
}

void FreakPhaseAudioProcessorEditor::handleFrontendGetSuggestedMidiMappings(juce::var& result)
{
    auto suggested = audioProcessor.suggestMidiMappings();
    auto* obj = new juce::DynamicObject();
    for (const auto& [cc, paramId] : suggested)
    {
        obj->setProperty(juce::String(cc), juce::var(paramId));
    }
    result = juce::var(obj);
}

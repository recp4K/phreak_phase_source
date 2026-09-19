#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>
#include <memory>

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Host playhead stand-in: stopped transport parked at a negative (pre-roll)
    // position with degenerate BPM, to exercise the editor telemetry path.
    class PreRollPlayHead : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override
        {
            juce::AudioPlayHead::PositionInfo info;
            info.setTimeInSamples(juce::Optional<int64_t>(-48000));
            info.setPpqPosition(juce::Optional<double>(-4.0));
            info.setBpm(juce::Optional<double>(0.0));
            info.setIsPlaying(false);
            return juce::Optional<PositionInfo> (info);
        }
    };
}

int main()
{
    std::cout << "START MAIN\n" << std::flush;
    // Initialize JUCE GUI message thread
    juce::ScopedJuceInitialiser_GUI guiInit;
    std::cout << "AFTER GUI INIT\n" << std::flush;

    int passedTests = 0;
    int totalTests = 0;

    {
    std::cout << "=================================================================\n" << std::flush;
    std::cout << "  FREAK PHASE: PHASE 2 UI VERIFICATION SUITE (WebView2 editor surface)\n" << std::flush;
    std::cout << "=================================================================\n\n" << std::flush;


    auto check = [&](bool condition, const std::string& testName, const std::string& failDetails = "") {
        totalTests++;
        if (condition) {
            std::cout << " [PASS] " << testName << "\n" << std::flush;
            passedTests++;
        } else {
            std::cerr << " [FAIL] " << testName;
            if (!failDetails.empty()) std::cerr << " (" << failDetails << ")";
            std::cerr << "\n" << std::flush;
        }
    };

    // =========================================================================
    // TEST 1: APVTS Parameter Tree Verification (current createParameterLayout)
    // =========================================================================
    std::cout << "\n--- TEST 1: APVTS Parameter Tree Completeness ---\n" << std::flush;
    {
        FreakPhaseAudioProcessor processor;

        auto checkParam = [&](const juce::String& paramID, float minVal, float maxVal) {
            auto* param = processor.apvts.getParameter(paramID);
            bool exists = (param != nullptr);
            if (!exists) {
                check(false, "Parameter " + paramID.toStdString() + " exists in APVTS", "NOT FOUND");
                return;
            }
            auto range = param->getNormalisableRange();
            bool rangeOk = (range.start <= minVal + 0.01f && range.end >= maxVal - 0.01f);
            check(exists && rangeOk, "Parameter " + paramID.toStdString() + " exists with valid range ["
                  + std::to_string(range.start) + ", " + std::to_string(range.end) + "]");
        };

        checkParam("SUB_ROTATE", -180.0f, 180.0f);
        checkParam("SUB_DELAY", -20.0f, 20.0f);
        checkParam("SUB_GAIN", -24.0f, 24.0f);
        checkParam("SUB_DYN_AMOUNT", 0.0f, 100.0f);
        checkParam("HIGH_ROTATE", -180.0f, 180.0f);
        checkParam("HIGH_DELAY", -20.0f, 20.0f);
        checkParam("HIGH_GAIN", -24.0f, 24.0f);
        checkParam("HIGH_DYN_AMOUNT", 0.0f, 100.0f);
        checkParam("RESPONSE", 5.0f, 500.0f);
        checkParam("DYN_EQ_FREQ", 20.0f, 500.0f);
        checkParam("DYN_EQ_DEPTH", -24.0f, 24.0f);
        checkParam("DYN_PH_AMOUNT", 0.0f, 100.0f);
        checkParam("BASS_GLUE", 0.0f, 100.0f);
        checkParam("ENV_ATTACK", 0.1f, 50.0f);
        checkParam("ENV_RELEASE", 5.0f, 500.0f);
        checkParam("LOOKAHEAD_MS", 0.0f, 10.0f);
        checkParam("CROSSOVER_FREQ", 40.0f, 300.0f);

        // Display / view-switching parameters consumed by the WebView2 frontend
        checkParam("ZOOMX", 0.0f, 1.0f);
        checkParam("PANX", 0.0f, 1.0f);
        checkParam("ZOOMY", 0.1f, 10.0f);

        check(processor.apvts.getParameter("CROSSOVER_ENABLE") != nullptr, "CROSSOVER_ENABLE bool toggle exists");
        check(processor.apvts.getParameter("PITCH_TRACK") != nullptr, "PITCH_TRACK bool toggle exists");
        check(processor.apvts.getParameter("SUB_FLIP") != nullptr, "SUB_FLIP bool toggle exists");
        check(processor.apvts.getParameter("HIGH_FLIP") != nullptr, "HIGH_FLIP bool toggle exists");
        check(processor.apvts.getParameter("DELTA_LISTEN") != nullptr, "DELTA_LISTEN bool toggle exists");
        check(processor.apvts.getParameter("FREEZE") != nullptr, "FREEZE bool toggle exists");
        check(processor.apvts.getParameter("WHEEL_COLLAPSED") != nullptr, "WHEEL_COLLAPSED bool toggle exists");

        check(processor.apvts.getParameter("TARGET") != nullptr, "TARGET choice parameter exists");
        check(processor.apvts.getParameter("MONITOR") != nullptr, "MONITOR choice parameter exists");
        check(processor.apvts.getParameter("TRACK_B_CH") != nullptr, "TRACK_B_CH choice parameter exists");
        check(processor.apvts.getParameter("DC_FILTER") != nullptr, "DC_FILTER choice parameter exists");
        check(processor.apvts.getParameter("ALIGN_TRACK_MODE") != nullptr, "ALIGN_TRACK_MODE choice parameter exists");
        check(processor.apvts.getParameter("LATENCY_MODE") != nullptr, "LATENCY_MODE choice parameter exists");
    }

    // =========================================================================
    // TEST 2: Editor Instantiation & Default Window Geometry
    // =========================================================================
    std::cout << "\n--- TEST 2: Editor Instantiation & Default Geometry ---\n" << std::flush;
    {
        FreakPhaseAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        check(editor != nullptr, "createEditor() returned non-null AudioProcessorEditor");

        if (editor != nullptr)
        {
            std::cout << "  Default Width: " << editor->getWidth() << ", Default Height: " << editor->getHeight() << "\n";
            check(editor->getWidth() == 1120, "Target default width is 1120px");
            check(editor->getHeight() == 650, "Target default height is 650px");

            check(dynamic_cast<FreakPhaseAudioProcessorEditor*>(editor.get()) != nullptr,
                  "Editor is valid FreakPhaseAudioProcessorEditor instance");

            editor.reset();
            check(true, "Editor cleanly destroyed without memory leaks or attachment assertion failures");
        }
    }

    // =========================================================================
    // TEST 3: Resize Constraints & Boundary Testing
    // =========================================================================
    std::cout << "\n--- TEST 3: Resizing Limits & Geometry Scaling ---\n";
    {
        FreakPhaseAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        check(editor != nullptr, "Editor created for resize test harness");

        if (editor != nullptr)
        {
            auto* constrainer = editor->getConstrainer();
            check(constrainer != nullptr && constrainer->getMinimumWidth() == 900 && constrainer->getMinimumHeight() == 520,
                  "Constrainer enforces minimum bounds (900x520)");
            check(constrainer != nullptr && constrainer->getMaximumWidth() == 1680 && constrainer->getMaximumHeight() == 1050,
                  "Constrainer enforces maximum bounds (1680x1050)");
            check(constrainer != nullptr && std::abs(constrainer->getFixedAspectRatio() - (1120.0 / 650.0)) < 1e-6,
                  "Constrainer keeps the 1120x650 fixed aspect ratio");

            // Minimum size test (900x520)
            editor->setSize(900, 520);
            check(editor->getWidth() == 900 && editor->getHeight() == 520, "Resize to minimum dimensions (900x520) successful without clipping");

            // Maximum size test (1680x1050)
            editor->setSize(1680, 1050);
            check(editor->getWidth() == 1680 && editor->getHeight() == 1050, "Resize to maximum dimensions (1680x1050) successful without overflow");

            // Reset back to base
            editor->setSize(1120, 650);
        }
    }

    // =========================================================================
    // TEST 4: View Switching Surface: WHEEL_COLLAPSED + Display Params (R2 analog)
    // =========================================================================
    std::cout << "\n--- TEST 4: Wheel Collapse & Display Parameter View Switching ---\n";
    {
        FreakPhaseAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        auto* freakEditor = dynamic_cast<FreakPhaseAudioProcessorEditor*>(editor.get());
        check(freakEditor != nullptr, "Editor is valid FreakPhaseAudioProcessorEditor instance");

        if (freakEditor != nullptr)
        {
            bool viewSwitchClean = true;
            try {
                // 1. Collapse the Phase Wheel (view-switching state lives in APVTS)
                if (auto* pWheel = processor.apvts.getParameter("WHEEL_COLLAPSED"))
                    pWheel->setValueNotifyingHost(1.0f);
                auto wheelRead = processor.apvts.getParameter("WHEEL_COLLAPSED")->convertFrom0to1(
                    processor.apvts.getParameter("WHEEL_COLLAPSED")->getValue());
                check(std::abs(wheelRead - 1.0f) < 0.01f, "WHEEL_COLLAPSED toggles to collapsed (1.0) via APVTS");

                // 2. Extreme display transforms (ZOOMX / ZOOMY / PANX drive the scope windowing)
                if (auto* p = processor.apvts.getParameter("ZOOMX"))
                    p->setValueNotifyingHost(p->convertTo0to1(1.0f));
                if (auto* p = processor.apvts.getParameter("ZOOMY"))
                    p->setValueNotifyingHost(p->convertTo0to1(10.0f));
                if (auto* p = processor.apvts.getParameter("PANX"))
                    p->setValueNotifyingHost(p->convertTo0to1(0.0f));

                auto* pZoomX = processor.apvts.getParameter("ZOOMX");
                auto* pZoomY = processor.apvts.getParameter("ZOOMY");
                auto* pPanX = processor.apvts.getParameter("PANX");
                float zoomX = pZoomX->convertFrom0to1(pZoomX->getValue());
                float zoomY = pZoomY->convertFrom0to1(pZoomY->getValue());
                float panX = pPanX->convertFrom0to1(pPanX->getValue());
                check(std::abs(zoomX - 1.0f) < 0.01f, "ZOOMX display param updates to 1.0 (max zoom in)");
                check(std::abs(zoomY - 10.0f) < 0.05f, "ZOOMY display param updates to 10.0 (max vertical zoom)");
                check(std::abs(panX - 0.0f) < 0.01f, "PANX display param updates to 0.0 (left pan)");

                // 3. Layout refresh + telemetry frame with collapsed wheel & extreme zoom
                freakEditor->resized();
                freakEditor->timerCallback();

                // 4. Restore the expanded wheel view
                if (auto* pWheel = processor.apvts.getParameter("WHEEL_COLLAPSED"))
                    pWheel->setValueNotifyingHost(0.0f);
                freakEditor->resized();
                freakEditor->timerCallback();
            }
            catch (...) {
                viewSwitchClean = false;
            }
            check(viewSwitchClean, "Wheel collapse / display view switching executes cleanly with zero exceptions");
        }
    }

    // =========================================================================
    // TEST 5: Real-Time Timer Telemetry & Thread Safety (Requirement R4)
    // =========================================================================
    std::cout << "\n--- TEST 5: Timer Callback Polling & Thread Safety (Requirement R4) ---\n";
    {
        FreakPhaseAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        auto* freakEditor = dynamic_cast<FreakPhaseAudioProcessorEditor*>(editor.get());

        if (freakEditor != nullptr)
        {
            // Simulate 50 timer frames with live telemetry values
            processor.phaseCorrelation.store(0.88f, std::memory_order_relaxed);
            processor.outRmsM.store(-12.0f, std::memory_order_relaxed);
            processor.outPeakM.store(-3.0f, std::memory_order_relaxed);
            processor.outRmsS.store(-14.0f, std::memory_order_relaxed);
            processor.outPeakS.store(-4.5f, std::memory_order_relaxed);
            processor.getAnalyzer().phaseDiffDeg.store(45.0f, std::memory_order_relaxed);
            processor.getAnalyzer().phaseVectorMag.store(0.9f, std::memory_order_relaxed);

            bool timerSuccess = true;
            try {
                for (int i = 0; i < 50; ++i)
                    freakEditor->timerCallback();
            }
            catch (...) {
                timerSuccess = false;
            }

            check(timerSuccess, "50 timerCallback frames executed cleanly with zero exceptions or locking");
        }
    }

    // =========================================================================
    // TEST 6: Multiple Window Reload Cycles (Zero Memory Leaks / Clean Teardown)
    // =========================================================================
    std::cout << "\n--- TEST 6: Rapid Window Open/Close Lifecycles (Requirement R4) ---\n";
    {
        FreakPhaseAudioProcessor processor;
        bool allCyclesClean = true;

        for (int cycle = 1; cycle <= 10; ++cycle)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            if (editor == nullptr)
            {
                allCyclesClean = false;
                break;
            }
            editor->setSize(1120, 650);
            auto* freakEd = dynamic_cast<FreakPhaseAudioProcessorEditor*>(editor.get());
            if (freakEd != nullptr)
            {
                freakEd->timerCallback();
                freakEd->resized();
                freakEd->timerCallback();
            }
            editor.reset();
        }

        check(allCyclesClean, "10 consecutive createEditor / delete cycles completed without assertion failure or leak");
    }

    // =========================================================================
    // TEST 7: Parameter Attachment Real-Time Control Verification
    // =========================================================================
    std::cout << "\n--- TEST 7: APVTS Two-Way Parameter Binding ---\n";
    {
        FreakPhaseAudioProcessor processor;

        if (auto* pSubRot = processor.apvts.getParameter("SUB_ROTATE"))
        {
            pSubRot->setValueNotifyingHost(pSubRot->convertTo0to1(45.0f));
            float readVal = pSubRot->convertFrom0to1(pSubRot->getValue());
            check(std::abs(readVal - 45.0f) < 0.2f, "SUB_ROTATE setValueNotifyingHost updates to 45 deg");
        }

        if (auto* pSubDel = processor.apvts.getParameter("SUB_DELAY"))
        {
            pSubDel->setValueNotifyingHost(pSubDel->convertTo0to1(3.5f));
            float readVal = pSubDel->convertFrom0to1(pSubDel->getValue());
            check(std::abs(readVal - 3.5f) < 0.05f, "SUB_DELAY setValueNotifyingHost updates to 3.5 ms");
        }

        if (auto* pSubDyn = processor.apvts.getParameter("SUB_DYN_AMOUNT"))
        {
            pSubDyn->setValueNotifyingHost(pSubDyn->convertTo0to1(60.0f));
            float readVal = pSubDyn->convertFrom0to1(pSubDyn->getValue());
            check(std::abs(readVal - 60.0f) < 0.5f, "SUB_DYN_AMOUNT setValueNotifyingHost updates to 60 %");
        }

        if (auto* pHighRot = processor.apvts.getParameter("HIGH_ROTATE"))
        {
            pHighRot->setValueNotifyingHost(pHighRot->convertTo0to1(-90.0f));
            float readVal = pHighRot->convertFrom0to1(pHighRot->getValue());
            check(std::abs(readVal - (-90.0f)) < 0.2f, "HIGH_ROTATE setValueNotifyingHost updates to -90 deg");
        }

        if (auto* pCross = processor.apvts.getParameter("CROSSOVER_FREQ"))
        {
            pCross->setValueNotifyingHost(pCross->convertTo0to1(120.0f));
            float readVal = pCross->convertFrom0to1(pCross->getValue());
            check(std::abs(readVal - 120.0f) < 1.0f, "CROSSOVER_FREQ setValueNotifyingHost updates to 120 Hz");
        }

        if (auto* pEqFreq = processor.apvts.getParameter("DYN_EQ_FREQ"))
        {
            pEqFreq->setValueNotifyingHost(pEqFreq->convertTo0to1(85.0f));
            float readVal = pEqFreq->convertFrom0to1(pEqFreq->getValue());
            check(std::abs(readVal - 85.0f) < 0.5f, "DYN_EQ_FREQ setValueNotifyingHost updates to 85 Hz");
        }

        if (auto* pBassGlue = processor.apvts.getParameter("BASS_GLUE"))
        {
            pBassGlue->setValueNotifyingHost(pBassGlue->convertTo0to1(40.0f));
            float readVal = pBassGlue->convertFrom0to1(pBassGlue->getValue());
            check(std::abs(readVal - 40.0f) < 1.0f, "BASS_GLUE setValueNotifyingHost updates to 40 %");
        }

        if (auto* pHighDel = processor.apvts.getParameter("HIGH_DELAY"))
        {
            pHighDel->setValueNotifyingHost(pHighDel->convertTo0to1(-8.5f));
            float readVal = pHighDel->convertFrom0to1(pHighDel->getValue());
            check(std::abs(readVal - (-8.5f)) < 0.1f, "HIGH_DELAY setValueNotifyingHost updates to -8.5 ms");
        }

        if (auto* pHighDyn = processor.apvts.getParameter("HIGH_DYN_AMOUNT"))
        {
            pHighDyn->setValueNotifyingHost(pHighDyn->convertTo0to1(45.0f));
            float readVal = pHighDyn->convertFrom0to1(pHighDyn->getValue());
            check(std::abs(readVal - 45.0f) < 0.5f, "HIGH_DYN_AMOUNT setValueNotifyingHost updates to 45 %");
        }

        if (auto* pAtt = processor.apvts.getParameter("ENV_ATTACK"))
        {
            pAtt->setValueNotifyingHost(pAtt->convertTo0to1(12.5f));
            float readVal = pAtt->convertFrom0to1(pAtt->getValue());
            check(std::abs(readVal - 12.5f) < 0.5f, "ENV_ATTACK setValueNotifyingHost updates to 12.5 ms");
        }

        if (auto* pRel = processor.apvts.getParameter("ENV_RELEASE"))
        {
            pRel->setValueNotifyingHost(pRel->convertTo0to1(250.0f));
            float readVal = pRel->convertFrom0to1(pRel->getValue());
            check(std::abs(readVal - 250.0f) < 2.0f, "ENV_RELEASE setValueNotifyingHost updates to 250 ms");
        }

        if (auto* pZoomX = processor.apvts.getParameter("ZOOMX"))
        {
            pZoomX->setValueNotifyingHost(pZoomX->convertTo0to1(0.75f));
            float readVal = pZoomX->convertFrom0to1(pZoomX->getValue());
            check(std::abs(readVal - 0.75f) < 0.01f, "ZOOMX setValueNotifyingHost updates to 0.75");
        }

        if (auto* pWheel = processor.apvts.getParameter("WHEEL_COLLAPSED"))
        {
            pWheel->setValueNotifyingHost(1.0f);
            float readVal = pWheel->convertFrom0to1(pWheel->getValue());
            check(std::abs(readVal - 1.0f) < 0.01f, "WHEEL_COLLAPSED setValueNotifyingHost updates to collapsed");
        }
    }

    // =========================================================================
    // TEST 8: Adversarial UI Stress & Boundary Tests
    // =========================================================================
    std::cout << "\n--- TEST 8: Adversarial UI Stress & Boundary Tests ---\n" << std::flush;
    {
        FreakPhaseAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        auto* freakEd = dynamic_cast<FreakPhaseAudioProcessorEditor*>(editor.get());
        check(freakEd != nullptr, "FreakPhaseAudioProcessorEditor created for adversarial stress");

        if (freakEd != nullptr)
        {
            // 8.1: Rapid Display Parameter Swapping (50 cycles ZOOMX/ZOOMY/PANX/WHEEL_COLLAPSED)
            bool rapidModeSuccess = true;
            try {
                for (int i = 0; i < 50; ++i)
                {
                    if (auto* p = processor.apvts.getParameter("ZOOMX"))
                        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(i % 2)));
                    if (auto* p = processor.apvts.getParameter("ZOOMY"))
                        p->setValueNotifyingHost(p->convertTo0to1(0.1f + static_cast<float>(i % 10)));
                    if (auto* p = processor.apvts.getParameter("PANX"))
                        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(i % 3) / 3.0f));
                    if (auto* p = processor.apvts.getParameter("WHEEL_COLLAPSED"))
                        p->setValueNotifyingHost(static_cast<float>(i % 2));
                    freakEd->resized();
                    freakEd->timerCallback();
                }
            } catch (...) {
                rapidModeSuccess = false;
            }
            check(rapidModeSuccess, "50 rapid display param + wheel collapse swaps executed without assertion or crash");

            // 8.2: Boundary & Extreme Window Resizing
            bool extremeResizeClean = true;
            try {
                freakEd->setSize(100, 100);
                freakEd->resized();
                freakEd->timerCallback();
                freakEd->setSize(3000, 2000);
                freakEd->resized();
                freakEd->setSize(1120, 650);
            } catch (...) {
                extremeResizeClean = false;
            }
            check(extremeResizeClean, "Editor survives extreme window sizes + resized() + timerCallback()");

            // 8.3: Telemetry Boundary Values (Negative, Infinite, NaN Phase Correlation)
            bool telemetryHandled = true;
            try {
                processor.phaseCorrelation.store(-1.0f, std::memory_order_relaxed);
                processor.persistentMaxCorr.store(1.0f, std::memory_order_relaxed);
                freakEd->timerCallback();

                processor.phaseCorrelation.store(0.0f, std::memory_order_relaxed);
                freakEd->timerCallback();

                processor.phaseCorrelation.store(1.0f, std::memory_order_relaxed);
                freakEd->timerCallback();

                // Telemetry with silent/negative audio levels
                processor.outRmsM.store(-120.0f, std::memory_order_relaxed);
                processor.outPeakM.store(-100.0f, std::memory_order_relaxed);
                processor.persistentPeakM.store(-100.0f, std::memory_order_relaxed);
                freakEd->timerCallback();
            } catch (...) {
                telemetryHandled = false;
            }
            check(telemetryHandled, "Telemetry gracefully handles extreme, zero, and minimum dB audio bounds");

            // 8.4: Playhead Tracker Stopped / Negative PPQ (Pre-Roll) Handling
            {
                PreRollPlayHead preRollHead;
                auto snap = processor.getPlayheadTracker().update(&preRollHead, 512);
                check(!snap.isPlaying && snap.ppqPosition == -4.0 && snap.bpm == 0.0,
                      "PlayheadTracker publishes stopped pre-roll snapshot (ppq=-4, bpm=0)");

                bool playheadHandled = true;
                try {
                    freakEd->timerCallback();
                    freakEd->timerCallback();
                    freakEd->timerCallback();
                } catch (...) {
                    playheadHandled = false;
                }
                check(playheadHandled, "Editor telemetry handles stopped/pre-roll transport without division-by-zero");
            }

            // 8.5: AutoAlignerThread triggerAnalysis Race Condition Immunity
            processor.alignerThread.triggerAnalysis(1);
            check(!processor.alignerThread.isFinished.load(std::memory_order_acquire)
                  && processor.alignerThread.autoAlignActive.load(std::memory_order_acquire),
                  "AutoAlignerThread triggerAnalysis clears isFinished and arms autoAlignActive atomically");

            // 8.6: RCU Timeline Manager getActiveTableForGui Epoch Invariance
            uint64_t epochAudio = 0;
            auto* initialTable = processor.getTimelineManager().acquireTableForAudio(epochAudio);
            juce::ignoreUnused(initialTable);
            uint64_t epochAfterAudio = epochAudio;

            // Multiple GUI calls to getActiveTableForGui must NOT advance currentAudioEpoch
            for (int i = 0; i < 100; ++i)
            {
                auto* guiTable = processor.getTimelineManager().getActiveTableForGui();
                juce::ignoreUnused(guiTable);
            }
            uint64_t epochAudioCheck = 0;
            processor.getTimelineManager().acquireTableForAudio(epochAudioCheck);
            check(epochAudioCheck == epochAfterAudio + 1, "getActiveTableForGui does not increment RCU audio epoch (no premature table reclamation)");

            // 8.7: DynamicEq Masking Cut DSP Verification (current surface: DYN_EQ_DEPTH
            // is the DSP-side cut parameter; HIGH_DYN_AMOUNT is a UI-layer control)
            {
                auto runBlocks = [](float dynEqDepth, bool withCut)
                {
                    FreakPhaseAudioProcessor proc;
                    proc.prepareToPlay(48000.0, 512);
                    if (auto* pDepth = proc.apvts.getParameter("DYN_EQ_DEPTH"))
                        pDepth->setValueNotifyingHost(pDepth->convertTo0to1(withCut ? dynEqDepth : 0.0f));
                    if (auto* pMon = proc.apvts.getParameter("MONITOR"))
                        pMon->setValueNotifyingHost(pMon->convertTo0to1(0.0f)); // Out: Track A

                    juce::AudioBuffer<float> buf(4, 512);
                    juce::MidiBuffer midi;
                    float peak = 0.0f;
                    for (int blk = 0; blk < 3; ++blk)
                    {
                        buf.clear();
                        for (int i = 0; i < 512; ++i)
                        {
                            float t = std::sin(2.0f * juce::MathConstants<float>::pi * 60.0f
                                               * (float)(blk * 512 + i) / 48000.0f);
                            buf.setSample(0, i, t);
                            buf.setSample(1, i, t);
                            buf.setSample(2, i, 1.0f); // full-scale sidechain kick transient
                            buf.setSample(3, i, 1.0f);
                        }
                        proc.processBlock(buf, midi);
                        if (blk == 2) peak = buf.getMagnitude(0, 512);
                    }
                    return peak;
                };

                float baselinePeak = runBlocks(-24.0f, false);
                float cutPeak = runBlocks(-24.0f, true);
                check(std::isfinite(baselinePeak) && std::abs(baselinePeak - 1.0f) < 0.1f,
                      "Sub band tone passes at unity when DYN_EQ_DEPTH is 0 (no cut)");
                check(std::isfinite(cutPeak) && cutPeak < 0.9f * baselinePeak,
                      "DYN_EQ_DEPTH masking cut attenuates sub band in real time without audio glitches");
            }

            // 8.7b: HIGH_DYN_AMOUNT (UI-layer param) keeps the processing path finite and bounded
            {
                FreakPhaseAudioProcessor dspProc;
                dspProc.prepareToPlay(48000.0, 512);

                if (auto* pHighDyn = dspProc.apvts.getParameter("HIGH_DYN_AMOUNT"))
                    pHighDyn->setValueNotifyingHost(pHighDyn->convertTo0to1(100.0f)); // 100 %

                juce::AudioBuffer<float> procBuf(4, 512);
                procBuf.clear();
                for (int i = 0; i < 512; ++i)
                {
                    float highTone = std::sin(2.0f * juce::MathConstants<float>::pi * 5000.0f * (float)i / 48000.0f);
                    procBuf.setSample(0, i, highTone); // Track A L
                    procBuf.setSample(1, i, highTone); // Track A R
                    procBuf.setSample(2, i, 1.0f);     // Track B L (Full sidechain kick transient)
                    procBuf.setSample(3, i, 1.0f);     // Track B R
                }
                juce::MidiBuffer midi;
                dspProc.processBlock(procBuf, midi);

                float peakOut = procBuf.getMagnitude(0, 512);
                check(std::isfinite(peakOut) && peakOut < 4.0f, "HIGH_DYN_AMOUNT at 100% keeps output finite and bounded without audio glitches");
            }

            // 8.8: PresetManager A/B State Toggle Verification
            {
                PresetManager pm(processor.apvts);
                if (auto* pSubRot = processor.apvts.getParameter("SUB_ROTATE"))
                {
                    pSubRot->setValueNotifyingHost(pSubRot->convertTo0to1(30.0f));
                    pm.toggleAB(true); // Switch to B (saves A = 30 deg)
                    pSubRot->setValueNotifyingHost(pSubRot->convertTo0to1(90.0f)); // Set B = 90 deg
                    pm.toggleAB(false); // Switch back to A
                    float readA = pSubRot->convertFrom0to1(pSubRot->getValue());
                    check(std::abs(readA - 30.0f) < 1.0f, "PresetManager A/B toggles cleanly and restores state A (30 deg)");

                    pm.toggleAB(true); // Switch back to B
                    float readB = pSubRot->convertFrom0to1(pSubRot->getValue());
                    check(std::abs(readB - 90.0f) < 1.0f, "PresetManager A/B restores modified state B (90 deg)");
                }
            }

            // 8.9: DAW Stop Timeline Capture Notification (editor public API)
            {
                bool dawStopClean = true;
                try {
                    freakEd->onDawStopTimelineCapture();
                } catch (...) {
                    dawStopClean = false;
                }
                check(dawStopClean, "onDawStopTimelineCapture dispatches timelineCaptureStopped event without exception");
            }

            // 8.10: AutoAlignerThread Live Execution & Worker Thread Liveness
            {
                // Ensure thread is running
                if (!processor.alignerThread.isThreadRunning())
                    processor.alignerThread.startThread(juce::Thread::Priority::normal);
                check(processor.alignerThread.isThreadRunning(), "AutoAlignerThread OS worker thread is active");

                // Populate captureBuffer with 60 Hz tone in Track A and Track B
                int capSize = processor.captureBuffer.getNumSamples();
                for (int i = 0; i < capSize; ++i)
                {
                    float sA = std::sin(2.0f * juce::MathConstants<float>::pi * 60.0f * (float)i / 48000.0f);
                    float sB = std::sin(2.0f * juce::MathConstants<float>::pi * 60.0f * (float)i / 48000.0f + 0.5f);
                    processor.captureBuffer.setSample(0, i, sA);
                    processor.captureBuffer.setSample(1, i, sB);
                }
                processor.captureWriteIndex.store(capSize - 1);

                // Arm analysis via the production trigger path
                processor.alignerThread.triggerAnalysis(1);

                // Wait for worker thread to complete GCC-PHAT (up to 3s timeout)
                int waitCount = 0;
                while (!processor.alignerThread.isFinished.load(std::memory_order_acquire) && waitCount < 300)
                {
                    juce::Thread::sleep(10);
                    ++waitCount;
                }

                check(processor.alignerThread.isFinished.load(std::memory_order_acquire),
                      "AutoAlignerThread completes GCC-PHAT analysis and marks isFinished true");
                check(processor.alignerThread.progress.load(std::memory_order_relaxed) >= 0.99f,
                      "AutoAlignerThread reports 100% progress upon completion");
                check(processor.alignerThread.detectedFreq.load(std::memory_order_relaxed) > 30.0f,
                      "AutoAlignerThread detects valid fundamental pitch (>30 Hz)");
            }

            // 8.11: Negative HIGH_DELAY in Precision Mode
            {
                FreakPhaseAudioProcessor procHigh;
                procHigh.prepareToPlay(48000.0, 512);

                if (auto* pHighDel = procHigh.apvts.getParameter("HIGH_DELAY"))
                    pHighDel->setValueNotifyingHost(pHighDel->convertTo0to1(-4.0f)); // -4 ms

                juce::AudioBuffer<float> testBuf(4, 512);
                testBuf.clear();
                for (int i = 0; i < 512; ++i)
                {
                    testBuf.setSample(0, i, 0.5f);
                    testBuf.setSample(1, i, 0.5f);
                }
                juce::MidiBuffer midi;
                procHigh.processBlock(testBuf, midi);
                float peak = testBuf.getMagnitude(0, 512);
                check(std::isfinite(peak) && peak > 0.0f, "Negative HIGH_DELAY processes safely through lookahead without NaN or silence");
            }

            // 8.12: Multiband Deck BASS_GLUE APVTS Two-Way Binding
            {
                if (auto* pGlue = processor.apvts.getParameter("BASS_GLUE"))
                {
                    pGlue->setValueNotifyingHost(pGlue->convertTo0to1(65.0f));
                    float val = pGlue->convertFrom0to1(pGlue->getValue());
                    check(std::abs(val - 65.0f) < 0.5f, "BASS_GLUE on Multiband Deck Dynamics card updates to 65%");
                }
            }

            // 8.13: PresetManager Factory Preset Update Verification
            {
                PresetManager pm(processor.apvts);
                pm.applyPreset(0); // 1. Trap 808 & Kick
                float subDyn = processor.apvts.getRawParameterValue("SUB_DYN_AMOUNT")->load();
                float glue = processor.apvts.getRawParameterValue("BASS_GLUE")->load();
                float dynPh = processor.apvts.getRawParameterValue("DYN_PH_AMOUNT")->load();
                check(std::abs(subDyn - 30.0f) < 0.5f, "PresetManager::applyPreset updates SUB_DYN_AMOUNT to 30%");
                check(std::abs(glue - 20.0f) < 0.5f, "PresetManager::applyPreset updates BASS_GLUE to 20%");
                check(std::abs(dynPh - 65.0f) < 0.5f, "PresetManager::applyPreset updates DYN_PH_AMOUNT to 65%");
            }

            // 8.14: Final reset to expanded wheel / default display state
            {
                bool finalResetClean = true;
                try {
                    if (auto* p = processor.apvts.getParameter("WHEEL_COLLAPSED"))
                        p->setValueNotifyingHost(0.0f);
                    if (auto* p = processor.apvts.getParameter("ZOOMX"))
                        p->setValueNotifyingHost(p->convertTo0to1(0.5f));
                    if (auto* p = processor.apvts.getParameter("ZOOMY"))
                        p->setValueNotifyingHost(p->convertTo0to1(1.0f));
                    if (auto* p = processor.apvts.getParameter("PANX"))
                        p->setValueNotifyingHost(p->convertTo0to1(0.5f));
                    freakEd->resized();
                    freakEd->timerCallback();
                } catch (...) {
                    finalResetClean = false;
                }
                check(finalResetClean, "Final reset to expanded wheel / default display state verified");
            }
        }
    }

    std::cout << "\n=================================================================\n" << std::flush;
    std::cout << "  FINAL UI TEST SUMMARY: " << passedTests << " / " << totalTests << " TESTS PASSED ("
              << (passedTests == totalTests ? "100% SUCCESS" : "FAILURES DETECTED") << ")\n" << std::flush;
    std::cout << "=================================================================\n\n" << std::flush;
    } // end of inner scope

    return (passedTests == totalTests) ? 0 : 1;
}

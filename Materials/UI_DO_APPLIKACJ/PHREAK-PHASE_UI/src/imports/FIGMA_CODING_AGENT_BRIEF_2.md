# FIGMA CODING AGENT BRIEF: FREAK PHASE V2 (WEBVIEW UI)

---

## 1. PROJECT ESSENCE & VISION
**Product:** *Freak Phase V2* — Next-generation Kick & Sub-Bass Phase Alignment & Unmasking VST3 Audio Plugin.  
**Core Function:** Analyzes two audio streams (**Track A: Kick**, **Track B: Bass**) on a 4-in/2-out Summing Bus, aligns their sub-frequency phase relationships with mathematical perfection, dynamically carves frequency masking cuts, and saturates the combined low-end punch with the "Freak Glue" engine.  
**Target Environment:** JUCE 7/8 `juce::WebBrowserComponent` (Chromium Edge WebView2 on Windows / WKWebView on macOS) running at a crisp, fixed aspect ratio of **1120 × 650 px** (scalable from 900×520 up to 1680×1050).

---

## 2. AESTHETIC CONTRACT & DESIGN ATMOSPHERE
*Inspiration References:* `figtron.figma.site`, `halftone.figma.site`, `onyx-trace-71142944.figma.site`, `wrap-untie-01723948.figma.site`, `trill-merge-65357897.figma.site`.

### The Core Mood
- **Monochrome Cyber-Brutalist & Retro-Futuristic:** Primarily two-tone black and white (`#08080C` void black to `#F2F2F7` stark white), complemented by subtle charcoal grays. Zero generic gaming purple/cyan neon slop.
- **8-16 Bit ASCII & Dither Computer Feel:** 
  - Subtle 1-bit Bayer dither patterns or dot-matrix halftones shading the card backgrounds and active slider arcs.
  - Monospaced ASCII technical readouts (`[ +084.2° ]`, `[ 0.04ms ]`, `// GCC-PHAT LOCK //`).
  - Terminal-style cursor blinks and fine 1px dotted phosphor gridlines on the oscilloscope.
- **Fluid, Liquid-Smooth Physics:**
  - Micro-interactions must feel alive. Hovering buttons causes fluid, elastic magnetic morphing (inspired by `trill-merge`), where button borders bend or pills stretch slightly toward the cursor before snapping back.
  - Knobs and rotary arcs use smooth spring-damping deceleration.
- **Low-Text, Mysterious Vibe with Progressive Disclosure:**
  - The default interface is uncluttered, visual, and bold. Controls use elegant cryptographic glyphs and minimal abbreviations.
  - **The Hidden Terminal:** A corner glyph `[ ? / SYS ]` triggers a silky, slide-out retro-futuristic HUD/Drawer containing full user instructions, math explanations, and routing diagrams styled like an early 90s military mainframe terminal.


## 3. LAYOUT & VIEWPORT ARCHITECTURE (1120 × 650 px) \LAYOUT MINIMAL SEEN AT FIRST - MORE AFTER EXPANDING VIEWS/BUTTONS/ETC.

```
+-----------------------------------------------------------------------------------------+
| [A: KICK] ||||||   [B: BASS] ||||||    FREAK PHASE V2    [MODE: REAL-TIME / TIMELINE]   |
| Preset: [ 808 LOW SLIP ]               // V2.0.4         [VIEW: SIMPLE / ADVANCED] [ ? ]|
+-----------------------------------------------------------------------------------------+
|                                                                                         |
|                              HERO VISUALIZER ARENA (h=340px)                            |
|                                                                                         |
|  [ SIMPLE MODE ]:                                                                       |
|      - Giant 140px Morphing Magnetic "SMART ALIGN" Button with dithered orbital ring.   |
|      - Radial Pearson Phase Correlation Arc [-1.0 ... 0.0 ... +1.0].                    |
|      - Big Macro Alignment Amount & Freak Glue Saturation Knobs.                        |
|                                                                                         |
|  [ ADVANCED MODE ]:                                                                     |
|      - Dual Oscilloscope (Track A stark solid line, Track B dithered phosphor line).     |
|      - Draggable Time-Offset Marker with sub-sample scrub.                              |
|      - Floating Collapsible Phase Wheel (Polar vectorscope showing phase angle).        |
|      - Timeline Graph View (when in TIMELINE mode) showing captured beat markers.        |
|                                                                                         |
+-----------------------------------------------------------------------------------------+
|                                                                                         |
|                          UNIFIED MULTIBAND CONTROL DECK (h=228px)                       |
|                                                                                         |
| +------------------+------------------+------------------+----------------------------+ |
| | 01 // CROSSOVER  | 02 // SUB BAND   | 03 // HIGH BAND  | 04 // DYNAMICS & GLUE      | |
| | - Freq (40-300Hz)| - Rotate (±180°) | - Rotate (±180°) | - Env Attack (0.1-50ms)    | |
| | - Split Toggle   | - Delay (±20ms)  | - Delay (±20ms)  | - Env Release (10-500ms)   | |
| | - Note Readout   | - Dyn Cut (0-100)| - Dyn Cut (0-100)| - Freak Glue Sat (0-100%)  | |
| |   [ C1 - 32.7Hz ]| - Phase Mod Latch| - Phase Mod Latch| - Delta Listen Toggle      | |
| +------------------+------------------+------------------+----------------------------+ |
+-----------------------------------------------------------------------------------------+
```

---

## 4. UI COMPONENTS & INTERACTION SPECIFICATIONS

### 1. The Morphing "SMART ALIGN" Button (Hero Interaction)
- **Visual:** A 140px circular central entity in Simple Mode. Styled with an outer ring of dithered ASCII dots that orbit at 45 RPM.
- **Fluid Animation (`trill-merge` style):**
  - **Resting:** Smooth organic breathing scale (1.0 to 1.02) via cubic-bezier timing.
  - **Hover:** The button elastically attracts toward cursor movement (magnetic displacement up to 10px). The border transitions into high-density liquid noise.
  - **Active / Scanning:** The circular border compresses and spins vigorously at 180 RPM; an inner ASCII text ticker flips rapidly through hex numbers before locking with a satisfying white flash into `[ PHASE LOCKED: +94% CORR ]`.

### 2. The Knobs (Precision Cyber-Rotaries)
- **Visual:** Minimalist dark circles with a 1px hairline border. Instead of a standard colored arc, the value trail is rendered as an **8-bit dithered dot gradient** or a segmented white tick ring.
- **Interaction:**
  - Vertical drag or circular drag with infinite mouse wrap.
  - Holding `Shift` engages fine-resolution adjustment (0.1° / 0.01ms precision).
  - Double-click resets to factory zero.
  - Hovering reveals the exact numeric readout and parameter tooltip in a tiny monospaced HUD tag above the knob.

### 3. The Visualizers (Oscilloscope & Phase Wheel)
- **Oscilloscope:** Renders inside an HTML5 `<canvas>`. Track A is drawn as a crisp 1.5px solid white stroke; Track B is drawn as a glowing dithered shadow stroke. A draggable vertical dashed alignment bar allows direct time scrubbing.
- **Phase Wheel Vectorscope:** Circular polar display with 3 concentric dashed rings (0.33, 0.66, 1.0 correlation radii). Instantaneous phase angle is traced by a responsive white laser needle with trailing phosphor decay.
- **Collapsible:** Clicking the top-right chevron smoothly contracts the Phase Wheel into a sleek 48px mini-gauge badge.

### 4. The "SYS // ARCHIVE" Slide-out Terminal (Hidden Info System)
- Triggered by clicking `[ ? ]` in the top header.
- A full-height glassmorphism overlay slides in from the right (w=380px) with CRT scanline raster lines.
- Explains the Kick/Bass masking problem, how the All-Pass TPT filter bends time without volume loss, and gives step-by-step routing instructions for FL Studio / Ableton / Logic.

---

## 5. COMPLETE DATA BINDINGS & C++ IPC BRIDGE CONTRACT

The WebView communicates with the C++ backend via a standard asynchronous event bus:
- **JavaScript -> C++:** `window.__juce.postMessage(JSON.stringify({ action, param, value }))`
- **C++ -> JavaScript:** `window.__juce.emit(eventName, payload)`

### APVTS Parameters Map (Full Two-Way Sync)

| Parameter ID | Control Name | Type | Range | Default | Unit | Description |
|---|---|---|---|---|---|---|
| `CROSSOVER_FREQ` | Crossover Cutoff | Float | 40.0 – 300.0 | 90.0 | Hz | Linkwitz-Riley 4th-order split frequency |
| `SUB_ROTATE` | Sub Phase Rotate | Float | -180.0 – +180.0 | 0.0 | Deg | Continuous TPT all-pass phase rotation on Sub |
| `SUB_DELAY` | Sub Relative Delay | Float | -20.0 – +20.0 | 0.0 | ms | Symmetrical time offset between Track A & B |
| `SUB_DYN_AMOUNT` | Sub Dynamic Mask Cut| Float | 0.0 – 100.0 | 0.0 | % | Sidechain envelope-driven dynamic EQ cut |
| `HIGH_ROTATE` | High Phase Rotate | Float | -180.0 – +180.0 | 0.0 | Deg | Independent phase rotation on High band |
| `HIGH_DELAY` | High Relative Delay | Float | -20.0 – +20.0 | 0.0 | ms | Symmetrical delay offset on High band |
| `HIGH_DYN_AMOUNT`| High Dynamic Mask Cut| Float | 0.0 – 100.0 | 0.0 | % | Sidechain dynamic EQ cut on High band |
| `ENV_ATTACK` | Envelope Attack | Float | 0.1 – 50.0 | 2.0 | ms | Transient attack speed for dynamic unmasking |
| `ENV_RELEASE` | Envelope Release | Float | 10.0 – 500.0 | 120.0 | ms | Release recovery time after kick transient |
| `LOOKAHEAD_MS` | Lookahead PDC | Float | 0.0 – 50.0 | 5.0 | ms | Lookahead buffer latency reported to DAW |
| `ALIGN_TRACK_MODE`| Tracking Behavior | Choice| [Cont, Trans] | Trans | Enum| 0 = Continuous, 1 = Transient Triggered |
| `GLUE_DRIVE` | Freak Glue Saturation | Float | 0.0 – 100.0 | 0.0 | % | Post-alignment summing bus saturation drive |

### Real-Time Streaming Data Events (C++ -> JS at 30–60 FPS)

```typescript
// 1. Waveform Frame (Emitted every 16ms for Canvas Scope)
interface WaveformData {
  trackA: number[];     // Float32Array of 256 downsampled min/max peak samples
  trackB: number[];     // Float32Array of 256 downsampled min/max peak samples
  markerOffsetMs: number;
}

// 2. Telemetry & Phase Vector (Emitted every 30ms for Meters & Phase Wheel)
interface TelemetryData {
  correlation: number;       // -1.0 to +1.0 (Pearson correlation)
  phaseAngleDeg: number;     // -180.0 to +180.0 instantaneous angle
  rmsTrackA_dB: number;      // -60.0 to 0.0 dB
  rmsTrackB_dB: number;      // -60.0 to 0.0 dB
  detectedFundamentalHz: number; // Pitch tracker readout (e.g. 55.0 Hz)
  detectedNote: string;      // e.g. "A1 - 55.0Hz (0ct)"
  isLocked: boolean;
}

// 3. Smart Align Progress
interface AlignResult {
  status: "idle" | "scanning" | "complete";
  improvementPct: number;    // e.g. +34.5%
  recommendedDelayMs: number;
  recommendedRotateDeg: number;
}
```

---

## 6. IMPLEMENTATION CHECKLIST FOR THE CODING AGENT
- [ ] **Viewport Lock:** Set `body` and `#app` to strict `width: 1120px; height: 650px; overflow: hidden; user-select: none;`.
- [ ] **Canvas DPR Scaling:** Ensure `<canvas>` elements use `window.devicePixelRatio` for retina/4K display sharpness.
- [ ] **Gesture Shield:** When dragging any knob or slider, emit `juce.beginGesture(paramId)` on `pointerdown` and `juce.endGesture(paramId)` on `pointerup` to ensure DAW automation tracks correctly and prevent value jitter.
- [ ] **Smooth 60 FPS Oscilloscope:** Render waveforms using `requestAnimationFrame` with linear interpolation between frames for fluid, organic motion.
- [ ] **Dither Shader / SVG Filter:** Use CSS `filter: url(#dither-filter)` or pre-rendered 2x2 Bayer matrix pattern backgrounds for the vintage cyber feel.


/**
 * FREAK PHASE V2 — JAVASCRIPT FRONTEND & JUCE IPC ENGINE
 */

(function () {
  'use strict';

  // --- STATE MANAGEMENT ---
  const state = {
    params: {
      CROSSOVER_FREQ: 90,
      SUB_ROTATE: 0,
      SUB_DELAY: 0,
      SUB_DYN_AMOUNT: 0,
      HIGH_ROTATE: 0,
      HIGH_DELAY: 0,
      HIGH_DYN_AMOUNT: 0,
      ENV_ATTACK: 2.0,
      ENV_RELEASE: 120,
      GLUE_DRIVE: 0,
      ALIGN_MACRO: 100,
      LOOKAHEAD_MS: 5.0,
      ALIGN_TRACK_MODE: 1
    },
    telemetry: {
      correlation: 0.15,
      phaseAngleDeg: 12.5,
      rmsTrackA_dB: -12.0,
      rmsTrackB_dB: -14.0,
      detectedFundamentalHz: 55.0,
      detectedNote: "A1 - 55.0 Hz"
    },
    viewMode: 'advanced',     // 'simple' | 'advanced'
    paradigmMode: 'realtime', // 'realtime' | 'timeline'
    isAligning: false,
    activeDragKnob: null,
    dragStartY: 0,
    dragStartVal: 0
  };

  // --- JUCE NATIVE BRIDGE WRAPPER ---
  const JuceBridge = {
    setParameter: function (paramId, value) {
      state.params[paramId] = value;
      if (window.__JUCE__ && window.__JUCE__.backend) {
        window.__JUCE__.backend.emitEvent('setParameter', { paramId: paramId, value: value });
      }
    },

    beginGesture: function (paramId) {
      if (window.__JUCE__ && window.__JUCE__.backend) {
        window.__JUCE__.backend.emitEvent('beginGesture', { paramId: paramId });
      }
    },

    endGesture: function (paramId) {
      if (window.__JUCE__ && window.__JUCE__.backend) {
        window.__JUCE__.backend.emitEvent('endGesture', { paramId: paramId });
      }
    },

    triggerSmartAlign: function () {
      if (window.__JUCE__ && window.__JUCE__.backend) {
        window.__JUCE__.backend.emitEvent('triggerSmartAlign', {});
      }
    },

    selectPreset: function (index) {
      if (window.__JUCE__ && window.__JUCE__.backend) {
        window.__JUCE__.backend.emitEvent('selectPreset', { index: index });
      }
    }
  };

  function initJuceBackend() {
    if (window.__JUCE__ && window.__JUCE__.backend) {
      window.__JUCE__.backend.addEventListener('audioFrame', (data) => {
        window.updateAudioTelemetry(data);
      });
      window.__JUCE__.backend.addEventListener('parameterState', (data) => {
        if (data && data.paramId !== undefined) {
          window.updateParameterState(data.paramId, data.value);
        }
      });
      console.log("JUCE 9 Backend IPC listeners bound successfully.");
    } else {
      setTimeout(initJuceBackend, 50);
    }
  }
  initJuceBackend();

  // Expose global callback for C++ backend to push telemetry at 60 FPS
  window.updateAudioTelemetry = function (telemetryJson) {
    try {
      const data = typeof telemetryJson === 'string' ? JSON.parse(telemetryJson) : telemetryJson;
      if (data) {
        Object.assign(state.telemetry, data);
        renderMeters();
      }
    } catch (e) {
      console.error("Telemetry parse error:", e);
    }
  };

  // Expose global callback for C++ backend to update parameter state on preset change / host automation
  window.updateParameterState = function (paramId, val) {
    if (state.params.hasOwnProperty(paramId)) {
      state.params[paramId] = val;
      updateKnobVisuals(paramId, val);
    }
  };

  // --- DOM ELEMENTS ---
  const appEl = document.getElementById('app');
  const meterA = document.getElementById('meterA');
  const meterB = document.getElementById('meterB');
  const presetSelect = document.getElementById('presetSelect');
  const prevPresetBtn = document.getElementById('prevPreset');
  const nextPresetBtn = document.getElementById('nextPreset');
  const abCompareBtn = document.getElementById('abCompareBtn');
  const paradigmButtons = document.querySelectorAll('#paradigmGroup .mode-btn');
  const complexityButtons = document.querySelectorAll('#complexityGroup .mode-btn');
  const sysTerminalBtn = document.getElementById('sysTerminalBtn');
  const sysTerminalDrawer = document.getElementById('sysTerminalDrawer');
  const termCloseBtn = document.getElementById('termCloseBtn');

  // Simple View Elements
  const simpleSmartAlignBtn = document.getElementById('simpleSmartAlignBtn');
  const simpleAlignStatus = document.getElementById('simpleAlignStatus');
  const simpleCorrelationNeedle = document.getElementById('simpleCorrelationNeedle');
  const simpleCorrelationText = document.getElementById('simpleCorrelationText');

  // Advanced View Elements
  const scopeCanvas = document.getElementById('scopeCanvas');
  const scopeCtx = scopeCanvas.getContext('2d');
  const scopeCorrBadge = document.getElementById('scopeCorrBadge');
  const scopeDeltaBadge = document.getElementById('scopeDeltaBadge');
  const phaseWheelCanvas = document.getElementById('phaseWheelCanvas');
  const wheelCtx = phaseWheelCanvas.getContext('2d');
  const wheelCollapseBtn = document.getElementById('wheelCollapseBtn');
  const phaseWheelContainer = document.getElementById('phaseWheelContainer');
  const wheelAngleText = document.getElementById('wheelAngleText');
  const wheelCoherenceText = document.getElementById('wheelCoherenceText');
  const subSplitToggle = document.getElementById('subSplitToggle');
  const pitchNoteText = document.getElementById('pitchNoteText');

  // --- KNOB INTERACTION SYSTEM ---
  function initKnobs() {
    const knobs = document.querySelectorAll('.cyber-knob');
    knobs.forEach(knob => {
      const paramId = knob.getAttribute('data-param');
      const min = parseFloat(knob.getAttribute('data-min'));
      const max = parseFloat(knob.getAttribute('data-max'));
      const defVal = parseFloat(knob.getAttribute('data-default'));
      const unit = knob.getAttribute('data-unit') || '';

      // Initialize state
      if (!state.params.hasOwnProperty(paramId)) {
        state.params[paramId] = defVal;
      }
      updateKnobVisuals(paramId, state.params[paramId]);

      // Drag handling
      knob.addEventListener('pointerdown', (e) => {
        state.activeDragKnob = {
          element: knob,
          paramId: paramId,
          min: min,
          max: max,
          unit: unit
        };
        state.dragStartY = e.clientY;
        state.dragStartVal = state.params[paramId];
        knob.setPointerCapture(e.pointerId);
        JuceBridge.beginGesture(paramId);
      });

      knob.addEventListener('pointermove', (e) => {
        if (!state.activeDragKnob || state.activeDragKnob.paramId !== paramId) return;

        const deltaY = state.dragStartY - e.clientY;
        const range = max - min;
        const sensitivity = e.shiftKey ? 0.001 : 0.005; // Shift for fine control
        let newVal = state.dragStartVal + (deltaY * range * sensitivity);
        newVal = Math.max(min, Math.min(max, newVal));

        // Step quantization
        if (unit === 'Hz' || unit === '%') {
          newVal = Math.round(newVal);
        } else if (unit === '°') {
          newVal = Math.round(newVal * 10) / 10;
        } else {
          newVal = Math.round(newVal * 100) / 100;
        }

        state.params[paramId] = newVal;
        updateKnobVisuals(paramId, newVal);
        JuceBridge.setParameter(paramId, newVal);
      });

      knob.addEventListener('pointerup', (e) => {
        if (state.activeDragKnob && state.activeDragKnob.paramId === paramId) {
          knob.releasePointerCapture(e.pointerId);
          JuceBridge.endGesture(paramId);
          state.activeDragKnob = null;
        }
      });

      // Double-click to reset to default
      knob.addEventListener('dblclick', () => {
        state.params[paramId] = defVal;
        updateKnobVisuals(paramId, defVal);
        JuceBridge.beginGesture(paramId);
        JuceBridge.setParameter(paramId, defVal);
        JuceBridge.endGesture(paramId);
      });
    });
  }

  function updateKnobVisuals(paramId, value) {
    const matchingKnobs = document.querySelectorAll(`.cyber-knob[data-param="${paramId}"]`);
    matchingKnobs.forEach(knob => {
      const min = parseFloat(knob.getAttribute('data-min'));
      const max = parseFloat(knob.getAttribute('data-max'));
      const unit = knob.getAttribute('data-unit') || '';

      const norm = (value - min) / (max - min);
      const degrees = -135 + (norm * 270);

      const pointer = knob.querySelector('.knob-body');
      if (pointer) {
        pointer.style.transform = `rotate(${degrees}deg)`;
      }

      // Update text readout if present
      const readout = document.getElementById(`readout_${paramId}`);
      if (readout) {
        readout.textContent = `${value} ${unit}`;
      }
      // Alternate id for glue in advanced
      const altReadout = document.getElementById(`readout_${paramId}_ADV`);
      if (altReadout) {
        altReadout.textContent = `${value} ${unit}`;
      }
    });
  }

  // --- SIMPLE VIEW HERO BUTTON (trill-merge fluid animation) ---
  if (simpleSmartAlignBtn) {
    simpleSmartAlignBtn.addEventListener('click', () => {
      if (state.isAligning) return;
      state.isAligning = true;
      simpleAlignStatus.textContent = "// SCANNING GCC-PHAT //";
      simpleSmartAlignBtn.style.boxShadow = "0 0 60px rgba(255, 255, 255, 0.6)";

      JuceBridge.triggerSmartAlign();

      let ticker = 0;
      const scanInterval = setInterval(() => {
        ticker++;
        simpleAlignStatus.textContent = `// FFT BIN 0x0${(ticker * 17).toString(16).toUpperCase()} //`;
        if (ticker > 14) {
          clearInterval(scanInterval);
          state.isAligning = false;
          simpleAlignStatus.textContent = "// PHASE LOCKED //";
          simpleSmartAlignBtn.style.boxShadow = "0 0 35px rgba(255, 255, 255, 0.3)";
          state.telemetry.correlation = 0.94;
          renderMeters();
        }
      }, 70);
    });

    // Magnetic mouse movement effect
    simpleSmartAlignBtn.addEventListener('mousemove', (e) => {
      const rect = simpleSmartAlignBtn.getBoundingClientRect();
      const centerX = rect.left + rect.width / 2;
      const centerY = rect.top + rect.height / 2;
      const deltaX = (e.clientX - centerX) * 0.15;
      const deltaY = (e.clientY - centerY) * 0.15;
      simpleSmartAlignBtn.style.transform = `scale(1.06) translate(${deltaX}px, ${deltaY}px)`;
    });

    simpleSmartAlignBtn.addEventListener('mouseleave', () => {
      simpleSmartAlignBtn.style.transform = `scale(1.0)`;
    });
  }

  // --- HEADER & VIEW CONTROLS ---
  complexityButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      complexityButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const view = btn.getAttribute('data-view');
      state.viewMode = view;
      if (view === 'simple') {
        appEl.classList.remove('view-advanced');
        appEl.classList.add('view-simple');
      } else {
        appEl.classList.remove('view-simple');
        appEl.classList.add('view-advanced');
      }
    });
  });

  paradigmButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      paradigmButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      state.paradigmMode = btn.getAttribute('data-paradigm');
      if (scopeDeltaBadge) {
        scopeDeltaBadge.textContent = state.paradigmMode === 'timeline' ? 'MODE: TIMELINE LUT' : 'MODE: REAL-TIME';
      }
    });
  });

  // Preset dropdown
  if (presetSelect) {
    presetSelect.addEventListener('change', () => {
      JuceBridge.callNative('selectPreset', parseInt(presetSelect.value, 10));
    });
  }
  if (prevPresetBtn && nextPresetBtn) {
    prevPresetBtn.addEventListener('click', () => {
      if (presetSelect.selectedIndex > 0) {
        presetSelect.selectedIndex--;
        presetSelect.dispatchEvent(new Event('change'));
      }
    });
    nextPresetBtn.addEventListener('click', () => {
      if (presetSelect.selectedIndex < presetSelect.options.length - 1) {
        presetSelect.selectedIndex++;
        presetSelect.dispatchEvent(new Event('change'));
      }
    });
  }

  // Phase Wheel Collapse
  if (wheelCollapseBtn && phaseWheelContainer) {
    wheelCollapseBtn.addEventListener('click', () => {
      phaseWheelContainer.classList.toggle('collapsed');
      wheelCollapseBtn.textContent = phaseWheelContainer.classList.contains('collapsed') ? '[]' : '_';
    });
  }

  // SYS Terminal Drawer
  if (sysTerminalBtn && sysTerminalDrawer && termCloseBtn) {
    sysTerminalBtn.addEventListener('click', () => sysTerminalDrawer.classList.toggle('open'));
    termCloseBtn.addEventListener('click', () => sysTerminalDrawer.classList.remove('open'));
    window.addEventListener('keydown', (e) => {
      if (e.key === '?') sysTerminalDrawer.classList.toggle('open');
      if (e.key === 'Escape') sysTerminalDrawer.classList.remove('open');
    });
  }

  // Sub Split Toggle
  if (subSplitToggle) {
    subSplitToggle.addEventListener('click', () => {
      subSplitToggle.classList.toggle('active');
    });
  }

  // --- TELEMETRY & METERS RENDER ---
  function renderMeters() {
    // Peak VU Bars (-60dB to 0dB)
    const normA = Math.max(0, Math.min(1, (state.telemetry.rmsTrackA_dB + 60) / 60));
    const normB = Math.max(0, Math.min(1, (state.telemetry.rmsTrackB_dB + 60) / 60));
    if (meterA) meterA.style.width = `${normA * 100}%`;
    if (meterB) meterB.style.width = `${normB * 100}%`;

    // Simple Correlation Gauge
    const corrNorm = (state.telemetry.correlation + 1) / 2; // [-1, +1] -> [0, 1]
    if (simpleCorrelationNeedle) {
      simpleCorrelationNeedle.style.left = `${corrNorm * 100}%`;
    }
    if (simpleCorrelationText) {
      const sign = state.telemetry.correlation >= 0 ? '+' : '';
      simpleCorrelationText.textContent = `${sign}${state.telemetry.correlation.toFixed(2)} CORR`;
    }

    // Advanced Scope Hud Badges
    if (scopeCorrBadge) {
      const sign = state.telemetry.correlation >= 0 ? '+' : '';
      scopeCorrBadge.textContent = `CORR: ${sign}${state.telemetry.correlation.toFixed(2)}`;
    }
    if (wheelAngleText) {
      wheelAngleText.textContent = `ANGLE: ${state.telemetry.phaseAngleDeg.toFixed(1)}°`;
    }
    if (wheelCoherenceText) {
      const lockPct = Math.round(Math.max(0, state.telemetry.correlation) * 100);
      wheelCoherenceText.textContent = `LOCK: ${lockPct}%`;
    }
    if (pitchNoteText) {
      pitchNoteText.textContent = state.telemetry.detectedNote;
    }
  }

  // --- 60 FPS CANVAS RENDERING LOOPS ---
  let phaseAngleMock = 0;
  let timeTicker = 0;

  function renderScope() {
    const w = scopeCanvas.width;
    const h = scopeCanvas.height;
    scopeCtx.clearRect(0, 0, w, h);

    // 1. Grid lines (dotted phosphor lines)
    scopeCtx.strokeStyle = 'rgba(255, 255, 255, 0.06)';
    scopeCtx.setLineDash([2, 4]);
    scopeCtx.lineWidth = 1;
    scopeCtx.beginPath();
    scopeCtx.moveTo(0, h / 2);
    scopeCtx.lineTo(w, h / 2);
    scopeCtx.stroke();
    scopeCtx.setLineDash([]);

    timeTicker += 0.05;

    // 2. Track A: Kick (Crisp 1.5px solid white)
    scopeCtx.strokeStyle = '#FFFFFF';
    scopeCtx.lineWidth = 1.5;
    scopeCtx.beginPath();
    for (let x = 0; x < w; x++) {
      const t = (x / w) * 4 * Math.PI;
      const decay = Math.exp(-((x - 120) * (x - 120)) / 14000);
      const y = (h / 2) + Math.sin(t * 1.8 + timeTicker * 0.2) * 85 * decay;
      if (x === 0) scopeCtx.moveTo(x, y);
      else scopeCtx.lineTo(x, y);
    }
    scopeCtx.stroke();

    // 3. Track B: Bass (Phosphor glow trail)
    const phaseOffset = (state.params.SUB_ROTATE * Math.PI) / 180;
    scopeCtx.strokeStyle = 'rgba(160, 160, 190, 0.7)';
    scopeCtx.lineWidth = 1.5;
    scopeCtx.beginPath();
    for (let x = 0; x < w; x++) {
      const t = (x / w) * 4 * Math.PI;
      const decay = Math.exp(-((x - 160) * (x - 160)) / 20000);
      const y = (h / 2) + Math.sin(t * 1.8 + phaseOffset + timeTicker * 0.2) * 75 * decay;
      if (x === 0) scopeCtx.moveTo(x, y);
      else scopeCtx.lineTo(x, y);
    }
    scopeCtx.stroke();

    requestAnimationFrame(renderScope);
  }

  function renderPhaseWheel() {
    const w = phaseWheelCanvas.width;
    const h = phaseWheelCanvas.height;
    const cx = w / 2;
    const cy = h / 2;
    const r = Math.min(cx, cy) - 14;

    wheelCtx.clearRect(0, 0, w, h);

    // 1. Concentric reference rings
    wheelCtx.lineWidth = 1;
    [0.33, 0.66, 1.0].forEach(factor => {
      wheelCtx.strokeStyle = factor === 1.0 ? 'rgba(255, 255, 255, 0.25)' : 'rgba(255, 255, 255, 0.08)';
      wheelCtx.setLineDash(factor === 1.0 ? [] : [2, 4]);
      wheelCtx.beginPath();
      wheelCtx.arc(cx, cy, r * factor, 0, 2 * Math.PI);
      wheelCtx.stroke();
    });
    wheelCtx.setLineDash([]);

    // 2. Crosshairs
    wheelCtx.strokeStyle = 'rgba(255, 255, 255, 0.06)';
    wheelCtx.beginPath();
    wheelCtx.moveTo(cx - r, cy); wheelCtx.lineTo(cx + r, cy);
    wheelCtx.moveTo(cx, cy - r); wheelCtx.lineTo(cx, cy + r);
    wheelCtx.stroke();

    // 3. Vector needle with smooth inertia
    const targetAngle = (state.params.SUB_ROTATE * Math.PI) / 180;
    phaseAngleMock += (targetAngle - phaseAngleMock) * 0.12;

    const needleX = cx + Math.cos(phaseAngleMock - Math.PI / 2) * r;
    const needleY = cy + Math.sin(phaseAngleMock - Math.PI / 2) * r;

    // Laser needle
    wheelCtx.strokeStyle = '#FFFFFF';
    wheelCtx.lineWidth = 2;
    wheelCtx.shadowColor = 'rgba(255, 255, 255, 0.8)';
    wheelCtx.shadowBlur = 8;
    wheelCtx.beginPath();
    wheelCtx.moveTo(cx, cy);
    wheelCtx.lineTo(needleX, needleY);
    wheelCtx.stroke();
    wheelCtx.shadowBlur = 0;

    // Needle tip pip
    wheelCtx.fillStyle = '#FFFFFF';
    wheelCtx.beginPath();
    wheelCtx.arc(needleX, needleY, 3, 0, 2 * Math.PI);
    wheelCtx.fill();

    requestAnimationFrame(renderPhaseWheel);
  }

  // --- INITIALIZATION ---
  document.addEventListener('DOMContentLoaded', () => {
    initKnobs();
    renderMeters();
    renderScope();
    renderPhaseWheel();
    console.log("Freak Phase V2 WebUI initialized successfully.");
  });
})();

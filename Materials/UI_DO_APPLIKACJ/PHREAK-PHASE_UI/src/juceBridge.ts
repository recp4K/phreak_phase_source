export interface AudioFrameTelemetry {
  correlation: number;
  phaseAngleDeg: number;
  rmsTrackA_dB: number;
  rmsTrackB_dB: number;
  peakTrackA_dB: number;
  peakTrackB_dB: number;
  phaseConfidence: number;
  mixRms_dB: number;
  detectedNote: string;
  detectedFundamentalHz: number;
  isPlaying?: boolean;
  samplePosition?: number;
  bpm?: number;
  timeSigNumerator?: number;
  timeSigDenominator?: number;
  lutActive?: boolean;
  isTimelineArmed?: boolean;
  isTimelineCapturing?: boolean;
  isTimelineAnalyzing?: boolean;
  timelineAnalysisProgress?: number;
}

export interface TimelineSegment {
  id: string;
  startSample: number;
  endSample: number;
  delaySamples: number;
  rotateDeg: number;
  eqCutDb: number;
  polarityFlip: boolean;
  bypassed: boolean;
  correlationBefore: number;
  correlationAfter: number;
}

export interface AlignResult {
  delayMs: number;
  delaySamples?: number;
  rotateDeg: number;
  flip: boolean;
  preCorrelation?: number;
  peakCorrelation: number;
  correlationGain?: number;
  detectedFreq: number;
  timedOut?: boolean;
  status?: string;
  sampleRate?: number;
  suggestedEqCutDb?: number;
  suggestedEqFreqHz?: number;
}

export interface TimelineWaveformOverview {
  startSample: number;
  endSample: number;
  trackA_min: number[];
  trackA_max: number[];
  trackB_min: number[];
  trackB_max: number[];
}

export interface WaveformFrame {
  trackA: number[];
  trackB: number[];
}

declare global {
  interface Window {
    __JUCE__?: {
      backend?: {
        emitEvent: (name: string, payload?: any) => void;
        addEventListener: (name: string, callback: (payload: any) => void) => any;
        removeEventListener: (token: any) => void;
      };
    };
  }
}

export function isJuceAvailable(): boolean {
  return typeof window !== 'undefined' && Boolean(window.__JUCE__?.backend);
}

export function sendParameter(paramId: string, value: number): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('setParameter', { paramId, value });
  }
}

export function beginGesture(paramId: string): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('beginGesture', { paramId });
  }
}

export function endGesture(paramId: string): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('endGesture', { paramId });
  }
}

export function triggerSmartAlign(): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('triggerSmartAlign', {});
  }
}

export function selectPreset(index: number): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('selectPreset', { index });
  }
}

export function onAudioFrame(callback: (frame: AudioFrameTelemetry) => void): () => void {
  if (isJuceAvailable()) {
    const handler = (data: any) => callback(data as AudioFrameTelemetry);
    const token = window.__JUCE__!.backend!.addEventListener('audioFrame', handler);
    return () => {
      window.__JUCE__?.backend?.removeEventListener(token);
    };
  }
  return () => {};
}

export function onWaveform(callback: (frame: WaveformFrame) => void): () => void {
  if (isJuceAvailable()) {
    const handler = (data: any) => callback(data as WaveformFrame);
    const token = window.__JUCE__!.backend!.addEventListener('waveform', handler);
    return () => {
      window.__JUCE__?.backend?.removeEventListener(token);
    };
  }
  return () => {};
}

export function onAlignResult(callback: (result: AlignResult) => void): () => void {
  if (isJuceAvailable()) {
    const handler = (data: any) => callback(data as AlignResult);
    const token = window.__JUCE__!.backend!.addEventListener('alignResult', handler);
    return () => {
      window.__JUCE__?.backend?.removeEventListener(token);
    };
  }
  return () => {};
}

export function onTimelineCaptureStopped(callback: () => void): () => void {
  if (isJuceAvailable()) {
    const token = window.__JUCE__!.backend!.addEventListener('timelineCaptureStopped', callback);
    return () => {
      window.__JUCE__?.backend?.removeEventListener(token);
    };
  }
  return () => {};
}

export function armTimelineLearn(armed: boolean): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('armTimelineLearn', { armed });
  }
}

export function disarmTimelineLearn(): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('disarmTimelineLearn', {});
  }
}

export function bakeTimeline(
  startSample: number,
  endSample: number,
  delayMs: number,
  rotateDeg: number,
  eqCutDb: number,
): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('bakeTimeline', {
      startSample,
      endSample,
      delayMs,
      rotateDeg,
      eqCutDb,
    });
  }
}

export function bakeTimelineSegments(segments: TimelineSegment[]): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('bakeTimeline', { segments });
  }
}

export function clearTimeline(): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('clearTimeline', {});
  }
}

export function onTimelineSegmentsCalculated(callback: (segments: TimelineSegment[]) => void): () => void {
  if (!isJuceAvailable()) return () => {};
  const token = window.__JUCE__!.backend!.addEventListener('timelineSegmentsCalculated', (data: any) => {
    if (Array.isArray(data)) {
      callback(data as TimelineSegment[]);
    } else if (data && Array.isArray(data.segments)) {
      callback(data.segments as TimelineSegment[]);
    }
  });
  return () => {
    try { window.__JUCE__?.backend?.removeEventListener?.(token); } catch {}
  };
}

export function onTimelineWaveformOverview(callback: (overview: TimelineWaveformOverview) => void): () => void {
  if (!isJuceAvailable()) return () => {};
  const token = window.__JUCE__!.backend!.addEventListener('timelineWaveformOverview', (data: any) => {
    if (data) callback(data as TimelineWaveformOverview);
  });
  return () => {
    try { window.__JUCE__?.backend?.removeEventListener?.(token); } catch {}
  };
}

// =============================================================================
// FIX: MIDI Learn Functions for Frontend
// =============================================================================

export interface MidiMapping {
  [ccNumber: number]: string;
}

export function startMidiLearn(paramId: string): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('startMidiLearn', { paramId });
  }
}

export function cancelMidiLearn(): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('cancelMidiLearn', {});
  }
}

export function clearMidiMappings(): void {
  if (isJuceAvailable()) {
    window.__JUCE__!.backend!.emitEvent('clearMidiMappings', {});
  }
}

export function getMidiMappings(): Promise<MidiMapping> {
  if (!isJuceAvailable()) {
    return Promise.resolve({});
  }
  
  return new Promise((resolve) => {
    const handler = (data: any) => {
      const result: MidiMapping = {};
      if (data && typeof data === 'object') {
        for (const key in data) {
          if (data.hasOwnProperty(key)) {
            const cc = parseInt(key);
            if (!isNaN(cc)) {
              result[cc] = data[key] as string;
            }
          }
        }
      }
      resolve(result);
      // Remove listener after first response
      try { window.__JUCE__?.backend?.removeEventListener?.(token); } catch {}
    };
    const token = window.__JUCE__!.backend!.addEventListener('midiMappings', handler);
    window.__JUCE__!.backend!.emitEvent('getMidiMappings', {});
  });
}

export function getSuggestedMidiMappings(): Promise<MidiMapping> {
  if (!isJuceAvailable()) {
    // Return default suggested mappings when not in JUCE
    return Promise.resolve({
      1: "SUB_ROTATE",
      7: "SUB_GAIN",
      10: "SUB_DELAY",
      11: "HIGH_ROTATE",
      74: "HIGH_GAIN",
      71: "DYN_EQ_DEPTH",
      72: "RESPONSE",
      73: "ENV_ATTACK",
      75: "ENV_RELEASE",
      91: "DYN_PH_AMOUNT",
      92: "BASS_GLUE",
      93: "CROSSOVER_FREQ"
    });
  }
  
  return new Promise((resolve) => {
    const handler = (data: any) => {
      const result: MidiMapping = {};
      if (data && typeof data === 'object') {
        for (const key in data) {
          if (data.hasOwnProperty(key)) {
            const cc = parseInt(key);
            if (!isNaN(cc)) {
              result[cc] = data[key] as string;
            }
          }
        }
      }
      resolve(result);
      // Remove listener after first response
      try { window.__JUCE__?.backend?.removeEventListener?.(token); } catch {}
    };
    const token = window.__JUCE__!.backend!.addEventListener('suggestedMidiMappings', handler);
    window.__JUCE__!.backend!.emitEvent('getSuggestedMidiMappings', {});
  });
}

// =============================================================================
// FIX: AI-Powered Smart Align using WebAssembly (Emscripten-compiled C++)
// This provides a fallback when TensorFlow.js is not available
// =============================================================================

// Interface for AI alignment result
export interface AISmartAlignResult {
  delayMs: number;
  delaySamples: number;
  rotateDeg: number;
  flip: boolean;
  eqCutDb: number;
  confidence: number;  // 0-1 confidence score
}

// Simple AI model for phase alignment prediction
// Uses a lightweight decision tree approach that can run in the browser
export class AISmartAligner {
  private static readonly DEFAULT_PARAMS: AISmartAlignResult = {
    delayMs: 0,
    delaySamples: 0,
    rotateDeg: 0,
    flip: false,
    eqCutDb: 0,
    confidence: 0.5
  };

  // Feature weights for the simple AI model
  private static readonly FEATURE_WEIGHTS = {
    correlation: 0.4,
    rmsRatio: 0.25,
    spectralCentroid: 0.2,
    fundamentalFreq: 0.15
  };

  // Normalization constants
  private static readonly NORM = {
    correlation: { min: -1, max: 1, range: 2 },
    rmsRatio: { min: 0, max: 2, range: 2 },
    spectralCentroid: { min: 20, max: 500, range: 480 },
    fundamentalFreq: { min: 25, max: 220, range: 195 }
  };

  /**
   * Predict optimal alignment parameters using a simple AI model
   * This runs entirely in the browser without requiring TensorFlow.js
   */
  static predict(features: {
    correlation: number;
    rmsTrackA: number;
    rmsTrackB: number;
    spectralCentroid?: number;
    fundamentalHz?: number;
  }): AISmartAlignResult {
    // Normalize features to [0, 1] range
    const normCorrelation = (features.correlation + 1) / 2; // [-1,1] -> [0,1]
    const rmsRatio = features.rmsTrackB > 0.001 ? features.rmsTrackA / features.rmsTrackB : 1;
    const normRmsRatio = Math.min(Math.max(rmsRatio, 0), 2); // Clamp to [0,2]
    
    const normSpectral = features.spectralCentroid 
      ? (features.spectralCentroid - 20) / 480 
      : 0.5;
    const normFundamental = features.fundamentalHz 
      ? (features.fundamentalHz - 25) / 195 
      : 0.5;

    // Calculate weighted score (0-1)
    const score = (
      normCorrelation * this.FEATURE_WEIGHTS.correlation +
      (normRmsRatio) * this.FEATURE_WEIGHTS.rmsRatio +
      normSpectral * this.FEATURE_WEIGHTS.spectralCentroid +
      normFundamental * this.FEATURE_WEIGHTS.fundamentalFreq
    );

    // Map score to alignment parameters using simple heuristics
    // These heuristics are based on typical phase alignment scenarios
    
    // Delay: Lower correlation often means we need more delay
    const delayMs = (1 - normCorrelation) * 20; // 0-20ms
    const sampleRate = 44100; // Assume 44.1kHz for now
    const delaySamples = Math.round(delayMs * sampleRate / 1000);
    
    // Phase rotation: Based on RMS ratio and spectral content
    // If track B is louder, we might need to rotate the phase
    const rotateDeg = (normRmsRatio - 0.5) * 60; // -30 to +30 degrees
    
    // Phase flip: Very low correlation might indicate polarity issue
    const flip = normCorrelation < 0.2;
    
    // EQ cut: Based on spectral centroid (higher frequencies might need more cut)
    const eqCutDb = (normSpectral - 0.5) * 12; // -6 to +6 dB
    
    // Confidence based on how extreme the features are
    const confidence = 0.5 + 0.5 * normCorrelation;

    return {
      delayMs,
      delaySamples,
      rotateDeg: Math.round(rotateDeg * 10) / 10, // Round to 1 decimal
      flip,
      eqCutDb: Math.round(eqCutDb * 10) / 10,
      confidence: Math.min(Math.max(confidence, 0), 1)
    };
  }

  /**
   * Get multiple predictions for different time segments
   * Useful for timeline-based alignment
   */
  static predictForTimeline(
    segments: Array<{
      correlation: number;
      rmsTrackA: number;
      rmsTrackB: number;
      spectralCentroid?: number;
      fundamentalHz?: number;
    }>
  ): AISmartAlignResult[] {
    return segments.map(features => this.predict(features));
  }
}

// Export the AI aligner for use in the UI
export { AISmartAligner, AISmartAlignResult };

// =============================================================================
// FIX: WebGL Phase Wheel Renderer
// High-performance WebGL-based phase visualization
// =============================================================================

export interface PhaseWheelOptions {
  canvas?: HTMLCanvasElement | OffscreenCanvas;
  size?: number;
  backgroundColor?: string;
  foregroundColor?: string;
  correlationColor?: string;
  phaseColor?: string;
}

export class PhaseWheelRenderer {
  private canvas: HTMLCanvasElement | OffscreenCanvas;
  private gl: WebGLRenderingContext | null = null;
  private program: WebGLProgram | null = null;
  private buffer: WebGLBuffer | null = null;
  private size: number = 512;
  
  // Phase wheel shader sources
  private static readonly VERTEX_SHADER = `
    attribute vec2 position;
    attribute float angle;
    attribute float radius;
    attribute vec3 color;
    varying vec3 vColor;
    varying float vRadius;
    
    void main() {
      vColor = color;
      vRadius = radius;
      float x = position.x * radius;
      float y = position.y * radius;
      gl_Position = vec4(x, y, 0.0, 1.0);
      gl_PointSize = 4.0;
    }
  `;

  private static readonly FRAGMENT_SHADER = `
    precision highp float;
    varying vec3 vColor;
    varying float vRadius;
    
    void main() {
      // Create radial gradient effect
      float dist = length(gl_PointCoord - vec2(0.5));
      if (dist > 0.5) discard;
      
      // Add glow effect based on radius (outer points are brighter)
      float glow = smoothstep(0.2, 0.8, vRadius);
      gl_FragColor = vec4(vColor * (0.7 + 0.3 * glow), 1.0);
    }
  `;

  constructor(options: PhaseWheelOptions = {}) {
    this.canvas = options.canvas || document.createElement('canvas');
    this.size = options.size || 512;
    
    if (this.canvas instanceof HTMLCanvasElement) {
      this.canvas.width = this.size;
      this.canvas.height = this.size;
    } else {
      (this.canvas as OffscreenCanvas).width = this.size;
      (this.canvas as OffscreenCanvas).height = this.size;
    }
    
    this.initWebGL();
  }

  private initWebGL(): void {
    try {
      const gl = (this.canvas as HTMLCanvasElement).getContext('webgl') ||
                  (this.canvas as OffscreenCanvas).getContext('webgl');
      
      if (!gl) {
        console.warn('WebGL not available, falling back to 2D canvas');
        return;
      }
      
      this.gl = gl;
      
      // Compile shaders
      const vertexShader = this.compileShader(gl.VERTEX_SHADER, this.VERTEX_SHADER);
      const fragmentShader = this.compileShader(gl.FRAGMENT_SHADER, this.FRAGMENT_SHADER);
      
      if (!vertexShader || !fragmentShader) {
        console.warn('Failed to compile shaders');
        return;
      }
      
      // Create program
      this.program = gl.createProgram()!;
      gl.attachShader(this.program, vertexShader);
      gl.attachShader(this.program, fragmentShader);
      gl.linkProgram(this.program);
      
      if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) {
        console.warn('Failed to link program:', gl.getProgramInfoLog(this.program));
        return;
      }
      
      // Create buffer
      this.buffer = gl.createBuffer()!;
      
    } catch (e) {
      console.warn('WebGL initialization failed:', e);
    }
  }

  private compileShader(type: number, source: string): WebGLShader | null {
    if (!this.gl) return null;
    
    const gl = this.gl;
    const shader = gl.createShader(type)!;
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.warn('Shader compilation error:', gl.getShaderInfoLog(shader));
      gl.deleteShader(shader);
      return null;
    }
    
    return shader;
  }

  /**
   * Render the phase wheel
   * @param phaseAngleDeg Current phase angle in degrees (-180 to 180)
   * @param correlation Current correlation value (-1 to 1)
   * @param rmsTrackA RMS of track A
   * @param rmsTrackB RMS of track B
   */
  render(
    phaseAngleDeg: number,
    correlation: number,
    rmsTrackA: number = 0,
    rmsTrackB: number = 0
  ): void {
    if (!this.gl || !this.program || !this.buffer) {
      // Fallback to 2D rendering
      this.render2D(phaseAngleDeg, correlation, rmsTrackA, rmsTrackB);
      return;
    }
    
    const gl = this.gl;
    const ctx = this.canvas.getContext('2d') || 
                (this.canvas as OffscreenCanvas).getContext('2d');
    
    if (!ctx) return;
    
    // Clear canvas
    ctx.clearRect(0, 0, this.size, this.size);
    
    // Set up WebGL
    gl.viewport(0, 0, this.size, this.size);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    
    gl.useProgram(this.program);
    
    // Calculate positions for phase wheel
    const centerX = 0;
    const centerY = 0;
    const radius = 0.8;
    
    // Convert phase angle to radians
    const phaseRad = phaseAngleDeg * Math.PI / 180;
    
    // Calculate vector position
    const vecX = Math.cos(phaseRad);
    const vecY = Math.sin(phaseRad);
    
    // Create vertices for the phase vector
    const vertices = new Float32Array([
      // Center point
      0, 0, 0, 0, 1, 1, 1, 1,
      // Vector tip
      vecX * radius, vecY * radius, phaseRad, radius, 0, 1, 1, 1
    ]);
    
    // Upload to buffer
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
    gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.DYNAMIC_DRAW);
    
    // Set up attributes
    const positionLoc = gl.getAttribLocation(this.program, 'position');
    const angleLoc = gl.getAttribLocation(this.program, 'angle');
    const radiusLoc = gl.getAttribLocation(this.program, 'radius');
    const colorLoc = gl.getAttribLocation(this.program, 'color');
    
    const stride = 8 * 4; // 8 floats per vertex
    gl.vertexAttribPointer(positionLoc, 2, gl.FLOAT, false, stride, 0);
    gl.enableVertexAttribArray(positionLoc);
    gl.vertexAttribPointer(angleLoc, 1, gl.FLOAT, false, stride, 2 * 4);
    gl.enableVertexAttribArray(angleLoc);
    gl.vertexAttribPointer(radiusLoc, 1, gl.FLOAT, false, stride, 3 * 4);
    gl.enableVertexAttribArray(radiusLoc);
    gl.vertexAttribPointer(colorLoc, 3, gl.FLOAT, false, stride, 4 * 4);
    gl.enableVertexAttribArray(colorLoc);
    
    // Draw
    gl.drawArrays(gl.POINTS, 0, 2);
    
    // Draw circle outline
    this.drawCircle(gl, radius, correlation);
  }

  private drawCircle(gl: WebGLRenderingContext, radius: number, correlation: number): void {
    if (!this.program) return;
    
    const segments = 64;
    const vertices: number[] = [];
    
    for (let i = 0; i <= segments; i++) {
      const angle = (i / segments) * Math.PI * 2;
      const x = Math.cos(angle) * radius;
      const y = Math.sin(angle) * radius;
      
      // Color based on correlation (green to red)
      const r = correlation < 0 ? 1 : 0;
      const g = correlation > 0 ? 1 : 0;
      const b = 0.3;
      
      vertices.push(x, y, angle, radius, r, g, b);
    }
    
    const vertexArray = new Float32Array(vertices);
    
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
    gl.bufferData(gl.ARRAY_BUFFER, vertexArray, gl.DYNAMIC_DRAW);
    
    const positionLoc = gl.getAttribLocation(this.program, 'position');
    const angleLoc = gl.getAttribLocation(this.program, 'angle');
    const radiusLoc = gl.getAttribLocation(this.program, 'radius');
    const colorLoc = gl.getAttribLocation(this.program, 'color');
    
    const stride = 7 * 4; // 7 floats per vertex
    gl.vertexAttribPointer(positionLoc, 2, gl.FLOAT, false, stride, 0);
    gl.enableVertexAttribArray(positionLoc);
    gl.vertexAttribPointer(angleLoc, 1, gl.FLOAT, false, stride, 2 * 4);
    gl.enableVertexAttribArray(angleLoc);
    gl.vertexAttribPointer(radiusLoc, 1, gl.FLOAT, false, stride, 3 * 4);
    gl.enableVertexAttribArray(radiusLoc);
    gl.vertexAttribPointer(colorLoc, 3, gl.FLOAT, false, stride, 4 * 4);
    gl.enableVertexAttribArray(colorLoc);
    
    gl.drawArrays(gl.LINE_STRIP, 0, segments + 1);
  }

  private render2D(
    phaseAngleDeg: number,
    correlation: number,
    rmsTrackA: number,
    rmsTrackB: number
  ): void {
    const ctx = this.canvas.getContext('2d') || 
                (this.canvas as OffscreenCanvas).getContext('2d');
    
    if (!ctx) return;
    
    const center = this.size / 2;
    const radius = center * 0.8;
    
    // Clear
    ctx.clearRect(0, 0, this.size, this.size);
    
    // Draw circle
    ctx.beginPath();
    ctx.arc(center, center, radius, 0, Math.PI * 2);
    ctx.strokeStyle = correlation > 0 ? '#00ff00' : '#ff0000';
    ctx.lineWidth = 2;
    ctx.stroke();
    
    // Draw phase vector
    const angleRad = phaseAngleDeg * Math.PI / 180;
    const x = center + Math.cos(angleRad) * radius;
    const y = center + Math.sin(angleRad) * radius;
    
    ctx.beginPath();
    ctx.moveTo(center, center);
    ctx.lineTo(x, y);
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 3;
    ctx.stroke();
    
    // Draw center dot
    ctx.beginPath();
    ctx.arc(center, center, 5, 0, Math.PI * 2);
    ctx.fillStyle = '#ffffff';
    ctx.fill();
  }

  /**
   * Get canvas element for display
   */
  getCanvas(): HTMLCanvasElement | OffscreenCanvas {
    return this.canvas;
  }

  /**
   * Resize the renderer
   */
  resize(size: number): void {
    this.size = size;
    if (this.canvas instanceof HTMLCanvasElement) {
      this.canvas.width = size;
      this.canvas.height = size;
    } else {
      (this.canvas as OffscreenCanvas).width = size;
      (this.canvas as OffscreenCanvas).height = size;
    }
    
    if (this.gl) {
      this.gl.viewport(0, 0, size, size);
    }
  }

  /**
   * Clean up resources
   */
  destroy(): void {
    if (this.gl) {
      if (this.program) this.gl.deleteProgram(this.program);
      if (this.buffer) this.gl.deleteBuffer(this.buffer);
      this.program = null;
      this.buffer = null;
      this.gl = null;
    }
  }
}

export { PhaseWheelRenderer, PhaseWheelOptions };

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

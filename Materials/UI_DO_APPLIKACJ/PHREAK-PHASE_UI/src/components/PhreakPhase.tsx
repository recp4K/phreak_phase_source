import { useState, useEffect, useLayoutEffect, useRef, useCallback, useMemo } from 'react';
import SmartAlignButton from './SmartAlignButton';
import CyberKnob from './CyberKnob';
import Oscilloscope from './Oscilloscope';
import PhaseWheel, { PhaseWheelBand, EqSuggestion } from './PhaseWheel';
import WaveformSphere from './WaveformSphere';
import CrossoverFilterCurve from './CrossoverFilterCurve';
import {
  isJuceAvailable,
  sendParameter,
  beginGesture,
  endGesture,
  triggerSmartAlign,
  selectPreset,
  bakeTimeline,
  bakeTimelineSegments,
  clearTimeline,
  armTimelineLearn,
  disarmTimelineLearn,
  onAudioFrame,
  onWaveform,
  onAlignResult,
  onTimelineCaptureStopped,
  onTimelineSegmentsCalculated,
  onTimelineWaveformOverview,
  TimelineWaveformOverview,
  AlignResult,
  TimelineSegment,
} from '../juceBridge';

// ── Types ──────────────────────────────────────────────────────────────────
type AlignState = 'idle' | 'scanning' | 'locked' | 'timeout';
type ViewMode = 'simple' | 'advanced';
type TrackMode = 'realtime' | 'timeline';

interface LastAlignResult {
  preCorrelation: number;
  postCorrelation: number;
  correlationGain: number;
  delayMs: number;
  delaySamples: number;
  rotateDeg: number;
  flip: boolean;
  detectedFreq: number;
  confidencePct: number;
  qualityLabel: string;
  status: string;
  timedOut: boolean;
  suggestedEqCutDb?: number;
  suggestedEqFreqHz?: number;
}

interface Params {
  crossoverFreq: number;
  subRotate: number;
  subDelay: number;
  subDynAmount: number;
  highRotate: number;
  highDelay: number;
  highDynAmount: number;
  envAttack: number;
  envRelease: number;
  lookaheadMs: number;
  glueDrive: number;
  alignTrackMode: 'Cont' | 'Trans';
  subFlip: boolean;
}

interface Telemetry {
  correlation: number;
  phaseAngleDeg: number;
  rmsTrackA_dB: number;
  rmsTrackB_dB: number;
  peakTrackA_dB: number;
  peakTrackB_dB: number;
  phaseConfidence: number;
  detectedFundamentalHz: number;
  detectedNote: string;
  isLocked: boolean;
  isPlaying?: boolean;
  samplePosition?: number;
  bpm?: number;
  lutActive?: boolean;
  isTimelineArmed?: boolean;
  isTimelineCapturing?: boolean;
  isTimelineAnalyzing?: boolean;
  timelineAnalysisProgress?: number;
}

// ── Note lookup ────────────────────────────────────────────────────────────
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function freqToNote(freq: number): string {
  const semitones = Math.round(12 * Math.log2(freq / 16.352));
  const note = NOTE_NAMES[((semitones % 12) + 12) % 12];
  const octave = Math.floor(semitones / 12);
  return `${note}${octave}`;
}

// ── Waveform generator (simulated) ────────────────────────────────────────
function generateWave(
  t: number,
  phaseOffset: number,
  amp: number,
  samples: number,
): number[] {
  return Array.from({ length: samples }, (_, i) => {
    const x = (i / samples) * Math.PI * 8;
    const env =
      Math.exp(-((i - samples * 0.15) ** 2) / (2 * (samples * 0.12) ** 2)) * 0.6;
    const sus = 0.35 * Math.sin(x * 0.4 + t * 0.8);
    return (
      amp *
      (Math.sin(x + t * 2 + phaseOffset) * 0.5 +
        Math.sin(x * 2.3 + t * 3.1 + phaseOffset) * 0.2 +
        env +
        sus +
        (Math.random() - 0.5) * 0.035)
    );
  });
}

// ── Small reusable UI components ───────────────────────────────────────────

function CyberToggle({
  value,
  onChange,
  label,
  onLabel = 'ON',
  offLabel = 'OFF',
}: {
  value: boolean;
  onChange: (v: boolean) => void;
  label: string;
  onLabel?: string;
  offLabel?: string;
}) {
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        gap: 4,
        fontFamily: 'JetBrains Mono, monospace',
      }}
    >
      <div
        onClick={() => onChange(!value)}
        style={{
          width: 36,
          height: 18,
          borderRadius: 9,
          background: value ? 'rgba(255,255,255,0.2)' : 'rgba(255,255,255,0.06)',
          border: `1px solid ${value ? 'rgba(255,255,255,0.4)' : 'rgba(255,255,255,0.14)'}`,
          position: 'relative',
          cursor: 'pointer',
          transition: 'all 0.2s ease',
          flexShrink: 0,
        }}
      >
        <div
          style={{
            position: 'absolute',
            top: 2,
            left: value ? 18 : 2,
            width: 12,
            height: 12,
            borderRadius: '50%',
            background: value ? '#FFFFFF' : 'rgba(255,255,255,0.35)',
            transition: 'left 0.2s cubic-bezier(0.34,1.56,0.64,1), background 0.2s ease',
          }}
        />
      </div>
      <div
        style={{
          fontSize: 8,
          color: value ? 'rgba(255,255,255,0.6)' : 'rgba(255,255,255,0.25)',
          letterSpacing: '0.1em',
          textTransform: 'uppercase',
          textAlign: 'center',
          transition: 'color 0.2s',
        }}
      >
        {label}
      </div>
      <div
        style={{
          fontSize: 7,
          color: value ? 'rgba(255,255,255,0.45)' : 'rgba(255,255,255,0.18)',
          letterSpacing: '0.06em',
        }}
      >
        {value ? onLabel : offLabel}
      </div>
    </div>
  );
}

function CorrelationArc({ correlation }: { correlation: number }) {
  const W = 250;
  const H = 72;
  const cx = W / 2;
  const cy = H - 10;
  const r = 50;

  // angle: π at correlation=-1, 0 at correlation=+1
  const angle = Math.PI - ((correlation + 1) / 2) * Math.PI;
  const nx = cx + r * Math.cos(angle);
  const ny = cy - r * Math.sin(angle);

  const corrPct = (correlation * 100).toFixed(1);
  const corrColor =
    correlation > 0.5
      ? 'rgba(255,255,255,0.9)'
      : correlation > 0
        ? 'rgba(255,255,255,0.6)'
        : 'rgba(255,255,255,0.3)';

  return (
    <svg width={W} height={H} style={{ display: 'block', overflow: 'visible' }}>
      {/* Track arc */}
      <path
        d={`M ${cx - r} ${cy} A ${r} ${r} 0 0 1 ${cx + r} ${cy}`}
        fill="none"
        stroke="rgba(255,255,255,0.1)"
        strokeWidth="1.5"
        strokeDasharray="2.5 5"
      />
      {/* Value arc */}
      {Math.abs(correlation + 1) > 0.015 && (
        <path
          d={`M ${cx - r} ${cy} A ${r} ${r} 0 ${correlation > 0 ? 1 : 0} 1 ${nx} ${ny}`}
          fill="none"
          stroke="rgba(255,255,255,0.45)"
          strokeWidth="1.5"
        />
      )}
      {/* Needle */}
      <line
        x1={cx}
        y1={cy}
        x2={nx}
        y2={ny}
        stroke="rgba(255,255,255,0.65)"
        strokeWidth="1.2"
      />
      <circle
        cx={nx}
        cy={ny}
        r="3.5"
        fill={corrColor}
        style={{ filter: 'drop-shadow(0 0 4px rgba(255,255,255,0.45))' }}
      />
      <circle cx={cx} cy={cy} r="2" fill="rgba(255,255,255,0.22)" />
      {/* Labels */}
      <text
        x={cx - r - 2}
        y={cy + 11}
        fill="rgba(255,255,255,0.28)"
        fontSize="7.5"
        fontFamily="JetBrains Mono"
        textAnchor="middle"
      >
        -1.0
      </text>
      <text
        x={cx}
        y={cy - r - 4}
        fill="rgba(255,255,255,0.28)"
        fontSize="7.5"
        fontFamily="JetBrains Mono"
        textAnchor="middle"
      >
        0.0
      </text>
      <text
        x={cx + r + 2}
        y={cy + 11}
        fill="rgba(255,255,255,0.28)"
        fontSize="7.5"
        fontFamily="JetBrains Mono"
        textAnchor="middle"
      >
        +1.0
      </text>
      <text
        x={cx}
        y={cy + 22}
        fill={corrColor}
        fontSize="9.5"
        fontFamily="JetBrains Mono"
        textAnchor="middle"
        fontWeight="500"
      >
        CORR {correlation >= 0 ? '+' : ''}
        {corrPct}%
      </text>
    </svg>
  );
}

function VuMeter({
  label,
  db,
  peakDb,
  color = 'rgba(255,255,255,0.7)',
}: {
  label?: string;
  db: number;
  peakDb?: number;
  color?: string;
}) {
  const bars = 8;
  const level = Math.max(0, Math.min(1, (db + 60) / 60));
  const litBars = Math.round(level * bars);
  const effectivePeak = peakDb !== undefined ? peakDb : db;
  const peakLevel = Math.max(0, Math.min(1, (effectivePeak + 60) / 60));
  const rawPeakIdx = Math.floor(peakLevel * bars);
  const peakBarIdx = Math.max(litBars > 0 ? litBars - 1 : 0, Math.min(bars - 1, rawPeakIdx));

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2 }}>
      {label && (
        <div
          style={{
            fontSize: 7.5,
            color: 'rgba(255,255,255,0.3)',
            letterSpacing: '0.1em',
            fontFamily: 'JetBrains Mono',
          }}
        >
          {label}
        </div>
      )}
      <div style={{ display: 'flex', gap: 1.5, alignItems: 'flex-end', height: 18 }}>
        {Array.from({ length: bars }, (_, i) => {
          const lit = i < litBars;
          const isPeakPip = i === peakBarIdx && peakLevel > 0.05;
          const barH = 4 + (i / (bars - 1)) * 10;
          return (
            <div
              key={i}
              style={{
                width: 3,
                height: barH,
                background: isPeakPip ? '#FFFFFF' : (lit ? color : 'rgba(255,255,255,0.07)'),
                boxShadow: isPeakPip ? '0 0 5px rgba(255,255,255,0.85)' : 'none',
                transition: 'background 0.05s ease',
                alignSelf: 'flex-end',
              }}
            />
          );
        })}
      </div>
      <div
        style={{
          fontSize: 6.5,
          color: 'rgba(255,255,255,0.28)',
          fontFamily: 'JetBrains Mono',
          fontVariantNumeric: 'tabular-nums',
        }}
      >
        {db.toFixed(1)}dB
      </div>
    </div>
  );
}

function SegToggle<T extends string>({
  options,
  value,
  onChange,
}: {
  options: { label: string; value: T }[];
  value: T;
  onChange: (v: T) => void;
}) {
  return (
    <div
      style={{
        display: 'flex',
        border: '1px solid rgba(255,255,255,0.16)',
        borderRadius: 2,
        overflow: 'hidden',
        background: '#0D0D16',
        fontFamily: 'JetBrains Mono, monospace',
      }}
    >
      {options.map((opt, i) => {
        const isSelected = value === opt.value;
        return (
          <button
            key={opt.value}
            onClick={() => onChange(opt.value)}
            style={{
              padding: '3px 10px',
              fontSize: 8.5,
              fontWeight: isSelected ? 700 : 500,
              letterSpacing: '0.08em',
              background: isSelected ? 'rgba(255,255,255,0.18)' : 'transparent',
              color: isSelected ? '#FFFFFF' : 'rgba(255,255,255,0.50)',
              border: 'none',
              borderLeft: i > 0 ? '1px solid rgba(255,255,255,0.12)' : 'none',
              cursor: 'pointer',
              transition: 'all 0.15s ease',
              fontFamily: 'inherit',
              boxShadow: isSelected ? 'inset 0 0 8px rgba(255,255,255,0.08)' : 'none',
            }}
            onMouseEnter={(e) => {
              if (!isSelected) {
                e.currentTarget.style.color = '#FFFFFF';
                e.currentTarget.style.background = 'rgba(255,255,255,0.06)';
              }
            }}
            onMouseLeave={(e) => {
              if (!isSelected) {
                e.currentTarget.style.color = 'rgba(255,255,255,0.50)';
                e.currentTarget.style.background = 'transparent';
              }
            }}
          >
            {opt.label}
          </button>
        );
      })}
    </div>
  );
}

// ── SYS Terminal slide-out ─────────────────────────────────────────────────
const TERMINAL_LINES = [
  { text: '$ PHREAKPHASE V2.0.4 — SYS // ARCHIVE', d: 0 },
  { text: '$ ────────────────────────────────────────────', d: 40 },
  { text: '', d: 80 },
  { text: '// PROBLEM STATEMENT', d: 120 },
  { text: 'When Kick (Track A) and Sub-Bass (Track B) share', d: 160 },
  { text: 'a summing bus, phase misalignment causes destructive', d: 200 },
  { text: 'cancellation — your low-end sounds thin & anemic.', d: 240 },
  { text: '', d: 280 },
  { text: '// THE FIX — ALL-PASS TPT FILTER', d: 320 },
  { text: 'A Linkwitz-Riley 4th-order crossover splits each', d: 360 },
  { text: 'track at CROSSOVER_FREQ (40–300Hz) into Sub + High.', d: 400 },
  { text: 'Independent TPT all-pass filters rotate phase by', d: 440 },
  { text: 'SUB_ROTATE / HIGH_ROTATE (±180°) — time-bending', d: 480 },
  { text: 'WITHOUT amplitude loss.', d: 520 },
  { text: '', d: 560 },
  { text: '// SMART ALIGN — GCC-PHAT ALGORITHM', d: 600 },
  { text: 'Cross-correlates A & B in the frequency domain,', d: 640 },
  { text: 'finds the lag maximising Pearson ρ, then auto-sets', d: 680 },
  { text: 'SUB_DELAY and SUB_ROTATE.', d: 720 },
  { text: '', d: 760 },
  { text: '// DYNAMIC UNMASKING', d: 800 },
  { text: 'Sidechain envelope (Attack / Release) drives a', d: 840 },
  { text: 'dynamic EQ cut carved into the conflicting band.', d: 880 },
  { text: 'Higher DYN_CUT = deeper carve on transient peaks.', d: 920 },
  { text: '', d: 960 },
  { text: '// FREAK GLUE ENGINE', d: 1000 },
  { text: 'Post-alignment saturation on the summed output.', d: 1040 },
  { text: 'GLUE_DRIVE (0–100%) adds harmonic density.', d: 1080 },
  { text: '', d: 1120 },
  { text: '// ROUTING — FL STUDIO', d: 1160 },
  { text: '1. Load on Mixer channel (4-in / 2-out)', d: 1200 },
  { text: '2. Kick send → Input 1+2 / Bass → Input 3+4', d: 1240 },
  { text: '3. Enable PDC (latency compensation)', d: 1280 },
  { text: '', d: 1320 },
  { text: '// ROUTING — ABLETON', d: 1360 },
  { text: '1. Insert on Audio track (External In)', d: 1400 },
  { text: '2. Use Max4Live for 4-ch bus routing', d: 1440 },
  { text: '3. Ensure LOOKAHEAD_MS reported to DAW for PDC', d: 1480 },
  { text: '', d: 1520 },
  { text: '$ END OF ARCHIVE // SYS READY_', d: 1560 },
];

function SysTerminal({
  isOpen,
  onClose,
}: {
  isOpen: boolean;
  onClose: () => void;
}) {
  return (
    <div
      style={{
        position: 'absolute',
        top: 0,
        right: 0,
        bottom: 0,
        width: 390,
        background: 'rgba(5,5,9,0.97)',
        borderLeft: '1px solid rgba(255,255,255,0.09)',
        zIndex: 100,
        transform: isOpen ? 'translateX(0)' : 'translateX(100%)',
        transition: 'transform 0.44s cubic-bezier(0.16, 1, 0.3, 1)',
        display: 'flex',
        flexDirection: 'column',
        overflow: 'hidden',
      }}
    >
      {/* CRT scanlines */}
      <div
        style={{
          position: 'absolute',
          inset: 0,
          backgroundImage:
            'repeating-linear-gradient(0deg, transparent, transparent 3px, rgba(0,0,0,0.16) 3px, rgba(0,0,0,0.16) 4px)',
          pointerEvents: 'none',
          zIndex: 2,
        }}
      />
      {/* Header */}
      <div
        style={{
          padding: '11px 16px 9px',
          borderBottom: '1px solid rgba(255,255,255,0.07)',
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          position: 'relative',
          zIndex: 3,
          flexShrink: 0,
        }}
      >
        <div>
          <div
            style={{
              fontSize: 9,
              color: 'rgba(255,255,255,0.6)',
              letterSpacing: '0.2em',
              fontFamily: 'JetBrains Mono',
            }}
          >
            // SYS // ARCHIVE
          </div>
          <div
            style={{
              fontSize: 7.5,
              color: 'rgba(255,255,255,0.22)',
              letterSpacing: '0.1em',
              fontFamily: 'JetBrains Mono',
              marginTop: 3,
            }}
          >
            PHREAKPHASE DOCUMENTATION v2.0.4
          </div>
        </div>
        <button
          onClick={onClose}
          style={{
            background: 'none',
            border: '1px solid rgba(255,255,255,0.12)',
            color: 'rgba(255,255,255,0.35)',
            fontSize: 8.5,
            padding: '3px 9px',
            cursor: 'pointer',
            fontFamily: 'JetBrains Mono',
            letterSpacing: '0.1em',
            transition: 'all 0.15s',
          }}
          onMouseEnter={(e) => (e.currentTarget.style.color = 'rgba(255,255,255,0.75)')}
          onMouseLeave={(e) => (e.currentTarget.style.color = 'rgba(255,255,255,0.35)')}
        >
          [ × ]
        </button>
      </div>
      {/* Content */}
      <div
        style={{
          flex: 1,
          overflowY: 'auto',
          padding: '12px 16px 20px',
          position: 'relative',
          zIndex: 3,
        }}
      >
        {TERMINAL_LINES.map((line, i) => (
          <div
            key={i}
            style={{
              fontSize: 8.5,
              color: line.text.startsWith('//')
                ? 'rgba(255,255,255,0.7)'
                : line.text.startsWith('$')
                  ? 'rgba(255,255,255,0.88)'
                  : 'rgba(255,255,255,0.38)',
              fontFamily: 'JetBrains Mono, monospace',
              letterSpacing: '0.04em',
              lineHeight: 1.75,
              minHeight: '0.9em',
              animation: isOpen
                ? `term-line-in 0.28s ease ${line.d}ms both`
                : undefined,
            }}
          >
            {line.text || ' '}
          </div>
        ))}
        <div
          style={{
            fontSize: 9,
            color: 'rgba(255,255,255,0.45)',
            fontFamily: 'JetBrains Mono',
            animation: 'cursor-blink 1.1s step-end infinite',
          }}
        >
          █
        </div>
      </div>
    </div>
  );
}

// ── ControlPanel (4-band deck) ─────────────────────────────────────────────
interface ControlPanelProps {
  params: Params;
  setParam: <K extends keyof Params>(key: K, val: Params[K]) => void;
}

function ControlPanel({ params, setParam }: ControlPanelProps) {
  const [splitEnabled, setSplitEnabled] = useState(true);
  const [subLatch, setSubLatch] = useState(false);
  const [highLatch, setHighLatch] = useState(false);
  const [deltaListen, setDeltaListen] = useState(false);

  const toggleSplit = useCallback((v: boolean) => {
    setSplitEnabled(v);
    sendParameter('CROSSOVER_ENABLE', v ? 1 : 0);
  }, []);

  const toggleDeltaListen = useCallback((v: boolean) => {
    setDeltaListen(v);
    sendParameter('DELTA_LISTEN', v ? 1 : 0);
  }, []);

  const note = freqToNote(params.crossoverFreq);

  const panels = [
    {
      num: '01',
      title: 'CROSSOVER',
      accent: '#A855F7',
      content: (
        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 7 }}>
          <CrossoverFilterCurve
            crossoverFreq={params.crossoverFreq}
            active={splitEnabled}
            width={210}
            height={46}
          />
          <div style={{ display: 'flex', alignItems: 'center', gap: 14, marginTop: 2 }}>
            <CyberKnob
              label="FREQ"
              value={params.crossoverFreq}
              min={40}
              max={300}
              defaultValue={90}
              unit="Hz"
              size={52}
              decimals={0}
              onChange={(v) => setParam('crossoverFreq', v)}
              paramId="CROSSOVER_FREQ"
              accentColor="#A855F7"
            />
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6, alignItems: 'center' }}>
              <div
                style={{
                  fontSize: 9.5,
                  fontWeight: 600,
                  color: '#FFFFFF',
                  fontFamily: 'JetBrains Mono',
                  letterSpacing: '0.04em',
                  textAlign: 'center',
                  background: 'rgba(168,85,247,0.12)',
                  border: '1px solid rgba(168,85,247,0.4)',
                  padding: '3px 8px',
                  borderRadius: 2,
                  whiteSpace: 'nowrap',
                }}
              >
                [ {note} — {params.crossoverFreq.toFixed(0)}Hz ]
              </div>
              <CyberToggle
                value={splitEnabled}
                onChange={toggleSplit}
                label="SPLIT"
                onLabel="ACTIVE"
                offLabel="BYPASS"
              />
            </div>
          </div>
        </div>
      ),
    },
    {
      num: '02',
      title: 'SUB BAND',
      accent: '#00D4FF',
      content: (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 7, alignItems: 'center' }}>
          <div style={{ display: 'flex', gap: 14 }}>
            <CyberKnob
              label="ROTATE"
              value={params.subRotate}
              min={-180}
              max={180}
              defaultValue={0}
              unit="°"
              size={48}
              decimals={1}
              onChange={(v) => setParam('subRotate', v)}
              paramId="SUB_ROTATE"
              bipolar
              accentColor="#00D4FF"
            />
            <CyberKnob
              label="DELAY"
              value={params.subDelay}
              min={-20}
              max={20}
              defaultValue={0}
              unit="ms"
              size={48}
              decimals={2}
              onChange={(v) => setParam('subDelay', v)}
              paramId="SUB_DELAY"
              bipolar
              accentColor="#00D4FF"
            />
          </div>
          <div style={{ display: 'flex', gap: 14, alignItems: 'center' }}>
            <CyberKnob
              label="DYN CUT"
              value={params.subDynAmount}
              min={0}
              max={100}
              defaultValue={0}
              unit="%"
              size={42}
              decimals={1}
              onChange={(v) => setParam('subDynAmount', v)}
              paramId="SUB_DYN_AMOUNT"
              accentColor="#00D4FF"
            />
            <CyberToggle
              value={params.subFlip}
              onChange={(v) => setParam('subFlip', v)}
              label="POLARITY"
              onLabel="INV 180°"
              offLabel="NORMAL"
            />
          </div>
        </div>
      ),
    },
    {
      num: '03',
      title: 'HIGH BAND',
      accent: '#A855F7',
      content: (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 7, alignItems: 'center' }}>
          <div style={{ display: 'flex', gap: 14 }}>
            <CyberKnob
              label="ROTATE"
              value={params.highRotate}
              min={-180}
              max={180}
              defaultValue={0}
              unit="°"
              size={48}
              decimals={1}
              onChange={(v) => setParam('highRotate', v)}
              paramId="HIGH_ROTATE"
              bipolar
              accentColor="#A855F7"
            />
            <CyberKnob
              label="DELAY"
              value={params.highDelay}
              min={-20}
              max={20}
              defaultValue={0}
              unit="ms"
              size={48}
              decimals={2}
              onChange={(v) => setParam('highDelay', v)}
              paramId="HIGH_DELAY"
              bipolar
              accentColor="#A855F7"
            />
          </div>
          <div style={{ display: 'flex', gap: 14, alignItems: 'center' }}>
            <CyberKnob
              label="DYN CUT"
              value={params.highDynAmount}
              min={0}
              max={100}
              defaultValue={0}
              unit="%"
              size={42}
              decimals={1}
              onChange={(v) => setParam('highDynAmount', v)}
              paramId="HIGH_DYN_AMOUNT"
              accentColor="#A855F7"
            />
            <CyberToggle
              value={highLatch}
              onChange={setHighLatch}
              label="PHASE LATCH"
              onLabel="LOCKED"
              offLabel="FREE"
            />
          </div>
        </div>
      ),
    },
    {
      num: '04',
      title: 'DYNAMICS & GLUE',
      accent: '#FFB800',
      content: (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 7, alignItems: 'center' }}>
          <div style={{ display: 'flex', gap: 14 }}>
            <CyberKnob
              label="ATTACK"
              value={params.envAttack}
              min={0.1}
              max={50}
              defaultValue={2}
              unit="ms"
              size={48}
              decimals={1}
              onChange={(v) => setParam('envAttack', v)}
              paramId="ENV_ATTACK"
              accentColor="#FFB800"
            />
            <CyberKnob
              label="RELEASE"
              value={params.envRelease}
              min={10}
              max={500}
              defaultValue={120}
              unit="ms"
              size={48}
              decimals={0}
              onChange={(v) => setParam('envRelease', v)}
              paramId="ENV_RELEASE"
              accentColor="#FFB800"
            />
          </div>
          <div style={{ display: 'flex', gap: 14, alignItems: 'center' }}>
            <CyberKnob
              label="FREAK GLUE"
              value={params.glueDrive}
              min={0}
              max={100}
              defaultValue={0}
              unit="%"
              size={48}
              decimals={1}
              onChange={(v) => setParam('glueDrive', v)}
              paramId="GLUE_DRIVE"
              accentColor="#FFB800"
            />
            <CyberToggle
              value={deltaListen}
              onChange={toggleDeltaListen}
              label="Δ LISTEN"
              onLabel="DELTA"
              offLabel="FULL"
            />
          </div>
        </div>
      ),
    },
  ];

  return (
    <div style={{ display: 'flex', height: '100%', background: '#09090F' }}>
      {panels.map((panel, i) => (
        <div
          key={i}
          style={{
            flex: 1,
            borderLeft: i > 0 ? '1px solid rgba(255,255,255,0.08)' : 'none',
            display: 'flex',
            flexDirection: 'column',
            padding: '0 12px',
          }}
        >
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: 8,
              padding: '8px 0 6px',
              borderBottom: '1px solid rgba(255,255,255,0.07)',
              marginBottom: 8,
              flexShrink: 0,
            }}
          >
            <div
              style={{
                width: 6,
                height: 6,
                borderRadius: '50%',
                background: panel.accent,
                boxShadow: `0 0 6px ${panel.accent}`,
              }}
            />
            <span
              style={{
                fontSize: 8.5,
                color: 'rgba(255,255,255,0.45)',
                fontFamily: 'JetBrains Mono',
                letterSpacing: '0.08em',
              }}
            >
              {panel.num} //
            </span>
            <span
              style={{
                fontSize: 9.5,
                color: '#FFFFFF',
                fontWeight: 700,
                fontFamily: 'JetBrains Mono',
                letterSpacing: '0.12em',
                textTransform: 'uppercase',
              }}
            >
              {panel.title}
            </span>
          </div>
          <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            {panel.content}
          </div>
        </div>
      ))}
    </div>
  );
}

// ── Compact align trigger (advanced mode strip) ────────────────────────────
function CompactAlignBtn({
  state,
  onTrigger,
  correlation,
  result,
}: {
  state: AlignState;
  onTrigger: () => void;
  correlation: number;
  result?: LastAlignResult | null;
}) {
  const corrPct = Math.round(Math.abs(correlation) * 100);
  const label =
    state === 'idle'
      ? '[ SMART ALIGN ]'
      : state === 'scanning'
        ? '[ // SCANNING... ]'
        : state === 'timeout'
          ? '[ ⚠ NO AUDIO - RETRY ]'
          : `[ ● LOCKED +${corrPct}% ]`;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 5, alignItems: 'flex-start' }}>
      <button
        onClick={onTrigger}
        style={{
          background:
            state === 'locked'
              ? 'rgba(74, 222, 128, 0.12)'
              : state === 'timeout'
                ? 'rgba(239, 68, 68, 0.15)'
                : state === 'scanning'
                  ? 'rgba(255,255,255,0.04)'
                  : 'rgba(255,255,255,0.03)',
          border: `1px solid ${
            state === 'locked'
              ? 'rgba(74, 222, 128, 0.5)'
              : state === 'timeout'
                ? 'rgba(239, 68, 68, 0.6)'
                : state === 'scanning'
                  ? 'rgba(255,255,255,0.25)'
                  : 'rgba(255,255,255,0.16)'
          }`,
          color:
            state === 'locked'
              ? '#4ade80'
              : state === 'timeout'
                ? '#ff6b6b'
                : state === 'scanning'
                  ? 'rgba(255,255,255,0.6)'
                  : 'rgba(255,255,255,0.45)',
          fontSize: 8.5,
          padding: '6px 14px',
          cursor: 'pointer',
          fontFamily: 'JetBrains Mono, monospace',
          letterSpacing: '0.12em',
          transition: 'all 0.2s ease',
          whiteSpace: 'nowrap',
          animation:
            state === 'scanning' ? 'hex-flicker 0.4s linear infinite' : undefined,
        }}
        onMouseEnter={(e) => {
          if (state === 'idle') e.currentTarget.style.borderColor = 'rgba(255,255,255,0.3)';
        }}
        onMouseLeave={(e) => {
          if (state === 'idle') e.currentTarget.style.borderColor = 'rgba(255,255,255,0.16)';
        }}
      >
        {label}
      </button>
      {state === 'locked' && result && (
        <div
          style={{
            fontSize: 6.5,
            color: 'rgba(255,255,255,0.45)',
            display: 'flex',
            flexDirection: 'column',
            gap: 2,
            fontFamily: 'JetBrains Mono, monospace',
          }}
        >
          <span>
            GAIN:{' '}
            <span style={{ color: '#4ade80', fontWeight: 600 }}>
              +{Math.round(result.correlationGain * 100)}%
            </span>{' '}
            ({Math.round(result.preCorrelation * 100)}% → {Math.round(result.postCorrelation * 100)}%)
          </span>
          <span>
            OFFSET:{' '}
            <span style={{ color: '#fff' }}>{result.delayMs >= 0 ? '+' : ''}{result.delayMs.toFixed(2)}ms</span>{' '}
            ({result.delaySamples >= 0 ? '+' : ''}{Math.round(result.delaySamples)} smp)
          </span>
          <span>
            PHASE:{' '}
            <span style={{ color: '#fff' }}>{result.rotateDeg >= 0 ? '+' : ''}{result.rotateDeg.toFixed(1)}°</span> |{' '}
            <span style={{ color: result.flip ? '#f59e0b' : '#4ade80' }}>
              {result.flip ? 'INV' : 'NORM'}
            </span>
          </span>
        </div>
      )}
    </div>
  );
}

function TimelineView({
  telemetry,
  params,
  waveformA = [],
  waveformB = [],
  timelineOverview = null,
  width,
  height,
}: {
  telemetry: Telemetry;
  params: Params;
  waveformA?: number[];
  waveformB?: number[];
  timelineOverview?: TimelineWaveformOverview | null;
  width: number;
  height: number;
}) {
  const [zoomMode, setZoomMode] = useState<'4BARS' | '8BARS' | '16BARS' | 'ALL'>('8BARS');
  const [followPlayhead, setFollowPlayhead] = useState(true);
  const [scrollStartSample, setScrollStartSample] = useState(0);
  const [toastNotice, setToastNotice] = useState<string | null>(null);
  const [segments, setSegments] = useState<TimelineSegment[]>([]);
  const [selectedSegmentId, setSelectedSegmentId] = useState<string | null>(null);

  const mainCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const ribbonCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const minimapCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const toastTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const sr = 44100;
  const bpm = telemetry.bpm && telemetry.bpm > 20 ? telemetry.bpm : 120.0;
  const beatSamples = Math.round((60.0 / bpm) * sr);
  const barSamples = beatSamples * 4;

  // Total song length tracked (default 32 bars, expands as playback proceeds)
  const currentPos = telemetry.samplePosition ?? 0;
  const totalBars = Math.max(32, Math.ceil(Math.max(currentPos + barSamples * 8, 1) / barSamples));
  const totalSongSamples = totalBars * barSamples;

  // Visible window range based on zoomMode
  const visibleBars = zoomMode === '4BARS' ? 4 : zoomMode === '8BARS' ? 8 : zoomMode === '16BARS' ? 16 : totalBars;
  const visibleSamples = visibleBars * barSamples;

  // Auto-follow playhead calculation
  useEffect(() => {
    if (followPlayhead && telemetry.isPlaying && zoomMode !== 'ALL') {
      const targetStart = Math.max(0, currentPos - visibleSamples * 0.25);
      setScrollStartSample(targetStart);
    }
  }, [followPlayhead, telemetry.isPlaying, currentPos, visibleSamples, zoomMode]);

  // Ensure scroll is clamped
  const effectiveScrollStart = zoomMode === 'ALL'
    ? 0
    : Math.max(0, Math.min(totalSongSamples - visibleSamples, scrollStartSample));

  const isArmed = Boolean(telemetry.isTimelineArmed);
  const isCapturing = Boolean(telemetry.isTimelineCapturing);
  const isAnalyzing = Boolean(telemetry.isTimelineAnalyzing);

  const showToast = useCallback((msg: string) => {
    setToastNotice(msg);
    if (toastTimerRef.current) clearTimeout(toastTimerRef.current);
    toastTimerRef.current = setTimeout(() => setToastNotice(null), 3500);
  }, []);

  // ARM / LEARN toggle (Decision #4)
  const handleToggleArm = useCallback(() => {
    if (isArmed || isCapturing) {
      disarmTimelineLearn();
      showToast('○ TIMELINE LEARN DISARMED');
    } else {
      armTimelineLearn(true);
      showToast('● TIMELINE ARMED — PRESS PLAY IN DAW TO RECORD');
    }
  }, [isArmed, isCapturing, showToast]);

  // Real GCC-PHAT beat segments from C++ engine (Zero client-side faking)
  useEffect(() => {
    return onTimelineSegmentsCalculated((newSegs) => {
      setSegments(newSegs);
      showToast(`✓ C++ ENGINE: ${newSegs.length} BEAT SEGMENTS COMPUTED & BAKED`);
    });
  }, [showToast]);

  // Listen to DAW stop event: inform user that C++ analysis is running
  useEffect(() => {
    return onTimelineCaptureStopped(() => {
      showToast('⏳ DAW STOPPED — GCC-PHAT BEAT ANALYSIS IN C++...');
    });
  }, [showToast]);

  // Bake all segments
  const handleBakeAll = useCallback(() => {
    bakeTimelineSegments(segments);
    showToast(`✓ BAKED: ${segments.filter((s) => !s.bypassed).length} ACTIVE SEGMENTS TO RCU LUT`);
  }, [segments, showToast]);

  // Clear all segments & LUT
  const handleClearAll = useCallback(() => {
    clearTimeline();
    setSegments((segs) => segs.map((s) => ({ ...s, bypassed: true })));
    showToast('✕ TIMELINE CLEARED → REAL-TIME R3 FALLBACK ACTIVE');
  }, [showToast]);

  // Update a single segment
  const updateSegment = useCallback((id: string, patch: Partial<TimelineSegment>) => {
    setSegments((prev) => {
      const updated = prev.map((s) => (s.id === id ? { ...s, ...patch } : s));
      bakeTimelineSegments(updated);
      return updated;
    });
  }, []);

  // ── Render Main Dual Waveform Canvas (Kick Amber & Bass Cyan) ─────────────
  useEffect(() => {
    const canvas = mainCanvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const W = width;
    const H = 138;
    canvas.width = W * dpr;
    canvas.height = H * dpr;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);

    // Dark backdrop
    ctx.fillStyle = '#07070B';
    ctx.fillRect(0, 0, W, H);

    const laneH = 69;
    const midY1 = 34.5;
    const midY2 = 103.5;
    const ampY = 28;

    // Grid lines per Bar & Beat
    const startBar = Math.floor(effectiveScrollStart / barSamples);
    const endBar = Math.ceil((effectiveScrollStart + visibleSamples) / barSamples);

    for (let b = startBar; b <= endBar; b++) {
      const barSample = b * barSamples;
      const x = ((barSample - effectiveScrollStart) / visibleSamples) * W;

      // Bar divider line
      ctx.strokeStyle = 'rgba(255,255,255,0.12)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();

      // Quarter-note beat lines
      ctx.strokeStyle = 'rgba(255,255,255,0.04)';
      ctx.setLineDash([2, 4]);
      for (let q = 1; q < 4; q++) {
        const beatSample = barSample + q * beatSamples;
        const qx = ((beatSample - effectiveScrollStart) / visibleSamples) * W;
        ctx.beginPath();
        ctx.moveTo(qx, 0);
        ctx.lineTo(qx, H);
        ctx.stroke();
      }
      ctx.setLineDash([]);
    }

    // Lane divider
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.beginPath();
    ctx.moveTo(0, laneH);
    ctx.lineTo(W, laneH);
    ctx.stroke();

    // ── Lane 1: Kick Waveform (Amber #FFB800) ──
    ctx.strokeStyle = 'rgba(255, 184, 0, 0.15)';
    ctx.beginPath();
    ctx.moveTo(0, midY1);
    ctx.lineTo(W, midY1);
    ctx.stroke();

    ctx.fillStyle = '#FFB800';
    ctx.font = '7.5px JetBrains Mono, monospace';
    ctx.fillText(`TRACK A // KICK (AMBER) — ${telemetry.rmsTrackA_dB.toFixed(1)} dB`, 14, 14);

    ctx.beginPath();
    const steps = 380;
    const hasOverview = Boolean(timelineOverview && timelineOverview.trackA_max && timelineOverview.trackA_max.length > 0);
    const ovStart = hasOverview ? timelineOverview!.startSample : 0;
    const ovEnd = hasOverview ? timelineOverview!.endSample : 1;
    const ovLen = Math.max(1, ovEnd - ovStart);
    const numBins = hasOverview ? timelineOverview!.trackA_max.length : 0;

    for (let s = 0; s <= steps; s++) {
      const frac = s / steps;
      const sampleAtPoint = effectiveScrollStart + frac * visibleSamples;
      let waveVal = 0;

      if (hasOverview && sampleAtPoint >= ovStart && sampleAtPoint <= ovEnd) {
        const binIdx = Math.min(numBins - 1, Math.max(0, Math.floor(((sampleAtPoint - ovStart) / ovLen) * numBins)));
        const maxVal = timelineOverview!.trackA_max[binIdx] || 0;
        const minVal = timelineOverview!.trackA_min[binIdx] || 0;
        waveVal = (s % 2 === 0 ? maxVal : minVal) * 0.95;
      } else if (waveformA && waveformA.length > 0) {
        const beatPhase = ((sampleAtPoint % beatSamples) + beatSamples) % beatSamples / beatSamples;
        waveVal = waveformA[Math.floor(beatPhase * waveformA.length) % waveformA.length] * 0.95;
      }

      const x = frac * W;
      const y = midY1 - waveVal * ampY;
      if (s === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = '#FFB800';
    ctx.lineWidth = 1.3;
    ctx.stroke();
    ctx.lineTo(W, midY1);
    ctx.lineTo(0, midY1);
    ctx.closePath();
    ctx.fillStyle = 'rgba(255, 184, 0, 0.1)';
    ctx.fill();

    // ── Lane 2: Bass Waveform (Cyan #00D4FF) ──
    ctx.strokeStyle = 'rgba(0, 212, 255, 0.15)';
    ctx.beginPath();
    ctx.moveTo(0, midY2);
    ctx.lineTo(W, midY2);
    ctx.stroke();

    ctx.fillStyle = '#00D4FF';
    ctx.font = '7.5px JetBrains Mono, monospace';
    ctx.fillText(`TRACK B // BASS (CYAN) — ${telemetry.rmsTrackB_dB.toFixed(1)} dB`, 14, laneH + 14);

    ctx.beginPath();
    for (let s = 0; s <= steps; s++) {
      const frac = s / steps;
      const sampleAtPoint = effectiveScrollStart + frac * visibleSamples;
      let waveVal = 0;

      if (hasOverview && sampleAtPoint >= ovStart && sampleAtPoint <= ovEnd) {
        const binIdx = Math.min(numBins - 1, Math.max(0, Math.floor(((sampleAtPoint - ovStart) / ovLen) * numBins)));
        const maxVal = timelineOverview!.trackB_max[binIdx] || 0;
        const minVal = timelineOverview!.trackB_min[binIdx] || 0;
        waveVal = (s % 2 === 0 ? maxVal : minVal) * 0.85;
      } else if (waveformB && waveformB.length > 0) {
        const beatPhase = ((sampleAtPoint % beatSamples) + beatSamples) % beatSamples / beatSamples;
        waveVal = waveformB[Math.floor(beatPhase * waveformB.length) % waveformB.length] * 0.85;
      }

      const x = frac * W;
      const y = midY2 - waveVal * ampY;
      if (s === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = '#00D4FF';
    ctx.lineWidth = 1.3;
    ctx.stroke();
    ctx.lineTo(W, midY2);
    ctx.lineTo(0, midY2);
    ctx.closePath();
    ctx.fillStyle = 'rgba(0, 212, 255, 0.1)';
    ctx.fill();

    ctx.restore();
  }, [width, effectiveScrollStart, visibleSamples, barSamples, beatSamples, waveformA, waveformB, timelineOverview, params.subRotate, telemetry.rmsTrackA_dB, telemetry.rmsTrackB_dB]);

  // ── Render Heatmap Ribbon (Decision #3) ──────────────────────────────────
  useEffect(() => {
    const canvas = ribbonCanvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const W = width;
    const H = 16;
    canvas.width = W * dpr;
    canvas.height = H * dpr;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);

    ctx.fillStyle = '#0B0B10';
    ctx.fillRect(0, 0, W, H);

    // Draw correlation color gradient across visible segments
    const visibleSegs = segments.filter(
      (s) => s.endSample >= effectiveScrollStart && s.startSample <= effectiveScrollStart + visibleSamples,
    );

    if (visibleSegs.length === 0) {
      // Fallback neutral ribbon
      ctx.fillStyle = 'rgba(255,255,255,0.06)';
      ctx.fillRect(0, 0, W, H);
    } else {
      for (const seg of visibleSegs) {
        const x1 = Math.max(0, ((seg.startSample - effectiveScrollStart) / visibleSamples) * W);
        const x2 = Math.min(W, ((seg.endSample - effectiveScrollStart) / visibleSamples) * W);
        const segW = Math.max(1, x2 - x1);

        if (seg.bypassed) {
          ctx.fillStyle = 'rgba(255,255,255,0.05)';
        } else {
          const corr = seg.correlationAfter;
          // Gradient mapping: red (<0) -> yellow (0-0.4) -> green (>0.6)
          if (corr < 0) {
            ctx.fillStyle = '#ef4444';
          } else if (corr < 0.45) {
            ctx.fillStyle = '#f59e0b';
          } else if (corr < 0.75) {
            ctx.fillStyle = '#84cc16';
          } else {
            ctx.fillStyle = '#22c55e';
          }
        }
        ctx.fillRect(x1, 0, segW, H);

        // Divider
        ctx.fillStyle = 'rgba(0,0,0,0.4)';
        ctx.fillRect(x2 - 1, 0, 1, H);

        // Badge text
        if (segW > 28) {
          ctx.font = 'bold 7.5px JetBrains Mono, monospace';
          ctx.fillStyle = seg.bypassed ? 'rgba(255,255,255,0.45)' : '#000000';
          ctx.fillText(
            seg.bypassed ? 'BYP' : `${(seg.correlationAfter * 100).toFixed(0)}%`,
            x1 + 3,
            11,
          );
        }
      }
    }

    ctx.restore();
  }, [width, segments, effectiveScrollStart, visibleSamples]);

  // ── Render Minimap Overview (Decision #1) ────────────────────────────────
  useEffect(() => {
    const canvas = minimapCanvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const W = width - 32;
    const H = 28;
    canvas.width = W * dpr;
    canvas.height = H * dpr;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);

    ctx.fillStyle = '#09090D';
    ctx.fillRect(0, 0, W, H);

    // Miniature kick/bass wave envelopes across song
    if (timelineOverview && timelineOverview.trackA_max && timelineOverview.trackA_max.length > 0) {
      const numBins = timelineOverview.trackA_max.length;
      const ovStart = timelineOverview.startSample;
      const ovEnd = timelineOverview.endSample;
      const ovLen = Math.max(1, ovEnd - ovStart);

      for (let px = 0; px < W; px++) {
        const sample = (px / W) * totalSongSamples;
        if (sample >= ovStart && sample <= ovEnd) {
          const binIdx = Math.min(numBins - 1, Math.max(0, Math.floor(((sample - ovStart) / ovLen) * numBins)));
          const maxA = Math.max(0, timelineOverview.trackA_max[binIdx] || 0);
          const minA = Math.min(0, timelineOverview.trackA_min[binIdx] || 0);
          const maxB = Math.max(0, timelineOverview.trackB_max[binIdx] || 0);
          const minB = Math.min(0, timelineOverview.trackB_min[binIdx] || 0);

          const ampA = Math.max(1, (maxA - minA) * (H * 0.45));
          const ampB = Math.max(1, (maxB - minB) * (H * 0.45));

          ctx.fillStyle = 'rgba(255, 184, 0, 0.5)';
          ctx.fillRect(px, H / 2 - ampA, 1, ampA);

          ctx.fillStyle = 'rgba(0, 212, 255, 0.5)';
          ctx.fillRect(px, H / 2, 1, ampB);
        }
      }
    } else {
      // Clean flat baseline before audio is captured (no fake sine math)
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(W, H / 2);
      ctx.stroke();
    }

    // Viewport box showing currently visible window with grab handles
    const viewLeft = (effectiveScrollStart / totalSongSamples) * W;
    const viewWidth = Math.max(14, (visibleSamples / totalSongSamples) * W);

    ctx.fillStyle = 'rgba(168, 85, 247, 0.22)';
    ctx.fillRect(viewLeft, 0, viewWidth, H);
    ctx.strokeStyle = '#c084fc';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(viewLeft, 0.5, viewWidth, H - 1);

    // Left and right viewport drag handles
    ctx.fillStyle = '#a855f7';
    ctx.fillRect(viewLeft, 4, 2, H - 8);
    ctx.fillRect(viewLeft + viewWidth - 2, 4, 2, H - 8);

    // Playhead pip on minimap
    if (currentPos >= 0 && currentPos <= totalSongSamples) {
      const playheadX = (currentPos / totalSongSamples) * W;
      ctx.fillStyle = '#ff4444';
      ctx.fillRect(playheadX - 1, 0, 2, H);
    }

    ctx.restore();
  }, [width, effectiveScrollStart, visibleSamples, totalSongSamples, totalBars, currentPos, timelineOverview]);

  // Scrubber ratio for main playhead line
  const mainPlayheadRatio =
    currentPos >= effectiveScrollStart && currentPos <= effectiveScrollStart + visibleSamples
      ? (currentPos - effectiveScrollStart) / visibleSamples
      : null;

  return (
    <div
      style={{
        width,
        height,
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        background: 'rgba(6,6,10,0.99)',
        fontFamily: 'JetBrains Mono, monospace',
        userSelect: 'none',
      }}
    >
      {/* HUD Header Toolbar */}
      <div
        style={{
          height: 32,
          borderBottom: '1px solid rgba(255,255,255,0.08)',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          padding: '0 14px',
          background: 'rgba(255,255,255,0.018)',
          flexShrink: 0,
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ fontSize: 8, color: '#FFFFFF', fontWeight: 600, letterSpacing: '0.14em' }}>
            // TIMELINE GRAPH
          </span>

          {/* ARM / LEARN Button (Decision #4) */}
          <button
            onClick={handleToggleArm}
            style={{
              fontSize: 7.5,
              padding: '2px 9px',
              background: isCapturing
                ? 'rgba(239, 68, 68, 0.25)'
                : isArmed
                  ? 'rgba(245, 158, 11, 0.25)'
                  : 'rgba(168, 85, 247, 0.15)',
              border: `1px solid ${
                isCapturing
                  ? '#ef4444'
                  : isArmed
                    ? '#f59e0b'
                    : 'rgba(168, 85, 247, 0.5)'
              }`,
              color: isCapturing ? '#fca5a5' : isArmed ? '#fcd34d' : '#c084fc',
              borderRadius: 2,
              cursor: 'pointer',
              fontWeight: 600,
              letterSpacing: '0.08em',
              animation: isArmed || isCapturing ? 'hex-flicker 0.8s infinite' : undefined,
            }}
          >
            {isCapturing
              ? '● RECORDING (DAW PLAYING)'
              : isArmed
                ? '● ARMED (PLAY DAW TO LEARN)'
                : '○ ARM / LEARN'}
          </button>

          {/* Zoom Buttons (Decision #1) */}
          <div style={{ display: 'flex', gap: 3, marginLeft: 8 }}>
            {(['4BARS', '8BARS', '16BARS', 'ALL'] as const).map((mode) => (
              <button
                key={mode}
                onClick={() => setZoomMode(mode)}
                style={{
                  fontSize: 7,
                  padding: '1px 6px',
                  background: zoomMode === mode ? 'rgba(255,255,255,0.18)' : 'rgba(255,255,255,0.04)',
                  border: `1px solid ${zoomMode === mode ? 'rgba(255,255,255,0.4)' : 'rgba(255,255,255,0.1)'}`,
                  color: zoomMode === mode ? '#fff' : 'rgba(255,255,255,0.45)',
                  borderRadius: 2,
                  cursor: 'pointer',
                }}
              >
                {mode === 'ALL' ? 'ALL' : mode.replace('BARS', ' B')}
              </button>
            ))}
          </div>

          <button
            onClick={() => setFollowPlayhead(!followPlayhead)}
            style={{
              fontSize: 7,
              padding: '1px 7px',
              background: followPlayhead ? 'rgba(74, 222, 128, 0.15)' : 'rgba(255,255,255,0.04)',
              border: `1px solid ${followPlayhead ? 'rgba(74, 222, 128, 0.45)' : 'rgba(255,255,255,0.1)'}`,
              color: followPlayhead ? '#4ade80' : 'rgba(255,255,255,0.4)',
              borderRadius: 2,
              cursor: 'pointer',
            }}
            title="Auto-scroll timeline with DAW transport"
          >
            {followPlayhead ? '✓ FOLLOW' : '○ FOLLOW'}
          </button>
        </div>

        {/* Right Toolbar Actions */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          {toastNotice && (
            <span style={{ fontSize: 7.5, color: '#4ade80', fontWeight: 600, letterSpacing: '0.06em' }}>
              {toastNotice}
            </span>
          )}
          <span style={{ fontSize: 7, color: 'rgba(255,255,255,0.4)' }}>
            POS: <span style={{ color: '#fff' }}>{Math.round(currentPos).toLocaleString()}</span> smp
          </span>
          <span style={{ fontSize: 7, color: 'rgba(255,255,255,0.4)' }}>
            BPM: <span style={{ color: '#fff' }}>{bpm.toFixed(1)}</span>
          </span>
          <span
            style={{
              fontSize: 7,
              color: telemetry.lutActive ? '#38bdf8' : 'rgba(255,255,255,0.35)',
              fontWeight: telemetry.lutActive ? 600 : 400,
            }}
          >
            {telemetry.lutActive ? '● RCU LUT ACTIVE' : '○ R3 FALLBACK'}
          </span>
          <button
            onClick={handleBakeAll}
            style={{
              fontSize: 7.5,
              padding: '2px 8px',
              background: 'rgba(168, 85, 247, 0.2)',
              border: '1px solid rgba(168, 85, 247, 0.6)',
              color: '#c084fc',
              borderRadius: 2,
              cursor: 'pointer',
              fontWeight: 600,
            }}
          >
            ⚡ BAKE LUT
          </button>
          <button
            onClick={handleClearAll}
            style={{
              fontSize: 7.5,
              padding: '2px 7px',
              background: 'rgba(255,255,255,0.04)',
              border: '1px solid rgba(255,255,255,0.12)',
              color: 'rgba(255,255,255,0.45)',
              borderRadius: 2,
              cursor: 'pointer',
            }}
          >
            ✕ CLEAR
          </button>
        </div>
      </div>

      {/* Main Graph Area */}
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
        {/* Ruler Bar (Bars & Seconds) */}
        <div
          style={{
            height: 20,
            borderBottom: '1px solid rgba(255,255,255,0.06)',
            display: 'flex',
            position: 'relative',
            fontSize: 7,
            color: 'rgba(255,255,255,0.35)',
            background: 'rgba(255,255,255,0.015)',
            flexShrink: 0,
            overflow: 'hidden',
          }}
        >
          {Array.from({ length: visibleBars + 1 }, (_, i) => {
            const barNum = Math.floor(effectiveScrollStart / barSamples) + i + 1;
            const barStart = (barNum - 1) * barSamples;
            const x = ((barStart - effectiveScrollStart) / visibleSamples) * width;
            const sec = (barStart / sr).toFixed(2);
            if (x < -20 || x > width + 20) return null;
            return (
              <div
                key={barNum}
                style={{
                  position: 'absolute',
                  left: x,
                  top: 0,
                  bottom: 0,
                  paddingLeft: 4,
                  display: 'flex',
                  alignItems: 'center',
                  gap: 6,
                  borderLeft: '1px solid rgba(255,255,255,0.12)',
                }}
              >
                <span style={{ color: '#fff', fontWeight: 600 }}>BAR {barNum}</span>
                <span style={{ color: 'rgba(255,255,255,0.22)' }}>{sec}s</span>
              </div>
            );
          })}
        </div>

        {/* Dual Waveform Canvas */}
        <div style={{ height: 138, position: 'relative', flexShrink: 0 }}>
          <canvas ref={mainCanvasRef} style={{ width, height: 138, display: 'block' }} />
        </div>

        {/* Heatmap Ribbon Canvas (Decision #3) */}
        <div style={{ height: 16, position: 'relative', flexShrink: 0, borderTop: '1px solid rgba(255,255,255,0.06)' }}>
          <canvas ref={ribbonCanvasRef} style={{ width, height: 16, display: 'block' }} />
        </div>

        {/* Beat Segments Area (Decision #2) */}
        <div
          style={{
            height: 74,
            borderTop: '1px solid rgba(255,255,255,0.08)',
            background: 'rgba(255,255,255,0.015)',
            position: 'relative',
            overflowX: 'hidden',
            overflowY: 'hidden',
            flexShrink: 0,
          }}
        >
          {segments
            .filter(
              (seg) =>
                seg.endSample >= effectiveScrollStart &&
                seg.startSample <= effectiveScrollStart + visibleSamples,
            )
            .map((seg) => {
              const x1 = ((seg.startSample - effectiveScrollStart) / visibleSamples) * width;
              const x2 = ((seg.endSample - effectiveScrollStart) / visibleSamples) * width;
              const segW = Math.max(34, x2 - x1);
              const isSelected = selectedSegmentId === seg.id;
              const corrGain = seg.correlationAfter - seg.correlationBefore;
              const isGoodCorr = seg.correlationAfter >= 0.5;

              return (
                <div
                  key={seg.id}
                  onClick={() => setSelectedSegmentId(seg.id)}
                  style={{
                    position: 'absolute',
                    left: x1,
                    width: segW,
                    top: 2,
                    bottom: 2,
                    border: isSelected
                      ? '1.5px solid #c084fc'
                      : seg.bypassed
                        ? '1px dashed rgba(255,255,255,0.2)'
                        : isGoodCorr
                          ? '1px solid rgba(34, 197, 94, 0.45)'
                          : '1px solid rgba(255,255,255,0.14)',
                    boxShadow: isSelected
                      ? '0 0 10px rgba(168, 85, 247, 0.35)'
                      : isGoodCorr && !seg.bypassed
                        ? '0 0 6px rgba(34, 197, 94, 0.2)'
                        : 'none',
                    background: seg.bypassed
                      ? 'rgba(255,255,255,0.02)'
                      : isSelected
                        ? 'rgba(168, 85, 247, 0.18)'
                        : 'rgba(255,255,255,0.045)',
                    borderRadius: 3,
                    padding: '3px 5px',
                    display: 'flex',
                    flexDirection: 'column',
                    justifyContent: 'space-between',
                    cursor: 'pointer',
                    boxSizing: 'border-box',
                    transition: 'border 0.15s ease, background 0.15s ease, box-shadow 0.15s ease',
                  }}
                >
                  {/* Segment Header */}
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                    <span style={{ fontSize: 8, color: '#FFFFFF', fontWeight: 700, letterSpacing: '0.04em' }}>{seg.id}</span>
                    <span
                      style={{
                        fontSize: 7.5,
                        color: seg.bypassed
                          ? 'rgba(255,255,255,0.45)'
                          : seg.correlationAfter > 0.5
                            ? '#4ade80'
                            : '#f87171',
                        fontWeight: 700,
                        fontVariantNumeric: 'tabular-nums',
                      }}
                    >
                      {seg.bypassed ? 'R3 FALLBACK' : `+${(corrGain * 100).toFixed(0)}%`}
                    </span>
                  </div>

                  {/* Delay & Rotate controls inside segment */}
                  {!seg.bypassed ? (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                      <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 7.5, color: 'rgba(255,255,255,0.7)', fontVariantNumeric: 'tabular-nums' }}>
                        <span style={{ fontWeight: 600 }}>DLY</span>
                        <span style={{ color: '#00D4FF', fontWeight: 700 }}>
                          {((seg.delaySamples / sr) * 1000).toFixed(2)}ms
                        </span>
                      </div>
                      <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 7.5, color: 'rgba(255,255,255,0.7)', fontVariantNumeric: 'tabular-nums' }}>
                        <span style={{ fontWeight: 600 }}>ROT</span>
                        <span style={{ color: '#c084fc', fontWeight: 700 }}>{seg.rotateDeg.toFixed(1)}°</span>
                      </div>
                    </div>
                  ) : (
                    <div style={{ fontSize: 7, color: 'rgba(255,255,255,0.4)', textAlign: 'center', fontWeight: 600 }}>
                      [BYPASSED]
                    </div>
                  )}

                  {/* Buttons: Polarity & Bypass */}
                  <div style={{ display: 'flex', justifyContent: 'space-between', gap: 3 }}>
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        updateSegment(seg.id, { polarityFlip: !seg.polarityFlip });
                      }}
                      style={{
                        fontSize: 7.5,
                        padding: '1.5px 5px',
                        background: seg.polarityFlip ? 'rgba(245, 158, 11, 0.35)' : 'rgba(255,255,255,0.06)',
                        border: `1px solid ${seg.polarityFlip ? '#f59e0b' : 'rgba(255,255,255,0.18)'}`,
                        color: seg.polarityFlip ? '#fcd34d' : 'rgba(255,255,255,0.85)',
                        borderRadius: 2,
                        cursor: 'pointer',
                        fontWeight: 700,
                      }}
                      title="Toggle polarity invert"
                    >
                      Ø
                    </button>
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        updateSegment(seg.id, { bypassed: !seg.bypassed });
                      }}
                      style={{
                        fontSize: 7.5,
                        padding: '1.5px 5px',
                        background: seg.bypassed ? 'rgba(239, 68, 68, 0.25)' : 'rgba(255,255,255,0.06)',
                        border: `1px solid ${seg.bypassed ? '#ef4444' : 'rgba(255,255,255,0.18)'}`,
                        color: seg.bypassed ? '#fca5a5' : 'rgba(255,255,255,0.85)',
                        borderRadius: 2,
                        cursor: 'pointer',
                        fontWeight: 700,
                      }}
                    >
                      {seg.bypassed ? 'USE' : 'BYP'}
                    </button>
                  </div>
                </div>
              );
            })}
        </div>

        {/* Playhead Vertical Line */}
        {mainPlayheadRatio !== null && (
          <div
            style={{
              position: 'absolute',
              left: `${mainPlayheadRatio * 100}%`,
              top: 0,
              bottom: 0,
              width: 2,
              background: '#ff4444',
              boxShadow: '0 0 10px rgba(255, 68, 68, 0.9)',
              pointerEvents: 'none',
              zIndex: 10,
            }}
          >
            <div
              style={{
                position: 'absolute',
                top: 0,
                left: -4,
                width: 10,
                height: 10,
                background: '#ff4444',
                clipPath: 'polygon(0 0, 100% 0, 50% 100%)',
              }}
            />
          </div>
        )}
      </div>

      {/* Bottom Song Overview Minimap (Decision #1) */}
      <div
        style={{
          height: 38,
          borderTop: '1px solid rgba(255,255,255,0.08)',
          display: 'flex',
          alignItems: 'center',
          padding: '0 16px',
          background: 'rgba(255,255,255,0.012)',
          flexShrink: 0,
          gap: 12,
          cursor: 'pointer',
        }}
        onClick={(e) => {
          const rect = e.currentTarget.getBoundingClientRect();
          const clickFrac = Math.max(0, Math.min(1, (e.clientX - rect.left - 16) / (width - 32)));
          const newStart = Math.max(0, clickFrac * totalSongSamples - visibleSamples / 2);
          setScrollStartSample(newStart);
          setFollowPlayhead(false);
        }}
      >
        <span style={{ fontSize: 7, color: 'rgba(255,255,255,0.3)', width: 52, flexShrink: 0 }}>
          SONG MAP
        </span>
        <canvas ref={minimapCanvasRef} style={{ width: width - 32, height: 28, display: 'block' }} />
      </div>
    </div>
  );
}

// ── Main PhreakPhase ───────────────────────────────────────────────────────
const PRESETS = ['808 LOW SLIP', 'TRAP SUB ALIGN', 'DnB PUNCH LOCK', 'HIP HOP THUMP', 'CUSTOM'];

// UI param key → APVTS parameter ID (sent via juceBridge on every change)
const PARAM_MAP: Record<string, string> = {
  crossoverFreq: 'CROSSOVER_FREQ',
  subRotate: 'SUB_ROTATE',
  subDelay: 'SUB_DELAY',
  subFlip: 'SUB_FLIP',
  subDynAmount: 'SUB_DYN_AMOUNT',
  highRotate: 'HIGH_ROTATE',
  highDelay: 'HIGH_DELAY',
  highDynAmount: 'HIGH_DYN_AMOUNT',
  envAttack: 'ENV_ATTACK',
  envRelease: 'ENV_RELEASE',
  lookaheadMs: 'LOOKAHEAD_MS',
  glueDrive: 'BASS_GLUE',
  alignTrackMode: 'ALIGN_TRACK_MODE',
};

// Backend sends "A1 - 55.0 Hz" → display uses note name + Hz separately
function parseNoteName(detectedNote: string): string {
  const cut = detectedNote.indexOf(' - ');
  return cut > 0 ? detectedNote.slice(0, cut) : detectedNote;
}

export default function PhreakPhase() {
  // ── Params ───────────────────────────────────────────────────────────────
  const [params, setParams] = useState<Params>({
    crossoverFreq: 90,
    subRotate: 0,
    subDelay: 0,
    subFlip: false,
    subDynAmount: 0,
    highRotate: 0,
    highDelay: 0,
    highDynAmount: 0,
    envAttack: 2.0,
    envRelease: 120.0,
    lookaheadMs: 5.0,
    glueDrive: 0,
    alignTrackMode: 'Cont',
  });

  const juceAvailable = isJuceAvailable();

  const setParam = useCallback(<K extends keyof Params>(key: K, val: Params[K]) => {
    setParams((p) => ({ ...p, [key]: val }));
    const paramId = PARAM_MAP[key as string] || (key as string);
    if (typeof val === 'number') {
      sendParameter(paramId, val);
    } else if (typeof val === 'boolean') {
      beginGesture(paramId);
      sendParameter(paramId, val ? 1 : 0);
      endGesture(paramId);
    } else if (key === 'alignTrackMode') {
      beginGesture(paramId);
      sendParameter(paramId, val === 'Trans' ? 1 : 0);
      endGesture(paramId);
    }
  }, []);

  // ── UI state ─────────────────────────────────────────────────────────────
  const [view, setView] = useState<ViewMode>('simple');
  const [trackMode, setTrackMode] = useState<TrackMode>('realtime');
  const [terminalOpen, setTerminalOpen] = useState(false);
  const [alignState, setAlignState] = useState<AlignState>('idle');
  const [lastAlignResult, setLastAlignResult] = useState<LastAlignResult | null>(null);
  const [phaseWheelCollapsed, setPhaseWheelCollapsed] = useState(false);
  const [preset, setPreset] = useState(0);
  const [markerOffsetMs, setMarkerOffsetMs] = useState(0);
  // ── Phase Wheel band selection (Decision #7: SUB / MID / FULL) ───────────
  const [phaseWheelBand, setPhaseWheelBand] = useState<PhaseWheelBand>('SUB');

  // ── Live telemetry (simulated, replaced by C++ events in prod) ───────────
  const [telemetry, setTelemetry] = useState<Telemetry>({
    correlation: 0.42,
    phaseAngleDeg: 12.4,
    rmsTrackA_dB: -18.4,
    rmsTrackB_dB: -22.1,
    peakTrackA_dB: -12.0,
    peakTrackB_dB: -15.0,
    phaseConfidence: 0.85,
    detectedFundamentalHz: 55.0,
    detectedNote: 'A1',
    isLocked: false,
    isPlaying: false,
    samplePosition: 0,
    bpm: 120,
    lutActive: false,
  });

  // ── C++ Timeline Waveform Overview (Zero Client Math) ────────────────────
  const [timelineOverview, setTimelineOverview] = useState<TimelineWaveformOverview | null>(null);
  const [engineEqCutDb, setEngineEqCutDb] = useState<number | null>(null);
  const [engineEqFreqHz, setEngineEqFreqHz] = useState<number | null>(null);

  useEffect(() => {
    if (!juceAvailable) return;
    return onTimelineWaveformOverview((overview) => {
      setTimelineOverview(overview);
    });
  }, [juceAvailable]);

  // ── EQ Suggestion overlay on Phase Wheel (Decision #5: MAutoAlign hint) ──
  const [eqApplied, setEqApplied] = useState(false);

  // ── Sidechain timeout warning (Decision #8: 2.5s → ⚠ NO SIDECHAIN, auto-stop) ──
  const [sidechainWarning, setSidechainWarning] = useState(false);
  const sidechainTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const eqSuggestion = useMemo(() => {
    const freq = engineEqFreqHz || lastAlignResult?.detectedFreq || telemetry.detectedFundamentalHz;
    const cut = engineEqCutDb !== null && engineEqCutDb !== undefined ? engineEqCutDb : -3.5;
    if (!freq || freq < 25 || freq > 250) return null;
    return {
      freq,
      cutDb: cut,
      applied: eqApplied,
    };
  }, [engineEqFreqHz, engineEqCutDb, lastAlignResult?.detectedFreq, telemetry.detectedFundamentalHz, eqApplied]);

  const handleApplyEq = useCallback((cutDb: number, freq: number) => {
    beginGesture('DYN_EQ_DEPTH');
    sendParameter('DYN_EQ_DEPTH', cutDb);
    endGesture('DYN_EQ_DEPTH');

    beginGesture('DYN_EQ_FREQ');
    sendParameter('DYN_EQ_FREQ', freq);
    endGesture('DYN_EQ_FREQ');

    setParams((p) => ({
      ...p,
      subDynAmount: Math.min(100, Math.round(Math.abs(cutDb) * 5.5)),
    }));
    setEqApplied(true);
  }, []);

  const [waveformA, setWaveformA] = useState<number[]>([]);
  const [waveformB, setWaveformB] = useState<number[]>([]);
  const rafRef = useRef<number>(0);

  // Real-time waveform stream from C++ backend (30 Hz)
  useEffect(() => {
    if (!juceAvailable) return;
    return onWaveform((frame) => {
      if (frame.trackA && frame.trackB) {
        setWaveformA(frame.trackA);
        setWaveformB(frame.trackB);
      }
    });
  }, [juceAvailable]);

  // Simulated fallback animation for browser dev mode outside JUCE
  useEffect(() => {
    if (juceAvailable) return;

    const animate = (now: number) => {
      const t = now / 1000;
      const phaseOff =
        (params.subRotate / 180) * Math.PI +
        (params.subDelay / 20) * Math.PI * 0.5;
      setWaveformA(generateWave(t, 0, 0.75, 256));
      setWaveformB(generateWave(t, phaseOff, 0.6, 256));

      setTelemetry((prev) => ({
        ...prev,
        correlation: Math.max(
          -1,
          Math.min(1, 0.38 + 0.48 * Math.sin(t * 0.28) + (Math.random() - 0.5) * 0.04),
        ),
        phaseAngleDeg:
          params.subRotate +
          18 * Math.sin(t * 0.45) +
          (Math.random() - 0.5) * 1.8,
        rmsTrackA_dB:
          -18 + 5 * Math.sin(t * 2.1) + (Math.random() - 0.5) * 1.2,
        rmsTrackB_dB:
          -22 + 4 * Math.sin(t * 1.7 + 1) + (Math.random() - 0.5) * 1.0,
        peakTrackA_dB: -12 + 4 * Math.sin(t * 2.1),
        peakTrackB_dB: -16 + 3 * Math.sin(t * 1.7 + 1),
        phaseConfidence: 0.85,
      }));

      rafRef.current = requestAnimationFrame(animate);
    };
    rafRef.current = requestAnimationFrame(animate);
    return () => cancelAnimationFrame(rafRef.current);
  }, [params.subRotate, params.subDelay, juceAvailable]);

  // Real-time telemetry from the C++ backend (official JUCE 8 event bridge).
  useEffect(() => {
    if (!juceAvailable) return;
    return onAudioFrame((frame: any) => {
      setTelemetry((prev) => ({
        ...prev,
        correlation: frame.correlation,
        phaseAngleDeg: frame.phaseAngleDeg,
        rmsTrackA_dB: frame.rmsTrackA_dB,
        rmsTrackB_dB: frame.rmsTrackB_dB,
        peakTrackA_dB: frame.peakTrackA_dB !== undefined ? frame.peakTrackA_dB : prev.peakTrackA_dB,
        peakTrackB_dB: frame.peakTrackB_dB !== undefined ? frame.peakTrackB_dB : prev.peakTrackB_dB,
        phaseConfidence: frame.phaseConfidence !== undefined ? frame.phaseConfidence : prev.phaseConfidence,
        detectedNote: frame.detectedNote ? parseNoteName(frame.detectedNote) : prev.detectedNote,
        detectedFundamentalHz: frame.detectedFundamentalHz || prev.detectedFundamentalHz,
        isPlaying: frame.isPlaying !== undefined ? frame.isPlaying : prev.isPlaying,
        samplePosition: frame.samplePosition !== undefined ? frame.samplePosition : prev.samplePosition,
        bpm: frame.bpm !== undefined ? frame.bpm : prev.bpm,
        lutActive: frame.lutActive !== undefined ? frame.lutActive : prev.lutActive,
        isTimelineArmed: frame.isTimelineArmed !== undefined ? frame.isTimelineArmed : prev.isTimelineArmed,
        isTimelineCapturing: frame.isTimelineCapturing !== undefined ? frame.isTimelineCapturing : prev.isTimelineCapturing,
        isTimelineAnalyzing: frame.isTimelineAnalyzing !== undefined ? frame.isTimelineAnalyzing : prev.isTimelineAnalyzing,
        timelineAnalysisProgress: frame.timelineAnalysisProgress !== undefined ? frame.timelineAnalysisProgress : prev.timelineAnalysisProgress,
      }));
    });
  }, [juceAvailable]);

  // ── Sidechain timeout detection (Decision #8) ────────────────────────────
  // Monitoruje Track B RMS: jeśli < -60dB przez 2.5s podczas SCANNING → warning + auto-stop
  const alignStateRef = useRef(alignState);
  useLayoutEffect(() => {
    alignStateRef.current = alignState;
  });

  useEffect(() => {
    const noSignal = telemetry.rmsTrackB_dB < -60;
    const isActive = alignState === 'scanning';

    if (noSignal && isActive) {
      // Cheap condition satisfied → arm timeout
      if (sidechainTimerRef.current === null) {
        sidechainTimerRef.current = setTimeout(() => {
          sidechainTimerRef.current = null;
          // Double-check we're still in scanning state (avoid stale closure)
          if (alignStateRef.current === 'scanning') {
            setSidechainWarning(true);
            // Auto-stop Smart Align z gestami DAW
            beginGesture('SUB_DELAY');
            sendParameter('SUB_DELAY', 0);
            endGesture('SUB_DELAY');
            setAlignState('timeout');
          }
        }, 2500);
      }
    } else {
      // Sygnał wrócił lub Smart Align nie jest aktywny — anuluj timer i clear warning
      if (sidechainTimerRef.current !== null) {
        clearTimeout(sidechainTimerRef.current);
        sidechainTimerRef.current = null;
      }
      if (!noSignal && sidechainWarning) {
        setSidechainWarning(false);
      }
    }

    return () => {
      if (sidechainTimerRef.current !== null) {
        clearTimeout(sidechainTimerRef.current);
        sidechainTimerRef.current = null;
      }
    };
  }, [telemetry.rmsTrackB_dB, alignState, sidechainWarning]);

  // Smart Align result from C++ backend
  useEffect(() => {
    if (!juceAvailable) return;
    return onAlignResult((result: AlignResult) => {
      if (result.timedOut || result.status === 'NO AUDIO - CHECK ROUTING') {
        setAlignState('timeout');
        setLastAlignResult({
          preCorrelation: result.preCorrelation ?? 0,
          postCorrelation: result.peakCorrelation ?? 0,
          correlationGain: 0,
          delayMs: 0,
          delaySamples: 0,
          rotateDeg: 0,
          flip: false,
          detectedFreq: 0,
          confidencePct: 0,
          qualityLabel: 'FAILED / NO AUDIO',
          status: 'NO AUDIO - CHECK ROUTING',
          timedOut: true,
        });
        return;
      }

      if (result.peakCorrelation < 0.05) {
        setAlignState('idle');
        return;
      }

      const pre = result.preCorrelation ?? 0;
      const post = result.peakCorrelation;
      const gain = result.correlationGain ?? (post - pre);
      const conf = Math.min(100, Math.max(0, Math.round(Math.abs(post) * 100)));
      let qual = 'GOOD';
      if (conf >= 90) qual = 'EXCELLENT';
      else if (conf >= 75) qual = 'STRONG';
      else if (conf < 50) qual = 'FAIR';

      if (result.suggestedEqCutDb !== undefined && result.suggestedEqFreqHz !== undefined) {
        setEngineEqCutDb(result.suggestedEqCutDb);
        setEngineEqFreqHz(result.suggestedEqFreqHz);
      }

      setAlignState('locked');
      setLastAlignResult({
        preCorrelation: pre,
        postCorrelation: post,
        correlationGain: gain,
        delayMs: result.delayMs,
        delaySamples:
          result.delaySamples !== undefined
            ? result.delaySamples
            : Math.round(((result.delayMs * (result.sampleRate ?? 44100)) / 1000)),
        rotateDeg: result.rotateDeg,
        flip: result.flip,
        detectedFreq: result.detectedFreq,
        confidencePct: conf,
        qualityLabel: qual,
        status: 'OK',
        timedOut: false,
        suggestedEqCutDb: result.suggestedEqCutDb,
        suggestedEqFreqHz: result.suggestedEqFreqHz,
      });

      setParams((p) => ({
        ...p,
        subDelay: parseFloat(result.delayMs.toFixed(2)),
        subRotate: parseFloat(result.rotateDeg.toFixed(1)),
        subFlip: result.flip,
      }));
      setTelemetry((prev) => ({
        ...prev,
        correlation: result.peakCorrelation,
        isLocked: true,
        detectedFundamentalHz: result.detectedFreq > 0 ? result.detectedFreq : prev.detectedFundamentalHz,
      }));
    });
  }, [juceAvailable]);

  // Smart align trigger: clicking when locked or timeout resets parameters with DAW gestures & returns to idle
  const handleSmartAlign = useCallback(() => {
    if (alignState === 'locked' || alignState === 'timeout') {
      beginGesture('SUB_DELAY');
      sendParameter('SUB_DELAY', 0);
      endGesture('SUB_DELAY');

      beginGesture('SUB_ROTATE');
      sendParameter('SUB_ROTATE', 0);
      endGesture('SUB_ROTATE');

      beginGesture('SUB_FLIP');
      sendParameter('SUB_FLIP', 0);
      endGesture('SUB_FLIP');

      setParams((p) => ({
        ...p,
        subDelay: 0,
        subRotate: 0,
        subFlip: false,
      }));

      setTelemetry((prev) => ({
        ...prev,
        isLocked: false,
        correlation: 0.0,
      }));

      setLastAlignResult(null);
      setAlignState('idle');
      return;
    }

    if (alignState === 'idle') {
      setAlignState('scanning');
      setLastAlignResult(null);
      triggerSmartAlign();
      if (!juceAvailable) {
        // Fallback for Vite dev server mock mode only
        setTimeout(() => {
          setAlignState('locked');
          setLastAlignResult({
            preCorrelation: -0.15,
            postCorrelation: 0.89,
            correlationGain: 1.04,
            delayMs: -1.42,
            delaySamples: -63,
            rotateDeg: 24.5,
            flip: false,
            detectedFreq: 55.0,
            confidencePct: 94,
            qualityLabel: 'EXCELLENT',
            status: 'OK',
            timedOut: false,
          });
          setParams((p) => ({
            ...p,
            subDelay: -1.42,
            subRotate: 24.5,
            subFlip: false,
          }));
          setTelemetry((prev) => ({
            ...prev,
            correlation: 0.89,
            isLocked: true,
          }));
        }, 1200);
      }
    }
  }, [alignState, juceAvailable]);

  // Layout dimensions (fixed 1120 × 650)
  const W = 1120;
  const HEADER_H = 48;
  const HERO_H = 344;
  const DECK_H = 228;
  const STATUS_H = 30;

  return (
    <div
      style={{
        width: W,
        height: HEADER_H + HERO_H + DECK_H + STATUS_H,
        background: 'var(--color-surface-bg, #07070B)',
        overflow: 'hidden',
        userSelect: 'none',
        fontFamily: 'JetBrains Mono, monospace',
        color: '#F2F2F7',
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
      }}
    >
      {/* ── UNIFIED ADAPTIVE HEADER (48px) (Decyzja #2) ─────────────────── */}
      <header
        style={{
          height: HEADER_H,
          borderBottom: '1px solid rgba(255,255,255,0.09)',
          display: 'flex',
          alignItems: 'center',
          padding: '0 16px',
          gap: 16,
          background: 'var(--color-surface-panel, #0D0D15)',
          flexShrink: 0,
          zIndex: 25,
        }}
      >
        {/* Left: Branding & Track meters */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
          <span
            style={{
              fontSize: 12,
              fontWeight: 800,
              letterSpacing: '0.28em',
              color: '#FFFFFF',
              textTransform: 'uppercase',
              textShadow: '0 0 12px rgba(255,255,255,0.25)',
            }}
          >
            PHREAK<span style={{ color: '#00D4FF' }}>PHASE</span>
          </span>
          <div style={{ width: 1, height: 20, background: 'rgba(255,255,255,0.12)' }} />
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ fontSize: 8.5, color: '#FFB800', fontWeight: 600, letterSpacing: '0.1em' }}>
              A // KICK
            </span>
            <VuMeter db={telemetry.rmsTrackA_dB} peakDb={telemetry.peakTrackA_dB} color="#FFB800" />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ fontSize: 8.5, color: '#00D4FF', fontWeight: 600, letterSpacing: '0.1em' }}>
              B // BASS
            </span>
            <VuMeter db={telemetry.rmsTrackB_dB} peakDb={telemetry.peakTrackB_dB} color="#00D4FF" />
          </div>
        </div>

        {/* Center: Preset Selector */}
        <div style={{ flex: 1, display: 'flex', justifyContent: 'center', alignItems: 'center', gap: 8 }}>
          <span style={{ fontSize: 8, color: 'rgba(255,255,255,0.5)', letterSpacing: '0.14em', fontWeight: 600 }}>
            PRESET:
          </span>
          <select
            value={preset}
            onChange={(e) => {
              const index = Number(e.target.value);
              setPreset(index);
              selectPreset(index);
            }}
            style={{
              background: '#12121E',
              border: '1px solid rgba(255,255,255,0.16)',
              color: '#FFFFFF',
              fontSize: 9,
              padding: '3px 10px',
              fontFamily: 'JetBrains Mono, monospace',
              letterSpacing: '0.08em',
              outline: 'none',
              cursor: 'pointer',
              borderRadius: 2,
              fontWeight: 500,
            }}
          >
            {PRESETS.map((p, i) => (
              <option key={i} value={i} style={{ background: '#0C0C14' }}>
                [ {p} ]
              </option>
            ))}
          </select>
        </div>

        {/* Right: Mode switches (Contextual) */}
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          {/* TRACK MODE: REAL-TIME vs TIMELINE */}
          <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
            <span style={{ fontSize: 8, color: 'rgba(255,255,255,0.5)', letterSpacing: '0.12em', fontWeight: 600 }}>
              MODE
            </span>
            <SegToggle
              options={[
                { label: 'REAL-TIME', value: 'realtime' as TrackMode },
                { label: 'TIMELINE', value: 'timeline' as TrackMode },
              ]}
              value={trackMode}
              onChange={(m) => setTrackMode(m)}
            />
          </div>

          {/* Contextual VIEW MODE: only in real-time */}
          {trackMode === 'realtime' && (
            <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
              <span style={{ fontSize: 8, color: 'rgba(255,255,255,0.5)', letterSpacing: '0.12em', fontWeight: 600 }}>
                VIEW
              </span>
              <SegToggle
                options={[
                  { label: 'SIMPLE', value: 'simple' as ViewMode },
                  { label: 'ADVANCED', value: 'advanced' as ViewMode },
                ]}
                value={view}
                onChange={setView}
              />
            </div>
          )}

          {/* Contextual ALIGN MODE: only in real-time */}
          {trackMode === 'realtime' && (
            <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
              <span style={{ fontSize: 8, color: 'rgba(255,255,255,0.5)', letterSpacing: '0.12em', fontWeight: 600 }}>
                ALIGN
              </span>
              <SegToggle
                options={[
                  { label: 'CONTINUOUS', value: 'Cont' as const },
                  { label: 'TRANSIENT', value: 'Trans' as const },
                ]}
                value={params.alignTrackMode}
                onChange={(v) => setParam('alignTrackMode', v)}
              />
            </div>
          )}

          {/* Terminal Button */}
          <button
            onClick={() => setTerminalOpen(!terminalOpen)}
            style={{
              background: terminalOpen ? 'rgba(255,255,255,0.18)' : '#12121E',
              border: `1px solid ${terminalOpen ? 'rgba(255,255,255,0.45)' : 'rgba(255,255,255,0.16)'}`,
              color: terminalOpen ? '#FFFFFF' : 'rgba(255,255,255,0.7)',
              fontSize: 8.5,
              padding: '4px 10px',
              cursor: 'pointer',
              fontFamily: 'JetBrains Mono, monospace',
              letterSpacing: '0.1em',
              fontWeight: 600,
              borderRadius: 2,
              transition: 'all 0.15s ease',
            }}
          >
            [ ? / SYS ]
          </button>
        </div>
      </header>

      {/* ── HERO (340px) ─────────────────────────────────────────────────── */}
      <main
        style={{
          height: HERO_H,
          flexShrink: 0,
          borderBottom: '1px solid rgba(255,255,255,0.08)',
          position: 'relative',
          overflow: 'hidden',
        }}
      >
        {/* Dither dot backdrop */}
        <div
          style={{
            position: 'absolute',
            inset: 0,
            backgroundImage:
              'radial-gradient(rgba(255,255,255,0.042) 1px, transparent 1px)',
            backgroundSize: '3px 3px',
            pointerEvents: 'none',
          }}
        />

        {trackMode === 'timeline' ? (
          <TimelineView
            telemetry={telemetry}
            params={params}
            waveformA={waveformA}
            waveformB={waveformB}
            timelineOverview={timelineOverview}
            width={W}
            height={HERO_H}
          />
        ) : view === 'simple' ? (
          /* ── SIMPLE MODE ── */
          <div style={{ height: '100%', display: 'flex', flexDirection: 'column', position: 'relative', zIndex: 1 }}>
            {/* Status & Results strip */}
            <div
              style={{
                height: 30,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                padding: '0 18px',
                borderBottom: '1px solid rgba(255,255,255,0.08)',
                background:
                  alignState === 'locked'
                    ? 'rgba(74, 222, 128, 0.08)'
                    : alignState === 'timeout'
                      ? 'rgba(239, 68, 68, 0.10)'
                      : 'rgba(255,255,255,0.015)',
                flexShrink: 0,
                transition: 'background 0.3s ease',
              }}
            >
              {alignState === 'locked' && lastAlignResult ? (
                <>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
                    <span
                      style={{
                        fontSize: 8.5,
                        padding: '2px 8px',
                        background: 'rgba(74, 222, 128, 0.20)',
                        border: '1px solid rgba(74, 222, 128, 0.6)',
                        color: '#4ade80',
                        borderRadius: 2,
                        fontWeight: 700,
                        letterSpacing: '0.08em',
                      }}
                    >
                      ● {lastAlignResult.confidencePct}% {lastAlignResult.qualityLabel}
                    </span>
                    <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.65)' }}>
                      CORR:{' '}
                      <span style={{ color: 'rgba(255,255,255,0.45)' }}>
                        {(lastAlignResult.preCorrelation * 100).toFixed(0)}%
                      </span>
                      {' → '}
                      <span style={{ color: '#4ade80', fontWeight: 700 }}>
                        +{(lastAlignResult.postCorrelation * 100).toFixed(0)}%
                      </span>{' '}
                      <span style={{ color: '#00D4FF', fontWeight: 600 }}>
                        (+{(lastAlignResult.correlationGain * 100).toFixed(0)}%)
                      </span>
                    </span>
                    <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.65)' }}>
                      DLY:{' '}
                      <span style={{ color: '#FFFFFF', fontWeight: 700 }}>
                        {lastAlignResult.delayMs >= 0 ? '+' : ''}{lastAlignResult.delayMs.toFixed(2)}ms
                      </span>{' '}
                      <span style={{ color: 'rgba(255,255,255,0.45)' }}>
                        ({lastAlignResult.delaySamples >= 0 ? '+' : ''}{Math.round(lastAlignResult.delaySamples)} smp)
                      </span>
                    </span>
                    <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.65)' }}>
                      ROT:{' '}
                      <span style={{ color: '#FFFFFF', fontWeight: 700 }}>
                        {lastAlignResult.rotateDeg >= 0 ? '+' : ''}{lastAlignResult.rotateDeg.toFixed(1)}°
                      </span>
                    </span>
                    <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.65)' }}>
                      POL:{' '}
                      <span style={{ color: lastAlignResult.flip ? '#f59e0b' : '#4ade80', fontWeight: 700 }}>
                        {lastAlignResult.flip ? 'INVERTED [Ø]' : 'NORMAL'}
                      </span>
                    </span>
                  </div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.50)' }}>
                      FUND: {telemetry.detectedNote} — {telemetry.detectedFundamentalHz.toFixed(1)}Hz
                    </span>
                    <button
                      onClick={handleSmartAlign}
                      style={{
                        fontSize: 8,
                        padding: '2px 9px',
                        background: 'rgba(255,255,255,0.08)',
                        border: '1px solid rgba(255,255,255,0.25)',
                        color: '#FFFFFF',
                        borderRadius: 2,
                        cursor: 'pointer',
                        fontFamily: 'JetBrains Mono, monospace',
                        fontWeight: 600,
                      }}
                    >
                      [ RESET ]
                    </button>
                  </div>
                </>
              ) : alignState === 'timeout' ? (
                <>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <span
                      style={{
                        fontSize: 8.5,
                        padding: '2px 8px',
                        background: 'rgba(239, 68, 68, 0.22)',
                        border: '1px solid rgba(239, 68, 68, 0.65)',
                        color: '#ef4444',
                        borderRadius: 2,
                        fontWeight: 700,
                        letterSpacing: '0.08em',
                      }}
                    >
                      ⚠ TIMEOUT (2.5s)
                    </span>
                    <span style={{ fontSize: 8.5, color: '#fca5a5', letterSpacing: '0.06em', fontWeight: 600 }}>
                      NO AUDIO DETECTED — CHECK SIDECHAIN / MAIN ROUTING
                    </span>
                  </div>
                  <button
                    onClick={handleSmartAlign}
                    style={{
                      fontSize: 8,
                      padding: '2px 9px',
                      background: 'rgba(239, 68, 68, 0.18)',
                      border: '1px solid rgba(239, 68, 68, 0.55)',
                      color: '#fca5a5',
                      borderRadius: 2,
                      cursor: 'pointer',
                      fontFamily: 'JetBrains Mono, monospace',
                      fontWeight: 600,
                    }}
                  >
                    [ ↺ RETRY SMART ALIGN ]
                  </button>
                </>
              ) : alignState === 'scanning' ? (
                <>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <span
                      style={{
                        fontSize: 8.5,
                        padding: '2px 8px',
                        background: 'rgba(0, 212, 255, 0.15)',
                        border: '1px solid rgba(0, 212, 255, 0.45)',
                        color: '#00D4FF',
                        borderRadius: 2,
                        letterSpacing: '0.08em',
                        fontWeight: 700,
                        animation: 'hex-flicker 0.4s linear infinite',
                      }}
                    >
                      ◌ GCC-PHAT SCANNING...
                    </span>
                    <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.55)', letterSpacing: '0.06em' }}>
                      ESTIMATING SUB-SAMPLE TIME DELAY & EXPONENTIAL OCTAVE PHASE
                    </span>
                  </div>
                  <span style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.45)' }}>
                    FUND: {telemetry.detectedNote} — {telemetry.detectedFundamentalHz.toFixed(1)}Hz
                  </span>
                </>
              ) : (
                <>
                  <div style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.45)', letterSpacing: '0.08em' }}>
                    // STANDBY — CLICK SMART ALIGN TO COMMENCE PHASE ANALYSIS //
                  </div>
                  <div style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.35)', letterSpacing: '0.06em' }}>
                    FUND: {telemetry.detectedNote} — {telemetry.detectedFundamentalHz.toFixed(1)}Hz
                  </div>
                  <div style={{ fontSize: 8.5, color: 'rgba(255,255,255,0.45)', letterSpacing: '0.06em' }}>
                    XOVER: {freqToNote(params.crossoverFreq)} / {params.crossoverFreq.toFixed(0)}Hz &nbsp;|&nbsp; PDC:{' '}
                    {params.lookaheadMs.toFixed(1)}ms
                  </div>
                </>
              )}
            </div>

            {/* Center: waveform sphere + SmartAlign + knobs */}
            <div
              style={{
                flex: 1,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                gap: 36,
                padding: '0 24px',
                position: 'relative',
              }}
            >
              {/* Left knob group (Sub) */}
              <div style={{ display: 'flex', gap: 20, alignItems: 'center' }}>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 5 }}>
                  <div style={{ fontSize: 8, color: '#00D4FF', letterSpacing: '0.14em', fontWeight: 600 }}>
                    [ SUB ]
                  </div>
                  <CyberKnob
                    label="ROTATE"
                    value={params.subRotate}
                    min={-180}
                    max={180}
                    defaultValue={0}
                    unit="°"
                    size={72}
                    decimals={1}
                    onChange={(v) => setParam('subRotate', v)}
                    paramId="SUB_ROTATE"
                    bipolar
                    accentColor="#00D4FF"
                  />
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 5 }}>
                  <div style={{ fontSize: 8, color: '#00D4FF', letterSpacing: '0.14em', fontWeight: 600 }}>
                    [ DELAY ]
                  </div>
                  <CyberKnob
                    label="OFFSET"
                    value={params.subDelay}
                    min={-20}
                    max={20}
                    defaultValue={0}
                    unit="ms"
                    size={72}
                    decimals={2}
                    onChange={(v) => setParam('subDelay', v)}
                    paramId="SUB_DELAY"
                    bipolar
                    accentColor="#00D4FF"
                  />
                </div>
              </div>

              {/* Waveform sphere + SmartAlign overlay */}
              <div style={{ position: 'relative', flexShrink: 0 }}>
                <WaveformSphere
                  waveformA={waveformA}
                  waveformB={waveformB}
                  size={234}
                  opacity={0.24}
                />
                {/* SmartAlign button centered over sphere */}
                <div
                  style={{
                    position: 'absolute',
                    inset: 0,
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                  }}
                >
                  <SmartAlignButton
                    state={alignState}
                    onTrigger={handleSmartAlign}
                    correlation={lastAlignResult ? lastAlignResult.postCorrelation : telemetry.correlation}
                    preCorrelation={lastAlignResult ? lastAlignResult.preCorrelation : undefined}
                    improvementPct={lastAlignResult ? Math.round(lastAlignResult.correlationGain * 100) : undefined}
                    confidencePct={lastAlignResult?.confidencePct}
                    statusMessage={lastAlignResult?.status}
                    delayMs={lastAlignResult?.delayMs}
                    delaySamples={lastAlignResult?.delaySamples}
                    rotateDeg={lastAlignResult?.rotateDeg}
                    flip={lastAlignResult?.flip}
                  />
                  {/* ⚠ NO SIDECHAIN warning — Decision #8 */}
                  {sidechainWarning && (
                    <div
                      style={{
                        position: 'absolute',
                        bottom: -28,
                        left: '50%',
                        transform: 'translateX(-50%)',
                        display: 'flex',
                        alignItems: 'center',
                        gap: 5,
                        padding: '3px 10px',
                        background: 'rgba(239, 68, 68, 0.22)',
                        border: '1px solid rgba(239, 68, 68, 0.65)',
                        borderRadius: 2,
                        whiteSpace: 'nowrap',
                        fontFamily: 'JetBrains Mono, monospace',
                        fontSize: 8.5,
                        color: '#fca5a5',
                        letterSpacing: '0.08em',
                        fontWeight: 700,
                        pointerEvents: 'none',
                      }}
                    >
                      ⚠ NO SIDECHAIN
                    </div>
                  )}
                </div>
              </div>

              {/* Right knob group (Glue & Xover) */}
              <div style={{ display: 'flex', gap: 20, alignItems: 'center' }}>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 5 }}>
                  <div style={{ fontSize: 8, color: '#FFB800', letterSpacing: '0.14em', fontWeight: 600 }}>
                    [ GLUE ]
                  </div>
                  <CyberKnob
                    label="DRIVE"
                    value={params.glueDrive}
                    min={0}
                    max={100}
                    defaultValue={0}
                    unit="%"
                    size={72}
                    decimals={1}
                    onChange={(v) => setParam('glueDrive', v)}
                    paramId="GLUE_DRIVE"
                    accentColor="#FFB800"
                  />
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 5 }}>
                  <div style={{ fontSize: 8, color: '#A855F7', letterSpacing: '0.14em', fontWeight: 600 }}>
                    [ XOVER ]
                  </div>
                  <CyberKnob
                    label="FREQ"
                    value={params.crossoverFreq}
                    min={40}
                    max={300}
                    defaultValue={90}
                    unit="Hz"
                    size={72}
                    decimals={0}
                    onChange={(v) => setParam('crossoverFreq', v)}
                    paramId="CROSSOVER_FREQ"
                    accentColor="#A855F7"
                  />
                </div>
              </div>
            </div>

            {/* Correlation + phase readout strip */}
            <div
              style={{
                height: 76,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                borderTop: '1px solid rgba(255,255,255,0.08)',
                gap: 52,
                flexShrink: 0,
                background: 'rgba(255,255,255,0.01)',
              }}
            >
              <CorrelationArc correlation={telemetry.correlation} />

              <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 180 }}>
                <div style={{ fontSize: 8.5, fontWeight: 700, color: 'rgba(255,255,255,0.5)', letterSpacing: '0.12em' }}>
                  INSTANTANEOUS PHASE
                </div>
                <div
                  style={{
                    fontSize: 22,
                    color: '#FFFFFF',
                    letterSpacing: '0.04em',
                    fontWeight: 800,
                    fontVariantNumeric: 'tabular-nums',
                  }}
                >
                  [{' '}
                  <span
                    style={{
                      color: '#FFFFFF',
                      textShadow: '0 0 14px rgba(255,255,255,0.5)',
                    }}
                  >
                    {telemetry.phaseAngleDeg >= 0 ? '+' : ''}
                    {telemetry.phaseAngleDeg.toFixed(1)}°
                  </span>{' '}
                  ]
                </div>
                <div
                  style={{
                    fontSize: 8.5,
                    color: 'rgba(255,255,255,0.45)',
                    letterSpacing: '0.06em',
                    fontVariantNumeric: 'tabular-nums',
                  }}
                >
                  ROT: <span style={{ color: '#fff' }}>{params.subRotate >= 0 ? '+' : ''}{params.subRotate.toFixed(1)}°</span> &nbsp;|&nbsp; DLY:{' '}
                  <span style={{ color: '#fff' }}>{params.subDelay >= 0 ? '+' : ''}{params.subDelay.toFixed(2)}ms</span>
                </div>
              </div>
            </div>
          </div>
        ) : (
          /* ── ADVANCED MODE (Split View Dock — Decyzja #1) ── */
          <div
            style={{
              height: '100%',
              display: 'flex',
              flexDirection: 'column',
              position: 'relative',
              zIndex: 1,
              background: '#08080E',
            }}
          >
            {/* Top Row: Oscilloscope (840px) + Analytical Dock (280px) */}
            <div
              style={{
                height: 246,
                display: 'flex',
                borderBottom: '1px solid rgba(255,255,255,0.08)',
                flexShrink: 0,
              }}
            >
              {/* Left Column: Full Unoccluded Oscilloscope (840px) */}
              <div
                style={{
                  width: W - 280,
                  height: '100%',
                  position: 'relative',
                  overflow: 'hidden',
                }}
              >
                <Oscilloscope
                  trackA={waveformA}
                  trackB={waveformB}
                  markerOffsetMs={markerOffsetMs}
                  onMarkerChange={setMarkerOffsetMs}
                  width={W - 280}
                  height={246}
                  subDelayMs={params.subDelay}
                  onSubDelayChange={(ms) => setParam('subDelay', ms)}
                />
              </div>

              {/* Right Column: Dedicated Phase Vectorscope Dock (280px) */}
              <div
                style={{
                  width: 280,
                  height: '100%',
                  borderLeft: '1px solid rgba(255,255,255,0.08)',
                  background: 'var(--color-surface-panel, #0B0B14)',
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  padding: '8px 12px',
                  boxSizing: 'border-box',
                  flexShrink: 0,
                }}
              >
                {/* Vectorscope Header + Band Selector */}
                <div
                  style={{
                    width: '100%',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                    marginBottom: 6,
                    paddingBottom: 5,
                    borderBottom: '1px solid rgba(255,255,255,0.06)',
                  }}
                >
                  <span
                    style={{
                      fontSize: 8.5,
                      fontWeight: 700,
                      letterSpacing: '0.12em',
                      color: '#00D4FF',
                      textTransform: 'uppercase',
                    }}
                  >
                    // VECTORSCOPE
                  </span>

                  {/* Band Selector */}
                  <div style={{ display: 'flex', gap: 3 }}>
                    {(['SUB', 'MID', 'FULL'] as PhaseWheelBand[]).map((b) => {
                      const isSel = phaseWheelBand === b;
                      return (
                        <button
                          key={b}
                          onClick={() => setPhaseWheelBand(b)}
                          style={{
                            fontSize: 7.5,
                            fontWeight: isSel ? 700 : 500,
                            padding: '1px 6px',
                            background: isSel ? 'rgba(0, 212, 255, 0.22)' : 'rgba(255,255,255,0.04)',
                            border: `1px solid ${isSel ? '#00D4FF' : 'rgba(255,255,255,0.1)'}`,
                            color: isSel ? '#00D4FF' : 'rgba(255,255,255,0.5)',
                            borderRadius: 2,
                            cursor: 'pointer',
                            fontFamily: 'JetBrains Mono, monospace',
                            transition: 'all 0.15s ease',
                          }}
                        >
                          {b}
                        </button>
                      );
                    })}
                  </div>
                </div>

                {/* Phase Wheel Canvas (150px) */}
                <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                  <PhaseWheel
                    phaseAngleDeg={telemetry.phaseAngleDeg}
                    correlation={telemetry.correlation}
                    phaseConfidence={telemetry.phaseConfidence}
                    size={146}
                    collapsed={false}
                    band={phaseWheelBand}
                    onBandChange={setPhaseWheelBand}
                    eqSuggestion={eqSuggestion}
                    onApplyEq={handleApplyEq}
                  />
                </div>

                {/* Instantaneous Angle & Correlation Pill */}
                <div
                  style={{
                    width: '100%',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                    marginTop: 4,
                    paddingTop: 4,
                    borderTop: '1px solid rgba(255,255,255,0.05)',
                  }}
                >
                  <span
                    style={{
                      fontSize: 12,
                      fontWeight: 800,
                      color: '#FFFFFF',
                      fontVariantNumeric: 'tabular-nums',
                      letterSpacing: '0.04em',
                    }}
                  >
                    {telemetry.phaseAngleDeg >= 0 ? '+' : ''}
                    {telemetry.phaseAngleDeg.toFixed(1)}°
                  </span>
                  <span
                    style={{
                      fontSize: 8.5,
                      fontWeight: 600,
                      padding: '1px 6px',
                      borderRadius: 2,
                      background:
                        telemetry.correlation >= 0.5
                          ? 'rgba(74, 222, 128, 0.15)'
                          : telemetry.correlation >= 0
                            ? 'rgba(245, 158, 11, 0.15)'
                            : 'rgba(239, 68, 68, 0.15)',
                      color:
                        telemetry.correlation >= 0.5
                          ? '#4ade80'
                          : telemetry.correlation >= 0
                            ? '#f59e0b'
                            : '#ef4444',
                      border: `1px solid ${
                        telemetry.correlation >= 0.5
                          ? 'rgba(74, 222, 128, 0.4)'
                          : telemetry.correlation >= 0
                            ? 'rgba(245, 158, 11, 0.4)'
                            : 'rgba(239, 68, 68, 0.4)'
                      }`,
                    }}
                  >
                    r = {telemetry.correlation >= 0 ? '+' : ''}
                    {telemetry.correlation.toFixed(2)}
                  </span>
                </div>
              </div>
            </div>

            {/* Bottom strip — fixed 98px */}
            <div
              style={{
                height: 98,
                display: 'flex',
                alignItems: 'center',
                padding: '0 18px',
                gap: 0,
                flexShrink: 0,
                background: 'var(--color-surface-bg, #07070B)',
              }}
            >
              {/* Correlation arc */}
              <div style={{ marginRight: 18 }}>
                <CorrelationArc correlation={telemetry.correlation} />
              </div>

              <div
                style={{
                  width: 1,
                  height: 64,
                  background: 'rgba(255,255,255,0.08)',
                  marginRight: 18,
                  flexShrink: 0,
                }}
              />

              {/* Telemetry readouts */}
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'column',
                  gap: 3,
                  minWidth: 200,
                  marginRight: 22,
                }}
              >
                <div
                  style={{
                    fontSize: 8,
                    fontWeight: 700,
                    color: 'rgba(255,255,255,0.45)',
                    letterSpacing: '0.14em',
                    marginBottom: 2,
                  }}
                >
                  // TELEMETRY
                </div>
                {(
                  [
                    ['PHASE', `${telemetry.phaseAngleDeg >= 0 ? '+' : ''}${telemetry.phaseAngleDeg.toFixed(1)}°`],
                    ['FUND', `${telemetry.detectedNote} — ${telemetry.detectedFundamentalHz.toFixed(1)}Hz`],
                    ['A RMS', `${telemetry.rmsTrackA_dB.toFixed(1)}dB`],
                    ['B RMS', `${telemetry.rmsTrackB_dB.toFixed(1)}dB`],
                    ['MARKER', `${markerOffsetMs >= 0 ? '+' : ''}${markerOffsetMs.toFixed(2)}ms`],
                  ] as [string, string][]
                ).map(([k, v]) => (
                  <div key={k} style={{ display: 'flex', gap: 10 }}>
                    <span
                      style={{
                        fontSize: 8.5,
                        color: 'rgba(255,255,255,0.50)',
                        letterSpacing: '0.08em',
                        minWidth: 54,
                        fontWeight: 600,
                      }}
                    >
                      {k}
                    </span>
                    <span
                      style={{
                        fontSize: 8.5,
                        color: '#FFFFFF',
                        letterSpacing: '0.06em',
                        fontVariantNumeric: 'tabular-nums',
                        fontWeight: 500,
                      }}
                    >
                      {v}
                    </span>
                  </div>
                ))}
              </div>

              <div
                style={{
                  width: 1,
                  height: 64,
                  background: 'rgba(255,255,255,0.08)',
                  marginRight: 22,
                  flexShrink: 0,
                }}
              />

              {/* Compact align button */}
              <CompactAlignBtn
                state={alignState}
                onTrigger={handleSmartAlign}
                correlation={lastAlignResult ? lastAlignResult.postCorrelation : telemetry.correlation}
                result={lastAlignResult}
              />

              <div style={{ flex: 1 }} />

              {/* Tracking mode + lookahead */}
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'flex-end',
                  gap: 7,
                }}
              >
                <div style={{ fontSize: 8, fontWeight: 600, color: 'rgba(255,255,255,0.45)', letterSpacing: '0.12em' }}>
                  // TRACKING MODE
                </div>
                <SegToggle
                  options={[
                    { label: 'CONTINUOUS', value: 'Cont' as const },
                    { label: 'TRANSIENT', value: 'Trans' as const },
                  ]}
                  value={params.alignTrackMode}
                  onChange={(v) => setParam('alignTrackMode', v)}
                />
                <div
                  style={{
                    fontSize: 8,
                    color: 'rgba(255,255,255,0.35)',
                    letterSpacing: '0.08em',
                    fontVariantNumeric: 'tabular-nums',
                  }}
                >
                  PDC LOOKAHEAD: {params.lookaheadMs.toFixed(1)}ms
                </div>
              </div>
            </div>
          </div>
        )}
      </main>

      {/* ── CONTROL DECK (228px) ─────────────────────────────────────────── */}
      <section
        style={{
          height: DECK_H,
          flexShrink: 0,
          borderBottom: '1px solid rgba(255,255,255,0.07)',
          background: 'rgba(255,255,255,0.008)',
          position: 'relative',
          overflow: 'hidden',
        }}
      >
        {/* Dither backdrop */}
        <div
          style={{
            position: 'absolute',
            inset: 0,
            backgroundImage:
              'radial-gradient(rgba(255,255,255,0.035) 1px, transparent 1px)',
            backgroundSize: '4px 4px',
            pointerEvents: 'none',
          }}
        />
        <div style={{ position: 'relative', zIndex: 1, height: '100%' }}>
          <ControlPanel params={params} setParam={setParam} />
        </div>
      </section>

      {/* ── STATUS BAR (30px) ────────────────────────────────────────────── */}
      <footer
        style={{
          height: STATUS_H,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          padding: '0 14px',
          gap: 14,
          background: 'rgba(0,0,0,0.35)',
        }}
      >
        <span
          style={{
            fontSize: 7,
            color: 'rgba(255,255,255,0.18)',
            letterSpacing: '0.1em',
          }}
        >
          // PHREAKPHASE V2.0.4 — JUCE 7/8 WEBVIEW
        </span>
        <div style={{ flex: 1 }} />
        <span
          style={{
            fontSize: 7,
            color: 'rgba(255,255,255,0.14)',
            letterSpacing: '0.08em',
          }}
        >
          SR: 44.1kHz &nbsp;|&nbsp; BUF: 512 smp &nbsp;|&nbsp;
        </span>
        <span
          style={{
            fontSize: 7,
            letterSpacing: '0.1em',
            color:
              alignState === 'locked'
                ? 'rgba(255,255,255,0.7)'
                : alignState === 'scanning'
                  ? 'rgba(255,255,255,0.45)'
                  : 'rgba(255,255,255,0.18)',
            transition: 'color 0.35s ease',
            fontVariantNumeric: 'tabular-nums',
          }}
        >
          {alignState === 'locked'
            ? '● ALIGNED'
            : alignState === 'scanning'
              ? '◌ SCANNING...'
              : '○ STANDBY'}
        </span>
        <div
          style={{
            width: 1,
            height: 12,
            background: 'rgba(255,255,255,0.08)',
          }}
        />
        <span
          style={{
            fontSize: 7,
            color: 'rgba(255,255,255,0.14)',
            letterSpacing: '0.08em',
          }}
        >
          PDC: {params.lookaheadMs.toFixed(1)}ms
        </span>
      </footer>

      {/* ── SYS TERMINAL ─────────────────────────────────────────────────── */}
      <SysTerminal isOpen={terminalOpen} onClose={() => setTerminalOpen(false)} />
    </div>
  );
}

import { useRef, useEffect, useState } from 'react';

export type PhaseWheelBand = 'SUB' | 'MID' | 'FULL';

export interface EqSuggestion {
  freq: number;
  cutDb: number;
  applied?: boolean;
}

interface PhaseWheelProps {
  phaseAngleDeg: number;
  correlation: number;
  phaseConfidence?: number;
  size: number;
  collapsed?: boolean;
  onToggleCollapse?: () => void;
  /** Band selector: które dane analizuje Phase Wheel */
  band?: PhaseWheelBand;
  onBandChange?: (band: PhaseWheelBand) => void;
  /** Sugestia EQ wycinającego (MAutoAlign style) — overlay na Phase Wheel (Decision #5) */
  eqSuggestion?: EqSuggestion | null;
  onApplyEq?: (cutDb: number, freq: number) => void;
}

export default function PhaseWheel({
  phaseAngleDeg,
  correlation,
  phaseConfidence,
  size,
  collapsed = false,
  onToggleCollapse,
  band: bandProp,
  onBandChange,
  eqSuggestion,
  onApplyEq,
}: PhaseWheelProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const trailRef = useRef<Array<{ angle: number; alpha: number }>>([]);
  const prevAngle = useRef(phaseAngleDeg);

  // Lokalny stan przełącznika pasm — fallback gdy rodzic nie kontroluje bandu
  const [localBand, setLocalBand] = useState<PhaseWheelBand>('SUB');
  const activeBand = bandProp ?? localBand;
  const handleBandChange = (b: PhaseWheelBand) => {
    setLocalBand(b);
    onBandChange?.(b);
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = size * dpr;
    canvas.height = size * dpr;
  }, [size, collapsed]);

  useEffect(() => {
    // Add to trail
    trailRef.current.push({ angle: prevAngle.current, alpha: 0.6 });
    if (trailRef.current.length > 20) trailRef.current.shift();
    trailRef.current = trailRef.current.map((t) => ({ ...t, alpha: t.alpha * 0.75 }));
    prevAngle.current = phaseAngleDeg;

    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size, size);

    const cx = size / 2;
    const cy = size / 2;
    const maxR = size / 2 - 10;

    // Background
    ctx.fillStyle = 'rgba(8,8,12,0.6)';
    ctx.beginPath();
    ctx.arc(cx, cy, maxR + 8, 0, Math.PI * 2);
    ctx.fill();

    // Concentric dashed rings at 0.33, 0.66, 1.0 correlation radii
    [0.33, 0.66, 1.0].forEach((ratio, i) => {
      ctx.beginPath();
      ctx.arc(cx, cy, maxR * ratio, 0, Math.PI * 2);
      ctx.strokeStyle = i === 2 ? 'rgba(255,255,255,0.22)' : 'rgba(255,255,255,0.1)';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 5]);
      ctx.stroke();
    });
    ctx.setLineDash([]);

    // Cross hairs
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(cx - maxR, cy);
    ctx.lineTo(cx + maxR, cy);
    ctx.moveTo(cx, cy - maxR);
    ctx.lineTo(cx, cy + maxR);
    ctx.stroke();

    // Phosphor trail
    trailRef.current.forEach((t) => {
      if (t.alpha < 0.02) return;
      const rad = ((t.angle - 90) * Math.PI) / 180;
      const tr = maxR * Math.min(1, Math.abs(correlation));
      const tx = cx + tr * Math.cos(rad);
      const ty = cy + tr * Math.sin(rad);
      ctx.beginPath();
      ctx.arc(tx, ty, 2, 0, Math.PI * 2);
      ctx.fillStyle = `rgba(255,255,255,${t.alpha * 0.5})`;
      ctx.fill();
    });

    // Needle with minimum length and confidence scaling
    const needleRad = ((phaseAngleDeg - 90) * Math.PI) / 180;
    const minNeedleR = maxR * 0.22;
    const nr = Math.max(minNeedleR, maxR * Math.min(1, Math.abs(correlation)));
    const nx = cx + nr * Math.cos(needleRad);
    const ny = cy + nr * Math.sin(needleRad);

    const conf = phaseConfidence !== undefined ? phaseConfidence : Math.min(1, Math.abs(correlation) * 1.5);
    const needleAlpha = Math.max(0.35, Math.min(1.0, conf));

    // Needle glow
    ctx.shadowBlur = 10;
    ctx.shadowColor = `rgba(255,255,255,${0.6 * needleAlpha})`;
    ctx.strokeStyle = `rgba(255,255,255,${0.9 * needleAlpha})`;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(nx, ny);
    ctx.stroke();
    ctx.shadowBlur = 0;

    // Needle tip
    ctx.fillStyle = `rgba(255,255,255,${needleAlpha})`;
    ctx.beginPath();
    ctx.arc(nx, ny, 3, 0, Math.PI * 2);
    ctx.fill();

    // Center dot
    ctx.fillStyle = 'rgba(255,255,255,0.4)';
    ctx.beginPath();
    ctx.arc(cx, cy, 3, 0, Math.PI * 2);
    ctx.fill();

    // Labels
    ctx.font = '7px JetBrains Mono, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.3)';
    ctx.textAlign = 'center';
    ctx.fillText('+90°', cx, cy - maxR - 4);
    ctx.fillText('-90°', cx, cy + maxR + 10);
    ctx.textAlign = 'right';
    ctx.fillText('±180°', cx - maxR - 2, cy + 3);
    ctx.textAlign = 'left';
    ctx.fillText('0°', cx + maxR + 3, cy + 3);

    // Ghost Dynamic EQ suggestion notch on outer ring (Option A)
    if (eqSuggestion && Math.abs(eqSuggestion.cutDb) > 0.5) {
      const eqAngle = ((eqSuggestion.freq / 250) * 360 - 90) * (Math.PI / 180);
      const isKickActive = (phaseConfidence ?? 0) > 0.4 || Math.abs(correlation) > 0.4;
      const ghostAlpha = isKickActive ? 0.75 : 0.28;
      ctx.strokeStyle = eqSuggestion.applied ? `rgba(74, 222, 128, ${ghostAlpha})` : `rgba(255, 184, 0, ${ghostAlpha})`;
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.arc(cx, cy, maxR + 3, eqAngle - 0.22, eqAngle + 0.22);
      ctx.stroke();
    }

    ctx.restore();
  }, [phaseAngleDeg, correlation, phaseConfidence, size, collapsed, eqSuggestion]);

  if (collapsed) {
    return (
      <div
        onClick={onToggleCollapse}
        style={{
          width: 48,
          height: 48,
          borderRadius: '50%',
          border: '1px solid rgba(255,255,255,0.2)',
          background: '#0C0C14',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          cursor: 'pointer',
          position: 'relative',
        }}
        title="Expand Phase Wheel"
      >
        {/* Mini indicator */}
        <svg width={36} height={36}>
          <circle cx={18} cy={18} r={14} fill="none" stroke="rgba(255,255,255,0.1)" strokeWidth={1} strokeDasharray="2 4" />
          {(() => {
            const rad = ((phaseAngleDeg - 90) * Math.PI) / 180;
            return (
              <line
                x1={18}
                y1={18}
                x2={18 + 12 * Math.cos(rad)}
                y2={18 + 12 * Math.sin(rad)}
                stroke="rgba(255,255,255,0.7)"
                strokeWidth={1.5}
              />
            );
          })()}
          <circle cx={18} cy={18} r={2} fill="rgba(255,255,255,0.4)" />
        </svg>
      </div>
    );
  }

  return (
    <div style={{ position: 'relative', width: size, height: size + 22, display: 'flex', flexDirection: 'column' }}>
      {/* SUB / MID / FULL band toggle — neon tabs */}
      <div
        style={{
          display: 'flex',
          height: 18,
          flexShrink: 0,
          marginBottom: 4,
          border: '1px solid rgba(255,255,255,0.12)',
          overflow: 'hidden',
          fontFamily: 'JetBrains Mono, monospace',
        }}
      >
        {(['SUB', 'MID', 'FULL'] as PhaseWheelBand[]).map((b, i) => (
          <button
            key={b}
            onClick={() => handleBandChange(b)}
            style={{
              flex: 1,
              padding: '1px 0',
              fontSize: 7,
              letterSpacing: '0.1em',
              background: activeBand === b ? 'rgba(255,255,255,0.15)' : 'transparent',
              color: activeBand === b ? 'rgba(255,255,255,0.9)' : 'rgba(255,255,255,0.28)',
              border: 'none',
              borderLeft: i > 0 ? '1px solid rgba(255,255,255,0.1)' : 'none',
              cursor: 'pointer',
              fontFamily: 'inherit',
              transition: 'all 0.12s ease',
              textShadow: activeBand === b ? '0 0 8px rgba(255,255,255,0.6)' : 'none',
            }}
            title={b === 'SUB' ? 'Sub band (below crossover)' : b === 'MID' ? 'Mid band' : 'Full spectrum'}
          >
            {b}
          </button>
        ))}
      </div>
      <div style={{ position: 'relative', width: size, height: size }}>
        <canvas
          ref={canvasRef}
          style={{ width: size, height: size, display: 'block' }}
        />
        {/* Collapse button */}
        <button
          onClick={onToggleCollapse}
          style={{
            position: 'absolute',
            top: 2,
            right: 2,
            width: 16,
            height: 16,
            background: 'rgba(0,0,0,0.5)',
            border: '1px solid rgba(255,255,255,0.15)',
            borderRadius: 2,
            color: 'rgba(255,255,255,0.4)',
            fontSize: 8,
            cursor: 'pointer',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            fontFamily: 'JetBrains Mono',
            lineHeight: 1,
          }}
          title="Collapse"
        >
          ▲
        </button>
        {/* Band label overlay */}
        <div
          style={{
            position: 'absolute',
            top: 2,
            left: 2,
            fontSize: 7,
            color: 'rgba(255,255,255,0.35)',
            fontFamily: 'JetBrains Mono, monospace',
            letterSpacing: '0.1em',
            pointerEvents: 'none',
          }}
        >
          {activeBand}
        </div>
        {/* EQ Suggestion Overlay (MAutoAlign style — Decision #5) */}
        {eqSuggestion && (
          <div
            style={{
              position: 'absolute',
              bottom: 18,
              left: '50%',
              transform: 'translateX(-50%)',
              display: 'flex',
              alignItems: 'center',
              gap: 4,
              padding: '1px 5px',
              background: 'rgba(12, 12, 20, 0.88)',
              border: `1px solid ${eqSuggestion.applied ? 'rgba(74, 222, 128, 0.4)' : 'rgba(255, 184, 0, 0.45)'}`,
              borderRadius: 2,
              whiteSpace: 'nowrap',
              fontFamily: 'JetBrains Mono, monospace',
              fontSize: 6.5,
              color: eqSuggestion.applied ? '#4ade80' : '#f59e0b',
              boxShadow: '0 2px 8px rgba(0,0,0,0.5)',
              zIndex: 15,
            }}
          >
            <span>
              {eqSuggestion.applied ? '✓ EQ APPLIED' : `EQ: ${eqSuggestion.cutDb.toFixed(1)}dB @ ${Math.round(eqSuggestion.freq)}Hz`}
            </span>
            {!eqSuggestion.applied && onApplyEq && (
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  onApplyEq(eqSuggestion.cutDb, eqSuggestion.freq);
                }}
                style={{
                  background: 'rgba(245, 158, 11, 0.2)',
                  border: '1px solid rgba(245, 158, 11, 0.5)',
                  color: '#fbbf24',
                  fontSize: 6,
                  padding: '0 3px',
                  borderRadius: 1,
                  cursor: 'pointer',
                  fontFamily: 'inherit',
                  letterSpacing: '0.05em',
                  fontWeight: 600,
                }}
                title="Apply dynamic EQ cut"
              >
                APPLY
              </button>
            )}
          </div>
        )}

        {/* Angle readout */}
        <div
          style={{
            position: 'absolute',
            bottom: 4,
            left: 0,
            right: 0,
            textAlign: 'center',
            fontSize: 8,
            color: 'rgba(255,255,255,0.45)',
            fontFamily: 'JetBrains Mono, monospace',
            letterSpacing: '0.06em',
          }}
        >
          {phaseAngleDeg >= 0 ? '+' : ''}{phaseAngleDeg.toFixed(1)}°
        </div>
      </div>
    </div>
  );
}

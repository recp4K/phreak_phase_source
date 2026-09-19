import { useRef, useState, useCallback, useLayoutEffect } from 'react';
import { beginGesture, endGesture, sendParameter } from '../juceBridge';

interface CyberKnobProps {
  label: string;
  value: number;
  min: number;
  max: number;
  defaultValue: number;
  unit: string;
  size?: number;
  decimals?: number;
  onChange?: (value: number) => void;
  paramId?: string;
  bipolar?: boolean;
  accentColor?: string;
}

function toXY(angleDeg: number, r: number, cx: number, cy: number): [number, number] {
  const rad = (angleDeg * Math.PI) / 180;
  return [cx + r * Math.cos(rad), cy + r * Math.sin(rad)];
}

// Knob arc: starts at 135° (7:30), ends at 45° (4:30) going clockwise = 270° total
// In SVG coords: going clockwise from 135° → each +degree goes clockwise
const START_ANGLE = 135;
const RANGE_DEG = 270;

export default function CyberKnob({
  label,
  value,
  min,
  max,
  defaultValue,
  unit,
  size = 46,
  decimals = 1,
  onChange,
  paramId,
  bipolar = false,
  accentColor,
}: CyberKnobProps) {
  const [hovered, setHovered] = useState(false);
  const [dragging, setDragging] = useState(false);
  const startYRef = useRef(0);
  const startValRef = useRef(value);
  const shiftRef = useRef(false);

  const norm = (v: number) => (v - min) / (max - min);
  const p = norm(value);

  const r = size / 2 - 7;
  const cx = size / 2;
  const cy = size / 2;

  // Track arc endpoints
  const [sx, sy] = toXY(START_ANGLE, r, cx, cy);
  const [ex, ey] = toXY(START_ANGLE + RANGE_DEG, r, cx, cy); // = 405° = 45°

  // Value arc endpoint
  const sweep = p * RANGE_DEG;
  const valAngle = START_ANGLE + sweep;
  const [vx, vy] = toXY(valAngle, r, cx, cy);
  const valLargeArc = sweep > 180 ? 1 : 0;

  // For bipolar: center indicator at 50%
  const [zx, zy] = toXY(START_ANGLE + RANGE_DEG / 2, r, cx, cy);

  // ── Stable refs (Vercel advanced-event-handler-refs pattern) ───────────
  // Synchronizowane w useLayoutEffect, nigdy nie powodują re-rejestracji handlerów.
  const valueRef = useRef(value);
  const onChangeRef = useRef(onChange);
  useLayoutEffect(() => {
    valueRef.current = value;
    onChangeRef.current = onChange;
  });

  const onPointerDown = useCallback(
    (e: React.PointerEvent) => {
      e.preventDefault();
      (e.target as Element).setPointerCapture(e.pointerId);
      startYRef.current = e.clientY;
      startValRef.current = valueRef.current;
      shiftRef.current = e.shiftKey;
      setDragging(true);
      if (paramId) {
        beginGesture(paramId);
      }
    },
    [paramId],
  );

  const onPointerMove = useCallback(
    (e: React.PointerEvent) => {
      if (!dragging) return;
      const sensitivity = e.shiftKey ? 0.08 : 1.0;
      const dy = startYRef.current - e.clientY;
      const delta = (dy / 180) * (max - min) * sensitivity;
      const nv = Math.max(min, Math.min(max, startValRef.current + delta));
      onChangeRef.current?.(nv);
      if (paramId) {
        sendParameter(paramId, nv);
      }
    },
    [dragging, max, min, paramId],
  );

  const onPointerUp = useCallback(() => {
    setDragging((wasDragging) => {
      if (wasDragging && paramId) {
        endGesture(paramId);
      }
      return false;
    });
  }, [paramId]);

  const onDoubleClick = useCallback(() => {
    onChangeRef.current?.(defaultValue);
    if (paramId) {
      beginGesture(paramId);
      sendParameter(paramId, defaultValue);
      endGesture(paramId);
    }
  }, [defaultValue, paramId]);

  const displayVal = value.toFixed(decimals);
  const active = hovered || dragging;

  // Dither dot grid for the arc background (5 evenly spaced dots along the arc)
  const arcDots = Array.from({ length: 8 }, (_, i) => {
    const a = START_ANGLE + (i / 7) * RANGE_DEG;
    const [dx, dy2] = toXY(a, r, cx, cy);
    return { x: dx, y: dy2, lit: i / 7 <= p };
  });

  return (
    <div
      className="flex flex-col items-center gap-0.5 select-none"
      style={{ fontFamily: 'JetBrains Mono, monospace', position: 'relative' }}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => {
        setHovered(false);
      }}
    >
      {/* Hover tooltip */}
      {active && (
        <div
          style={{
            position: 'absolute',
            top: -26,
            left: '50%',
            transform: 'translateX(-50%)',
            background: '#0A0A12',
            border: '1px solid rgba(255,255,255,0.18)',
            padding: '2px 6px',
            fontSize: 9,
            color: 'rgba(255,255,255,0.85)',
            whiteSpace: 'nowrap',
            zIndex: 60,
            letterSpacing: '0.04em',
          }}
        >
          {displayVal}
          <span style={{ color: 'rgba(255,255,255,0.4)', marginLeft: 2 }}>{unit}</span>
        </div>
      )}

      {/* SVG Knob */}
      <svg
        width={size}
        height={size}
        style={{ cursor: dragging ? 'ns-resize' : 'pointer', touchAction: 'none', display: 'block' }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onLostPointerCapture={onPointerUp}
        onDoubleClick={onDoubleClick}
      >
        {/* Outer ring glow when active */}
        {active && (
          <circle
            cx={cx}
            cy={cy}
            r={size / 2 - 1}
            fill="none"
            stroke={accentColor ? `${accentColor}40` : 'rgba(255,255,255,0.18)'}
            strokeWidth="1"
          />
        )}

        {/* Knob body */}
        <circle
          cx={cx}
          cy={cy}
          r={size / 2 - 3}
          fill="#0D0D17"
          stroke={active ? (accentColor || 'rgba(242,242,247,0.5)') : 'rgba(242,242,247,0.16)'}
          strokeWidth="1.2"
        />

        {/* Dither dots along track */}
        {arcDots.map((d, i) => (
          <circle
            key={i}
            cx={d.x}
            cy={d.y}
            r={d.lit ? 1.6 : 1}
            fill={
              d.lit
                ? active
                  ? (accentColor || 'rgba(255,255,255,0.95)')
                  : (accentColor ? `${accentColor}cc` : 'rgba(255,255,255,0.7)')
                : 'rgba(255,255,255,0.12)'
            }
          />
        ))}

        {/* Track arc (dashed) */}
        <path
          d={`M ${sx} ${sy} A ${r} ${r} 0 1 1 ${ex} ${ey}`}
          fill="none"
          stroke="rgba(242,242,247,0.14)"
          strokeWidth="1.5"
          strokeLinecap="round"
          strokeDasharray="1.5 3.5"
        />

        {/* Value arc */}
        {p > 0.002 && (
          <path
            d={`M ${sx} ${sy} A ${r} ${r} 0 ${valLargeArc} 1 ${vx} ${vy}`}
            fill="none"
            stroke={active ? (accentColor || '#FFFFFF') : (accentColor ? `${accentColor}ee` : 'rgba(242,242,247,0.85)')}
            strokeWidth={active ? 2 : 1.6}
            strokeLinecap="round"
            style={{
              filter: active && accentColor ? `drop-shadow(0 0 4px ${accentColor})` : undefined,
            }}
          />
        )}

        {/* Bipolar center tick */}
        {bipolar && (
          <circle cx={zx} cy={zy} r="1.5" fill="rgba(255,255,255,0.4)" />
        )}

        {/* Indicator dot at value position */}
        <circle
          cx={vx}
          cy={vy}
          r={active ? 3.2 : 2.5}
          fill={active ? '#FFFFFF' : (accentColor || 'rgba(242,242,247,0.95)')}
          style={{
            filter: active ? `drop-shadow(0 0 4px ${accentColor || '#FFFFFF'})` : undefined,
          }}
        />

        {/* Center pip */}
        <circle cx={cx} cy={cy} r="2" fill="rgba(242,242,247,0.25)" />
      </svg>

      {/* Label */}
      <div
        style={{
          fontSize: size >= 54 ? 9 : 8.5,
          color: active ? '#FFFFFF' : 'rgba(255,255,255,0.65)',
          letterSpacing: '0.12em',
          textTransform: 'uppercase',
          textAlign: 'center',
          lineHeight: 1.2,
          marginTop: 2,
          fontWeight: 600,
          transition: 'color 0.15s',
        }}
      >
        {label}
      </div>

      {/* Value (always shown below label, clearly legible) */}
      <div
        style={{
          fontSize: size >= 54 ? 11 : 10,
          color: active ? '#FFFFFF' : (accentColor ? `${accentColor}dd` : 'rgba(255,255,255,0.85)'),
          letterSpacing: '0.04em',
          textAlign: 'center',
          fontVariantNumeric: 'tabular-nums',
          fontWeight: 600,
          transition: 'color 0.15s',
        }}
      >
        {displayVal}
        <span
          style={{
            fontSize: 8.5,
            color: active ? 'rgba(255,255,255,0.6)' : 'rgba(255,255,255,0.45)',
            marginLeft: 2,
            fontWeight: 400,
          }}
        >
          {unit}
        </span>
      </div>
    </div>
  );
}

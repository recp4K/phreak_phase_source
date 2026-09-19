import { useRef, useState, useEffect, useCallback } from 'react';

export type AlignState = 'idle' | 'scanning' | 'locked' | 'timeout';

export interface SmartAlignButtonProps {
  state: AlignState;
  onTrigger: () => void;
  correlation?: number;
  preCorrelation?: number;
  improvementPct?: number;
  confidencePct?: number;
  statusMessage?: string;
  delayMs?: number;
  delaySamples?: number;
  rotateDeg?: number;
  flip?: boolean;
}

const HEX_CHARS = '0123456789ABCDEF';
function randHex(len: number) {
  return Array.from({ length: len }, () => HEX_CHARS[Math.floor(Math.random() * 16)]).join('');
}

// 20 orbital dot positions at radius
function orbitalDots(cx: number, cy: number, r: number, n: number) {
  return Array.from({ length: n }, (_, i) => {
    const a = (i / n) * 2 * Math.PI;
    return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a), i };
  });
}

export default function SmartAlignButton({
  state,
  onTrigger,
  correlation = 0,
  preCorrelation = 0,
  improvementPct = 0,
  confidencePct,
  statusMessage,
  delayMs,
  delaySamples,
  rotateDeg,
  flip,
}: SmartAlignButtonProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const [magnetOffset, setMagnetOffset] = useState({ x: 0, y: 0 });
  const [hexDisplay, setHexDisplay] = useState('0x3F8A2D1C');
  const [flashVisible, setFlashVisible] = useState(false);
  const scanTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const orbitalRef = useRef<SVGGElement | null>(null);
  const animFrameRef = useRef<number>(0);
  const angleRef = useRef(0);

  const SIZE = 140;
  const cx = SIZE / 2;
  const cy = SIZE / 2;
  const orbitR = SIZE / 2 - 6;
  const dots = orbitalDots(0, 0, orbitR, 24); // centered at 0,0 for rotation

  // Hex flicker during scanning
  useEffect(() => {
    if (state === 'scanning') {
      scanTimerRef.current = setInterval(() => {
        setHexDisplay(`0x${randHex(8)}`);
      }, 48);
    } else {
      if (scanTimerRef.current) clearInterval(scanTimerRef.current);
    }
    return () => {
      if (scanTimerRef.current) clearInterval(scanTimerRef.current);
    };
  }, [state]);

  // Flash on lock
  useEffect(() => {
    if (state === 'locked') {
      setFlashVisible(true);
      const t = setTimeout(() => setFlashVisible(false), 600);
      return () => clearTimeout(t);
    }
  }, [state]);

  // Orbital animation via RAF
  useEffect(() => {
    const rpm = state === 'scanning' ? 180 : state === 'timeout' ? 15 : 45;
    const speed = (rpm / 60) * 360; // degrees per second
    let last = performance.now();

    const tick = (now: number) => {
      const dt = (now - last) / 1000;
      last = now;
      angleRef.current = (angleRef.current + speed * dt) % 360;
      if (orbitalRef.current) {
        orbitalRef.current.style.transform = `rotate(${angleRef.current}deg)`;
        orbitalRef.current.style.transformOrigin = `${cx}px ${cy}px`;
      }
      animFrameRef.current = requestAnimationFrame(tick);
    };

    animFrameRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(animFrameRef.current);
  }, [state, cx, cy]);

  // Magnetic hover
  const handleMouseMove = useCallback(
    (e: React.MouseEvent) => {
      if (!containerRef.current || state !== 'idle') return;
      const rect = containerRef.current.getBoundingClientRect();
      const bx = rect.left + rect.width / 2;
      const by = rect.top + rect.height / 2;
      const dx = e.clientX - bx;
      const dy = e.clientY - by;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const maxDist = 120;
      if (dist < maxDist) {
        const force = Math.pow(1 - dist / maxDist, 1.5) * 10;
        setMagnetOffset({ x: (dx / (dist || 1)) * force, y: (dy / (dist || 1)) * force });
      } else {
        setMagnetOffset({ x: 0, y: 0 });
      }
    },
    [state],
  );

  const handleMouseLeave = useCallback(() => {
    setMagnetOffset({ x: 0, y: 0 });
  }, []);

  const corrPct = Math.round(Math.abs(correlation) * 100);

  return (
    <div
      ref={containerRef}
      style={{ position: 'relative', width: SIZE, height: SIZE }}
      onMouseMove={handleMouseMove}
      onMouseLeave={handleMouseLeave}
    >
      {/* Magnetic wrapper */}
      <div
        style={{
          transform: `translate(${magnetOffset.x}px, ${magnetOffset.y}px)`,
          transition:
            state === 'idle'
              ? 'transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1)'
              : 'transform 0.1s ease',
          width: SIZE,
          height: SIZE,
          position: 'relative',
        }}
      >
        {/* SVG: orbital ring + button shape */}
        <svg
          width={SIZE}
          height={SIZE}
          style={{ position: 'absolute', inset: 0, overflow: 'visible' }}
        >
          {/* Outer dither ring dots — orbiting */}
          <g ref={orbitalRef} style={{ transformOrigin: `${cx}px ${cy}px` }}>
            {dots.map((d, i) => {
              const brightness =
                state === 'scanning'
                  ? (i % 3 === 0 ? 0.9 : 0.3)
                  : state === 'timeout'
                    ? 0.15
                    : (i % 4 === 0 ? 0.6 : 0.18);
              return (
                <circle
                  key={i}
                  cx={cx + d.x}
                  cy={cy + d.y}
                  r={i % 6 === 0 ? 2 : i % 3 === 0 ? 1.5 : 1}
                  fill={state === 'timeout' ? `rgba(255,100,100,${brightness})` : `rgba(255,255,255,${brightness})`}
                />
              );
            })}
          </g>

          {/* Scan ring pulse (during scanning) */}
          {state === 'scanning' && (
            <>
              <circle
                cx={cx}
                cy={cy}
                r={orbitR + 6}
                fill="none"
                stroke="rgba(255,255,255,0.06)"
                strokeWidth="8"
              />
              <circle
                cx={cx}
                cy={cy}
                r={orbitR + 14}
                fill="none"
                stroke="rgba(255,255,255,0.03)"
                strokeWidth="12"
              />
            </>
          )}
        </svg>

        {/* Button circle */}
        <button
          onClick={onTrigger}
          style={{
            position: 'absolute',
            top: '50%',
            left: '50%',
            transform: 'translate(-50%, -50%)',
            width: 100,
            height: 100,
            borderRadius: '50%',
            border: `1px solid ${
              state === 'locked'
                ? 'rgba(255,255,255,0.6)'
                : state === 'timeout'
                  ? 'rgba(255,90,90,0.7)'
                  : 'rgba(255,255,255,0.22)'
            }`,
            background:
              state === 'locked'
                ? 'rgba(255,255,255,0.08)'
                : state === 'timeout'
                  ? 'rgba(255,40,40,0.09)'
                  : state === 'scanning'
                    ? 'rgba(255,255,255,0.04)'
                    : '#0C0C14',
            boxShadow:
              state === 'timeout'
                ? '0 0 15px rgba(255,50,50,0.2)'
                : state === 'locked'
                  ? '0 0 15px rgba(255,255,255,0.1)'
                  : undefined,
            cursor: 'pointer',
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            justifyContent: 'center',
            gap: 4,
            fontFamily: 'JetBrains Mono, monospace',
            userSelect: 'none',
            outline: 'none',
            animation:
              state === 'idle'
                ? 'breathe 3.2s cubic-bezier(0.45, 0, 0.55, 1) infinite'
                : state === 'scanning'
                  ? 'scan-ring 0.8s ease-in-out infinite'
                  : undefined,
            transition: 'background 0.4s ease, border-color 0.4s ease',
          }}
        >
          {/* Flash overlay on lock */}
          {flashVisible && (
            <div
              style={{
                position: 'absolute',
                inset: 0,
                borderRadius: '50%',
                background: 'rgba(255,255,255,0.85)',
                animation: 'lock-flash 0.6s ease-out forwards',
                pointerEvents: 'none',
              }}
            />
          )}

          {/* Inner label */}
          {state === 'idle' && (
            <>
              <div style={{ fontSize: 9, color: 'rgba(255,255,255,0.35)', letterSpacing: '0.15em' }}>
                CLICK TO
              </div>
              <div style={{ fontSize: 11, color: 'rgba(255,255,255,0.85)', letterSpacing: '0.12em', fontWeight: 600 }}>
                SMART
              </div>
              <div style={{ fontSize: 11, color: 'rgba(255,255,255,0.85)', letterSpacing: '0.12em', fontWeight: 600 }}>
                ALIGN
              </div>
            </>
          )}

          {state === 'scanning' && (
            <>
              <div
                style={{
                  fontSize: 8,
                  color: 'rgba(255,255,255,0.45)',
                  letterSpacing: '0.1em',
                  animation: 'hex-flicker 0.1s linear infinite',
                }}
              >
                // GCC-PHAT //
              </div>
              <div
                style={{
                  fontSize: 9,
                  color: 'rgba(255,255,255,0.7)',
                  letterSpacing: '0.06em',
                  fontVariantNumeric: 'tabular-nums',
                  animation: 'hex-flicker 0.05s linear infinite',
                }}
              >
                {hexDisplay}
              </div>
              <div style={{ fontSize: 7, color: 'rgba(255,255,255,0.3)', letterSpacing: '0.12em' }}>
                SCANNING...
              </div>
            </>
          )}

          {state === 'timeout' && (
            <>
              <div style={{ fontSize: 8, color: '#ff6b6b', letterSpacing: '0.14em', fontWeight: 700 }}>
                NO AUDIO
              </div>
              <div style={{ fontSize: 6.5, color: 'rgba(255,120,120,0.85)', letterSpacing: '0.06em', textAlign: 'center', lineHeight: 1.2 }}>
                CHECK ROUTING
              </div>
              <div style={{ fontSize: 7, color: 'rgba(255,255,255,0.45)', letterSpacing: '0.1em', marginTop: 2 }}>
                [ RETRY ]
              </div>
            </>
          )}

          {state === 'locked' && (
            <>
              <div style={{ fontSize: 7, color: '#4ade80', letterSpacing: '0.12em', fontWeight: 600 }}>
                {confidencePct !== undefined ? `${confidencePct}% ` : ''}LOCKED
              </div>
              <div
                style={{
                  fontSize: 13,
                  color: '#FFFFFF',
                  letterSpacing: '0.04em',
                  fontWeight: 700,
                }}
              >
                +{corrPct}%
              </div>
              <div style={{ fontSize: 6.5, color: 'rgba(255,255,255,0.38)', letterSpacing: '0.08em' }}>
                CLICK TO RESET
              </div>
            </>
          )}
        </button>
      </div>
    </div>
  );
}

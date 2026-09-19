import { useMemo } from 'react';

interface CrossoverFilterCurveProps {
  crossoverFreq: number;
  active?: boolean;
  width?: number;
  height?: number;
}

export default function CrossoverFilterCurve({
  crossoverFreq,
  active = true,
  width = 200,
  height = 54,
}: CrossoverFilterCurveProps) {
  // Logarithmic frequency scale from 20 Hz to 600 Hz
  const minF = 20;
  const maxF = 600;
  const logMin = Math.log10(minF);
  const logMax = Math.log10(maxF);

  const freqToX = (f: number) => {
    const logF = Math.log10(Math.max(minF, Math.min(maxF, f)));
    return ((logF - logMin) / (logMax - logMin)) * width;
  };

  const fcX = freqToX(crossoverFreq);

  // Generate SVG path for Low-Pass (Sub) and High-Pass (High)
  const { subPath, highPath, subArea, highArea } = useMemo(() => {
    const steps = 40;
    const subPts: [number, number][] = [];
    const highPts: [number, number][] = [];

    const minDb = -36;
    const maxDb = +3;

    const dbToY = (db: number) => {
      const norm = (db - minDb) / (maxDb - minDb);
      return height - norm * (height - 8) - 4;
    };

    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const f = Math.pow(10, logMin + t * (logMax - logMin));
      const x = t * width;

      // LR4 Low-Pass magnitude: 1 / sqrt(1 + (f/fc)^8) -> dB: -10 * log10(1 + (f/fc)^8)
      // LR4 is two cascaded Butterworth 2nd orders (Q=0.7071)
      const ratio = f / crossoverFreq;
      const subDb = active ? -10 * Math.log10(1 + Math.pow(ratio, 8)) : 0;
      const highDb = active ? -10 * Math.log10(1 + Math.pow(1 / Math.max(1e-5, ratio), 8)) : -36;

      subPts.push([x, Math.max(2, Math.min(height - 2, dbToY(subDb)))]);
      highPts.push([x, Math.max(2, Math.min(height - 2, dbToY(highDb)))]);
    }

    const toSvgPath = (pts: [number, number][]) =>
      pts.map((p, i) => `${i === 0 ? 'M' : 'L'} ${p[0].toFixed(1)} ${p[1].toFixed(1)}`).join(' ');

    const subP = toSvgPath(subPts);
    const highP = toSvgPath(highPts);

    const subA = `${subP} L ${width} ${height} L 0 ${height} Z`;
    const highA = `${highP} L ${width} ${height} L 0 ${height} Z`;

    return { subPath: subP, highPath: highP, subArea: subA, highArea: highA };
  }, [crossoverFreq, active, width, height, logMin, logMax]);

  const freqTicks = [30, 60, 100, 200, 500];

  return (
    <div
      style={{
        width,
        height,
        position: 'relative',
        background: '#0A0A14',
        border: '1px solid rgba(255,255,255,0.09)',
        borderRadius: 2,
        overflow: 'hidden',
        userSelect: 'none',
      }}
    >
      <svg width={width} height={height} style={{ display: 'block' }}>
        <defs>
          <linearGradient id="subGradient" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#00D4FF" stopOpacity="0.32" />
            <stop offset="100%" stopColor="#00D4FF" stopOpacity="0.02" />
          </linearGradient>
          <linearGradient id="highGradient" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#A855F7" stopOpacity="0.32" />
            <stop offset="100%" stopColor="#A855F7" stopOpacity="0.02" />
          </linearGradient>
        </defs>

        {/* Freq grid ticks */}
        {freqTicks.map((f) => {
          const x = freqToX(f);
          return (
            <g key={f}>
              <line
                x1={x}
                y1={0}
                x2={x}
                y2={height}
                stroke="rgba(255,255,255,0.06)"
                strokeDasharray="2 3"
              />
              <text
                x={x + 2}
                y={height - 3}
                fill="rgba(255,255,255,0.28)"
                fontSize="6.5"
                fontFamily="JetBrains Mono"
              >
                {f}
              </text>
            </g>
          );
        })}

        {/* 0 dB reference line */}
        <line
          x1={0}
          y1={6}
          x2={width}
          y2={6}
          stroke="rgba(255,255,255,0.08)"
          strokeWidth="1"
        />

        {/* Areas */}
        <path d={subArea} fill="url(#subGradient)" />
        {active && <path d={highArea} fill="url(#highGradient)" />}

        {/* Curves */}
        <path
          d={subPath}
          fill="none"
          stroke="#00D4FF"
          strokeWidth="1.5"
          strokeLinecap="round"
          style={{ filter: 'drop-shadow(0 0 2px rgba(0,212,255,0.5))' }}
        />
        {active && (
          <path
            d={highPath}
            fill="none"
            stroke="#A855F7"
            strokeWidth="1.5"
            strokeLinecap="round"
            style={{ filter: 'drop-shadow(0 0 2px rgba(168,85,247,0.5))' }}
          />
        )}

        {/* Crossover split point indicator */}
        {active && (
          <g>
            <line
              x1={fcX}
              y1={0}
              x2={fcX}
              y2={height}
              stroke="#FFFFFF"
              strokeWidth="1"
              strokeDasharray="3 2"
              strokeOpacity="0.75"
            />
            <circle
              cx={fcX}
              cy={height / 2}
              r="2.5"
              fill="#FFFFFF"
              style={{ filter: 'drop-shadow(0 0 4px #FFFFFF)' }}
            />
          </g>
        )}
      </svg>

      {/* Sub / High labels */}
      <div
        style={{
          position: 'absolute',
          top: 3,
          left: 6,
          fontSize: 7.5,
          color: '#00D4FF',
          fontWeight: 600,
          letterSpacing: '0.08em',
          fontFamily: 'JetBrains Mono',
        }}
      >
        SUB
      </div>
      <div
        style={{
          position: 'absolute',
          top: 3,
          right: 6,
          fontSize: 7.5,
          color: active ? '#A855F7' : 'rgba(255,255,255,0.3)',
          fontWeight: 600,
          letterSpacing: '0.08em',
          fontFamily: 'JetBrains Mono',
        }}
      >
        {active ? 'HIGH' : 'BYPASS'}
      </div>
    </div>
  );
}

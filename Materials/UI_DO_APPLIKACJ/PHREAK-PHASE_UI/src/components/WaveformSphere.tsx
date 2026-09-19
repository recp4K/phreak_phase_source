import { useEffect, useRef } from 'react';

interface WaveformSphereProps {
  waveformA: number[];
  waveformB: number[];
  size?: number;
  opacity?: number;
}

export default function WaveformSphere({
  waveformA,
  waveformB,
  size = 290,
  opacity = 0.18,
}: WaveformSphereProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = size * dpr;
    canvas.height = size * dpr;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.scale(dpr, dpr);
  }, [size]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.clearRect(0, 0, size, size);

    const cx = size / 2;
    const cy = size / 2;
    const R = size / 2 - 6;
    const LINES = 28;

    for (let li = 0; li < LINES; li++) {
      // y position: distribute lines vertically across the sphere
      const t = (li / (LINES - 1)) * 2 - 1; // -1 to +1
      const y = cy + t * R;
      // chord half-width at this y
      const halfW = Math.sqrt(Math.max(0, R * R - (y - cy) ** 2));
      if (halfW < 2) continue;

      // brightness: brighter toward the center
      const brightness = 1 - Math.abs(t) * 0.55;

      // alternate using A/B waveform
      const src = li % 2 === 0 ? waveformA : waveformB;
      const amp = halfW * 0.12 * brightness;

      ctx.beginPath();
      ctx.strokeStyle = `rgba(255,255,255,${0.55 * brightness})`;
      ctx.lineWidth = 0.8;

      const steps = 80;
      for (let i = 0; i <= steps; i++) {
        const frac = i / steps;
        const x = cx - halfW + frac * halfW * 2;
        const wIdx = Math.floor(frac * (src.length - 1));
        const wv = src[wIdx] ?? 0;
        const py = y + wv * amp;

        if (i === 0) ctx.moveTo(x, py);
        else ctx.lineTo(x, py);
      }
      ctx.stroke();
    }
  });

  return (
    <canvas
      ref={canvasRef}
      style={{
        width: size,
        height: size,
        display: 'block',
        opacity,
        pointerEvents: 'none',
      }}
    />
  );
}

import { useRef, useEffect, useCallback, useState } from 'react';
import { sendParameter, beginGesture, endGesture } from '../juceBridge';

interface OscilloscopeProps {
  trackA: number[];
  trackB: number[];
  markerOffsetMs: number;
  onMarkerChange?: (ms: number) => void;
  width: number;
  height: number;
  /** Gdy true i frozen — drag Track B lane → zmiana SUB_DELAY */
  subDelayMs?: number;
  onSubDelayChange?: (ms: number) => void;
}

export default function Oscilloscope({
  trackA,
  trackB,
  markerOffsetMs,
  onMarkerChange,
  width,
  height,
  subDelayMs = 0,
  onSubDelayChange,
}: OscilloscopeProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const markerDragging = useRef(false);
  const markerXRef = useRef<number | null>(null);

  // FREEZE scrubbing: Track B drag → SUB_DELAY (Decision #6)
  const scrubDragging = useRef(false);
  const scrubStartX = useRef(0);
  const scrubStartDelay = useRef(0);

  // Zoom and freeze states
  const [zoomX, setZoomX] = useState<number>(0.5);
  const [zoomY, setZoomY] = useState<number>(1.0);
  const [panX, setPanX] = useState<number>(0.5);
  const [isFrozen, setIsFrozen] = useState<boolean>(false);

  // Keep frozen frame snapshot when freeze is toggled
  const frozenTrackARef = useRef<number[]>([]);
  const frozenTrackBRef = useRef<number[]>([]);
  const liveTrackARef = useRef<number[]>(trackA);
  const liveTrackBRef = useRef<number[]>(trackB);

  // Update frozen snapshot when actively freezing
  const toggleFreeze = useCallback(() => {
    const next = !isFrozen;
    if (next) {
      frozenTrackARef.current = [...liveTrackARef.current];
      frozenTrackBRef.current = [...liveTrackBRef.current];
    }
    setIsFrozen(next);
    beginGesture('FREEZE');
    sendParameter('FREEZE', next ? 1 : 0);
    endGesture('FREEZE');
  }, [isFrozen]);

  const handleZoomIn = useCallback(() => {
    const nextX = Math.min(1.0, parseFloat((zoomX + 0.1).toFixed(2)));
    const nextY = Math.min(4.0, parseFloat((zoomY * 1.25).toFixed(2)));
    setZoomX(nextX);
    setZoomY(nextY);
    beginGesture('ZOOMX');
    sendParameter('ZOOMX', nextX);
    endGesture('ZOOMX');
    beginGesture('ZOOMY');
    sendParameter('ZOOMY', nextY);
    endGesture('ZOOMY');
  }, [zoomX, zoomY]);

  const handleZoomOut = useCallback(() => {
    const nextX = Math.max(0.0, parseFloat((zoomX - 0.1).toFixed(2)));
    const nextY = Math.max(0.25, parseFloat((zoomY / 1.25).toFixed(2)));
    setZoomX(nextX);
    setZoomY(nextY);
    beginGesture('ZOOMX');
    sendParameter('ZOOMX', nextX);
    endGesture('ZOOMX');
    beginGesture('ZOOMY');
    sendParameter('ZOOMY', nextY);
    endGesture('ZOOMY');
  }, [zoomX, zoomY]);

  const handleResetZoom = useCallback(() => {
    setZoomX(0.5);
    setZoomY(1.0);
    setPanX(0.5);
    beginGesture('ZOOMX');
    sendParameter('ZOOMX', 0.5);
    endGesture('ZOOMX');
    beginGesture('ZOOMY');
    sendParameter('ZOOMY', 1.0);
    endGesture('ZOOMY');
    beginGesture('PANX');
    sendParameter('PANX', 0.5);
    endGesture('PANX');
  }, []);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);

    const W = width;
    const H = height;
    const midY = H / 2;

    // Grid lines (1px dotted phosphor)
    ctx.save();
    ctx.setLineDash([2, 6]);
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.lineWidth = 1;
    // Horizontal grid
    for (let i = 0; i <= 4; i++) {
      const y = (i / 4) * H;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();
    }
    // Vertical grid
    for (let i = 0; i <= 8; i++) {
      const x = (i / 8) * W;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();
    }
    ctx.restore();

    // Center line
    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    ctx.setLineDash([]);
    ctx.beginPath();
    ctx.moveTo(0, midY);
    ctx.lineTo(W, midY);
    ctx.stroke();
    ctx.restore();

    // Select active or frozen waveform data
    const activeDataA = isFrozen ? frozenTrackARef.current : liveTrackARef.current;
    const activeDataB = isFrozen ? frozenTrackBRef.current : liveTrackBRef.current;

    const drawWave = (
      data: number[],
      color: string,
      lineWidth: number,
      dashed: boolean,
      glowAlpha: number,
    ) => {
      if (!data.length) return;
      ctx.save();

      if (glowAlpha > 0) {
        ctx.shadowBlur = 6;
        ctx.shadowColor = color.replace(/[\d.]+\)$/, `${glowAlpha})`);
      }

      ctx.strokeStyle = color;
      ctx.lineWidth = lineWidth;
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      if (dashed) ctx.setLineDash([3, 2]);
      else ctx.setLineDash([]);

      ctx.beginPath();
      for (let i = 0; i < data.length; i++) {
        const x = (i / (data.length - 1)) * W;
        const clampedSample = Math.max(-2.0, Math.min(2.0, data[i]));
        const y = midY - clampedSample * (H / 2) * 0.85 * zoomY;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
      ctx.restore();
    };

    // Track B — radiant cyan phosphor (Bass)
    drawWave(activeDataB, 'rgba(0, 212, 255, 0.82)', 1.5, false, 0.35);

    // Track A — warm amber phosphor (Kick)
    drawWave(activeDataA, 'rgba(255, 184, 0, 0.92)', 1.6, false, 0.45);

    // Marker line
    const markerX = markerXRef.current;
    if (markerX !== null) {
      ctx.save();
      ctx.strokeStyle = 'rgba(255, 224, 102, 0.85)';
      ctx.lineWidth = 1.2;
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(markerX, 0);
      ctx.lineTo(markerX, H);
      ctx.stroke();
      // Marker label
      ctx.fillStyle = '#FFE066';
      ctx.font = `9px JetBrains Mono, monospace`;
      const smpVal = Math.round(markerOffsetMs * 48.0);
      ctx.fillText(`Δ ${markerOffsetMs >= 0 ? '+' : ''}${markerOffsetMs.toFixed(2)}ms (${smpVal >= 0 ? '+' : ''}${smpVal} smp)`, markerX + 5, 14);
      ctx.restore();
    }

    // Track labels with matching colors
    ctx.fillStyle = '#FFB800';
    ctx.font = '9px JetBrains Mono, monospace';
    ctx.fillText('A // KICK', 8, 14);
    ctx.fillStyle = '#00D4FF';
    ctx.fillText('B // BASS', 8, 26);

    if (isFrozen) {
      ctx.fillStyle = 'rgba(74, 222, 128, 0.85)';
      ctx.font = '8px JetBrains Mono, monospace';
      ctx.fillText('[INSPECTION MODE: FROZEN WAVEFORMS]', 8, 38);
    }

    ctx.restore();
  }, [isFrozen, zoomY, markerOffsetMs, width, height]);

  // Scale canvas for DPR
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    draw();
  }, [width, height, draw]);

  // Live incoming frames: only trigger canvas redraw when not frozen
  useEffect(() => {
    liveTrackARef.current = trackA;
    liveTrackBRef.current = trackB;
    if (!isFrozen) {
      draw();
    }
  }, [trackA, trackB, isFrozen, draw]);

  // Redraw when draw dependencies (freeze toggle, zoom, marker, size) change
  useEffect(() => {
    draw();
  }, [draw]);

  // Initialize marker position
  useEffect(() => {
    markerXRef.current = width / 2;
  }, [width]);

  const handlePointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const rect = canvasRef.current!.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    // FREEZE scrubbing: dolna połowa canvas = Track B lane (Decision #6)
    const isTrackBLane = y > height / 2;
    if (isFrozen && isTrackBLane && onSubDelayChange) {
      scrubDragging.current = true;
      scrubStartX.current = x;
      scrubStartDelay.current = subDelayMs;
      (e.target as Element).setPointerCapture(e.pointerId);
      beginGesture('SUB_DELAY');
      return; // Priorytet scrubbing nad marker drag
    }

    // Marker drag — tylko gdy nie scrubbing
    if (Math.abs(x - (markerXRef.current ?? width / 2)) < 12) {
      markerDragging.current = true;
      (e.target as Element).setPointerCapture(e.pointerId);
    }
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (scrubDragging.current) {
      const rect = canvasRef.current!.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const dx = x - scrubStartX.current;
      // 1px = 0.1ms, clamp do [-20, +20] ms
      const newDelay = Math.max(-20, Math.min(20, scrubStartDelay.current + dx * 0.1));
      onSubDelayChange?.(newDelay);
      sendParameter('SUB_DELAY', newDelay);
      return;
    }
    if (!markerDragging.current) return;
    const rect = canvasRef.current!.getBoundingClientRect();
    const x = Math.max(0, Math.min(width, e.clientX - rect.left));
    markerXRef.current = x;
    const ms = ((x / width) * 2 - 1) * 20;
    onMarkerChange?.(ms);
  };

  const handlePointerUp = () => {
    if (scrubDragging.current) {
      scrubDragging.current = false;
      endGesture('SUB_DELAY');
      return;
    }
    markerDragging.current = false;
  };

  return (
    <div style={{ position: 'relative', width, height }}>
      <canvas
        ref={canvasRef}
        style={{
          width,
          height,
          display: 'block',
          cursor: 'col-resize',
        }}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onLostPointerCapture={handlePointerUp}
      />

      {/* Top Toolbar: Zoom controls & Freeze button */}
      <div
        style={{
          position: 'absolute',
          top: 6,
          left: 100,
          display: 'flex',
          alignItems: 'center',
          gap: 6,
          zIndex: 5,
          fontFamily: 'JetBrains Mono, monospace',
          userSelect: 'none',
        }}
      >
        <button
          onClick={handleZoomOut}
          style={{
            background: 'rgba(0,0,0,0.65)',
            border: '1px solid rgba(255,255,255,0.18)',
            color: 'rgba(255,255,255,0.7)',
            padding: '2px 7px',
            fontSize: 8.5,
            cursor: 'pointer',
            borderRadius: 2,
            lineHeight: 1.2,
          }}
          title="Zoom Out (-)"
        >
          ZOOM -
        </button>
        <button
          onClick={handleResetZoom}
          style={{
            background: 'rgba(0,0,0,0.65)',
            border: '1px solid rgba(255,255,255,0.18)',
            color: 'rgba(255,255,255,0.85)',
            padding: '2px 7px',
            fontSize: 8.5,
            cursor: 'pointer',
            borderRadius: 2,
            lineHeight: 1.2,
          }}
          title="Reset Zoom (1x)"
        >
          1x
        </button>
        <button
          onClick={handleZoomIn}
          style={{
            background: 'rgba(0,0,0,0.65)',
            border: '1px solid rgba(255,255,255,0.18)',
            color: 'rgba(255,255,255,0.7)',
            padding: '2px 7px',
            fontSize: 8.5,
            cursor: 'pointer',
            borderRadius: 2,
            lineHeight: 1.2,
          }}
          title="Zoom In (+)"
        >
          ZOOM +
        </button>
        <button
          onClick={toggleFreeze}
          style={{
            background: isFrozen ? 'rgba(255, 60, 60, 0.25)' : 'rgba(0,0,0,0.65)',
            border: `1px solid ${isFrozen ? 'rgba(255, 80, 80, 0.7)' : 'rgba(255,255,255,0.18)'}`,
            color: isFrozen ? '#ff6b6b' : 'rgba(255,255,255,0.6)',
            padding: '2px 8px',
            fontSize: 8.5,
            cursor: 'pointer',
            borderRadius: 2,
            lineHeight: 1.2,
            fontWeight: isFrozen ? 600 : 400,
          }}
          title="Freeze display to inspect transient phase alignment"
        >
          {isFrozen ? '● CAPTURED (FROZEN)' : '○ CAPTURE / FREEZE'}
        </button>
        {isFrozen && (
          <span
            style={{
              fontSize: 7.5,
              color: '#4ade80',
              padding: '1px 5px',
              background: 'rgba(74, 222, 128, 0.12)',
              border: '1px solid rgba(74, 222, 128, 0.3)',
              borderRadius: 2,
              letterSpacing: '0.08em',
            }}
          >
            INSPECTION ACTIVE
          </span>
        )}
        <span
          style={{
            fontSize: 7.5,
            color: 'rgba(255,255,255,0.3)',
            marginLeft: 4,
          }}
        >
          ZOOM: {zoomY.toFixed(1)}x
        </span>
      </div>
    </div>
  );
}

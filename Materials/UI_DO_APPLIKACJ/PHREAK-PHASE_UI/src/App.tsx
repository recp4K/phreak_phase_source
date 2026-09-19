import { useState, useEffect } from 'react';
import PhreakPhase from './components/PhreakPhase';

export default function App() {
  const [scale, setScale] = useState(1);

  useEffect(() => {
    const handleResize = () => {
      const s = Math.min(window.innerWidth / 1120, window.innerHeight / 650);
      setScale(s > 0 ? s : 1);
    };
    handleResize();
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  return (
    <div
      style={{
        width: '100vw',
        height: '100vh',
        overflow: 'hidden',
        background: '#08080C',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}
    >
      <div
        style={{
          width: 1120,
          height: 650,
          transform: `scale(${scale})`,
          transformOrigin: 'center center',
          flexShrink: 0,
        }}
      >
        <PhreakPhase />
      </div>
    </div>
  );
}

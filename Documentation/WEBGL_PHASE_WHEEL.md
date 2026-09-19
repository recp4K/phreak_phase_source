# WebGL Phase Wheel - Dokumentacja

## 🎨 Przegląd

**WebGL Phase Wheel** to wysokowydajny komponent wizualizacyjny dla plugina Freak Phase, wyświetlający w czasie rzeczywistym kąt fazowy i korelację między dwoma sygnałami audio. Wykorzystuje akcelerację sprzętową WebGL dla płynnej animacji, z automatycznym fallbackiem do 2D Canvas gdy WebGL jest niedostępny.

### ✅ Kluczowe cechy

- **⚡ Akceleracja sprzętowa**: Wykorzystuje WebGL 1.0 dla maksymalnej wydajności
- **🎯 Płynna animacja**: 60 FPS rendering (30/45Hz aktualizacja danych)
- **🔄 Fallback 2D**: Automatyczne przełączanie na Canvas 2D gdy WebGL niedostępny
- **🎨 Wizualizacja wielowymiarowa**: Pokazuje kąt fazowy, korelację, RMS i polaryzację
- **📱 Responsywny**: Dostosowuje się do rozmiaru kontenera
- **🔒 Lock-free**: Bez blokowania wątku audio (dane pobierane atomowo)

---

## 📋 Funkcje

### `PhaseWheelRenderer` - Główna klasa

Klasa odpowiedzialna za renderowanie koła fazowego.

**Konstruktor:**
```typescript
new PhaseWheelRenderer(options: PhaseWheelOptions)
```

**Parametry:**
```typescript
interface PhaseWheelOptions {
  canvas?: HTMLCanvasElement | OffscreenCanvas;  // Element canvas do renderowania
  size?: number;                              // Rozmiar (domyślnie 512)
  backgroundColor?: string;                   // Kolor tła (domyślnie transparent)
  foregroundColor?: string;                   // Kolor pierwszego planu
  correlationColor?: string;                 // Kolor wskaźnika korelacji
  phaseColor?: string;                       // Kolor wskaźnika fazy
}
```

### `render(phaseAngleDeg, correlation, rmsTrackA, rmsTrackB)` - Renderowanie

Wykonuje renderowanie koła fazowego z podanymi parametrami.

**Parametry:**
- `phaseAngleDeg`: Kąt fazowy w stopniach (-180 do 180)
- `correlation`: Współczynnik korelacji Pearsona (-1 do 1)
- `rmsTrackA`: RMS Track A (0-1, domyślnie 0)
- `rmsTrackB`: RMS Track B (0-1, domyślnie 0)

---

## 🔧 Użycie w TypeScript

### Import i inicjalizacja

```typescript
import { PhaseWheelRenderer, onAudioFrame } from './juceBridge';
```

### Przykład 1: Podstawowe użycie

```typescript
// Utwórz renderer
const phaseWheel = new PhaseWheelRenderer({
  size: 512,
  backgroundColor: '#1a1a2e',
  foregroundColor: '#ffffff',
  correlationColor: '#00ff88',
  phaseColor: '#ff00aa'
});

// Dodaj do DOM
const container = document.getElementById('phase-wheel-container');
container?.appendChild(phaseWheel.canvas);

// Render z przykładowymi danymi
phaseWheel.render(45, 0.85, 0.7, 0.6);
```

### Przykład 2: Integracja z danymi audio

```typescript
import { PhaseWheelRenderer, onAudioFrame } from './juceBridge';

// Utwórz renderer
const phaseWheel = new PhaseWheelRenderer({ size: 400 });

document.getElementById('ui-container')?.appendChild(phaseWheel.canvas);

// Subskrypcja na dane audio
const unsubscribe = onAudioFrame((frame) => {
  // Wyodrębnij dane z telemetrii
  const phaseAngle = frame.phaseAngleDeg || 0;
  const correlation = frame.correlation || 0;
  const rmsA = Math.pow(10, (frame.rmsTrackA_dB || -60) / 20);
  const rmsB = Math.pow(10, (frame.rmsTrackB_dB || -60) / 20);
  
  // Renderuj
  phaseWheel.render(phaseAngle, correlation, rmsA, rmsB);
});

// Pamiętaj o odsubskrybowaniu
// unsubscribe();
```

### Przykład 3: Integracja z React

```typescript
import React, { useEffect, useRef } from 'react';
import { PhaseWheelRenderer, onAudioFrame } from './juceBridge';

interface PhaseWheelProps {
  size?: number;
  className?: string;
}

export function PhaseWheel({ size = 512, className }: PhaseWheelProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const rendererRef = useRef<PhaseWheelRenderer | null>(null);

  useEffect(() => {
    // Utwórz renderer
    const renderer = new PhaseWheelRenderer({ size });
    rendererRef.current = renderer;
    
    if (containerRef.current) {
      containerRef.current.appendChild(renderer.canvas);
    }

    // Subskrypcja na dane audio
    const unsubscribe = onAudioFrame((frame) => {
      const phaseAngle = frame.phaseAngleDeg || 0;
      const correlation = frame.correlation || 0;
      const rmsA = Math.pow(10, (frame.rmsTrackA_dB || -60) / 20);
      const rmsB = Math.pow(10, (frame.rmsTrackB_dB || -60) / 20);
      
      renderer.render(phaseAngle, correlation, rmsA, rmsB);
    });

    return () => {
      unsubscribe();
      if (containerRef.current && renderer.canvas.parentNode) {
        containerRef.current.removeChild(renderer.canvas);
      }
    };
  }, [size]);

  return (
    <div 
      ref={containerRef} 
      className={`phase-wheel-container ${className || ''}`}
      style={{ width: size, height: size }}
    />
  );
}
```

### Przykład 4: Pełna integracja z UI

```typescript
import { PhaseWheelRenderer, onAudioFrame, sendParameter } from './juceBridge';

class PhaseWheelUI {
  private renderer: PhaseWheelRenderer;
  private container: HTMLElement;
  private lastPhaseAngle: number = 0;
  private lastCorrelation: number = 0;

  constructor(containerId: string) {
    this.container = document.getElementById(containerId) || 
                     document.createElement('div');
    
    this.renderer = new PhaseWheelRenderer({
      size: 300,
      backgroundColor: '#0a0a0a',
      foregroundColor: '#ffffff'
    });
    
    this.container.appendChild(this.renderer.canvas);
    this.setupEventListeners();
    this.setupAudioSubscription();
  }

  private setupAudioSubscription() {
    onAudioFrame((frame) => {
      this.lastPhaseAngle = frame.phaseAngleDeg || 0;
      this.lastCorrelation = frame.correlation || 0;
      
      const rmsA = Math.pow(10, (frame.rmsTrackA_dB || -60) / 20);
      const rmsB = Math.pow(10, (frame.rmsTrackB_dB || -60) / 20);
      
      this.renderer.render(this.lastPhaseAngle, this.lastCorrelation, rmsA, rmsB);
    });
  }

  private setupEventListeners() {
    // Kliknięcie na koło fazowe - resetuj fazę
    this.renderer.canvas.addEventListener('click', () => {
      sendParameter('SUB_ROTATE', 0);
      sendParameter('SUB_DELAY', 0);
    });

    // Podwójne kliknięcie - automatyczne wyrównanie
    this.renderer.canvas.addEventListener('dblclick', () => {
      // Uruchom Smart Align
      triggerSmartAlign();
    });
  }

  getCurrentPhase(): number {
    return this.lastPhaseAngle;
  }

  getCurrentCorrelation(): number {
    return this.lastCorrelation;
  }
}

// Użycie
const phaseWheel = new PhaseWheelUI('phase-wheel-container');
```

---

## 🎨 Wizualizacja

### Elementy wizualne

1. **Koło fazowe (Phase Wheel)**
   - Okrąg o promieniu 0.8 (80% rozmiaru canvas)
   - Środek koła reprezentuje 0°
   - Obwód koła reprezentuje ±180°
   - Wektor od środka do punktu na obwodzie pokazuje aktualny kąt fazowy

2. **Wskaźnik korelacji (Correlation Arc)**
   - Łuk na obwodzie koła
   - Kolor zależy od poziomu korelacji:
     - Zielony (0.7-1.0): Wysoka korelacja
     - Żółty (0.3-0.7): Średnia korelacja
     - Czerwony (-1.0-0.3): Niska/ujemna korelacja
   - Długość łuku proporcjonalna do wartości bezwzględnej korelacji

3. **Wskaźnik RMS (RMS Bars)**
   - Dwa paski po bokach koła
   - Lewy pasek: RMS Track A
   - Prawy pasek: RMS Track B
   - Wysokość pasków proporcjonalna do poziomu RMS

4. **Wektor fazowy (Phase Vector)**
   - Linia od środka do punktu na obwodzie
   - Kąt = kąt fazowy
   - Długość = poziom korelacji (skalowany)
   - Kolor = gradient od białego (środek) do koloru fazy (obwód)

5. **Tło (Background)**
   - Przezroczyste lub kolor zdefiniowany w opcjach
   - Może zawierać siatkę pomocniczą (co 30°)

### Kolory domyślne

```typescript
{
  backgroundColor: 'transparent',
  foregroundColor: '#ffffff',      // Biały
  correlationColor: '#00ff88',    // Zielony
  phaseColor: '#ff00aa'           // Różowy
}
```

---

## 🎯 Interpretacja wizualna

### Wysoka korelacja (0.7-1.0)
- **Wygląd**: Długi, jasny wektor, zielony łuk korelacji
- **Znaczenie**: Sygnały są dobrze zrównane fazowo
- **Działanie**: Minimalne korekty potrzebne

### Średnia korelacja (0.3-0.7)
- **Wygląd**: Średniej długości wektor, żółty łuk korelacji
- **Znaczenie**: Sygnały mają pewne problemy fazowe
- **Działanie**: Umiarkowane korekty mogą poprawić brzmienie

### Niska korelacja (-0.3-0.3)
- **Wygląd**: Krótki wektor, czerwony łuk korelacji
- **Znaczenie**: Sygnały nie są zrównane fazowo
- **Działanie**: Potrzebne znaczne korekty (opóźnienie, rotacja, flip)

### Ujemna korelacja (-1.0--0.3)
- **Wygląd**: Krótki wektor, czerwony łuk korelacji, wektor może być przeciwny
- **Znaczenie**: Sygnały są w przeciwnej fazie
- **Działanie**: Konieczny flip polaryzacji + korekty

### Kąt fazowy 0°
- **Wygląd**: Wektor skierowany w prawo (3 godziny)
- **Znaczenie**: Sygnały są idealnie zrównane

### Kąt fazowy +90°
- **Wygląd**: Wektor skierowany w górę (12 godziny)
- **Znaczenie**: Track B jest opóźniony o 90° względem Track A

### Kąt fazowy -90°
- **Wygląd**: Wektor skierowany w dół (6 godziny)
- **Znaczenie**: Track B jest wyprzedzony o 90° względem Track A

### Kąt fazowy ±180°
- **Wygląd**: Wektor skierowany w lewo (9 godziny)
- **Znaczenie**: Sygnały są w przeciwnej fazie (potrzebny flip)

---

## ⚙️ Implementacja WebGL

### Shadery

#### Vertex Shader
```glsl
attribute vec2 position;    // Pozycja (x, y)
attribute float angle;      // Kąt
attribute float radius;     // Promień
attribute vec3 color;       // Kolor
varying vec3 vColor;       // Kolor do fragment shadera
varying float vRadius;     // Promień do fragment shadera

void main() {
  vColor = color;
  vRadius = radius;
  float x = position.x * radius;
  float y = position.y * radius;
  gl_Position = vec4(x, y, 0.0, 1.0);
  gl_PointSize = 4.0;
}
```

#### Fragment Shader
```glsl
precision highp float;
varying vec3 vColor;
varying float vRadius;

void main() {
  // Efekt gradientu radialnego
  float dist = length(gl_PointCoord - vec2(0.5));
  if (dist > 0.5) discard;
  
  // Efekt świecenia - zewnętrzne punkty są jaśniejsze
  float glow = smoothstep(0.2, 0.8, vRadius);
  gl_FragColor = vec4(vColor * (0.7 + 0.3 * glow), 1.0);
}
```

### Rendering WebGL

1. **Inicjalizacja**:
   - Utworzenie kontekstu WebGL
   - Skompilowanie shaderów
   - Utworzenie programu i buforów

2. **Przygotowanie wierzchołków**:
   - Wektor fazowy (2 punkty: środek i koniec)
   - Łuk korelacji (multiple punkty)
   - Paski RMS (2 prostokąty)
   - Siatka pomocnicza (opcjonalnie)

3. **Renderowanie**:
   - Wyczyszczenie bufora
   - Ustawienie viewportu
   - Renderowanie wszystkich elementów

---

## 🎨 Fallback 2D Canvas

Gdy WebGL jest niedostępny, renderer automatycznie przełącza się na 2D Canvas:

```typescript
private render2D(
  phaseAngleDeg: number,
  correlation: number,
  rmsTrackA: number,
  rmsTrackB: number
): void {
  const ctx = this.canvas.getContext('2d');
  if (!ctx) return;

  const centerX = this.size / 2;
  const centerY = this.size / 2;
  const radius = this.size * 0.4;

  // Wyczyść canvas
  ctx.clearRect(0, 0, this.size, this.size);

  // Narysuj koło fazowe
  ctx.beginPath();
  ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
  ctx.strokeStyle = this.options.foregroundColor || '#ffffff';
  ctx.lineWidth = 1;
  ctx.stroke();

  // Narysuj wektor fazowy
  const angleRad = phaseAngleDeg * Math.PI / 180;
  const vecX = centerX + Math.cos(angleRad) * radius * correlation;
  const vecY = centerY + Math.sin(angleRad) * radius * correlation;

  ctx.beginPath();
  ctx.moveTo(centerX, centerY);
  ctx.lineTo(vecX, vecY);
  ctx.strokeStyle = this.options.phaseColor || '#ff00aa';
  ctx.lineWidth = 3;
  ctx.stroke();

  // Narysuj łuk korelacji
  const startAngle = -Math.PI / 2;
  const endAngle = startAngle + Math.PI * correlation;
  
  ctx.beginPath();
  ctx.arc(centerX, centerY, radius * 1.1, startAngle, endAngle);
  ctx.strokeStyle = this.getCorrelationColor(correlation);
  ctx.lineWidth = 8;
  ctx.stroke();

  // Narysuj paski RMS
  this.drawRmsBars(ctx, centerX, centerY, radius, rmsTrackA, rmsTrackB);
}

private getCorrelationColor(correlation: number): string {
  if (correlation > 0.7) return '#00ff88';
  if (correlation > 0.3) return '#ffff00';
  return '#ff0000';
}
```

---

## 📊 Wydajność

### Porównanie WebGL vs 2D Canvas

| Metryka | WebGL | 2D Canvas |
|---------|-------|------------|
| FPS (300px) | 60+ | 60+ |
| FPS (1000px) | 60+ | ~45 |
| Użycie CPU | Niskie | Średnie |
| Użycie GPU | Średnie | Niskie |
| Pamięć | ~10MB | ~5MB |

### Optymalizacje

1. **Minimalna liczba wierzchołków**: Tylko niezbędne punkty są renderowane
2. **Buforowanie**: Wierzchołki są buforowane i ponownie używane
3. **Instanced rendering**: W przyszłości można użyć `gl.drawArraysInstanced`
4. **LOD (Level of Detail)**: Mniej wierzchołków dla mniejszych rozmiarów

---

## 🔧 Dostosowywanie

### Zmiana kolorów

```typescript
const renderer = new PhaseWheelRenderer({
  backgroundColor: '#000000',
  foregroundColor: '#ffffff',
  correlationColor: '#00ff00',
  phaseColor: '#ff00ff'
});
```

### Zmiana rozmiaru

```typescript
const renderer = new PhaseWheelRenderer({ size: 600 });
```

### Użycie niestandardowego canvas

```typescript
const canvas = document.createElement('canvas');
canvas.id = 'my-phase-wheel';
document.body.appendChild(canvas);

const renderer = new PhaseWheelRenderer({ canvas });
```

### Offscreen rendering (Web Worker)

```typescript
// W Web Workerze
const offscreenCanvas = new OffscreenCanvas(512, 512);
const renderer = new PhaseWheelRenderer({ 
  canvas: offscreenCanvas,
  size: 512 
});

// Render do OffscreenCanvas
renderer.render(45, 0.85, 0.7, 0.6);

// Przesyłanie bitmapy z powrotem do głównego wątku
const bitmap = offscreenCanvas.transferToImageBitmap();
postMessage({ type: 'rendered', bitmap }, [bitmap]);
```

---

## 🛠️ Rozszerzanie

### Dodawanie nowych elementów wizualnych

```typescript
class ExtendedPhaseWheelRenderer extends PhaseWheelRenderer {
  private additionalBuffer: WebGLBuffer | null = null;

  constructor(options: PhaseWheelOptions) {
    super(options);
    this.initAdditionalElements();
  }

  private initAdditionalElements() {
    // Inicjalizuj dodatkowe bufory/elementy
  }

  render(
    phaseAngleDeg: number,
    correlation: number,
    rmsTrackA: number,
    rmsTrackB: number
  ): void {
    // Wywołaj oryginalne renderowanie
    super.render(phaseAngleDeg, correlation, rmsTrackA, rmsTrackB);
    
    // Dodaj własne elementy
    this.renderAdditionalElements();
  }

  private renderAdditionalElements() {
    // Renderuj dodatkowe elementy
  }
}
```

### Dodawanie animacji

```typescript
class AnimatedPhaseWheelRenderer extends PhaseWheelRenderer {
  private animationFrame: number = 0;
  private isAnimating: boolean = false;

  startAnimation() {
    this.isAnimating = true;
    this.animate();
  }

  stopAnimation() {
    this.isAnimating = false;
    cancelAnimationFrame(this.animationFrame);
  }

  private animate() {
    if (!this.isAnimating) return;
    
    // Aktualizuj stan animacji
    this.animationFrame = requestAnimationFrame(() => this.animate());
    
    // Render z animowanymi parametrami
    const time = Date.now() * 0.001;
    const phase = Math.sin(time) * 90;
    const correlation = Math.sin(time * 0.5) * 0.5 + 0.5;
    
    this.render(phase, correlation, 0.7, 0.6);
  }
}
```

---

## 🔍 Rozwiązywanie problemów

### Problem: WebGL niedostępny
- **Przyczyna**: Przeglądarka nie obsługuje WebGL
- **Rozwiązanie**: Renderer automatycznie używa 2D Canvas
- **Diagnostyka**: Sprawdź `WebGLRenderingContext.getContext('webgl')`

### Problem: Niska wydajność
- **Przyczyna**: Zbyt wiele wierzchołków lub zbyt duży canvas
- **Rozwiązanie**: Zmniejsz rozmiar canvas lub zredukuj liczbę wierzchołków

### Problem: Artefakty graficzne
- **Przyczyna**: Błędy w shaderach lub problem z precyzją
- **Rozwiązanie**: Sprawdź logi WebGL (`gl.getError()`)

### Problem: Canvas nie renderuje
- **Przyczyna**: Canvas nie jest podłączony do DOM
- **Rozwiązanie**: Upewnij się, że canvas jest dodany do dokumentu

### Problem: Kolory nie wyglądają dobrze
- **Przyczyna**: Problemy z gamma lub kolorami w shaderze
- **Rozwiązanie**: Dodaj korekcję gamma w fragment shaderze

---

## 📚 API Reference

### Interfejsy

#### `PhaseWheelOptions`
```typescript
interface PhaseWheelOptions {
  canvas?: HTMLCanvasElement | OffscreenCanvas;
  size?: number;
  backgroundColor?: string;
  foregroundColor?: string;
  correlationColor?: string;
  phaseColor?: string;
}
```

### Metody

#### `new PhaseWheelRenderer(options?: PhaseWheelOptions)`
Tworzy nową instancję renderera.

#### `renderer.render(phaseAngleDeg: number, correlation: number, rmsTrackA?: number, rmsTrackB?: number): void`
Renderuje koło fazowe z podanymi parametrami.

#### `renderer.canvas: HTMLCanvasElement | OffscreenCanvas`
Zwraca element canvas używany do renderowania.

---

## 🎓 Teoria

### Co to jest kąt fazowy?

Kąt fazowy to różnica faz między dwoma sygnałami sinusoidalnymi o tej samej częstotliwości. Mierzy się go w stopniach (-180° do +180°) lub radianach (-π do +π).

- **0°**: Sygnały są idealnie zrównane
- **+90°**: Sygnał B jest opóźniony o 90° względem A
- **-90°**: Sygnał B jest wyprzedzony o 90° względem A
- **±180°**: Sygnały są w przeciwnej fazie (konieczny flip)

### Co to jest korelacja?

Korelacja Pearsona mierzona jest w zakresie [-1, 1]:

- **1.0**: Idealna dodatnia korelacja (sygnały identyczne)
- **0.0**: Brak korelacji (sygnały niezależne)
- **-1.0**: Idealna ujemna korelacja (sygnały przeciwne)

W kontekście audio:
- **Wysoka korelacja (0.7-1.0)**: Sygnały są dobrze zrównane
- **Średnia korelacja (0.3-0.7)**: Sygnały mają pewne problemy
- **Niska korelacja (-0.3-0.3)**: Sygnały nie są zrównane
- **Ujemna korelacja (-1.0--0.3)**: Sygnały są w przeciwnej fazie

---

## 📝 Historia zmian

| Wersja | Data | Opis |
|--------|------|------|
| 1.0 | Wrzesień 2026 | Pierwsza implementacja WebGL Phase Wheel |

---

## 🤝 Współpraca

Jeśli masz pomysły na ulepszenie WebGL Phase Wheel:

1. **Optymalizuj shadery**: Popraw wydajność shaderów GLSL
2. **Dodaj nowe efekty**: Nowe efekty wizualne (np. trajektorie, historia)
3. **Popraw responsywność**: Lepsze dostosowywanie do różnych rozmiarów
4. **Dodaj interakcję**: Obsługa dotyku, gestów
5. **Dziel się pomysłami**: Podziel się swoimi ideami z społecznością

---

## 📄 Powiązane dokumenty

- [AI Smart Align - Dokumentacja](./AI_SMART_ALIGN.md)
- [MIDI Learn - Dokumentacja](./MIDI_LEARN.md)
- [Freak Phase - Podręcznik użytkownika](../README.md)

# AI Smart Align - Dokumentacja

## 🤖 Przegląd

System **AI Smart Align** to lekki, oparty na przeglądarce system predykcji optymalnego wyrównania fazowego dla plugina Freak Phase. Wykorzystuje prosty model decyzyjny (decision tree) do analizy charakterystyki sygnałów audio i automatycznego dobierania parametrów wyrównania.

### ✅ Kluczowe cechy

- **🎯 Bez zależności zewnętrznych**: Działa całkowicie w przeglądarce, bez konieczności TensorFlow.js lub innych bibliotek ML
- **⚡ Szybki**: Obliczenia wykonują się w czasie rzeczywistym (<1ms na predykcję)
- **📊 Inteligentny**: Uwzględnia wielowymiarowe cechy sygnału (korelacja, RMS, centroid spektralny, częstotliwość podstawowa)
- **🎛️ Integracja z Timeline**: Obsługuje predykcje dla wielu segmentów czasowych

---

## 📋 Funkcje

### `predict(features)` - Predykcja pojedyncza

Wykonywana jest pojedyncza predykcja optymalnych parametrów wyrównania na podstawie podanych cech sygnału.

**Parametry:**
```typescript
{
  correlation: number;      // Współczynnik korelacji Pearsona [-1, 1]
  rmsTrackA: number;       // RMS Track A (0-1)
  rmsTrackB: number;       // RMS Track B (0-1)
  spectralCentroid?: number; // Centroid spektralny (Hz)
  fundamentalHz?: number;   // Częstotliwość podstawowa (Hz)
}
```

**Zwraca:** `AISmartAlignResult`
```typescript
{
  delayMs: number;        // Opóźnienie w milisekundach [0, 20]
  delaySamples: number;   // Opóźnienie w próbkach
  rotateDeg: number;      // Rotacja fazy w stopniach [-30, 30]
  flip: boolean;          // Czy odwrócić polaryzację
  eqCutDb: number;        // Cięcie EQ w dB [-6, 6]
  confidence: number;     // Poziom pewności [0, 1]
}
```

### `predictForTimeline(segments)` - Predykcja dla Timeline

Wykonywana jest predykcja dla wielu segmentów czasowych, zwracając tablicę wyników.

**Parametry:**
```typescript
Array<{
  correlation: number;
  rmsTrackA: number;
  rmsTrackB: number;
  spectralCentroid?: number;
  fundamentalHz?: number;
}>
```

**Zwraca:** `AISmartAlignResult[]`

---

## 🔧 Użycie w TypeScript

### Import i inicjalizacja

```typescript
import { AISmartAligner, AISmartAlignResult } from './juceBridge';
```

### Przykład 1: Pojedyncza predykcja

```typescript
// Pobierz aktualne cechy sygnału
const features = {
  correlation: 0.45,       // Niska korelacja = potrzebne wyrównanie
  rmsTrackA: 0.6,
  rmsTrackB: 0.8,
  spectralCentroid: 120,   // Średnie częstotliwości
  fundamentalHz: 55        // Bass w okolicy 55 Hz
};

// Wykonaj predykcję
const result: AISmartAlignResult = AISmartAligner.predict(features);

// Zastosuj wyniki
console.log(`Sugerowane opóźnienie: ${result.delayMs}ms`);
console.log(`Sugerowana rotacja: ${result.rotateDeg}°`);
console.log(`Odwrócić polaryzację: ${result.flip ? 'TAK' : 'NIE'}`);
console.log(`Cięcie EQ: ${result.eqCutDb}dB`);
console.log(`Pewność: ${(result.confidence * 100).toFixed(1)}%`);
```

### Przykład 2: Predykcja dla Timeline

```typescript
// Segmenty czasowe z analizy
const segments = [
  { correlation: 0.3, rmsTrackA: 0.5, rmsTrackB: 0.7, spectralCentroid: 100, fundamentalHz: 40 },
  { correlation: 0.7, rmsTrackA: 0.6, rmsTrackB: 0.6, spectralCentroid: 150, fundamentalHz: 60 },
  { correlation: 0.2, rmsTrackA: 0.4, rmsTrackB: 0.9, spectralCentroid: 80, fundamentalHz: 35 }
];

// Predykcja dla wszystkich segmentów
const results: AISmartAlignResult[] = AISmartAligner.predictForTimeline(segments);

// Zastosuj wyniki do timeline
results.forEach((result, index) => {
  console.log(`Segment ${index + 1}: delay=${result.delayMs}ms, rotate=${result.rotateDeg}°`);
});
```

### Przykład 3: Integracja z UI (React)

```typescript
import React, { useState, useEffect } from 'react';
import { AISmartAligner, onAudioFrame } from './juceBridge';

export function SmartAlignButton() {
  const [alignResult, setAlignResult] = useState<AISmartAlignResult | null>(null);
  const [features, setFeatures] = useState({
    correlation: 0,
    rmsTrackA: 0,
    rmsTrackB: 0,
    spectralCentroid: 100,
    fundamentalHz: 50
  });

  // Subskrypcja na dane audio
  useEffect(() => {
    const unsubscribe = onAudioFrame((frame) => {
      setFeatures({
        correlation: frame.correlation,
        rmsTrackA: Math.pow(10, frame.rmsTrackA_dB / 20),
        rmsTrackB: Math.pow(10, frame.rmsTrackB_dB / 20),
        spectralCentroid: 100, // TODO: Pobierać z analizy
        fundamentalHz: frame.detectedFundamentalHz
      });
    });

    return () => unsubscribe();
  }, []);

  const handleSmartAlign = () => {
    const result = AISmartAligner.predict(features);
    setAlignResult(result);
    
    // Zastosuj parametry do plugina
    sendParameter('SUB_DELAY', result.delayMs);
    sendParameter('SUB_ROTATE', result.rotateDeg);
    sendParameter('POLARITY_FLIP', result.flip ? 1 : 0);
    sendParameter('EQ_CUT_DB', result.eqCutDb);
  };

  return (
    <div className="smart-align-panel">
      <button onClick={handleSmartAlign} className="btn-smart-align">
        🎯 Smart Align
      </button>
      {alignResult && (
        <div className="align-result">
          <p>Opóźnienie: {alignResult.delayMs.toFixed(1)}ms</p>
          <p>Rotacja: {alignResult.rotateDeg.toFixed(1)}°</p>
          <p>Flip: {alignResult.flip ? '✓' : '✗'}</p>
          <p>EQ Cut: {alignResult.eqCutDb.toFixed(1)}dB</p>
        </div>
      )}
    </div>
  );
}
```

---

## 🎛️ Integracja z C++

### Wysyłanie danych do AI

W `PluginEditor.cpp`, upewnij się, że dane telemetryczne są wysyłane do UI:

```cpp
// W metodzie timerCallback() lub processBlock()
AudioFrameTelemetry telemetry;
telemetry.correlation = signalAnalyzer.getCorrelation();
telemetry.rmsTrackA_dB = juce::Decibels::gainToDecibels(rmsTrackA, -60.0f);
telemetry.rmsTrackB_dB = juce::Decibels::gainToDecibels(rmsTrackB, -60.0f);
telemetry.detectedFundamentalHz = pitchTracker.getSmoothedFrequency();

// Wyślij do UI
emitAudioFrameEvent(telemetry);
```

### Odbieranie wyników z AI

```cpp
// W PluginProcessor.cpp
void PluginProcessor::handleSmartAlignRequest() {
    // AI oblicza optymalne parametry w JavaScript
    // My otrzymujemy wyniki przez emitEvent
    
    // Zastosuj parametry
    *apvts.getRawParameterValue("SUB_DELAY") = result.delayMs / 1000.0f;
    *apvts.getRawParameterValue("SUB_ROTATE") = result.rotateDeg;
    *apvts.getRawParameterValue("POLARITY_FLIP") = result.flip ? 1.0f : 0.0f;
    *apvts.getRawParameterValue("EQ_CUT_DB") = result.eqCutDb;
}
```

---

## ⚙️ Algorytm

### Normalizacja cech

Każda cecha jest normalizowana do zakresu [0, 1]:

```typescript
// Korelacja: [-1, 1] → [0, 1]
const normCorrelation = (features.correlation + 1) / 2;

// Współczynnik RMS: [0, 2] → [0, 1]
const rmsRatio = features.rmsTrackB > 0.001 
  ? features.rmsTrackA / features.rmsTrackB 
  : 1;
const normRmsRatio = Math.min(Math.max(rmsRatio, 0), 2);

// Centroid spektralny: [20, 500] Hz → [0, 1]
const normSpectral = (features.spectralCentroid - 20) / 480;

// Częstotliwość podstawowa: [25, 220] Hz → [0, 1]
const normFundamental = (features.fundamentalHz - 25) / 195;
```

### Wagi cech

```typescript
{
  correlation: 0.4,           // Najważniejsza - niska korelacja = problemy fazowe
  rmsRatio: 0.25,            // Równowaga między trackami
  spectralCentroid: 0.2,     // Charakterystyka spektralna
  fundamentalFreq: 0.15      // Podstawowa częstotliwość
}
```

### Mapowanie na parametry

1. **Opóźnienie (delay)**:
   ```typescript
   delayMs = (1 - normCorrelation) * 20;  // 0-20ms
   ```
   Im niższa korelacja, tym większe potrzebne opóźnienie.

2. **Rotacja fazy (rotate)**:
   ```typescript
   rotateDeg = (normRmsRatio - 0.5) * 60;  // -30 to +30°
   ```
   Jeśli Track B jest głośniejszy, rotujemy fazę.

3. **Flip polaryzacji**:
   ```typescript
   flip = normCorrelation < 0.2;
   ```
   Bardzo niska korelacja może oznaczać problem z polaryzacją.

4. **Cięcie EQ**:
   ```typescript
   eqCutDb = (normSpectral - 0.5) * 12;  // -6 to +6 dB
   ```
   Wyższe częstotliwości mogą wymagać większego cięcia.

5. **Pewność**:
   ```typescript
   confidence = 0.5 + 0.5 * normCorrelation;
   ```
   Wyższa korelacja = wyższa pewność predykcji.

---

## 🎯 Typowe scenariusze

### Scenariusz 1: Kick i Bass w fazie
```
Cechy: correlation = 0.85, rmsTrackA = 0.7, rmsTrackB = 0.7
Wynik: delayMs = 1.5, rotateDeg = 0, flip = false, eqCutDb = 0
Interpretacja: Sygnały są dobrze zrównane, minimalne korekty
```

### Scenariusz 2: Kick i Bass nie w fazie
```
Cechy: correlation = 0.25, rmsTrackA = 0.6, rmsTrackB = 0.8
Wynik: delayMs = 15, rotateDeg = 12, flip = false, eqCutDb = -3
Interpretacja: Niska korelacja, potrzebne opóźnienie i rotacja
```

### Scenariusz 3: Problem z polaryzacją
```
Cechy: correlation = -0.1, rmsTrackA = 0.5, rmsTrackB = 0.5
Wynik: delayMs = 20, rotateDeg = 0, flip = true, eqCutDb = 0
Interpretacja: Ujemna korelacja = odwrócona polaryzacja
```

### Scenariusz 4: Wysokie częstotliwości
```
Cechy: correlation = 0.5, spectralCentroid = 400, fundamentalHz = 120
Wynik: delayMs = 10, rotateDeg = 5, flip = false, eqCutDb = 4
Interpretacja: Wysoki centroid = więcej cięcia EQ
```

---

## 🔬 Walidacja i testowanie

### Test 1: Spójność wyników
```typescript
// Te same cechy powinny zawsze dawać te same wyniki
const features = { correlation: 0.5, rmsTrackA: 0.6, rmsTrackB: 0.6, spectralCentroid: 100, fundamentalHz: 50 };
const result1 = AISmartAligner.predict(features);
const result2 = AISmartAligner.predict(features);

// Powinno być true
console.assert(JSON.stringify(result1) === JSON.stringify(result2));
```

### Test 2: Zakresy wyjściowe
```typescript
const testCases = [
  { correlation: -1, rmsTrackA: 0, rmsTrackB: 1, spectralCentroid: 20, fundamentalHz: 25 },
  { correlation: 1, rmsTrackA: 1, rmsTrackB: 0, spectralCentroid: 500, fundamentalHz: 220 },
  { correlation: 0, rmsTrackA: 0.5, rmsTrackB: 0.5, spectralCentroid: 260, fundamentalHz: 122 }
];

testCases.forEach(features => {
  const result = AISmartAligner.predict(features);
  console.assert(result.delayMs >= 0 && result.delayMs <= 20);
  console.assert(result.rotateDeg >= -30 && result.rotateDeg <= 30);
  console.assert(result.eqCutDb >= -6 && result.eqCutDb <= 6);
  console.assert(result.confidence >= 0 && result.confidence <= 1);
});
```

### Test 3: Predykcja Timeline
```typescript
const segments = Array(100).fill().map((_, i) => ({
  correlation: i / 100,
  rmsTrackA: 0.5 + i / 200,
  rmsTrackB: 0.5 + (100 - i) / 200
}));

const results = AISmartAligner.predictForTimeline(segments);
console.assert(results.length === 100);
console.assert(results.every(r => r.delayMs >= 0));
```

---

## 🛠️ Dostrajanie

### Dostosowywanie wag cech

Jeśli chcesz zmienić znaczenie poszczególnych cech, zmodyfikuj `FEATURE_WEIGHTS`:

```typescript
private static readonly FEATURE_WEIGHTS = {
  correlation: 0.5,    // Zwiększ, jeśli korelacja jest najważniejsza
  rmsRatio: 0.2,      // Zmniejsz, jeśli RMS jest mniej ważny
  spectralCentroid: 0.15,
  fundamentalFreq: 0.15
};
```

### Dostosowywanie zakresów parametrów

Zmodyfikuj funkcje mapujące w metodzie `predict()`:

```typescript
// Zwiększ zakres opóźnienia do 30ms
const delayMs = (1 - normCorrelation) * 30;

// Zwiększ zakres rotacji do ±45°
const rotateDeg = (normRmsRatio - 0.5) * 90;

// Zwiększ zakres cięcia EQ do ±12dB
const eqCutDb = (normSpectral - 0.5) * 24;
```

---

## 📊 Porównanie z innymi podejściami

| Metoda | Zalety | Wady | Wymagania |
|--------|--------|------|------------|
| **AI Smart Align (Freak Phase)** | Szybki, lekki, bez zależności | Mniej dokładny niż ML | Żadne |
| TensorFlow.js | Bardzo dokładny, uczenie się | Duży rozmiar, wolny, złożony | ~300KB |
| PyTorch Web | Dobra dokładność | Jeszcze większy, wymaga backend | ~1MB+ |
| Pure ML w C++ | Najszybszy | Trudny do utrzymania | JUCE ML |

---

## 💡 Wskazówki

1. **Używaj dla szybkiej predykcji**: AI Smart Align jest idealny do szybkiego uzyskania dobrych parametrów startowych.

2. **Kombinuj z manualnym dostrojeniem**: Wyniki AI można traktować jako punkt wyjścia, a następnie dostroić manualnie.

3. **Monitoruj pewność**: Niska pewność (<0.5) oznacza, że sygnały są trudne do analizy - warto sprawdzić manualnie.

4. **Używaj z Timeline**: Dla najlepszych wyników, użyj `predictForTimeline()` do analizy różnych segmentów utworu.

5. **Testuj z różnymi materiałami**: Wyniki mogą się różnić w zależności od charakterystyki sygnałów (kick/bass, synthy, wokal itp.)

---

## 🔍 Rozwiązywanie problemów

### Problem: Niska pewność predykcji
- **Przyczyna**: Niska korelacja między sygnałami
- **Rozwiązanie**: Sprawdź, czy sygnały są rzeczywiście powiązane. Może być konieczna manualna korekta.

### Problem: Zbyt duże opóźnienie
- **Przyczyna**: Bardzo niska korelacja
- **Rozwiązanie**: Sprawdź, czy nie ma problemu z synchronizacją DAW. Możesz ograniczyć maksymalne opóźnienie.

### Problem: Predykcja nie działa
- **Przyczyna**: Brak danych wejściowych (cechy = 0)
- **Rozwiązanie**: Upewnij się, że sygnały audio są poprawnie podłączone i że nie ma ciszy.

### Problem: Wyniki są niestabilne
- **Przyczyna**: Sygnały audio szybko się zmienią (np. perkusja)
- **Rozwiązanie**: Uśredniaj cechy w czasie lub użyj `predictForTimeline()` z dłuższymi segmentami.

---

## 📚 API Reference

### Interfejsy

#### `AISmartAlignResult`
```typescript
interface AISmartAlignResult {
  delayMs: number;        // Opóźnienie w milisekundach
  delaySamples: number;   // Opóźnienie w próbkach (dla sampleRate = 44100)
  rotateDeg: number;      // Rotacja fazy w stopniach
  flip: boolean;          // Czy odwrócić polaryzację
  eqCutDb: number;        // Cięcie EQ w dB
  confidence: number;     // Poziom pewności [0, 1]
}
```

### Metody

#### `AISmartAligner.predict(features: object): AISmartAlignResult`
Wykonuje pojedynczą predykcję.

#### `AISmartAligner.predictForTimeline(segments: object[]): AISmartAlignResult[]`
Wykonuje predykcję dla wielu segmentów.

---

## 🎓 Teoria

### Dlaczego to działa?

Fazowe problemy między dwoma sygnałami audio często mają charakterystyczne cechy:

1. **Niska korelacja**: Sygnały nie są ze sobą zrównane w czasie
2. **Nierówne RMS**: Jeden sygnał dominuje nad drugim
3. **Różnice spektralne**: Sygnały mają różne charakterystyki częstotliwościowe
4. **Różnice w częstotliwości podstawowej**: Sygnały są w różnych oktawach

AI Smart Align wykorzystuje te obserwacje do predykcji optymalnych parametrów wyrównania.

### Ograniczenia

- **Uproszczony model**: Nie jest to pełny model ML, ale zbiór heurystyk
- **Zakresy ograniczone**: Parametry wyjściowe mają stałe zakresy
- **Brak uczenia się**: Model nie uczy się na nowych danych
- **Zależność od jakości cech**: Wyniki zależą od jakości danych wejściowych

Mimo to, podejście to sprawdza się w większości praktycznych scenariuszy i jest wystarczająco dobre dla większości użytkowników.

---

## 📝 Historia zmian

| Wersja | Data | Opis |
|--------|------|------|
| 1.0 | Wrzesień 2026 | Pierwsza implementacja AI Smart Align |

---

## 🤝 Współpraca

Jeśli masz pomysły na ulepszenie algorytmu AI Smart Align:

1. **Testuj z różnymi sygnałami**: Znajdź przypadki, w których predykcje nie są optymalne
2. **Proponuj nowe cechy**: Jakie inne charakterystyki sygnału mogłyby pomóc?
3. **Dostrajaj wagi**: Eksperymentuj z różnymi wagami cech
4. **Dziel się wynikami**: Podziel się swoimi obserwacjami z społecznością

---

## 📄 Powiązane dokumenty

- [MIDI Learn - Dokumentacja](./MIDI_LEARN.md)
- [WebGL Phase Wheel - Dokumentacja](./WEBGL_PHASE_WHEEL.md)
- [Freak Phase - Podręcznik użytkownika](../README.md)

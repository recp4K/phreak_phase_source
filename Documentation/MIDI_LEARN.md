# MIDI Learn - Dokumentacja

## 🎹 Przegląd

System **MIDI Learn** umożliwia mapowanie **MIDI Control Change (CC)** do parametrów plugina **Freak Phase**. Pozwala to na sterowanie pluginem za pomocą **kontrolerów MIDI**, **automation w DAW** lub **zewnętrznych urządzeń**.n

## 📋 Funkcje

| Funkcja | Opis |
|---------|------|
| `startMidiLearn(paramId)` | Rozpoczyna tryb uczenia się dla określonego parametru |
| `cancelMidiLearn()` | Anuluje tryb uczenia się |
| `clearMidiMappings()` | Usuwa wszystkie mapowania MIDI |
| `getMidiMappings()` | Zwraca aktualne mapowania CC → Parametr |
| `getSuggestedMidiMappings()` | Zwraca sugerowane mapowania |

---

## 🔧 Użycie w TypeScript (Frontend)

### Rozpoczęcie trybu MIDI Learn

```typescript
import { startMidiLearn, cancelMidiLearn } from './juceBridge';

// Rozpocznij uczenie się dla parametru SUB_ROTATE
startMidiLearn("SUB_ROTATE");

// Po otrzymaniu MIDI CC, mapowanie zostanie automatycznie zapisane
// i tryb uczenia się zostanie wyłączony
```

### Anulowanie trybu MIDI Learn

```typescript
cancelMidiLearn();
```

### Pobieranie aktualnych mapań

```typescript
import { getMidiMappings } from './juceBridge';

async function loadMappings() {
  const mappings = await getMidiMappings();
  console.log("Aktualne mapowania:", mappings);
  // { 1: "SUB_ROTATE", 7: "SUB_GAIN", ... }
}
```

### Pobieranie sugerowanych mapań

```typescript
import { getSuggestedMidiMappings } from './juceBridge';

async function loadSuggestedMappings() {
  const suggested = await getSuggestedMidiMappings();
  console.log("Sugerowane mapowania:", suggested);
  // { 1: "SUB_ROTATE", 7: "SUB_GAIN", 10: "SUB_DELAY", ... }
}
```

### Czyszczenie mapań

```typescript
import { clearMidiMappings } from './juceBridge';

clearMidiMappings();
```

---

## 🎛️ Użycie w C++ (Backend)

### Inicjalizacja

```cpp
// W PluginProcessor.h
std::map<int, juce::String> midiCCToParam;
bool midiLearnMode = false;
int currentLearningCC = -1;
juce::String currentLearningParam;
```

### Rozpoczęcie trybu uczenia

```cpp
void FreakPhaseAudioProcessor::startMidiLearn(const juce::String& paramId) {
    midiLearnMode = true;
    currentLearningParam = paramId;
    currentLearningCC = -1;
}
```

### Obsługa wiadomości MIDI

```cpp
void FreakPhaseAudioProcessor::handleMidiMessage(const juce::MidiMessage& msg) {
    if (msg.isController()) {
        const int ccNumber = msg.getControllerNumber();
        const float ccValue = msg.getControllerValue() / 127.0f;
        
        // Sprawdź, czy ten CC jest zmapowany
        auto it = midiCCToParam.find(ccNumber);
        if (it != midiCCToParam.end()) {
            const juce::String& paramId = it->second;
            if (auto* param = apvts.getParameter(paramId)) {
                const auto range = param->getNormalisableRange();
                const float normalizedValue = range.convertFrom0to1(ccValue);
                param->setValueNotifyingHost(normalizedValue);
            }
        }
        else if (midiLearnMode) {
            // Jeśli w trybie uczenia, zmapuj ten CC do bieżącego parametru
            handleMidiLearn(ccNumber);
        }
    }
}
```

### Sugerowane mapowania

```cpp
std::map<int, juce::String> FreakPhaseAudioProcessor::suggestMidiMappings() const {
    return {
        {1, "SUB_ROTATE"},      // Mod Wheel
        {7, "SUB_GAIN"},        // Volume
        {10, "SUB_DELAY"},      // Pan
        {11, "HIGH_ROTATE"},    // Expression
        {74, "HIGH_GAIN"},      // Filter Cutoff
        {71, "DYN_EQ_DEPTH"},   // Resonance
        {72, "RESPONSE"},       // Release Time
        {73, "ENV_ATTACK"},     // Attack Time
        {75, "ENV_RELEASE"},    // Decay Time
        {91, "DYN_PH_AMOUNT"},  // Reverb Wet/Dry
        {92, "BASS_GLUE"},      // Vibrato Rate
        {93, "CROSSOVER_FREQ"}  // Vibrato Depth
    };
}
```

---

## 📊 Standardowe mapowania MIDI CC

| CC # | Nazwa | Typowy parametr |
|-------|-------|-----------------|
| 0 | Bank Select MSB | - |
| 1 | Modulation Wheel | SUB_ROTATE |
| 2 | Breath Controller | - |
| 4 | Foot Controller | - |
| 5 | Portamento Time | - |
| 6 | Data Entry MSB | - |
| 7 | Channel Volume | SUB_GAIN |
| 8 | Balance | - |
| 10 | Pan | SUB_DELAY |
| 11 | Expression | HIGH_ROTATE |
| 12 | Effect Control 1 | - |
| 13 | Effect Control 2 | - |
| 16-19 | General Purpose | - |
| 32-63 | LSB for CC 0-31 | - |
| 64 | Hold Pedal | - |
| 65 | Portamento | - |
| 66 | Sostenuto | - |
| 67 | Soft Pedal | - |
| 68 | Legato Footswitch | - |
| 69 | Hold 2 | - |
| 70 | Sound Variation | - |
| 71 | Harmonic Content / Resonance | DYN_EQ_DEPTH |
| 72 | Release Time | RESPONSE |
| 73 | Attack Time | ENV_ATTACK |
| 74 | Brightness / Filter Cutoff | HIGH_GAIN |
| 75 | Decay Time | ENV_RELEASE |
| 76 | Vibrato Rate | - |
| 77 | Vibrato Depth | - |
| 78 | Vibrato Delay | - |
| 79 | Vibrato Rate LSB | - |
| 80 | Vibrato Depth LSB | - |
| 81 | Vibrato Delay LSB | - |
| 82 | Sound Controller 6 | - |
| 83 | Sound Controller 7 | - |
| 84 | Portamento Control | - |
| 85-87 | Sound Controllers | - |
| 91 | Reverb Wet/Dry | DYN_PH_AMOUNT |
| 92 | Tremolo Wet/Dry | BASS_GLUE |
| 93 | Chorus Wet/Dry | CROSSOVER_FREQ |
| 94 | Celeste Wet/Dry | - |
| 95 | Phaser Wet/Dry | - |
| 96 | Data Increment | - |
| 97 | Data Decrement | - |
| 98 | Non-Registered Parameter LSB | - |
| 99 | Non-Registered Parameter MSB | - |
| 100 | Registered Parameter LSB | - |
| 101 | Registered Parameter MSB | - |

---

## 🎯 Integracja z UI

### Przykład: Przycisk MIDI Learn

```typescript
import React from 'react';
import { startMidiLearn, cancelMidiLearn, getMidiMappings } from './juceBridge';

interface MidiLearnButtonProps {
  paramId: string;
}

export function MidiLearnButton({ paramId }: MidiLearnButtonProps) {
  const [isLearning, setIsLearning] = React.useState(false);
  const [mappedCC, setMappedCC] = React.useState<number | null>(null);

  React.useEffect(() => {
    // Sprawdź, czy parametr jest już zmapowany
    getMidiMappings().then(mappings => {
      for (const [cc, param] of Object.entries(mappings)) {
        if (param === paramId) {
          setMappedCC(parseInt(cc));
          break;
        }
      }
    });
  }, [paramId]);

  const handleStartLearn = () => {
    startMidiLearn(paramId);
    setIsLearning(true);
  };

  const handleCancelLearn = () => {
    cancelMidiLearn();
    setIsLearning(false);
  };

  return (
    <div className="midi-learn-container">
      {isLearning ? (
        <button onClick={handleCancelLearn} className="cancel-btn">
          Cancel MIDI Learn
        </button>
      ) : mappedCC ? (
        <button onClick={handleStartLearn} className="remap-btn">
          Remap (CC {mappedCC})
        </button>
      ) : (
        <button onClick={handleStartLearn} className="learn-btn">
          Learn MIDI CC
        </button>
      )}
      {isLearning && (
        <div className="learning-indicator">
          Waiting for MIDI CC...
        </div>
      )}
    </div>
  );
}
```

### Wyświetlanie listy mapań

```typescript
import React from 'react';
import { getMidiMappings, clearMidiMappings } from './juceBridge';

export function MidiMappingsList() {
  const [mappings, setMappings] = React.useState<Record<number, string>>({});

  React.useEffect(() => {
    getMidiMappings().then(setMappings);
  }, []);

  const handleClearAll = () => {
    clearMidiMappings();
    setMappings({});
  };

  return (
    <div className="midi-mappings-list">
      <h3>MIDI CC Mappings</h3>
      {Object.keys(mappings).length === 0 ? (
        <p>No MIDI mappings defined.</p>
      ) : (
        <table>
          <thead>
            <tr>
              <th>CC #</th>
              <th>Parameter</th>
              <th>Action</th>
            </tr>
          </thead>
          <tbody>
            {Object.entries(mappings).map(([cc, param]) => (
              <tr key={cc}>
                <td>{cc}</td>
                <td>{param}</td>
                <td>
                  <button onClick={() => startMidiLearn(param)}>
                    Relearn
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      <button onClick={handleClearAll} className="clear-all-btn">
        Clear All Mappings
      </button>
    </div>
  );
}
```

---

## ⚠️ Ograniczenia i Uwagi

### Ograniczenia

1. **MIDI CC tylko** - Obecnie obsługiwane są jedynie wiadomości **Control Change** (CC)
2. **Zakres CC** - Obsługiwane są CC od **0 do 127**
3. **Wartości 0-127** - Wartości MIDI są mapowane do zakresu **0-1** parametrów
4. **Brak obsługi NRPN/RPN** - Zaawansowane wiadomości MIDI nie są obsługiwane

### Uwagi

1. **Tryb uczenia** - Aktywny tylko dla **jednego parametru na raz**
2. **Zapis mapań** - Mapowania są przechowywane **w pamięci** (nie są zapisywanane do presetów)
3. **Automation** - MIDI CC **nadpisuje** automation z DAW (jeśli parametr jest zmapowany)
4. **Priorytet** - MIDI Learn ma **wyższy priorytet** niż zwykłe sterowanie MIDI

---

## 🔧 Rozwiązywanie problemów

### Problem: MIDI Learn nie działa

**Możliwe przyczyny:**
1. Kontroler MIDI nie jest podłączony
2. Plugin nie odbiera wiadomości MIDI
3. Tryb uczenia nie został aktywowany

**Rozwiązanie:**
1. Sprawdź, czy kontroler MIDI jest **podłączony i aktywny**
2. Upewnij się, że **MIDI input jest włączony** w ustawieniach DAW
3. Sprawdź, czy **acceptsMidi() zwraca true** w pluginie

### Problem: CC nie steruje parametrem

**Możliwe przyczyny:**
1. CC nie jest zmapowany do parametru
2. Parametr nie istnieje
3. Wartość MIDI jest poza zakresem parametru

**Rozwiązanie:**
1. Sprawdź mapowania za pomocą `getMidiMappings()`
2. Upewnij się, że **parametr istnieje** w APVTS
3. Sprawdź, czy **zakres parametru** jest poprawnie zdefiniowany

### Problem: Wartości parametrów „skaczą”

**Możliwe przyczyny:**
1. Konflikty między MIDI CC a automation DAW
2. Różne rozdzielczości (7-bit vs 14-bit CC)

**Rozwiązanie:**
1. Wyłącz **automation DAW** dla zmapowanych parametrów
2. Użyj **14-bit CC** (MSB + LSB) dla wyższej rozdzielczości

---

## 📚 API Reference

### Funkcje C++

```cpp
// Rozpoczyna tryb uczenia
void startMidiLearn(const juce::String& paramId);

// Anuluje tryb uczenia
void cancelMidiLearn();

// Czyści wszystkie mapowania
void clearMidiMappings();

// Obsługuje nauczenie nowego mapowania
void handleMidiLearn(int ccNumber);

// Obsługuje wiadomości MIDI
void handleMidiMessage(const juce::MidiMessage& msg);

// Zwraca sugerowane mapowania
std::map<int, juce::String> suggestMidiMappings() const;
```

### Funkcje TypeScript

```typescript
// Rozpoczyna tryb uczenia
function startMidiLearn(paramId: string): void;

// Anuluje tryb uczenia
function cancelMidiLearn(): void;

// Czyści wszystkie mapowania
function clearMidiMappings(): void;

// Pobiera aktualne mapowania
function getMidiMappings(): Promise<Record<number, string>>;

// Pobiera sugerowane mapowania
function getSuggestedMidiMappings(): Promise<Record<number, string>>;
```

---

## 🎓 Przykłady użycia

### Przykład 1: Mapowanie Mod Wheel do Phase Rotation

```typescript
// Rozpocznij uczenie
startMidiLearn("SUB_ROTATE");
// Porusz Mod Wheel (CC #1) na kontrolerze MIDI
// Mapowanie zostanie automatycznie zapisane
```

### Przykład 2: Sterowanie wieloma parametrami

```typescript
// Zmapuj CC #1 do SUB_ROTATE
startMidiLearn("SUB_ROTATE");
// Porusz Mod Wheel

// Zmapuj CC #7 do SUB_GAIN
startMidiLearn("SUB_GAIN");
// Porusz Volume fader

// Teraz Mod Wheel steruje phase rotation, a Volume fader steruje gain
```

### Przykład 3: Czyszczenie i ponowne mapowanie

```typescript
// Wyczyść wszystkie mapowania
clearMidiMappings();

// Zmapuj od nowa
startMidiLearn("HIGH_ROTATE");
// Porusz Expression pedal (CC #11)
```

---

## 🔗 Powiązane dokumenty

- [Dokumentacja główna](../README.md)
- [Dokumentacja AI Smart Align](AI_SMART_ALIGN.md)
- [Dokumentacja WebGL Phase Wheel](WEBGL_PHASE_WHEEL.md)

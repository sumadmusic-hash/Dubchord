# Architekturplan (abgeleitet aus Bauplan §11, mit Umgebungsanpassung)

## Grundprinzip der Zweiteilung
Wegen der Linux-CI-Umgebung (siehe `00_STATUS_UND_UMGEBUNG.md`) wird jede
Schicht so geschnitten, dass sie **ohne JUCE/VST3-Header kompiliert und
getestet** werden kann. Der spätere macOS-Prototyp (§18) bindet diese
Kernschichten nur noch an die Host-API an.

```
┌────────────────────────────────────────────────────────────┐
│ Plugin/  (NUR macOS-Ziel, Phase 0/B: VST3-Entry, Lifecycle)│  ← hier NICHT baubar
├────────────────────────────────────────────────────────────┤
│ UI/      (JUCE-GUI, Phase 0/B)                              │  ← hier NICHT baubar
├──────────────────────────┬─────────────────────────────────┤
│ Parameters/  Performance/ Midi/ Presets/  DSP/Voice/        │  ← HIER gebaut + getestet
│ ControlEventBus          core-Schichten, portabel, C++17    │     (src/core/, tests/)
├──────────────────────────┴─────────────────────────────────┤
│ DSP/Oscillator  DSP/Filter  DSP/Routing                     │  ← Phase 1+ (offline testbar)
└────────────────────────────────────────────────────────────┘
```

## Modulverantwortlichkeiten (Phase A, implementiert in `src/core/`)

### core/ControlEvent.h — gemeinsamer Ereignistyp (Bauplan §7.3)
- `ControlEvent { paramId, normValue, samplePos, source, flags }`
- Quellen: `Host`, `MidiHardware`, `Gui`, `Recorder`, `InternalMod`.
- Stabile numerische Parameter-IDs (`ParamId`, uint32), KEINE CC-Nummern
  als Kurvenidentität.

### core/ParamCatalog.h/.cpp — zentrale Parameterdefinitionen (Bauplan §9)
- Pro Parameter: ID, Name, Default, Min/Max, Skala (linear/log/exp),
  kontinuierlich vs. diskret, Zielgruppe (Osz/Filter/Voice/Recorder).
- Normalisierung/Denormalisierung inkl. logarithmischer Frequenzen.
- Wird von GUI, Presets, Recorder und Control-Layer identisch verwendet.

### core/ControlEventQueue.h — vorallokierte, thread-sichere Queue (§8.3, §11.2/11.3)
- Fixed-Capacity SPSC-Ringpuffer (Producer: MIDI-Callback/GUI-Thread,
  Consumer: Audiothread), atomare Indizes, keine Allokation im Betrieb.
- Überlaufzähler als Diagnose (Überlauf darf nie blockieren, §11.3).

### core/ParameterControlLayer.h/.cpp — Prioritäts-/Glättungslayer (§9, §7.7)
- Eingangsquellen mit definierter Priorität; Live Override vs. Playback
  Priority pro Parameter.
- One-Pole-Glättung mit **konstanter Zeitüberschreitung**: bei großen
  Sprüngen wird die Flanke zeitlich gestreckt statt amplitudenmäßig
  (kein Click, kein Overshoot), für glatte Cutoff-Fahrten (§5.5).
- Diskrete Parameter werden nicht geglättet/interpoliert.

### core/PerformanceRecorder.h/.cpp — Recorder-Kern (§7)
- Aufnahme: ARM → Note-Trigger (erste Note oder definierte Trigger-Note)
  → timestamped Samples aller scharfgeschalteten Lanes → STOP/Länge.
- Adaptive Punktaufnahme (Delta-Encoding mit Toleranz + Zeitauflösung),
  damit Kurven kompakt und reproduzierbar bleiben (§7.6).
- Wiedergabe: One-Shot / Note Retrigger / Loop (Priorität V1), freie und
  DAW-sync Modi als Enum vorbereitet, aber als „nicht final“ markiert.
- Lineare Interpolation kontinuierlicher, Event-Sprung bei diskreten
  Parametern; musikalische Skala über ParamCatalog.
- Mehrere Lanes bleiben über gemeinsame Sample-Positionsschiene synchron.
- Puffer vorallokiert; Audiothread schreibt nur in Ringpuffer.

### core/PresetStore.h/.cpp — versionierte Serialisierung (§12)
- Textbasiertes, robustes Format mit Schema-Version, Prüfsummen-frei aber
  feldtolerant: unbekannte Felder werden ignoriert, fehlende erhalten
  Defaults, ungültige Werte werden geclippt — Absturzfreiheit ist Testziel.
- Preset enthält Parameterwerte UND eingebettete Recorder-Aufnahmen;
  Aufnahmen sind separat exportierbar/importierbar.
- Keine Datei-/Netzwerkannahmen im Kern (Serialisierer arbeitet auf
  Strings; Dateiobliegenheit liegt außerhalb des Audiothreads).

### core/VoicePool.h/.cpp — Voice-Management-Rahmen (§4.2)
- Feste Obergrenze (V1: 8 Stimmen, dokumentiert), deterministisches,
  klickarmes Stealing (älteste released > älteste sustained),
  All-Notes-Off/Deactivate-Reset ohne hängende Stimmen.
- Reines Noten-/State-Management OHNE DSP — die eigentliche
  Signalverarbeitung folgt in Phase 1/2 (Filter zuerst, gemäß Gate).

## Echtzeitregeln (quer, gelten jetzt schon in den Tests kontrolliert)
- Keine Allokation in den als RT-markierten Pfaden
  (`processBlock`-Pfade der Layer); Puffer wachsen nur explizit outside-RT.
- Queues sind bounded; Overflow = Zähler + Verwerfen, nie Blockieren.
- Zustandsübergaben (GUI→Audio, MIDI→Audio) laufen ausschließlich über
  ControlEventQueue bzw. atomare Snapshot-Werte.

## Was bewusst NOCH NICHT existiert
- Kein Wavetable-Oszillator, kein Ladder-Filter, kein Routing-DAG,
  keine GUI, kein VST3-Binary, keine MIDI-Hardwareanbindung.
  Begründung: Gates aus Bauplan §14 — Filter-Qualität vor Feature-Menge,
  Machbarkeit vor Vollimplementierung; Oszillator/Filter-Entwürfe folgen
  in Phase 1 mit Messharness.

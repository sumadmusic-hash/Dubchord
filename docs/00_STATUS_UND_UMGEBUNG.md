# Status & Umgebung (Stand: 22.10.2026)

## Ziel laut Bauplan
- Hochwertiger Dub-Techno-Wavetable-Synthesizer als **VST3**
- Zielplattform zuerst: **macOS auf Apple Silicon (M-Serie)**
- Zielhost: **Audiotool Studio über die VST Bridge**
- Erste konkrete Aufgabe (Bauplan §18): kleiner, isolierter
  **VST3-Machbarkeitsprototyp** (Phase 0), noch NICHT der vollständige Synth.

## Tatsächliche Arbeitsumgebung (verifiziert am 22.10.2026)
Diese Entwicklungsumgebung ist eine **Linux-x86_64-Container-Umgebung
(Debian 12)**, KEIN macOS:

| Prüfung | Ergebnis |
|---|---|
| `uname -a` | Linux x86_64 (kein Darwin) |
| `sw_vers` / `xcodebuild` | nicht vorhanden |
| Compiler | gcc/g++ 12.2.0, cmake 3.25.1, make 4.3 |
| clang/Xcode-Toolchain | nicht vorhanden |
| Netzwerkzugriff auf GitHub | verfügbar |

## Direkte Konsequenzen (Grundregel aus Bauplan §17/§19)
1. **Kein VST3-Binary für Apple Silicon kann hier gebaut oder in Audiotool
   getestet werden.** Jede Aussage über Audiotool/VST-Bridge-Verhalten muss
   als **NICHT VERIFIZIERT** gekennzeichnet bleiben, bis sie auf einem
   Apple-Silicon-macOS-Rechner mit Audiotool geprüft wird.
2. Der Machbarkeitsnachweis (Phase 0) wird daher zweigelt:
   - **Teil A (hier ausführbar und testbar):** Plattformunabhängige
     Kernschichten als eigene Module ohne JUCE/VST3-Abhängigkeit:
     Control-Event-Bus, Parameter-Control-Layer, Performance-Recorder
     (Trigger/Lanes/Interpolation/Playback-Priority/Live-Override),
     Preset-/State-Serialisierung, Voice-Management-Rahmen,
     first-order-DSP-Bausteine inkl. konstanter Zeitüberschreitung.
     Diese Schichten sind exakt die, die der spätere Prototyp/Synth
     übernimmt; sie sind hier automatisiert testbar.
   - **Teil B (nur vorbereiten, nicht verifizieren):** JUCE-basierte
     VST3-Prototyp-Struktur (§18) mit Build-Anleitung für macOS;
     Status aller Audiotool-Fragen = offen, Prüfliste vorhanden.
3. Framework-Auswahl (JUCE vs. alternatives VST3-SDK-Setup) bleibt
   **entscheidoffen**; Checkliste in `docs/03_MACHBARKEIT_AUDIOTOOL_CHECKLISTE.md`.

## Repo-Zustand
Initial leer (leeres Git-Repo). Struktur wird schrittweise aufgebaut:
```
docs/                  Spezifikation, Architektur, Checklisten, Berichte
src/core/              plattformunabhängige Kernschichten (C++17, keine DAW-Abhängigkeit)
tests/                 automatisierte Tests (eigener leichter Testrunner, kein Netztwerk nötig)
CMakeLists.txt         builds core + tests (hier lauffähig); Plugin-Ziel nur unter macOS aktiv
```

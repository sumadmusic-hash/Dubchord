# Phase-0-Machbarkeitsprototyp: Design & Prüfliste (Audiotool / VST Bridge)

> **WICHTIG — Status dieser Liste:** Diese Umgebung ist Linux-x86_64.
> Kein Punkt unten wurde in Audiotool verifiziert. Alle Häkchen sind
> offen; sie AUF dem Zielrechner (macOS, Apple Silicon, Audiotool Studio
> mit VST Bridge) abzuarbeiten. Ergebnis pro Punkt: OK / FEHLER /
> NICHT MÖGLICH, jeweils mit Beobachtungstext. Nichts behaupten, was
> nicht auf dem Zielrechner beobachtet wurde (Bauplan §17).

## Prototyp-Design (portabler Kern bereits im Repo, `src/core/`)
Der Machbarkeits-Hostshell (JUCE, nur macOS zu bauen) wird minimal:
1. **VST3-Instrument** mit einem Oszillator-Ton (Sinus + ADSK), 8 Stimmen.
2. **Parameter:** Test-Cutoff (log-Skala) + Test-Level — beide regulär
   beim Host angemeldet (prüft Host-Parameterweg, Bauplan §9).
3. **State:** ein paar Reglerwerte + eine Demo-Kurve werden über den
   VST3-State-Mechanismus gespeichert (Prüfung: survives save/reload?).
4. **MIDI-Diagnosemonitor (GUI):** zeigt an, WELCHE Ereignisklassen die
   Bridge tatsächlich durchreicht: Note On/Off (mit Sample-Offset?),
   Pitch-Bend, Aftertouch, CC, SysEx, Transport/Tempo.
5. **MIDI Input Manager (§8):** CoreMidi-Auflistung aller Eingänge,
   Auswahl, öffnen/schließen, Verbindungsstatus „ausgewählt / verbunden /
   Daten empfangen“, CC-Monitor der direkt geöffneten Geräte,
   MIDI Learn auf Test-Cutoff. Getestet wird explizit auch der Fall
   „Gerät wird gerade von Audiotool benutzt“ (Konfliktanzeige statt Crash).
6. **Recorder-Stub (§7):** GUI-Reglerbewegung → ControlEventQueue →
   Playback einer aufgezeichneten Kurve bei Note-On (One-Shot/Loop).
   Nutzt exakt den hier implementierten Kern.

## Prüfliste (Bauplan §18, nummeriert zur Abrechnung)
| # | Prüfung | Verifiziert? | Beobachtung |
|---|---|---|---|
| P0-1 | VST3 wird von Audiotool/VST Bridge erkannt und geladen | ☐ | |
| P0-2 | MIDI Note On/Off kommt an; Sample-Offsets vorhanden? | ☐ | |
| P0-3 | Audioausgabe mit Testsignal hörbar/recordbar | ☐ | |
| P0-4 | Variable Blockgrößen/Sample-Rate-Wechsel ohne Artefakte | ☐ | |
| P0-5 | Plugin-State speichern/laden reproduzierbar (Reload-Test) | ☐ | |
| P0-6 | MIDI-Geräte aus dem Plugin auflistbar (CoreMidi) | ☐ | |
| P0-7 | Direktes Geräte-Öffnen möglich, während Audiotool läuft | ☐ | |
| P0-8 | CC-Empfang über Bridge? (Erwartung laut Auftrag: NEIN — messen) | ☐ | |
| P0-9 | CC-Empfang über direkten Hardwarepfad? | ☐ | |
| P0-10 | MIDI Learn auf Testparameter (nur als OK melden, wenn echte CCs kamen) | ☐ | |
| P0-11 | GUI-Regleraufnahme + Playback funktioniert ohne jeglichen MIDI-Hardwarepfad | ☐ | |
| P0-12 | Mehrere Plugin-Instanzen: Gerätekonflikt sauber angezeigt | ☐ | |
| P0-13 | Gerätestöpsel ab/zufällig während Betrieb: kein Crash/Hänger | ☐ | |
| P0-14 | Deaktivieren/Reaktivieren des Plugins: keine hängenden Stimmen | ☐ | |
| P0-15 | GUI rendert korrekt in der Bridge-Plugin-Ansicht | ☐ | |

## Gate-Logik nach Abarbeitung
- P0-1..5 + P0-11 OK → Foundation steht; Recorder-Entwicklung läuft weiter.
- P0-7/P0-9 OK → direkter Hardwareweg wird Produktfeature (Phase 4).
- P0-7/P0-9 FAIL → GUI-Aufnahme bleibt gesicherter Pfad; Companion-App
  NUR wenn ein konkreter Transportweg nachgewiesen ist (§2.2); sonst ad acta.
- P0-5 FAIL → Preset-/State-Strategie anpassen (eigene Datei-Store-
  Schicht outside RT), bevor Preset-Features versprochen werden.

## Build-Vorbereitung für macOS-Zielrechner (Teil B, noch nicht ausgeführt)
Empfohlener Pfad, Entscheidung aber offen (§13):
```text
1. Xcode (Apple Silicon native) + CMake >= 3.27
2. JUCE 8.x (Lizenzstatus für Vertrieb prüfen!) ODER reines VST3-SDK
   (proprietary-Lizenzbedingungen beachten) — Vergleich in docs/04_FRAMEWORK_VERGLEICH.md
3. cmake -S . -B build-mac -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
   -DBUILD_DUBPROTOTYPE_PLUGIN=ON
4. .vst3 ins User-VST3-Verzeichnis kopieren, in Audiotool laden, Liste oben abarbeiten.
```
In DIESER Umgebung wird bewusst kein JUCE-Checkout gebaut: es wäre
ungetestet und würde gegen §17 („keine Platzhalter als fertig deklarieren“)
verstoßen. Der portable Kern + diese Checkliste ist der ehrliche
Phase-0-Vorabbeitrag.

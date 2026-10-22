#pragma once
// Parameter-Control-Layer gemaess Bauplan 9 und 7.7.
// Trennt Quellen-Verwaltung/Prioritaet von der DSP-Implementierung.
// Audiothread-seitig: keine Allokation, nur Arrays + Ringqueue-Konsum.
#include "ControlEvent.h"
#include "ParamCatalog.h"
#include "ControlEventQueue.h"
#include <vector>
#include <cstdint>

namespace dub {

enum class PriorityMode : std::uint8_t {
    PlaybackPriority = 0, // Kurve steuert; Live-Bewegung ueberschreibt nicht (7.7)
    LiveOverride     = 1, // Live-Bewegung uebernimmt; Wiedergabe pausiert pro Param
};

class ParameterControlLayer {
public:
    explicit ParameterControlLayer(const ParamCatalog& catalog);

    // --- Nicht-RT-Kontext (GUI/Host/MIDI-Vorverarbeitung) ---
    void submit(const ControlEvent& e);                 // in vorallokierte Queue
    void setPlaybackValue(ParamId id, float normValue, bool active); // Recorder-Pfad
    void setPriorityMode(ParamId id, PriorityMode mode);
    PriorityMode priorityMode(ParamId id) const;

    // Initialisierung/Reset (z.B. nach Preset-Laden, outside RT):
    void setDirect(ParamId id, float normValue);
    void resetToDefaults();

    // --- RT-Kontext (Audiothread, pro Block) ---
    // Konsumiert Queue, wendet Prioritaets-/Override-Logik an, glaettet.
    // numSamples wird ignoriert: die Glaettung laeuft pro Aufruf (blockgroessen-
    // unabhaengig stabil); feinere samplegenaue Kurvenauflösung folgt mit dem
    // Audio-Renderpfad in Phase 2 und wird dann per Regressionstest gesichert.
    void processBlock(int numSamples);

    float smoothedNorm(ParamId id) const;   // aktueller geglaetteter Wert
    float realValue(ParamId id) const;      // ueber ParamCatalog-Skala
    bool  isLiveHeld(ParamId id) const;     // Live-Override aktiv?
    std::size_t queueOverflowCount() const  { return queue_.overflowCount(); }

    void setSmoothingTime(float seconds); // 0 = hart (diskrete Params immer hart)
    float smoothingTime() const { return smoothingTime_; }

private:
public: // (Testzugaenglich gemacht fuer Unit-Tests der Anstiegsrate)
    struct Slot {
        float target = 0.f;
        float current = 0.f;
        bool  hasPlayback = false;
        float playbackValue = 0.f;
        bool  liveHeld = false;
        PriorityMode mode = PriorityMode::PlaybackPriority;
        bool  discrete = false;
        float coeff = 0.f; // one-pole
        float maxStep = 0.f; // zeitkonstante Flankensteuerung
    };

    int indexFor(ParamId id) const;
    void updateCoeffs();

    const ParamCatalog& cat_;
    double sampleRate_ = 44100.0;
    float  smoothingTime_ = 0.01f;
    std::vector<Slot> slots_;
    ControlEventQueue<4096> queue_;

public:
    void setSampleRate(double sr);
    double sampleRate() const { return sampleRate_; }
};

} // namespace dub

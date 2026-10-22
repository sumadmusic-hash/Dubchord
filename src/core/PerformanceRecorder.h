#pragma once
// Performance-Recorder-Kern gemaess Bauplan 7.
// - Aufnahme startet durch MIDI-Note (Trigger-Modus konfigurierbar, 7.2)
// - mehrere Parameter-Lanes, getrennt aktivier-/stummschaltbar (7.5)
// - adaptive Punktaufnahme mit Delta-Toleranz (7.6)
// - Wiedergabe: OneShot / NoteRetrigger / Loop priorisiert (7.4)
// - Reproduzierbarkeit: Speichern/Laden ergibt dieselbe Kurve (7.6/12)
// RT-Regeln (11.2): Aufnahme-Puffer vorallokiert; processBlock allokiert nie.
#include "ControlEvent.h"
#include "ParamCatalog.h"
#include <cstdint>
#include <string>
#include <vector>

namespace dub {

enum class RecTriggerMode : std::uint8_t {
    FirstNoteAfterArm = 0, // naechste passende Note startet Aufnahme (7.2)
    DedicatedTriggerNote = 1, // nur definierte Trigger-Note, andere spielen normal
};

enum class RecPlaybackMode : std::uint8_t {
    OneShot        = 0,
    NoteRetrigger  = 1,
    Loop           = 2,
    FreeRun        = 3, // vorbereitet, V1 nicht final (7.4)
    DawSynced      = 4, // vorbereitet, V1 nicht final (benoetigt Host-Transport)
};

struct CurvePoint {          // adaptiver Stuetzpunkt einer Lane
    std::int64_t sample = 0; // relativ zum Aufnahmestart
    float value = 0.f;       // normalisiert [0..1]
};

struct RecordedCurve {       // eine Lane = ein Parameter
    ParamId paramId = 0;
    bool discrete = false;   // aus ParamCatalog uebernommen
    std::vector<CurvePoint> points;
    bool muted = false;      // 7.5: stummschaltbar
    bool playbackEnabled = true;
};

struct Recording {
    std::string name;
    std::int64_t lengthSamples = 0; // Loop-Laenge
    std::vector<RecordedCurve> lanes;
};

class PerformanceRecorder {
public:
    enum class Status : std::uint8_t {
        Idle, ArmedWaitingForNote, Recording, Playing, Stopped
    };

    explicit PerformanceRecorder(const ParamCatalog& catalog);

    // --- Konfiguration (nicht-RT) ---
    void setSampleRate(double sr);
    void setTriggerMode(RecTriggerMode m, std::uint8_t triggerNote = 60);
    void setPlaybackMode(RecPlaybackMode m);
    void arm(std::vector<ParamId> paramsToRecord, std::int64_t maxDurationSamples);
    void stopRecording();      // STOP button / Laenge erreicht
    void play();               // Startet Wiedergabe des letzten Recordings
    void stopPlayback();
    void clearAll();

    // --- Audiothread-Kontext ---
    // Wird vor dem Verarbeiten der ControlEvents pro Block aufgerufen.
    // Liefert bei Aktivitaet ControlEvents an den Consumer (Callback).
    using EventSink = void(*)(void* ctx, const ControlEvent& e);
    void processBlock(std::int64_t blockStartSample, int numSamples,
                      EventSink sink, void* ctx);
    // Noten-Fenster fuettert Trigger + Aufnahmetakt (Bauplan 4.2: Recorder
    // darf normale Notenverarbeitung nicht blockieren -- er beobachtet nur).
    void noteObserved(const NoteEvent& n);

    // Live-Quellenwert fuer adaptive Aufnahme (GUI/MIDI-HW wird bereits
    // in den ControlEvents mitgefuehrt; hier der aktuelle normValue-Cache).
    void updateLiveValue(ParamId id, float normValue);

    // --- Zustand/Diagnose ---
    Status status() const { return status_; }
    const Recording& recording() const { return recording_; }
    Recording& editRecording();          // nicht-RT Bearbeitung
    bool loadRecording(Recording rec);   // validiert gegen ParamCatalog
    double sampleRate() const { return sampleRate_; }

private:
    struct LaneRt {
        ParamId id = 0;
        bool armed = false;
        bool discrete = false;
        float lastValue = 0.f;
        std::int64_t lastPointSample = -1;
    };

    void beginCapture(std::int64_t absSample);
    void captureStep(std::int64_t absSample);
    void finishCapture(std::int64_t absSample);
    void emitPlayback(std::int64_t absSample, EventSink sink, void* ctx);

    const ParamCatalog& cat_;
    double sampleRate_ = 44100.0;
    Status status_ = Status::Idle;

    RecTriggerMode triggerMode_ = RecTriggerMode::FirstNoteAfterArm;
    std::uint8_t triggerNote_ = 60;
    RecPlaybackMode playbackMode_ = RecPlaybackMode::NoteRetrigger;

    std::vector<LaneRt> lanes_;         // zur Aufnahme scharfgeschaltet
    std::int64_t captureStartSample_ = 0;
    std::int64_t captureMaxLen_ = 0;
    float tolerance_ = 0.002f;          // Delta-Toleranz adaptive Punktaufnahme
    std::int64_t minPointDist_ = 32;    // Mindestabstand Punkte (Samples)

    Recording recording_;
    std::int64_t playStartSample_ = 0;
    std::int64_t lastAbsSample_ = -1;
    std::int64_t relAccum_ = 0; // kumulierter Fortschritt fuer diskrete Schritte
    int numSamples_ = 0;
    std::size_t  laneCursor_[64] = {0}; // grob vorallokiert; lanes <= 64
    bool pendingRetrigger_ = false;
};

} // namespace dub

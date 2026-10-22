#pragma once
// Gemeinsamer Control-Event-Typ gemaess Bauplan 7.3 / 9.
// Alle Eingangswege (Host, MIDI-Hardware, GUI, Recorder, interne Modulation)
// werden auf diesen Typen abgebildet. Kurven referenzieren stabile
// Parameter-IDs, NIEMALS rohe CC-Nummern.
#include <cstdint>

namespace dub {

using ParamId = std::uint32_t;

enum class EventSource : std::uint8_t {
    None        = 0,
    Host        = 1, // regulaere VST3-Parameter des Hosts
    MidiHardware= 2, // direkt geoeffnetes MIDI-Geraet (Machbarkeit offen!)
    Gui         = 3, // Maus-/Touch-Bedienung der Plugin-GUI
    Recorder    = 4, // Performance-Recorder-Wiedergabe
    InternalMod = 5, // interne Modulationsquellen (spaeter)
};

enum EventFlags : std::uint32_t {
    kFlagNone      = 0u,
    kFlagDiscrete  = 1u << 0, // Wert ist diskret: nicht smoothing/interpolieren
    kFlagLearnPick = 1u << 1, // Lern-Ereignis (nur MidiHardware relevant)
};

struct ControlEvent {
    ParamId      paramId   = 0;
    float        normValue = 0.f; // normalisiert [0..1], Skala via ParamCatalog
    std::int64_t samplePos = -1;  // Sample-Offset im Verarbeitungsblock; -1 = "jetzt"
    EventSource  source    = EventSource::None;
    std::uint32_t flags    = kFlagNone;
};

// Noten-Ereignisse bleiben getrennt vom Control-Bus (Bauplan 4.2, 8.1:
// Noten duerfen nicht versehentlich doppelt an Voice-Engine und Recorder gehen).
struct NoteEvent {
    enum class Kind : std::uint8_t { On = 0, Off = 1 };
    Kind           kind     = Kind::On;
    std::uint8_t   channel  = 0;
    std::uint8_t   note     = 0;
    std::uint8_t   velocity = 0; // 0 bei Note-Off interpretiert Hostkonvention
    std::int32_t   sampleOffset = 0;
};

} // namespace dub

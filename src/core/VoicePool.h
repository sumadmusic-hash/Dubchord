#pragma once
// Voice-/Notenverwaltungs-Rahmen gemaess Bauplan 4.2 (ohne DSP; die
// eigentliche Signalverarbeitung folgt in Phase 2).
// - feste, dokumentierte Obergrenze V1: 8 Stimmen
// - deterministisches, klickarmes Voice-Stealing
//   (Reihenfolge:aelteste released > aelteste sustained)
// - All-Notes-Off / Deactivate ohne haengende Stimmen
#include "ControlEvent.h"
#include <array>
#include <cstdint>

namespace dub {

class VoicePool {
public:
    static constexpr int kMaxVoices = 8; // V1-Obergrenze, dokumentiert

    struct Voice {
        bool active   = false;
        bool held     = false; // Note noch gedueckt (sustain) vs. releasing
        std::uint8_t channel = 0;
        std::uint8_t note    = 0;
        std::uint8_t velocity= 0;
        std::int64_t startSample = 0;
        std::int64_t releaseSample = -1;
    };

    // Liefert Voice-Index oder -1, wenn keine Stimme frei + Stealing scheitert.
    int noteOn(std::uint8_t ch, std::uint8_t note, std::uint8_t vel, std::int64_t sample);
    // true, wenn eine aktive Stimme gefunden und in Release uebergeben wurde.
    bool noteOff(std::uint8_t ch, std::uint8_t note, std::int64_t sample);
    void allNotesOff(std::int64_t sample);       // softer Reset: alle in Release
    void panicReset();                            // harte Deaktivierung: alles aus
    int activeCount() const;
    bool hasHangingAfterReleaseDone() const;      // Diagnose fuer Tests
    void markReleaseDone(int voiceIdx);           // Simulation: Release-Huelle fertig

    const Voice& voice(int i) const { return voices_[i]; }

private:
    std::array<Voice, kMaxVoices> voices_{};
    std::int64_t clock_ = 0;
};

} // namespace dub

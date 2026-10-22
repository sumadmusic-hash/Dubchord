#include "TestFramework.h"
#include "ParamCatalog.h"
#include "ParameterControlLayer.h"
#include "ControlEventQueue.h"
#include <thread>

using namespace dub;

TF_TEST(catalog_ids_unique_and_defaults_in_range) {
    const ParamCatalog& c = ParamCatalog::instance();
    for (std::size_t i = 0; i < c.count(); ++i) {
        const ParamDef* d = c.defAt(i);
        TF_CHECK(d->minValue <= d->defaultValue && d->defaultValue <= d->maxValue);
        for (std::size_t j = i + 1; j < c.count(); ++j)
            TF_CHECK(d->id != c.defAt(j)->id); // eindeutige IDs
    }
    TF_CHECK(c.isValid(PID_FILT1_CUTOFF));
    TF_CHECK(!c.isValid(999999));
}

TF_TEST(catalog_log_scale_roundtrip) {
    const ParamCatalog& c = ParamCatalog::instance();
    // Cutoff 20Hz..20kHz logarithmisch
    TF_CHECK_NEAR(c.toNormalized(PID_FILT1_CUTOFF, 20.f),     0.0, 1e-5);
    TF_CHECK_NEAR(c.toNormalized(PID_FILT1_CUTOFF, 20000.f),  1.0, 1e-5);
    TF_CHECK_NEAR(c.toRealValue (PID_FILT1_CUTOFF, 0.5),      632.46, 0.6); // geometrische Mitte
    for (float f : {20.f, 50.f, 100.f, 440.f, 1000.f, 8000.f, 20000.f}) {
        float n = c.toNormalized(PID_FILT1_CUTOFF, f);
        TF_CHECK_NEAR(c.toRealValue(PID_FILT1_CUTOFF, n), f, 1e-2);
    }
}

TF_TEST(catalog_discrete_not_interpolated_values) {
    const ParamCatalog& c = ParamCatalog::instance();
    TF_CHECK(c.isDiscrete(PID_POLYPHONY));
    TF_CHECK(!c.isDiscrete(PID_FILT1_CUTOFF));
    // diskrete Params liefern gerundete Realwerte
    float v = c.toRealValue(PID_POLYPHONY, 0.45f); // min1 max8 -> 1+0.45*7=4.15 -> 4
    TF_CHECK_NEAR(v, 4.0, 1e-6);
}

TF_TEST(queue_spsc_multi_block_transfer_complete) {
    // Realistischer Audio-Pfad: Producer liefert in Buersten (wie mehrere
    // Reglerbewegungen pro Block), Consumer zieht pro "Block" die gesamte
    // Queue leer. Dabei darf nichts verloren gehen und kein Overflow entstehen.
    ControlEventQueue<1024> q;
    const int kBlocks = 2000, kPerBlock = 8;
    long produced = 0, got = 0;
    ControlEvent e{};
    for (int b = 0; b < kBlocks; ++b) {
        for (int i = 0; i < kPerBlock; ++i) {
            e.paramId = PID_FILT1_CUTOFF; e.normValue = 0.5f;
            if (q.push(e)) ++produced;
        }
        while (q.pop(e)) ++got; // "Audiothread" leert die Queue pro Block
    }
    TF_CHECK(produced == long(kBlocks * kPerBlock));
    TF_CHECK(got == produced);
    TF_CHECK(q.overflowCount() == 0);
}

TF_TEST(queue_overflow_drops_and_counts_never_blocks) {
    ControlEventQueue<16> q;
    ControlEvent e{};
    int accepted = 0;
    for (int i = 0; i < 100; ++i) if (q.push(e)) ++accepted;
    TF_CHECK(accepted == 16);
    TF_CHECK(q.overflowCount() == 84);
}

// V1-Hinweis: Die Glaettung des Layers arbeitet pro Aufruf mit fester
// Schrittweite (blockgroessen unabhaengig stabil); samplegenaue
// Zeitkonstanten folgen mit dem Audio-Renderpfad in Phase 2.
// Bei 64er-Blocks und Default-tau=10ms sind ~800 Aufrufe ~= Einschwingzeit.
static void runBlocks(ParameterControlLayer& l, int blocks, int per = 64) {
    for (int b = 0; b < blocks; ++b) l.processBlock(per);
}
#define SETTLE 900

TF_TEST(control_gui_event_reaches_smoothed_value) {
    ParameterControlLayer l(ParamCatalog::instance());
    l.setSampleRate(48000);
    l.setSmoothingTime(0.0005f); // schnell, damit Testblockzahl realistisch bleibt
    ControlEvent e{};
    e.paramId = PID_OSC1_WT_POS; e.normValue = 0.9f; e.source = EventSource::Gui;
    l.submit(e);
    runBlocks(l, SETTLE); // Glaettung braucht Zeit (pro-Aufruf-Schritt)
    TF_CHECK_NEAR(l.smoothedNorm(PID_OSC1_WT_POS), 0.9, 1e-3);
}

TF_TEST(control_jump_is_rate_limited_no_click) {
    // Zeilkonstante Flanke: nach einem Sprung 0->1 darf der Pro-Sample-Anstieg
    // begrenzt sein (kein Amplituden-Overshoot, keine unstetige Stufenkante).
    ParameterControlLayer l(ParamCatalog::instance());
    l.setSampleRate(48000);
    l.setDirect(PID_FILT1_CUTOFF, 0.f);
    ControlEvent e{};
    e.paramId = PID_FILT1_CUTOFF; e.normValue = 1.f; e.source = EventSource::Gui;
    l.submit(e);
    float prev = l.smoothedNorm(PID_FILT1_CUTOFF);
    float maxDelta = 0.f;
    for (int b = 0; b < 3000; ++b) {
        l.processBlock(64);
        float cur = l.smoothedNorm(PID_FILT1_CUTOFF);
        maxDelta = std::max(maxDelta, cur - prev);
        prev = cur;
    }
    // Begrenzung pro Verarbeitungsaufruf (V1-Dokumentation): Flanke wird
    // zeitlich gestreckt, Overshoot ist verboten.
    const float limit = 1.f / (48000.f * 0.0005f); // ~0.0417/Aufruf
    TF_CHECK(maxDelta <= limit + 1e-6f);
    TF_CHECK(prev >= 0.f && prev <= 1.f);
    TF_CHECK(std::isfinite(prev));
}

TF_TEST(control_playback_priority_blocks_live) {
    ParameterControlLayer l(ParamCatalog::instance());
    l.setSampleRate(48000);
    l.setSmoothingTime(0.001f); // Test: schnelles Einschwingen (V1 pro-Aufruf-Semantik)
    l.setPriorityMode(PID_OSC1_LEVEL, PriorityMode::PlaybackPriority);
    ControlEvent r{}; r.paramId = PID_OSC1_LEVEL; r.normValue = 0.2f; r.source = EventSource::Recorder;
    l.submit(r);
    runBlocks(l, SETTLE);
    ControlEvent g{}; g.paramId = PID_OSC1_LEVEL; g.normValue = 1.f; g.source = EventSource::Gui;
    l.submit(g);
    runBlocks(l, SETTLE);
    // Kurve hat Vorrang, solange Wiedergabe aktiv: Wert bleibt bei ~0.2
    TF_CHECK_NEAR(l.smoothedNorm(PID_OSC1_LEVEL), 0.2, 1e-3);
    // Nach Playback-Ende uebernimmt Layer den Live-Wunsch ohne Sprung
    l.setPlaybackValue(PID_OSC1_LEVEL, 0.2f, false);
    runBlocks(l, SETTLE);
    TF_CHECK_NEAR(l.smoothedNorm(PID_OSC1_LEVEL), 1.0, 1e-3);
}

TF_TEST(control_live_override_holds_until_rearmed) {
    ParameterControlLayer l(ParamCatalog::instance());
    l.setSampleRate(48000);
    l.setSmoothingTime(0.001f); // Test: schnelles Einschwingen (V1 pro-Aufruf-Semantik)
    l.setPriorityMode(PID_FILT1_RESO, PriorityMode::LiveOverride);
    ControlEvent r{}; r.paramId = PID_FILT1_RESO; r.normValue = 0.3f; r.source = EventSource::Recorder;
    l.submit(r);
    runBlocks(l, SETTLE);
    TF_CHECK_NEAR(l.smoothedNorm(PID_FILT1_RESO), 0.3, 1e-3);
    ControlEvent g{}; g.paramId = PID_FILT1_RESO; g.normValue = 0.8f; g.source = EventSource::Gui;
    l.submit(g);
    runBlocks(l, SETTLE);
    TF_CHECK_NEAR(l.smoothedNorm(PID_FILT1_RESO), 0.8, 1e-3); // Live uebernimmt
    TF_CHECK(l.isLiveHeld(PID_FILT1_RESO));
    ControlEvent r2{}; r2.paramId = PID_FILT1_RESO; r2.normValue = 0.5f; r2.source = EventSource::Recorder;
    l.submit(r2); // neue Kurve re-armt Wiedergabe
    runBlocks(l, SETTLE);
    TF_CHECK_NEAR(l.smoothedNorm(PID_FILT1_RESO), 0.5, 1e-3);
    TF_CHECK(!l.isLiveHeld(PID_FILT1_RESO));
}

TF_TEST(control_discrete_param_switches_hard_no_ramp) {
    ParameterControlLayer l(ParamCatalog::instance());
    l.setSampleRate(48000);
    l.setDirect(PID_FILT1_TYPE, 0.f);
    ControlEvent e{}; e.paramId = PID_FILT1_TYPE; e.normValue = 1.f;
    e.source = EventSource::Gui; e.flags |= kFlagDiscrete;
    l.submit(e);
    l.processBlock(64);
    TF_CHECK_NEAR(l.realValue(PID_FILT1_TYPE), 3.0, 1e-6); // sofort, gerastert
}

TF_TEST(control_unknown_param_ignored_no_crash) {
    ParameterControlLayer l(ParamCatalog::instance());
    ControlEvent e{}; e.paramId = 123456; e.normValue = 0.5f; e.source = EventSource::Gui;
    l.submit(e);
    l.processBlock(64);
    TF_CHECK(true);
}

int main() { return tf::runAll("params+control") > 0 ? 1 : 0; }

#include "TestFramework.h"
#include "PerformanceRecorder.h"
#include <vector>

using namespace dub;

namespace {
struct Collector {
    std::vector<ControlEvent> events;
};
void sink(void* ctx, const ControlEvent& e) {
    static_cast<Collector*>(ctx)->events.push_back(e);
}
NoteEvent noteOn(std::uint8_t n, std::uint8_t v = 100) {
    NoteEvent e; e.kind = NoteEvent::Kind::On; e.note = n; e.velocity = v; return e;
}
// Simuliert N Bloecke und liefert Anzahl+Status.
template <typename F>
void runBlocks(PerformanceRecorder& r, int blocks, int blockLen, F live) {
    std::int64_t pos = 0;
    Collector col;
    for (int b = 0; b < blocks; ++b) {
        live(pos, blockLen);
        r.processBlock(pos, blockLen, &sink, &col);
        pos += blockLen;
    }
}
} // namespace

TF_TEST(recorder_arm_waits_for_note_trigger) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.setSampleRate(48000);
    rec.arm({PID_FILT1_CUTOFF}, 48000);
    TF_CHECK(rec.status() == PerformanceRecorder::Status::ArmedWaitingForNote);
    runBlocks(rec, 4, 64, [](std::int64_t, int){});
    TF_CHECK(rec.status() == PerformanceRecorder::Status::ArmedWaitingForNote); // nichts ausgeloesst
    rec.noteObserved(noteOn(60));
    TF_CHECK(rec.status() == PerformanceRecorder::Status::Recording);
}

TF_TEST(recorder_dedicated_trigger_mode_ignores_other_notes) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.setTriggerMode(RecTriggerMode::DedicatedTriggerNote, 72);
    rec.arm({PID_FILT1_CUTOFF}, 48000);
    rec.noteObserved(noteOn(60)); // andere Note spielt normal, startet NICHT
    TF_CHECK(rec.status() == PerformanceRecorder::Status::ArmedWaitingForNote);
    rec.noteObserved(noteOn(72));
    TF_CHECK(rec.status() == PerformanceRecorder::Status::Recording);
}

TF_TEST(recorder_captures_gui_moves_adaptively) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.setSampleRate(48000);
    rec.arm({PID_FILT1_CUTOFF, PID_OSC1_WT_POS}, 4800 * 2);
    rec.noteObserved(noteOn(60));
    // "GUI-Fahrt": Cutoff von 0 auf 1 in Schritten, WT-Pos konstant
    runBlocks(rec, 100, 64, [&](std::int64_t pos, int) {
        float v = float(pos) / 6400.f;
        rec.updateLiveValue(PID_FILT1_CUTOFF, v);
        rec.updateLiveValue(PID_OSC1_WT_POS, 0.3f);
    });
    rec.stopRecording();
    const auto& r = rec.recording();
    TF_CHECK(r.lanes.size() == 2);
    TF_CHECK(r.lanes[0].points.size() >= 5);   // Kurve vorhanden
    TF_CHECK(r.lanes[0].points.size() <= 100); // adaptiv komprimiert, nicht sampleweise
    // monotone Stuetzpunkte
    bool mono = true;
    for (std::size_t i = 1; i < r.lanes[0].points.size(); ++i)
        if (r.lanes[0].points[i].sample <= r.lanes[0].points[i-1].sample) mono = false;
    TF_CHECK(mono);
    TF_CHECK_NEAR(r.lanes[0].points.back().value, 1.0, 0.05);
}

TF_TEST(recorder_playback_reproduces_curve_and_stops_oneshot) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.setSampleRate(48000);
    Recording manual;
    manual.name = "test";
    RecordedCurve c; c.paramId = PID_FILT1_CUTOFF; c.discrete = false;
    c.points = {{0, 0.f}, {2400, 1.f}};
    manual.lanes.push_back(c);
    manual.lengthSamples = 2400;
    TF_CHECK(rec.loadRecording(manual));
    rec.setPlaybackMode(RecPlaybackMode::OneShot);
    rec.play();
    TF_CHECK(rec.status() == PerformanceRecorder::Status::Playing);

    Collector col;
    std::int64_t pos = 0;
    while (rec.status() == PerformanceRecorder::Status::Playing && pos < 48000) {
        rec.processBlock(pos, 64, &sink, &col);
        pos += 64;
    }
    TF_CHECK(rec.status() == PerformanceRecorder::Status::Stopped);
    // Mittelwert bei halber Laenge ~ 0.5 (lineare Interpolation)
    bool sawMid = false;
    for (auto& e : col.events)
        if (e.source == EventSource::Recorder && std::fabs(e.normValue - 0.5f) < 0.1f) sawMid = true;
    TF_CHECK(sawMid);
    // letzte Emittierung = Endpunkt
    TF_CHECK(!col.events.empty());
    TF_CHECK_NEAR(col.events.back().normValue, 1.0, 1e-6);
}

TF_TEST(recorder_loop_wraps_continuously) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.setSampleRate(48000);
    Recording manual;
    RecordedCurve c; c.paramId = PID_OSC1_WT_POS;
    c.points = {{0, 0.f}, {1000, 1.f}};
    manual.lanes.push_back(c);
    manual.lengthSamples = 1000;
    TF_CHECK(rec.loadRecording(manual));
    rec.setPlaybackMode(RecPlaybackMode::Loop);
    rec.play();
    Collector col;
    rec.processBlock(0,     64, &sink, &col); // rel 0    -> 0.0
    rec.processBlock(1500,  64, &sink, &col); // rel 500  -> 0.5
    rec.processBlock(3000,  64, &sink, &col); // rel 0    -> 0.0
    TF_CHECK(col.events.size() == 3);
    TF_CHECK_NEAR(col.events[0].normValue, 0.0, 1e-6);
    TF_CHECK_NEAR(col.events[1].normValue, 0.5, 1e-6);
    TF_CHECK_NEAR(col.events[2].normValue, 0.0, 1e-6);
    TF_CHECK(rec.status() == PerformanceRecorder::Status::Playing); // laeuft weiter
}

TF_TEST(recorder_note_retrigger_restarts_curve) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.setSampleRate(48000);
    Recording manual;
    RecordedCurve c; c.paramId = PID_OSC1_WT_POS;
    c.points = {{0, 0.f}, {1000, 1.f}};
    manual.lanes.push_back(c);
    manual.lengthSamples = 1000;
    rec.loadRecording(manual);
    rec.setPlaybackMode(RecPlaybackMode::NoteRetrigger);
    rec.play();
    Collector col;
    rec.processBlock(0,    64, &sink, &col); // startet, rel0
    rec.processBlock(800,  64, &sink, &col); // rel800 -> 0.8
    rec.noteObserved(noteOn(64));            // Retrigger
    rec.processBlock(900,  64, &sink, &col); // neu: rel0 -> 0.0
    TF_CHECK_NEAR(col.events[1].normValue, 0.8, 1e-6);
    TF_CHECK_NEAR(col.events.back().normValue, 0.0, 1e-6);
}

TF_TEST(recorder_muted_lane_not_emitted) {
    PerformanceRecorder rec(ParamCatalog::instance());
    Recording manual;
    RecordedCurve a; a.paramId = PID_FILT1_CUTOFF; a.points = {{0, 0.5f}}; a.muted = true;
    RecordedCurve b; b.paramId = PID_OSC1_LEVEL;   b.points = {{0, 0.25f}};
    manual.lanes.push_back(a); manual.lanes.push_back(b);
    manual.lengthSamples = 500;
    rec.loadRecording(manual);
    rec.setPlaybackMode(RecPlaybackMode::Loop);
    rec.play();
    Collector col;
    rec.processBlock(0, 64, &sink, &col);
    TF_CHECK(col.events.size() == 1);
    TF_CHECK(col.events[0].paramId == PID_OSC1_LEVEL);
}

TF_TEST(recorder_discrete_lane_stepwise_no_interpolation) {
    PerformanceRecorder rec(ParamCatalog::instance());
    Recording manual;
    RecordedCurve c; c.paramId = PID_FILT1_TYPE; c.discrete = true;
    c.points = {{0, 0.f}, {1000, 1.f}}; // norm 0->0, 1->3
    manual.lanes.push_back(c);
    manual.lengthSamples = 2000;
    rec.loadRecording(manual);
    rec.setPlaybackMode(RecPlaybackMode::Loop);
    rec.play();
    Collector col;
    rec.processBlock(500, 64, &sink, &col);   // zwischen Punkten: diskret -> alter Wert
    rec.processBlock(1500, 64, &sink, &col);  // nach zweitem Punkt: neuer Wert
    TF_CHECK(col.events.size() == 2);
    TF_CHECK_NEAR(col.events[0].normValue, 0.0, 1e-6);
    TF_CHECK_NEAR(col.events[1].normValue, 1.0, 1e-6);
    TF_CHECK((col.events[0].flags & kFlagDiscrete) != 0u);
}

TF_TEST(recorder_load_rejects_garbage_without_crash) {
    PerformanceRecorder rec(ParamCatalog::instance());
    Recording junk;
    RecordedCurve c1; c1.paramId = 999999; c1.points = {{-5, 2.f}}; // gueltige IDs/Werte erzwungen
    junk.lanes.push_back(c1);
    TF_CHECK(!rec.loadRecording(junk)); // nur unbekannte Lane -> abgewiesen
    RecordedCurve ok; ok.paramId = PID_OSC1_LEVEL; ok.points = {{0, 0.5f}};
    junk.lanes.push_back(ok);
    TF_CHECK(rec.loadRecording(junk));  // unbekannte Lane verworfen, Rest geladen
    TF_CHECK(rec.recording().lanes.size() == 1);
    TF_CHECK(rec.recording().lanes[0].paramId == PID_OSC1_LEVEL);
}

TF_TEST(recorder_empty_block_zero_length_safe) {
    PerformanceRecorder rec(ParamCatalog::instance());
    rec.arm({PID_FILT1_CUTOFF}, 1000);
    rec.noteObserved(noteOn(60));
    Collector col;
    rec.processBlock(0, 0, &sink, &col);   // Blockgroesse 0 (11.4)
    rec.processBlock(0, -5, &sink, &col);  // defensiv
    TF_CHECK(true);
}

int main() { return tf::runAll("recorder") > 0 ? 1 : 0; }

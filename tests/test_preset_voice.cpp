#include "TestFramework.h"
#include "PresetStore.h"
#include "VoicePool.h"

using namespace dub;

TF_TEST(preset_roundtrip_byte_identical) {
    const ParamCatalog& cat = ParamCatalog::instance();
    PresetStore store(cat);
    PresetData p;
    p.name = "DubChordWarm";
    p.params = {{PID_FILT1_CUTOFF, 0.35f}, {PID_MASTER_LEVEL, 0.7f}, {PID_OSC1_TABLE, 0.5f}};
    Recording r;
    r.name = "cutoffSweep";
    RecordedCurve c; c.paramId = PID_FILT1_CUTOFF;
    c.points = {{0, 0.1f}, {4800, 0.9f}, {9600, 0.4f}};
    r.lanes.push_back(c);
    r.lengthSamples = 9600;
    p.recordings.push_back(r);

    std::string s1 = store.serialize(p);
    PresetData back; bool partial = true;
    TF_CHECK(store.deserialize(s1, back, &partial));
    TF_CHECK(!partial);
    TF_CHECK(back.name == "DubChordWarm");
    TF_CHECK(back.params.size() == 3);
    TF_CHECK(back.recordings.size() == 1);
    TF_CHECK(back.recordings[0].lanes[0].points.size() == 3);
    TF_CHECK_NEAR(back.recordings[0].lanes[0].points[1].value, 0.9f, 1e-6);
    std::string s2 = store.serialize(back);
    TF_CHECK(s1 == s2); // deterministisch => identische Runden
}

TF_TEST(preset_robustness_garbage_inputs) {
    const ParamCatalog& cat = ParamCatalog::instance();
    PresetStore store(cat);
    PresetData out;
    TF_CHECK(!store.deserialize("", out));                    // leer
    TF_CHECK(!store.deserialize("Quatsch", out));             // fremdformat
    TF_CHECK(!store.deserialize("DUBPRO_PRESET\nname=x\n", out)); // abgeschnitten ohne Inhalt
    // unbekannter Parameter + ungueltiger Wert: partial, kein Absturz
    std::string weird = "DUBPRO_PRESET\nschema=1\nname=n\nparam 777777 0.5\nparam 1200 5.0\nEND_DUBPRO_PRESET\n";
    bool partial = false;
    TF_CHECK(store.deserialize(weird, out, &partial));
    TF_CHECK(partial);
    TF_CHECK(out.params.size() == 1);
    TF_CHECK_NEAR(out.params[0].second, 1.0, 1e-6); // 5.0 -> geclippt
}

TF_TEST(preset_recording_separate_export_import) {
    const ParamCatalog& cat = ParamCatalog::instance();
    PresetStore store(cat);
    Recording r; r.name = "standalone";
    RecordedCurve c; c.paramId = PID_OSC1_WT_POS;
    c.points = {{0, 0.f}, {100, 1.f}};
    r.lanes.push_back(c);
    std::string txt = store.serializeRecording(r);
    Recording back;
    TF_CHECK(store.deserializeRecording(txt, back));
    TF_CHECK(back.name == "standalone");
    TF_CHECK(back.lanes.size() == 1);
    TF_CHECK(back.lanes[0].points.size() == 2);
    TF_CHECK_NEAR(back.lanes[0].points[1].value, 1.0, 1e-6);
}

TF_TEST(voice_basic_on_off_release) {
    VoicePool vp;
    int v = vp.noteOn(0, 60, 100, 0);
    TF_CHECK(v >= 0);
    TF_CHECK(vp.activeCount() == 1);
    TF_CHECK(vp.noteOff(0, 60, 1000));
    TF_CHECK(vp.voice(v).held == false);
    vp.markReleaseDone(v);
    TF_CHECK(vp.activeCount() == 0);
}

TF_TEST(voice_stealing_prefers_released_then_oldest) {
    VoicePool vp;
    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
        vp.noteOn(0, std::uint8_t(40 + i), 90, i * 100);
    TF_CHECK(vp.activeCount() == VoicePool::kMaxVoices);
    // zwei Stimmen releasen
    vp.noteOff(0, 41, 1000);
    vp.noteOff(0, 43, 1001);
    int stolen = vp.noteOn(0, 99, 100, 2000);
    TF_CHECK(stolen >= 0);
    TF_CHECK(vp.voice(stolen).note == 99);
    TF_CHECK(vp.activeCount() == VoicePool::kMaxVoices);
    // Volle Pool ohne released: aelteste sustained wird geklaut (startSample 0)
    vp.noteOn(0, 100, 100, 3000);
    bool found100 = false;
    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
        if (vp.voice(i).active && vp.voice(i).note == 100) found100 = true;
    TF_CHECK(found100);
}

TF_TEST(voice_all_notes_off_no_hanging) {
    VoicePool vp;
    for (int i = 0; i < 5; ++i) vp.noteOn(0, std::uint8_t(50 + i), 80, i * 10);
    vp.allNotesOff(1000);
    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
        if (!vp.voice(i).held) vp.markReleaseDone(i);
    TF_CHECK(vp.activeCount() == 0);
    TF_CHECK(!vp.hasHangingAfterReleaseDone());
    vp.panicReset();
    TF_CHECK(vp.activeCount() == 0);
}

int main() { return tf::runAll("preset+voice") > 0 ? 1 : 0; }

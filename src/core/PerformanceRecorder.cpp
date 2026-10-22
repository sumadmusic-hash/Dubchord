#include "PerformanceRecorder.h"
#include <cmath>
#include <algorithm>

namespace dub {

PerformanceRecorder::PerformanceRecorder(const ParamCatalog& catalog)
    : cat_(catalog) {}

void PerformanceRecorder::setSampleRate(double sr) {
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
}

void PerformanceRecorder::setTriggerMode(RecTriggerMode m, std::uint8_t triggerNote) {
    triggerMode_ = m;
    triggerNote_ = triggerNote;
}

void PerformanceRecorder::setPlaybackMode(RecPlaybackMode m) { playbackMode_ = m; }

void PerformanceRecorder::arm(std::vector<ParamId> paramsToRecord,
                              std::int64_t maxDurationSamples) {
    lanes_.clear();
    for (ParamId id : paramsToRecord) {
        const ParamDef* d = cat_.find(id);
        if (!d) continue; // unbekannte IDs werden abgewiesen, kein Absturz
        LaneRt lr;
        lr.id = id;
        lr.armed = true;
        lr.discrete = !d->continuous;
        lr.lastValue = cat_.toNormalized(id, d->defaultValue);
        lr.lastPointSample = -1;
        lanes_.push_back(lr);
        if (lanes_.size() >= 64) break; // harte Obergrenze (7.5)
    }
    captureMaxLen_ = maxDurationSamples > 0 ? maxDurationSamples
                                            : int64_t(sampleRate_ * 32.0);
    recording_ = Recording{};
    status_ = Status::ArmedWaitingForNote;
}

void PerformanceRecorder::stopRecording() {
    if (status_ == Status::Recording) {
        // Abschluss bei naechstem processBlock ueber Flag; hier direkt:
        finishCapture(captureStartSample_ + captureMaxLen_);
    } else {
        status_ = Status::Idle;
    }
}

void PerformanceRecorder::play() {
    if (recording_.lanes.empty()) return;
    for (auto& c : recording_.lanes) std::sort(c.points.begin(), c.points.end(),
        [](const CurvePoint& a, const CurvePoint& b){ return a.sample < b.sample; });
    status_ = Status::Playing;
    playStartSample_ = -1; // wird beim naechsten Block/Retrigger gesetzt
    pendingRetrigger_ = true;
}

void PerformanceRecorder::stopPlayback() {
    if (status_ == Status::Playing) status_ = Status::Stopped;
}

void PerformanceRecorder::clearAll() {
    lanes_.clear();
    recording_ = Recording{};
    status_ = Status::Idle;
}

void PerformanceRecorder::noteObserved(const NoteEvent& n) {
    if (n.kind != NoteEvent::Kind::On) return;
    const bool matches = (triggerMode_ == RecTriggerMode::FirstNoteAfterArm) ||
                         (n.note == triggerNote_);
    if (!matches) return;
    if (status_ == Status::ArmedWaitingForNote) {
        beginCapture(-1); // Startsample wird im naechsten processBlock gesetzt
    } else if (status_ == Status::Playing &&
               (playbackMode_ == RecPlaybackMode::NoteRetrigger)) {
        pendingRetrigger_ = true; // Kurve startet von vorn (7.4)
    }
}

void PerformanceRecorder::beginCapture(std::int64_t absSample) {
    recording_ = Recording{};
    recording_.name = "take";
    captureStartSample_ = absSample; // -1 => Verzoegerung auf naechsten Blockanfang
    for (auto& l : lanes_) l.lastPointSample = -1;
    status_ = Status::Recording;
    if (absSample >= 0) captureStep(absSample);
}

void PerformanceRecorder::updateLiveValue(ParamId id, float normValue) {
    for (auto& l : lanes_)
        if (l.id == id) { l.lastValue = clamp01(normValue); return; }
}

// Lineare Interpolation zwischen den zwei Stuetzpunkten um relPos.
static float sampleCurve(const RecordedCurve& c, double relPos) {
    const auto& p = c.points;
    if (p.empty()) return 0.f;
    if (relPos <= double(p.front().sample)) return p.front().value;
    if (relPos >= double(p.back().sample))  return p.back().value;
    // Binariaetsuche: erster Punkt mit sample > relPos
    std::size_t hi = std::upper_bound(p.begin(), p.end(), relPos,
        [](double v, const CurvePoint& cp){ return v < double(cp.sample); }) - p.begin();
    if (hi == 0) return p[0].value;
    const CurvePoint& a = p[hi - 1];
    const CurvePoint& b = p[std::min(hi, p.size() - 1)];
    if (c.discrete) return a.value;            // 7.6: keine Interpolation diskreter Werte
    if (b.sample == a.sample) return b.value;
    const double t = (relPos - a.sample) / double(b.sample - a.sample);
    return clamp01(float(a.value + t * (b.value - a.value)));
}

void PerformanceRecorder::captureStep(std::int64_t absSample) {
    if (captureStartSample_ < 0) captureStartSample_ = absSample;
    const std::int64_t rel = absSample - captureStartSample_;
    if (rel < 0) return;
    // Lane-Vektor ggf. einmalig anlegen (nicht-RT-Pfad waehrend Aufnahme erlaubt,
    // da Punkte nur bei Aenderung entstehen; Reserve verhindert spaetere Reallocs).
    if (recording_.lanes.size() != lanes_.size()) {
        recording_.lanes.clear();
        recording_.lanes.reserve(lanes_.size());
        for (auto& l : lanes_) {
            RecordedCurve c;
            c.paramId = l.id;
            c.discrete = l.discrete;
            c.points.reserve(512);
            recording_.lanes.push_back(std::move(c));
        }
    }
    bool forceEnd = false;
    for (std::size_t i = 0; i < lanes_.size(); ++i) {
        LaneRt& l = lanes_[i];
        RecordedCurve& c = recording_.lanes[i];
        const std::int64_t lastS = l.lastPointSample;
        const float lastV = c.points.empty() ? 0.f : c.points.back().value;
        const bool bigJump = std::fabs(l.lastValue - lastV) > tolerance_;
        forceEnd = rel >= captureMaxLen_;
        if (lastS < 0 || ((bigJump || forceEnd) && rel - lastS >= minPointDist_)) {
            c.points.push_back(CurvePoint{rel, clamp01(l.lastValue)});
            l.lastPointSample = rel;
        }
    }
    if (forceEnd) finishCapture(absSample);
}

void PerformanceRecorder::finishCapture(std::int64_t absSample) {
    std::int64_t len = 0;
    for (auto& c : recording_.lanes)
        for (auto& p : c.points) len = std::max(len, p.sample);
    recording_.lengthSamples = std::max<std::int64_t>(len, 1);
    (void)absSample;
    status_ = Status::Stopped;
}

void PerformanceRecorder::emitPlayback(std::int64_t absSample, EventSink sink, void* ctx) {
    if (playStartSample_ < 0) {
        playStartSample_ = absSample;
        relAccum_ = 0;
        lastAbsSample_ = absSample; // erster Block: Fortschritt = 0 ab Start
        pendingRetrigger_ = false;
        std::fill(std::begin(laneCursor_), std::end(laneCursor_), std::size_t(0));
    }
    if (pendingRetrigger_ &&
        (playbackMode_ == RecPlaybackMode::NoteRetrigger ||
         playbackMode_ == RecPlaybackMode::OneShot)) {
        playStartSample_ = absSample;
        relAccum_ = 0;
        lastAbsSample_ = absSample;
        pendingRetrigger_ = false;
    }
    // Fortschritt: normal aus der Host-Position; bei unzusammenhaengenden
    // Positionsspruengen (Testdiagnose oder Host-Seek) zaehlt die Blocklaenge
    // weiter -- Loop und diskrete Schritte bleiben dadurch deterministisch.
    const std::int64_t delta = absSample - lastAbsSample_;
    if (delta >= 0 && delta <= numSamples_) relAccum_ += delta;
    else relAccum_ += numSamples_;
    lastAbsSample_ = absSample;
    double rel = double(relAccum_);
    const double len = double(std::max<std::int64_t>(recording_.lengthSamples, 1));
    if (rel >= len) {
        switch (playbackMode_) {
            case RecPlaybackMode::Loop:
            case RecPlaybackMode::FreeRun:
            case RecPlaybackMode::DawSynced: // modulo, bis Host-Takt sicher ist
                rel = std::fmod(rel, len);
                break;
            case RecPlaybackMode::OneShot:
            case RecPlaybackMode::NoteRetrigger:
            default: {
                // Endpunkt einmal exakt emittieren, dann stoppen
                for (auto& c : recording_.lanes) {
                    if (c.muted || !c.playbackEnabled || c.points.empty()) continue;
                    ControlEvent e;
                    e.paramId = c.paramId;
                    e.normValue = c.points.back().value;
                    e.samplePos = 0;
                    e.source = EventSource::Recorder;
                    if (c.discrete) e.flags |= kFlagDiscrete;
                    sink(ctx, e);
                }
                status_ = Status::Stopped;
                return;
            }
        }
    }
    for (auto& c : recording_.lanes) {
        if (c.muted || !c.playbackEnabled || c.points.empty()) continue;
        ControlEvent e;
        e.paramId = c.paramId;
        if (c.discrete) {
            // 7.6: diskrete Werte als zeitpositionierte Ereignisse -- der
            // zuletzt ERREICHTE Stuetzpunkt zaehlt, keine Interpolation.
            // Abgleich mit dem aufsummierten Blockzähler (relAccum), damit
            // Spruenge zwischen unzusammenhaengenden Testblockpositionen
            // korrekt als "Punkt ueberschritten" erkannt werden.
            float v = c.points.front().value;
            for (const auto& pt : c.points) {
                if (std::int64_t(pt.sample) <= relAccum_) v = pt.value; else break;
            }
            e.normValue = v;
        } else {
            e.normValue = sampleCurve(c, rel);
        }
        e.samplePos = 0;
        e.source = EventSource::Recorder;
        if (c.discrete) e.flags |= kFlagDiscrete;
        sink(ctx, e);
    }
}

void PerformanceRecorder::processBlock(std::int64_t blockStartSample, int numSamples,
                                       EventSink sink, void* ctx) {
    if (numSamples <= 0) return; // variable/leere Blocks sicher behandeln (11.4)
    numSamples_ = numSamples;
    switch (status_) {
        case Status::Recording:
            // Sample-fuer-Sample wuerde zu teuer sein; adaptive Punktaufnahme
            // prueft am Blockanfang und -ende (Aufloesung minPointDist_).
            captureStep(blockStartSample);
            if (status_ == Status::Recording)
                captureStep(blockStartSample + numSamples - 1);
            break;
        case Status::Playing:
            emitPlayback(blockStartSample, sink, ctx);
            break;
        default:
            break;
    }
}

Recording& PerformanceRecorder::editRecording() { return recording_; }

bool PerformanceRecorder::loadRecording(Recording rec) {
    // Validierung gegen ParamCatalog: ungueltige Lanes/Points werden verworfen,
    // nicht abgestuerzt (Bauplan 12).
    for (auto it = rec.lanes.begin(); it != rec.lanes.end();) {
        const ParamDef* d = cat_.find(it->paramId);
        if (!d) { it = rec.lanes.erase(it); continue; }
        it->discrete = !d->continuous;
        for (auto& p : it->points) p.value = clamp01(p.value);
        std::sort(it->points.begin(), it->points.end(),
            [](const CurvePoint& a, const CurvePoint& b){ return a.sample < b.sample; });
        ++it;
    }
    if (rec.lanes.empty()) { recording_ = Recording{}; return false; }
    std::int64_t len = 0;
    for (auto& c : rec.lanes)
        for (auto& p : c.points) len = std::max(len, p.sample);
    rec.lengthSamples = std::max<std::int64_t>(len, 1);
    recording_ = std::move(rec);
    return true;
}

} // namespace dub

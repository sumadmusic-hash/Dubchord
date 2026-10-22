#include "ParameterControlLayer.h"
#include <cmath>

namespace dub {

ParameterControlLayer::ParameterControlLayer(const ParamCatalog& catalog)
    : cat_(catalog) {
    slots_.resize(catalog.count());
    resetToDefaults();
}

int ParameterControlLayer::indexFor(ParamId id) const {
    for (std::size_t i = 0; i < cat_.count(); ++i)
        if (cat_.defAt(i)->id == id) return static_cast<int>(i);
    return -1;
}

void ParameterControlLayer::resetToDefaults() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const ParamDef* d = cat_.defAt(i);
        Slot& s = slots_[i];
        s.current = s.target = cat_.toNormalized(d->id, d->defaultValue);
        s.hasPlayback = false;
        s.liveHeld = false;
        s.discrete = !d->continuous;
        s.mode = PriorityMode::PlaybackPriority;
    }
    updateCoeffs();
}

void ParameterControlLayer::setSampleRate(double sr) {
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
    updateCoeffs();
}

void ParameterControlLayer::setSmoothingTime(float seconds) {
    smoothingTime_ = seconds < 0.f ? 0.f : seconds;
    updateCoeffs();
}

void ParameterControlLayer::updateCoeffs() {
    // One-Pole mit Zeitkonstante tau: coeff = exp(-1/(sr*tau)).
    // zusaetzlich maxStep: begrenzt die Amplitude PRO Sample -> bei grossen
    // Sprungen wird die Flanke zeitlich gestreckt statt amplitudenmaessig
    // (Click-Schutz ohne Overshoot, Bauplan 5.5).
    const double tau = smoothingTime_ > 1e-6 ? static_cast<double>(smoothingTime_) : 1.0;
    const double c = std::exp(-1.0 / (sampleRate_ * tau));
    for (auto& s : slots_) {
        s.coeff = s.discrete ? 1.f : static_cast<float>(c);
        // langsameste erlaubte Flanke: voller Bereich in ~0.5 ms Minimum
        s.maxStep = s.discrete ? 1.f : static_cast<float>(1.0 / (sampleRate_ * 0.0005));
    }
}

void ParameterControlLayer::submit(const ControlEvent& e) { queue_.push(e); }

void ParameterControlLayer::setPlaybackValue(ParamId id, float normValue, bool active) {
    const int i = indexFor(id);
    if (i < 0) return;
    Slot& s = slots_[i];
    s.playbackValue = clamp01(normValue);
    s.hasPlayback = active;
    if (!active && s.mode == PriorityMode::LiveOverride) s.liveHeld = false;
}

void ParameterControlLayer::setPriorityMode(ParamId id, PriorityMode mode) {
    const int i = indexFor(id);
    if (i < 0) return;
    slots_[i].mode = mode;
    if (mode == PriorityMode::PlaybackPriority) slots_[i].liveHeld = false;
}

PriorityMode ParameterControlLayer::priorityMode(ParamId id) const {
    const int i = indexFor(id);
    return i < 0 ? PriorityMode::PlaybackPriority : slots_[i].mode;
}

void ParameterControlLayer::setDirect(ParamId id, float normValue) {
    const int i = indexFor(id);
    if (i < 0) return;
    slots_[i].current = slots_[i].target = clamp01(normValue);
}

void ParameterControlLayer::processBlock(int /*numSamples*/) {
    // 1) Ereignisse aus Queue ziehen (samplePos处理 koennte praeiser werden;
    //    fuer V1 gilt: innerhalb des Blocks anwendbar, Glaettung verhindert Clicks).
    ControlEvent e;
    while (queue_.pop(e)) {
        const int i = indexFor(e.paramId);
        if (i < 0) continue;
        Slot& s = slots_[i];
        const bool live = (e.source == EventSource::Gui ||
                           e.source == EventSource::MidiHardware ||
                           e.source == EventSource::Host);
        if (live) {
            // Bauplan 7.7:
            //  LiveOverride   -> Live uebernimmt sofort, Wiedergabe pausiert pro Param.
            //  PlaybackPriority -> waehrend aktiver Wiedergabe steuert die Kurve;
            //                      der Live-Wunsch wird nur vorgemerkt und wirkt,
            //                      sobald die Wiedergabe endet (setPlaybackValue(false)).
            if (s.mode == PriorityMode::LiveOverride || !s.hasPlayback) {
                s.target = clamp01(e.normValue);
                s.liveHeld = (s.mode == PriorityMode::LiveOverride);
            }
            // bei PlaybackPriority && hasPlayback: target bleibt Vorgemerkt-los,
            // Kurve hat Vorrang (bewusst keine Ziel-Aenderung).
        } else if (e.source == EventSource::Recorder) {
            s.hasPlayback = true;
            s.playbackValue = clamp01(e.normValue);
            if (s.mode == PriorityMode::LiveOverride) s.liveHeld = false; // neue Kurve re-armed
        }
    }
    // 2) Effektiven Zielwert bestimmen + glaetten.
    for (auto& s : slots_) {
        float eff;
        if (s.hasPlayback && !(s.mode == PriorityMode::LiveOverride && s.liveHeld))
            eff = s.playbackValue;
        else
            eff = s.target;
        if (s.discrete || s.coeff >= 1.f) {
            s.current = eff;
        } else {
            float err = eff - s.current;
            float step = err * (1.f - s.coeff);
            if (step >  s.maxStep) step =  s.maxStep;
            if (step < -s.maxStep) step = -s.maxStep;
            s.current += step;
            if (std::fabs(eff - s.current) < 1e-7f) s.current = eff;
        }
    }
}

float ParameterControlLayer::smoothedNorm(ParamId id) const {
    const int i = indexFor(id);
    return i < 0 ? 0.f : slots_[i].current;
}

float ParameterControlLayer::realValue(ParamId id) const {
    const int i = indexFor(id);
    return i < 0 ? 0.f : cat_.toRealValue(id, slots_[i].current);
}

bool ParameterControlLayer::isLiveHeld(ParamId id) const {
    const int i = indexFor(id);
    return i >= 0 && slots_[i].liveHeld;
}

} // namespace dub

#include "ParamCatalog.h"

namespace dub {

namespace {
constexpr float kFreqMin = 20.f, kFreqMax = 20000.f;
constexpr float kCentsMin = -100.f, kCentsMax = 100.f;
constexpr float kOctaveMin = -3.f,  kOctaveMax = 3.f;
} // namespace

ParamCatalog::ParamCatalog() {
    auto add = [&](ParamId id, const char* name, float mn, float mx, float dflt,
                   ParamScale sc, bool cont, ParamGroup g) {
        defs_[numDefs_++] = ParamDef{id, name, mn, mx, dflt, sc, cont, g};
    };

    add(PID_MASTER_LEVEL, "Master Level", 0.f, 1.f, 0.7f, ParamScale::Exp, true, ParamGroup::Global);
    add(PID_POLYPHONY,    "Polyphony",    1.f, 8.f, 8.f,  ParamScale::Linear, false, ParamGroup::Voice);

    for (int o = 0; o < 3; ++o) {
        const ParamId b = PID_OSC1_WT_POS + static_cast<ParamId>(o * 10);
        add(b + 0, "Wavetable Position", 0.f, 1.f, 0.f,  ParamScale::Linear, true,  ParamGroup::Oscillator);
        add(b + 1, "Level",              0.f, 1.f, 0.8f, ParamScale::Exp,    true,  ParamGroup::Oscillator);
        add(b + 2, "Fine Tune", kCentsMin, kCentsMax, 0.f, ParamScale::Linear, true, ParamGroup::Oscillator);
        add(b + 3, "Coarse Octave", kOctaveMin, kOctaveMax, 0.f, ParamScale::Linear, false, ParamGroup::Oscillator);
        add(b + 4, "Wavetable", 0.f, 15.f, 0.f, ParamScale::Linear, false, ParamGroup::Oscillator);
    }

    for (int f = 0; f < 4; ++f) {
        const ParamId b = PID_FILT1_CUTOFF + static_cast<ParamId>(f * 10);
        add(b + 0, "Cutoff", kFreqMin, kFreqMax, 1000.f, ParamScale::Log, true, ParamGroup::Filter);
        add(b + 1, "Resonance", 0.f, 1.f, 0.2f, ParamScale::Linear, true, ParamGroup::Filter);
        add(b + 2, "Drive", 0.f, 1.f, 0.f, ParamScale::Exp, true, ParamGroup::Filter);
        if (f == 0) {
            add(PID_FILT1_TYPE, "Filter Type", 0.f, 3.f, 0.f, ParamScale::Linear, false, ParamGroup::Filter);
            add(PID_FILT1_MIX,  "Filter Mix",  0.f, 1.f, 1.f, ParamScale::Linear, true,  ParamGroup::Filter);
        }
    }

    add(PID_ENV1_ATTACK,  "Attack",  0.001f, 4.0f, 0.01f, ParamScale::Log, true, ParamGroup::Voice);
    add(PID_ENV1_DECAY,   "Decay",   0.005f, 8.0f, 0.30f, ParamScale::Log, true, ParamGroup::Voice);
    add(PID_ENV1_SUSTAIN, "Sustain", 0.f, 1.f, 0.6f, ParamScale::Linear, true, ParamGroup::Voice);
    add(PID_ENV1_RELEASE, "Release", 0.005f, 8.0f, 0.4f, ParamScale::Log, true, ParamGroup::Voice);
    add(PID_VEL_TO_VOLUME,"Vel->Volume", 0.f, 1.f, 0.6f, ParamScale::Linear, true, ParamGroup::Voice);
    add(PID_VEL_TO_FILTER,"Vel->Filter", 0.f, 1.f, 0.0f, ParamScale::Linear, true, ParamGroup::Voice);

    add(PID_REC_PLAYBACK_MODE, "Rec Playback Mode", 0.f, 4.f, 1.f, ParamScale::Linear, false, ParamGroup::Recorder);
    add(PID_REC_TRIGGER_MODE,  "Rec Trigger Mode",  0.f, 2.f, 0.f, ParamScale::Linear, false, ParamGroup::Recorder);
    add(PID_REC_PRIORITY_MODE, "Rec Priority Mode", 0.f, 1.f, 0.f, ParamScale::Linear, false, ParamGroup::Recorder);
}

const ParamDef* ParamCatalog::find(ParamId id) const {
    for (std::size_t i = 0; i < numDefs_; ++i)
        if (defs_[i].id == id) return &defs_[i];
    return nullptr;
}

bool ParamCatalog::isDiscrete(ParamId id) const {
    const ParamDef* d = find(id);
    return d && !d->continuous;
}

float ParamCatalog::toNormalized(ParamId id, float realValue) const {
    const ParamDef* d = find(id);
    if (!d) return 0.f;
    realValue = std::clamp(realValue, d->minValue, d->maxValue);
    switch (d->scale) {
        case ParamScale::Log:
            return std::log(realValue / d->minValue) / std::log(d->maxValue / d->minValue);
        case ParamScale::Exp:
            return std::pow(clamp01((realValue - d->minValue) / (d->maxValue - d->minValue)),
                            1.f / 3.f);
        case ParamScale::Linear:
        default:
            return (realValue - d->minValue) / (d->maxValue - d->minValue);
    }
}

float ParamCatalog::toRealValue(ParamId id, float normValue) const {
    const ParamDef* d = find(id);
    if (!d) return 0.f;
    normValue = clamp01(normValue);
    switch (d->scale) {
        case ParamScale::Log:
            return d->minValue * std::pow(d->maxValue / d->minValue, normValue);
        case ParamScale::Exp: {
            const float t = normValue * normValue * normValue;
            return d->minValue + t * (d->maxValue - d->minValue);
        }
        case ParamScale::Linear:
        default: {
            float v = d->minValue + normValue * (d->maxValue - d->minValue);
            if (!d->continuous) v = std::round(v);
            return v;
        }
    }
}

const ParamCatalog& ParamCatalog::instance() {
    static ParamCatalog c;
    return c;
}

} // namespace dub

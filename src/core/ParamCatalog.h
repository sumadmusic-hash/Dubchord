#pragma once
// Zentrale Parameterdefinitionen gemaess Bauplan 9:
// stabile IDs, Bereiche, Defaults, Einheiten, Skalierung.
// GUI, Presets, Recorder und Control-Layer verwenden exakt diese Definitionen.
#include "ControlEvent.h"
#include <array>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace dub {

enum class ParamScale : std::uint8_t { Linear, Log, Exp };
enum class ParamGroup : std::uint8_t { Global, Oscillator, Filter, Voice, Recorder };

struct ParamDef {
    ParamId      id        = 0;
    const char*  name      = "";   // zeigt in statischen Stringvorrat
    float        minValue  = 0.f;
    float        maxValue  = 1.f;
    float        defaultValue = 0.5f;
    ParamScale   scale     = ParamScale::Linear;
    bool         continuous = true; // false => diskret (nicht glätten/interpolieren)
    ParamGroup   group     = ParamGroup::Global;
};

// Stabile, eindeutige IDs (Bauplan 12).
// Regel: neue Parameter nur am Ende ergänzen; bestehende IDs nie umnumerieren.
enum : ParamId {
    PID_MASTER_LEVEL      = 1000,
    PID_POLYPHONY         = 1001,

    PID_OSC1_WT_POS       = 1100, PID_OSC1_LEVEL = 1101, PID_OSC1_FINE = 1102,
    PID_OSC1_COARSE       = 1103, PID_OSC1_TABLE = 1104,
    PID_OSC2_WT_POS       = 1110, PID_OSC2_LEVEL = 1111, PID_OSC2_FINE = 1112,
    PID_OSC2_COARSE       = 1113, PID_OSC2_TABLE = 1114,
    PID_OSC3_WT_POS       = 1120, PID_OSC3_LEVEL = 1121, PID_OSC3_FINE = 1122,
    PID_OSC3_COARSE       = 1123, PID_OSC3_TABLE = 1124,

    PID_FILT1_CUTOFF      = 1200, PID_FILT1_RESO = 1201, PID_FILT1_DRIVE = 1202,
    PID_FILT1_TYPE        = 1203, PID_FILT1_MIX  = 1204,
    PID_FILT2_CUTOFF      = 1210, PID_FILT2_RESO = 1211, PID_FILT2_DRIVE = 1212,
    PID_FILT3_CUTOFF      = 1220, PID_FILT3_RESO = 1221, PID_FILT3_DRIVE = 1222,
    PID_FILT4_CUTOFF      = 1230, PID_FILT4_RESO = 1231, PID_FILT4_DRIVE = 1232,

    PID_ENV1_ATTACK       = 1300, PID_ENV1_DECAY = 1301, PID_ENV1_SUSTAIN = 1302,
    PID_ENV1_RELEASE      = 1303, PID_VEL_TO_VOLUME = 1310, PID_VEL_TO_FILTER = 1311,

    PID_REC_PLAYBACK_MODE = 1400, PID_REC_TRIGGER_MODE = 1401,
    PID_REC_PRIORITY_MODE = 1402,
};

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

class ParamCatalog {
public:
    static const ParamCatalog& instance();

    const ParamDef* find(ParamId id) const;
    bool isValid(ParamId id) const { return find(id) != nullptr; }
    bool isDiscrete(ParamId id) const;

    float toNormalized(ParamId id, float realValue) const;
    float toRealValue (ParamId id, float normValue)  const;

    std::size_t count() const { return numDefs_; }
    const ParamDef* defAt(std::size_t i) const { return &defs_[i]; }

private:
    ParamCatalog();
    std::array<ParamDef, 64> defs_{};
    std::size_t numDefs_ = 0;
};

} // namespace dub

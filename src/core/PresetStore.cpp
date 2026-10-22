#include "PresetStore.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dub {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parsePair(const std::string& line, std::string& key, std::string& val) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = trim(line.substr(0, eq));
    val = trim(line.substr(eq + 1));
    return !key.empty();
}

float parseFloat(const std::string& s, float fallback) {
    try {
        std::size_t pos = 0;
        float v = std::stof(s, &pos);
        if (!std::isfinite(v)) return fallback;
        return v;
    } catch (...) { return fallback; }
}

long parseIntSafe(const std::string& s, long fallback) {
    try {
        std::size_t pos = 0;
        long v = std::stol(s, &pos);
        return v;
    } catch (...) { return fallback; }
}

} // namespace

std::string PresetStore::serialize(const PresetData& p) const {
    std::ostringstream os;
    os << "DUBPRO_PRESET\n";
    os << "schema=" << PRESET_SCHEMA_VERSION << "\n";
    os << "name=" << p.name << "\n";
    // deterministisch sortiert nach ID
    auto sorted = p.params;
    std::sort(sorted.begin(), sorted.end(),
              [](auto& a, auto& b){ return a.first < b.first; });
    for (auto& kv : sorted)
        os << "param " << kv.first << " " << kv.second << "\n";
    for (const auto& r : p.recordings) {
        os << "BEGIN_RECORDING\n";
        os << serializeRecording(r);
        os << "END_RECORDING\n";
    }
    os << "END_DUBPRO_PRESET\n";
    return os.str();
}

std::string PresetStore::serializeRecording(const Recording& r) const {
    std::ostringstream os;
    os << "name=" << r.name << "\n";
    os << "length=" << r.lengthSamples << "\n";
    for (const auto& c : r.lanes) {
        os << "LANE " << c.paramId << " " << (c.discrete ? 1 : 0)
           << " " << (c.muted ? 1 : 0) << " " << (c.playbackEnabled ? 1 : 0) << "\n";
        for (const auto& pt : c.points)
            os << "PT " << pt.sample << " " << pt.value << "\n";
        os << "ENDLANE\n";
    }
    return os.str();
}

bool PresetStore::deserialize(const std::string& text, PresetData& out, bool* partial) const {
    if (partial) *partial = false;
    std::istringstream is(text);
    std::string line;
    if (!std::getline(is, line) || trim(line) != "DUBPRO_PRESET") return false;
    out = PresetData{};
    out.schemaVersion = 0;
    bool sawEnd = false;
    int schemaSeen = 0;
    while (std::getline(is, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;
        if (t == "END_DUBPRO_PRESET") { sawEnd = true; break; }
        if (t == "BEGIN_RECORDING") {
            Recording r;
            std::string inner;
            bool ok = false;
            while (std::getline(is, line)) {
                const std::string u = trim(line);
                if (u == "END_RECORDING") { ok = true; break; }
                inner += u + "\n";
            }
            if (ok && deserializeRecording(inner, r)) out.recordings.push_back(std::move(r));
            else if (partial) *partial = true;
            continue;
        }
        std::string key, val;
        if (t.rfind("param ", 0) == 0) {
            // param <id> <normvalue>
            std::istringstream ps(t);
            std::string tag; long id = 0; float v = 0.f;
            ps >> tag >> id >> v;
            const ParamDef* d = cat_.find(static_cast<ParamId>(id));
            if (!d) { if (partial) *partial = true; continue; } // unbekannt -> ignorieren
            out.params.emplace_back(static_cast<ParamId>(id), clamp01(v));
            continue;
        }
        if (parsePair(t, key, val)) {
            if (key == "schema") schemaSeen = parseIntSafe(val, 0);
            else if (key == "name") out.name = val;
            // unbekannte Schluessel: ignorieren (Zukunfts-/Altversionen)
            (void)schemaSeen;
        }
    }
    out.schemaVersion = schemaSeen;
    if (!sawEnd) { if (partial) *partial = true; }
    return sawEnd || !out.params.empty(); // auch abgeschnittene Datei nutzbar? nur mit Inhalt
}

bool PresetStore::deserializeRecording(const std::string& text, Recording& out) const {
    std::istringstream is(text);
    std::string line;
    Recording r;
    RecordedCurve* cur = nullptr;
    bool anyLane = false;
    while (std::getline(is, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;
        std::string key, val;
        if (t.rfind("LANE ", 0) == 0) {
            std::istringstream ls(t);
            std::string tag; long pid = 0; int disc = 0, mute = 0, play = 1;
            ls >> tag >> pid >> disc >> mute >> play;
            const ParamDef* d = cat_.find(static_cast<ParamId>(pid));
            if (!d) { cur = nullptr; continue; } // unbekannte Lane verwerfen
            r.lanes.emplace_back();
            cur = &r.lanes.back();
            cur->paramId = static_cast<ParamId>(pid);
            cur->discrete = !d->continuous; // katalognah, nicht dateiglaeubig
            cur->muted = mute != 0;
            cur->playbackEnabled = play != 0;
            cur->points.reserve(64);
            anyLane = true;
            continue;
        }
        if (t == "ENDLANE") { cur = nullptr; continue; }
        if (t.rfind("PT ", 0) == 0 && cur) {
            std::istringstream ps(t);
            std::string tag; long s = 0; float v = 0.f;
            ps >> tag >> s >> v;
            if (s >= 0 && std::isfinite(v))
                cur->points.push_back(CurvePoint{s, clamp01(v)});
            continue;
        }
        if (parsePair(t, key, val)) {
            if (key == "name") r.name = val;
            else if (key == "length") r.lengthSamples = parseIntSafe(val, 0);
        }
    }
    if (!anyLane) return false;
    out = std::move(r);
    return true;
}

} // namespace dub

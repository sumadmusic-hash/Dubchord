#pragma once
// Versionierte Preset-/State-Serialisierung gemaess Bauplan 12.
// Kern arbeitet auf Strings (Datei-/Netzwerkzugriff liegt ausserhalb,
// niemals im Audiothread). Ungueltige Dateien fuehren zu definiertem
// Fallback statt Absturz; fehlende Felder erhalten Defaults.
#include "PerformanceRecorder.h"
#include <string>
#include <vector>

namespace dub {

constexpr int PRESET_SCHEMA_VERSION = 1;

struct PresetData {
    int schemaVersion = PRESET_SCHEMA_VERSION;
    std::string name = "init";
    // paramId -> normalisierter Wert (nur explizit gesetzte Werte)
    std::vector<std::pair<ParamId, float>> params;
    std::vector<Recording> recordings;
};


class PresetStore {
public:
    explicit PresetStore(const ParamCatalog& catalog) : cat_(catalog) {}

    // Serialisieren: deterministisch sortiert => byte-identische Runden.
    std::string serialize(const PresetData& p) const;

    // Deserialisieren: robust. Liefert false bei unlesbarer Datei,
    // partial=true wenn Teile verworfen wurden. Unbekannte Parameter/
    // Felder werden ignoriert, Werte geclippt.
    bool deserialize(const std::string& text, PresetData& out, bool* partial = nullptr) const;

    // Recorder-Kurven separat exportierbar/importierbar (Bauplan 12).
    std::string serializeRecording(const Recording& r) const;
    bool deserializeRecording(const std::string& text, Recording& out) const;

private:
    const ParamCatalog& cat_;
};

} // namespace dub

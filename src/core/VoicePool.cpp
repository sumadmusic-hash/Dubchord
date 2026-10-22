#include "VoicePool.h"

namespace dub {

int VoicePool::noteOn(std::uint8_t ch, std::uint8_t note, std::uint8_t vel, std::int64_t sample) {
    ++clock_;
    // 1) freier Slot
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].active) {
            voices_[i] = Voice{true, true, ch, note, vel, sample, -1};
            return i;
        }
    }
    // 2) Stealing: aelteste released-Stimme (kleinstes releaseSample)
    int best = -1;
    for (int i = 0; i < kMaxVoices; ++i) {
        Voice& v = voices_[i];
        if (v.active && !v.held) {
            if (best < 0 || v.releaseSample < voices_[best].releaseSample) best = i;
        }
    }
    if (best >= 0) {
        voices_[best] = Voice{true, true, ch, note, vel, sample, -1};
        return best;
    }
    // 3) aelteste sustained-Stimme
    best = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (voices_[i].startSample < voices_[best].startSample) best = i;
    voices_[best] = Voice{true, true, ch, note, vel, sample, -1};
    return best;
}

bool VoicePool::noteOff(std::uint8_t ch, std::uint8_t note, std::int64_t sample) {
    bool found = false;
    for (int i = 0; i < kMaxVoices; ++i) {
        Voice& v = voices_[i];
        if (v.active && v.held && v.channel == ch && v.note == note) {
            v.held = false;
            v.releaseSample = sample;
            found = true;
        }
    }
    return found;
}

void VoicePool::allNotesOff(std::int64_t sample) {
    for (auto& v : voices_)
        if (v.active && v.held) { v.held = false; v.releaseSample = sample; }
}

void VoicePool::panicReset() {
    for (auto& v : voices_) v = Voice{};
}

void VoicePool::markReleaseDone(int idx) {
    if (idx >= 0 && idx < kMaxVoices && !voices_[idx].held) voices_[idx] = Voice{};
}

int VoicePool::activeCount() const {
    int n = 0;
    for (auto& v : voices_) if (v.active) ++n;
    return n;
}

bool VoicePool::hasHangingAfterReleaseDone() const {
    // Nach vollständigem Release darf nichts "held" bleiben.
    for (auto& v : voices_) if (v.active && v.held) return true;
    return false;
}

} // namespace dub

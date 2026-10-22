#pragma once
// Vorallokierte, thread-sichere SPSC-Ringpuffer-Queue fuer ControlEvents.
// Bauplan 8.3: MIDI-Callback schreibt nur kompakte Ereignisse in eine
// vorallokierte Queue; Bauplan 11.2/11.3: keine Allokation, kein Blockieren,
// Ueberlauf = Zaehler + Verwerfen.
#include "ControlEvent.h"
#include <atomic>
#include <array>
#include <cstddef>

namespace dub {

template <std::size_t Capacity> // Potenz-2 empfohlen
class ControlEventQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity muss Potenz von 2 sein");
public:
    ControlEventQueue() = default;

    // Producer-Kontext (MIDI-Callback / GUI-Thread). Niemals blockierend.
    bool push(const ControlEvent& e) noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) { // voll -> verwerfen + diagnostizieren
            overflowCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[w & (Capacity - 1)] = e;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    // Consumer-Kontext (Audiothread).
    bool pop(ControlEvent& out) noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        if (r == w) return false;
        out = slots_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

    std::size_t overflowCount() const noexcept {
        return overflowCount_.load(std::memory_order_relaxed);
    }
    void resetOverflow() noexcept { overflowCount_.store(0, std::memory_order_relaxed); }
    bool empty() noexcept {
        return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire);
    }

private:
    std::array<ControlEvent, Capacity> slots_{};
    std::atomic<std::size_t> write_{0};
    std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> overflowCount_{0};
};

} // namespace dub

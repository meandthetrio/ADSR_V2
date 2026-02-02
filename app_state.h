#pragma once
#include <atomic>
#include <cstdint>

enum class PerformPage : uint8_t
{
    Main = 0,
    // Later: Delay, Reverb, Sat, etc.
};

struct AppState
{
    PerformPage page = PerformPage::Main;

    // UI “dirty flag” = something visible changed and we should redraw.
    bool ui_dirty = true;

    // Debug / proof-of-life counters for SPSC event queue.
    std::atomic<uint32_t> events_pushed{0};
    std::atomic<uint32_t> events_popped{0};
    std::atomic<uint32_t> queue_overflows{0};
    std::atomic<uint32_t> midi_rx_count{0};

    // Voice engine debug (written by audio thread, read by UI).
    std::atomic<uint32_t> voices_active{0};
    std::atomic<uint32_t> voices_peak_1s{0};
    std::atomic<uint32_t> voice_steals{0};
    // Packed {voice_idx, note, velocity} in low 24 bits.
    std::atomic<uint32_t> last_voice_packed{0};

    // Main-loop owned UI helpers (not accessed from audio thread).
    uint32_t last_input_ms = 0;
    uint32_t ctrl_hz       = 0;
};

#pragma once
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

    bool sdram_ok = false;
};

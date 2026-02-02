#pragma once

#include "app_state.h"
#include "params.h"
#include "daisy_pod.h"
#include "dev/oled_ssd130x.h"
#include "oled_pager.h"

// UI Render Layer:
// - Draw only.
// - Uses app.ui_dirty and a 30Hz timer.
// - Does NOT change params or read controls.

class UIRender
{
  public:
    using PodDisplay = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;

    void Init(PodDisplay* display, daisy::DaisyPod& hw);
    void Tick(AppState& app, const Params& params);
    void TickOledTransfer(uint32_t now_ms, bool midi_busy);

  private:
    OledPager   oled_pager_;
    uint32_t    last_ui_ms_ = 0;
    uint32_t    last_stats_ms_ = 0;
    uint32_t    last_events_pushed_   = 0;
    uint32_t    last_events_popped_   = 0;
    uint32_t    last_queue_overflows_ = 0;
    uint32_t    last_rx_mod_          = 0;
    uint32_t    last_voices_active_   = 0;
    uint32_t    last_voices_peak_1s_  = 0;
    uint32_t    last_voice_steals_    = 0;
    uint32_t    last_voice_packed_    = 0;

    static int ToPct01(float x);
    void Render(const AppState& app, const Params& params);
};

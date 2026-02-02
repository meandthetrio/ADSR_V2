#include "ui_render.h"
#include <cstdio>

using namespace daisy;

void UIRender::Init(PodDisplay* display, DaisyPod& hw)
{
    (void)display;
    oled_pager_.Init(hw);
    last_ui_ms_  = 0;
    last_stats_ms_ = 0;
    last_events_pushed_   = 0;
    last_events_popped_   = 0;
    last_queue_overflows_ = 0;
    last_rx_mod_          = 0;
    last_voices_active_   = 0;
    last_voices_peak_1s_  = 0;
    last_voice_steals_    = 0;
    last_voice_packed_    = 0;
}

int UIRender::ToPct01(float x)
{
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return (int)(x * 100.0f + 0.5f);
}

void UIRender::Render(const AppState& app, const Params& params)
{
    const uint32_t pushed = app.events_pushed.load(std::memory_order_relaxed);
    const uint32_t popped = app.events_popped.load(std::memory_order_relaxed);
    const uint32_t ovf    = app.queue_overflows.load(std::memory_order_relaxed);
    const uint32_t vact   = app.voices_active.load(std::memory_order_relaxed);
    const uint32_t vpk1s  = app.voices_peak_1s.load(std::memory_order_relaxed);
    const uint32_t vstl   = app.voice_steals.load(std::memory_order_relaxed);
    const uint32_t vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
    const auto&    t      = params.TargetsForUI();

    (void)vpack;

    oled_pager_.Fill(false);

    oled_pager_.SetCursor(0, 0);
    char buf[32];
    const uint32_t rx = app.midi_rx_count.load(std::memory_order_relaxed);
    const uint32_t rx_mod = rx % 1000;
    std::snprintf(buf,
                  sizeof(buf),
                  "ADSR V2 C:%lu RX:%03lu",
                  (unsigned long)app.ctrl_hz,
                  (unsigned long)rx_mod);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "ACT:%2lu PK:%2lu STL:%3lu",
                  (unsigned long)vact,
                  (unsigned long)vpk1s,
                  (unsigned long)vstl);
    oled_pager_.SetCursor(0, 8);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "ML T:%3d",
                  ToPct01(t.master_level));
    oled_pager_.SetCursor(0, 16);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "Dly T:%3d %s",
                  ToPct01(t.delay_mix),
                  t.delay_on ? "ON" : "OFF");
    oled_pager_.SetCursor(0, 24);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "Rev %s Sat:%3d %s",
                  t.reverb_on ? "ON" : "OFF",
                  ToPct01(t.sat_drive),
                  t.sat_on ? "ON" : "OFF");
    oled_pager_.SetCursor(0, 32);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "PUSH: %lu", (unsigned long)pushed);
    oled_pager_.SetCursor(0, 40);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "POP : %lu", (unsigned long)popped);
    oled_pager_.SetCursor(0, 48);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "OVF : %lu", (unsigned long)ovf);
    oled_pager_.SetCursor(0, 56);
    oled_pager_.WriteString(buf, Font_6x8, true);

}

void UIRender::TickOledTransfer(uint32_t now_ms, bool midi_busy)
{
    oled_pager_.TickTransferOnePage(now_ms, midi_busy);
}

void UIRender::Tick(AppState& app, const Params& params)
{
    if(!oled_pager_.IsReady())
        return;

    const uint32_t now_ms = System::GetNow();

    // 30Hz timer gate
    if((now_ms - last_ui_ms_) < 33)
        return;
    last_ui_ms_ = now_ms;

    uint32_t pushed = 0, popped = 0, ovf = 0, vact = 0, vstl = 0, vpack = 0;
    uint32_t rx_mod = last_rx_mod_;
    bool     stats_loaded = false;

    const bool stats_due = (now_ms - last_stats_ms_) >= 100;
    if(stats_due)
    {
        pushed = app.events_pushed.load(std::memory_order_relaxed);
        popped = app.events_popped.load(std::memory_order_relaxed);
        ovf    = app.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t rx = app.midi_rx_count.load(std::memory_order_relaxed);
        rx_mod = rx % 1000;
        vact   = app.voices_active.load(std::memory_order_relaxed);
        const uint32_t vpk1s = app.voices_peak_1s.load(std::memory_order_relaxed);
        vstl   = app.voice_steals.load(std::memory_order_relaxed);
        vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
        stats_loaded = true;

        if((pushed != last_events_pushed_) || (popped != last_events_popped_)
           || (ovf != last_queue_overflows_) || (rx_mod != last_rx_mod_)
           || (vact != last_voices_active_)
           || (vpk1s != last_voices_peak_1s_)
           || (vstl != last_voice_steals_) || (vpack != last_voice_packed_))
            app.ui_dirty = true;

        // Decimate stats-driven dirty marking to 10Hz max.
        last_stats_ms_ = now_ms;

        last_voices_peak_1s_ = vpk1s;
    }

    if(oled_pager_.IsTransferring())
        return;

    // Only draw if dirty
    if(!app.ui_dirty)
        return;

    Render(app, params);

    oled_pager_.BeginFrameTransfer();
    app.ui_dirty = false;

    // Update cached stats after any render (even if render was triggered by controls).
    if(!stats_loaded)
    {
        pushed = app.events_pushed.load(std::memory_order_relaxed);
        popped = app.events_popped.load(std::memory_order_relaxed);
        ovf    = app.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t rx = app.midi_rx_count.load(std::memory_order_relaxed);
        rx_mod = rx % 1000;
        vact   = app.voices_active.load(std::memory_order_relaxed);
        last_voices_peak_1s_ = app.voices_peak_1s.load(std::memory_order_relaxed);
        vstl   = app.voice_steals.load(std::memory_order_relaxed);
        vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
    }

    last_events_pushed_   = pushed;
    last_events_popped_   = popped;
    last_queue_overflows_ = ovf;
    last_rx_mod_          = rx_mod;
    last_voices_active_   = vact;
    last_voice_steals_    = vstl;
    last_voice_packed_    = vpack;
}

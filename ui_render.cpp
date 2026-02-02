#include "ui_render.h"
#include "build_config.h"
#include "mod_matrix.h"
#include "macros.h"
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
    last_stolen_voice_index_ = 0;
    last_stolen_start_id_    = 0;
    last_new_start_id_       = 0;
    last_cpu_pct_            = 0;
    last_audio_late_         = 0;
    last_loop_mode_          = 0;
    last_clip_count_         = 0;
    last_voices_active_   = 0;
    last_voices_peak_1s_  = 0;
    last_voice_steals_    = 0;
    last_voice_packed_    = 0;
    last_sample_index_    = 0;
    last_fadeouts_started_ = 0;
    last_vel_layer_       = 0;
    last_lfo_             = 0;
    last_env_             = 0;
    last_lfo_rate_dbg_    = 0;
    last_lfo_depth_dbg_   = 0;
}

int UIRender::ToPct01(float x)
{
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return (int)(x * 100.0f + 0.5f);
}

static char SrcChar(uint8_t src)
{
    return (src == static_cast<uint8_t>(ModSource::LFO)) ? 'L' : 'E';
}

static char DstChar(uint8_t dst)
{
    return (dst == static_cast<uint8_t>(ModDest::FilterCutoff)) ? 'C' : 'P';
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
    const uint32_t kg_idx = app.last_sample_index.load(std::memory_order_relaxed);
    const uint32_t fadeouts = app.fadeouts_started.load(std::memory_order_relaxed);
    const uint32_t vel_layer = app.last_vel_layer.load(std::memory_order_relaxed);
    const int32_t lfo_val = app.last_lfo.load(std::memory_order_relaxed);
    const int32_t env_val = app.last_env.load(std::memory_order_relaxed);
    const uint32_t lfo_rate_dbg = app.lfo_rate_dbg.load(std::memory_order_relaxed);
    const uint32_t lfo_depth_dbg = app.lfo_depth_dbg.load(std::memory_order_relaxed);
    const auto&    t      = params.TargetsForUI();
    const uint32_t lsv    = app.last_stolen_voice_index.load(std::memory_order_relaxed);
    const uint32_t old_id = app.last_stolen_start_id.load(std::memory_order_relaxed);
    const uint32_t new_id = app.last_new_start_id.load(std::memory_order_relaxed);
    const uint32_t peak_cycles   = app.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;
    const uint32_t late_cnt = app.audio_late_count.load(std::memory_order_relaxed);
    const uint32_t loop_mode = app.loop_mode.load(std::memory_order_relaxed);
    const uint32_t clip_cnt = app.clip_count.load(std::memory_order_relaxed);
    const char lp_char = (loop_mode == 0) ? 'F' : 'P';
    const float mix_scale = 0.7f / 10.0f;

    (void)vpack;
    (void)lsv;
    (void)old_id;
    (void)new_id;
    (void)pushed;
    (void)popped;
    (void)kg_idx;
    (void)vel_layer;
    (void)ovf;
    (void)clip_cnt;

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

    std::snprintf(buf, sizeof(buf), "ML:%3d SRC:SAMP INT",
                  ToPct01(t.master_level));
    oled_pager_.SetCursor(0, 16);
    oled_pager_.WriteString(buf, Font_6x8, true);

    const uint32_t lpf_hz = static_cast<uint32_t>(t.lpf_cutoff_hz + 0.5f);
    char lpf_buf[8];
    if(lpf_hz >= 1000)
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%2luk", (unsigned long)((lpf_hz + 500) / 1000));
    else
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%3lu", (unsigned long)lpf_hz);
    char mix_buf[8];
    std::snprintf(mix_buf, sizeof(mix_buf), "%.2f", mix_scale);

    std::snprintf(buf, sizeof(buf), "D:%3d LP:%s M:%s %c",
                  ToPct01(t.delay_mix),
                  lpf_buf,
                  mix_buf,
                  lp_char);
    oled_pager_.SetCursor(0, 24);
    oled_pager_.WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "CPU:%3lu L:%lu FO:%lu",
                  (unsigned long)cpu_pct,
                  (unsigned long)late_cnt,
                  (unsigned long)fadeouts);
    oled_pager_.SetCursor(0, 32);
    oled_pager_.WriteString(buf, Font_6x8, true);

    const uint8_t mac_sel = app.macro_ui.selected % kNumMacros;
    uint32_t mac_val = (uint32_t)(app.macro_ui.value[mac_sel] * 100.0f + 0.5f);
    if(mac_val > 100)
        mac_val = 100;
    std::snprintf(buf, sizeof(buf), "MAC:%u V:%03lu",
                  (unsigned)mac_sel,
                  (unsigned long)mac_val);
    oled_pager_.SetCursor(0, 40);
    oled_pager_.WriteString(buf, Font_6x8, true);

    const uint8_t step_idx = app.plock_pattern.step_index % kSteps;
    const StepLock& step_lock = app.plock_pattern.steps[step_idx];
    uint32_t cutoff_pct = (uint32_t)(step_lock.cutoff_norm * 100.0f + 0.5f);
    if(cutoff_pct > 100)
        cutoff_pct = 100;
    std::snprintf(buf, sizeof(buf), "ST:%02u LK:%u C:%03lu",
                  (unsigned)step_idx,
                  (unsigned)step_lock.enabled,
                  (unsigned long)cutoff_pct);
    oled_pager_.SetCursor(0, 48);
    oled_pager_.WriteString(buf, Font_6x8, true);

    const uint8_t r_idx = app.mod_route_selected % kMaxModRoutes;
    const ModRoute& r = app.mod_routes_ui[r_idx];
    int amt = (int)(r.amount * 100.0f);
    if(amt > 99) amt = 99;
    if(amt < -99) amt = -99;
    (void)lfo_val;
    (void)env_val;
    (void)lfo_rate_dbg;
    (void)lfo_depth_dbg;
    (void)ovf;
    (void)clip_cnt;
    std::snprintf(buf, sizeof(buf), "R%u S:%c D:%c A:%+03d E:%u",
                  (unsigned)r_idx,
                  SrcChar(r.src),
                  DstChar(r.dst),
                  amt,
                  (unsigned)r.enabled);
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
    uint32_t lsv = last_stolen_voice_index_;
    uint32_t old_id = last_stolen_start_id_;
    uint32_t new_id = last_new_start_id_;
    uint32_t cpu_pct = last_cpu_pct_;
    uint32_t late_cnt = last_audio_late_;
    uint32_t loop_mode = last_loop_mode_;
    uint32_t clip_cnt = last_clip_count_;
    uint32_t kg_idx = last_sample_index_;
    uint32_t fadeouts = last_fadeouts_started_;
    uint32_t vel_layer = last_vel_layer_;
    int32_t lfo_val = last_lfo_;
    int32_t env_val = last_env_;
    uint32_t lfo_rate_dbg = last_lfo_rate_dbg_;
    uint32_t lfo_depth_dbg = last_lfo_depth_dbg_;
    bool     stats_loaded = false;

    const bool stats_due = (now_ms - last_stats_ms_) >= 100;
    if(stats_due)
    {
        pushed = app.events_pushed.load(std::memory_order_relaxed);
        popped = app.events_popped.load(std::memory_order_relaxed);
        ovf    = app.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t rx = app.midi_rx_count.load(std::memory_order_relaxed);
        rx_mod = rx % 1000;
        lsv    = app.last_stolen_voice_index.load(std::memory_order_relaxed);
        old_id = app.last_stolen_start_id.load(std::memory_order_relaxed);
        new_id = app.last_new_start_id.load(std::memory_order_relaxed);
        const uint32_t peak_cycles = app.audio_cycles_peak.load(std::memory_order_relaxed);
        const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
        cpu_pct = 0;
        if(budget_cycles > 0)
            cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
        if(cpu_pct > 999u)
            cpu_pct = 999u;
        late_cnt = app.audio_late_count.load(std::memory_order_relaxed);
        loop_mode = app.loop_mode.load(std::memory_order_relaxed);
        clip_cnt = app.clip_count.load(std::memory_order_relaxed);
        vact   = app.voices_active.load(std::memory_order_relaxed);
        const uint32_t vpk1s = app.voices_peak_1s.load(std::memory_order_relaxed);
        vstl   = app.voice_steals.load(std::memory_order_relaxed);
        vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
        kg_idx = app.last_sample_index.load(std::memory_order_relaxed);
        fadeouts = app.fadeouts_started.load(std::memory_order_relaxed);
        vel_layer = app.last_vel_layer.load(std::memory_order_relaxed);
        lfo_val = app.last_lfo.load(std::memory_order_relaxed);
        env_val = app.last_env.load(std::memory_order_relaxed);
        lfo_rate_dbg = app.lfo_rate_dbg.load(std::memory_order_relaxed);
        lfo_depth_dbg = app.lfo_depth_dbg.load(std::memory_order_relaxed);
        stats_loaded = true;

        if((pushed != last_events_pushed_) || (popped != last_events_popped_)
           || (ovf != last_queue_overflows_) || (rx_mod != last_rx_mod_)
           || (lsv != last_stolen_voice_index_)
           || (old_id != last_stolen_start_id_)
           || (new_id != last_new_start_id_)
           || (cpu_pct != last_cpu_pct_)
           || (late_cnt != last_audio_late_)
           || (loop_mode != last_loop_mode_)
           || (clip_cnt != last_clip_count_)
           || (vact != last_voices_active_)
           || (vpk1s != last_voices_peak_1s_)
           || (vstl != last_voice_steals_) || (vpack != last_voice_packed_)
           || (kg_idx != last_sample_index_)
           || (fadeouts != last_fadeouts_started_)
           || (vel_layer != last_vel_layer_)
           || (lfo_val != last_lfo_)
           || (env_val != last_env_)
           || (lfo_rate_dbg != last_lfo_rate_dbg_)
           || (lfo_depth_dbg != last_lfo_depth_dbg_))
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
        lsv    = app.last_stolen_voice_index.load(std::memory_order_relaxed);
        old_id = app.last_stolen_start_id.load(std::memory_order_relaxed);
        new_id = app.last_new_start_id.load(std::memory_order_relaxed);
        const uint32_t peak_cycles = app.audio_cycles_peak.load(std::memory_order_relaxed);
        const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
        cpu_pct = 0;
        if(budget_cycles > 0)
            cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
        if(cpu_pct > 999u)
            cpu_pct = 999u;
        late_cnt = app.audio_late_count.load(std::memory_order_relaxed);
        loop_mode = app.loop_mode.load(std::memory_order_relaxed);
        clip_cnt = app.clip_count.load(std::memory_order_relaxed);
        vact   = app.voices_active.load(std::memory_order_relaxed);
        last_voices_peak_1s_ = app.voices_peak_1s.load(std::memory_order_relaxed);
        vstl   = app.voice_steals.load(std::memory_order_relaxed);
        vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
        kg_idx = app.last_sample_index.load(std::memory_order_relaxed);
        fadeouts = app.fadeouts_started.load(std::memory_order_relaxed);
        vel_layer = app.last_vel_layer.load(std::memory_order_relaxed);
        lfo_val = app.last_lfo.load(std::memory_order_relaxed);
        env_val = app.last_env.load(std::memory_order_relaxed);
        lfo_rate_dbg = app.lfo_rate_dbg.load(std::memory_order_relaxed);
        lfo_depth_dbg = app.lfo_depth_dbg.load(std::memory_order_relaxed);
    }

    last_events_pushed_   = pushed;
    last_events_popped_   = popped;
    last_queue_overflows_ = ovf;
    last_rx_mod_          = rx_mod;
    last_stolen_voice_index_ = lsv;
    last_stolen_start_id_    = old_id;
    last_new_start_id_       = new_id;
    last_cpu_pct_            = cpu_pct;
    last_audio_late_         = late_cnt;
    last_loop_mode_          = loop_mode;
    last_clip_count_         = clip_cnt;
    last_voices_active_   = vact;
    last_voice_steals_    = vstl;
    last_voice_packed_    = vpack;
    last_sample_index_    = kg_idx;
    last_fadeouts_started_ = fadeouts;
    last_vel_layer_       = vel_layer;
    last_lfo_             = lfo_val;
    last_env_             = env_val;
    last_lfo_rate_dbg_    = lfo_rate_dbg;
    last_lfo_depth_dbg_   = lfo_depth_dbg;
}

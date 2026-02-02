#include "ui_logic.h"
#include "keygroups.h"
#include "velocity_layers.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"
#include <atomic>
#include <cmath>

using namespace daisy;

float UILogic::Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static float ClampSigned(float x)
{
    if(x < -1.0f) return -1.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

void UILogic::Init(DaisyPod& hw)
{
    // External Encoder2 wiring:
    // A: D7, B: D8, Click: A7/D22 to GND (internal pull-up)
    ext_enc_.Init(hw.seed.GetPin(7),   // D7
                  hw.seed.GetPin(8),   // D8
                  hw.seed.GetPin(22)); // A7 / D22

    // Shift button wiring: D9 to GND (internal pull-up)
    // Explicit init (momentary, inverted polarity, pull-up)
    shift_btn_.Init(hw.seed.GetPin(9),
                    0.0f,
                    Switch::Type::TYPE_MOMENTARY,
                    Switch::Polarity::POLARITY_INVERTED,
                    Switch::Pull::PULL_UP);
}

void UILogic::ControlTick(DaisyPod& hw, AppState& app, Params& params, EventQueueSPSC& evtq)
{
    bool targets_changed = false;

    PerformParamsTargets& t = params.EditTargets();

    // Update external controls each tick
    ext_enc_.Debounce();
    shift_btn_.Debounce();

    const bool shift = shift_btn_.Pressed();
    const uint32_t now_ms = System::GetNow();
    bool input_detected = false;

    if(shift != last_shift_)
    {
        last_shift_ = shift;
        input_detected = true;
    }

    if(app.seq_last_ms == 0)
        app.seq_last_ms = now_ms;
    uint32_t seq_dt = now_ms - app.seq_last_ms;
    app.seq_last_ms = now_ms;
    if(app.seq_running)
    {
        app.seq_accum_ms += seq_dt;
        float step_ms_f = 15000.0f / (float)app.seq_bpm;
        if(step_ms_f < 1.0f)
            step_ms_f = 1.0f;
        const uint32_t step_ms = static_cast<uint32_t>(step_ms_f + 0.5f);
        while(app.seq_accum_ms >= step_ms)
        {
            app.seq_accum_ms -= step_ms;
            app.plock_pattern.step_index = (app.plock_pattern.step_index + 1) % kSteps;
            PLocks_PublishCurrentStep(app.plocks, app.plock_pattern);
            app.ui_dirty = true;
        }
    }

    // 1-second rolling peak of active voices (sampled at control rate).
    const uint32_t active_now = app.voices_active.load(std::memory_order_relaxed);
    if(peak_window_start_ms_ == 0)
    {
        peak_window_start_ms_ = now_ms;
        peak_active_          = active_now;
        app.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);
    }
    else
    {
        if(active_now > peak_active_)
            peak_active_ = active_now;

        // Keep OLED updated quickly for short bursts (e.g. stress test).
        app.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);

        if((now_ms - peak_window_start_ms_) >= 1000)
        {
            app.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);
            peak_window_start_ms_ = now_ms;
            peak_active_          = active_now;
        }
    }

    // Drain scheduled NoteOffs (no heap; fixed list)
    for(size_t i = 0; i < kMaxPendingNoteOffs; i++)
    {
        auto& slot = pending_note_offs_[i];
        if(!slot.active)
            continue;
        if((int32_t)(now_ms - slot.due_ms) < 0)
            continue;

        const Event evt = Event::NoteOffEvent(slot.note);
        if(evtq.Push(evt))
        {
            app.events_pushed.fetch_add(1, std::memory_order_relaxed);
            slot.active = false;
        }
        else
        {
            app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
            app.ui_dirty = true;
        }
    }

    const bool b1_rise = hw.button1.RisingEdge();
    const bool b1_fall = hw.button1.FallingEdge();
    const bool b2_rise = hw.button2.RisingEdge();
    const bool b2_fall = hw.button2.FallingEdge();
    if(b1_rise || b1_fall || b2_rise || b2_fall)
        input_detected = true;

    bool mod_changed = false;
    bool mod_sel_changed = false;

    // Pod buttons
    if(b1_rise)
    {
        if(shift)
        {
            ModRoute& r = app.mod_routes_ui[app.mod_route_selected % kMaxModRoutes];
            r.src = (r.src == static_cast<uint8_t>(ModSource::LFO))
                        ? static_cast<uint8_t>(ModSource::ModEnv)
                        : static_cast<uint8_t>(ModSource::LFO);
            mod_changed = true;
        }
        else
        {
            app.mod_route_selected = (app.mod_route_selected + 1) % kMaxModRoutes;
            mod_sel_changed = true;
        }
    }
    if(b2_rise)
    {
        if(shift)
        {
            const Event evt = Event::AllNotesOffEvent();
            if(evtq.Push(evt))
                app.events_pushed.fetch_add(1, std::memory_order_relaxed);
            else
            {
                app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                app.ui_dirty = true;
            }

            // Cancel any scheduled NoteOffs from stress test.
            for(size_t i = 0; i < kMaxPendingNoteOffs; i++)
                pending_note_offs_[i].active = false;
            app.ui_dirty = true;
        }
        else
        {
            ModRoute& r = app.mod_routes_ui[app.mod_route_selected % kMaxModRoutes];
            r.enabled = r.enabled ? 0 : 1;
            mod_changed = true;
        }
    }

    // Pod encoder click toggles sat_on
    const bool enc_click = hw.encoder.RisingEdge();
    if(enc_click)
    {
        input_detected = true;
        if(shift)
        {
            app.macro_ui.selected = (app.macro_ui.selected + 1) % kNumMacros;
            Macros_Publish(app, app.macro_ui);
            app.ui_dirty = true;
        }
        else
        {
            t.sat_on = !t.sat_on;
            targets_changed = true;
        }
    }

    // Pod encoder rotate: adjust mod amount (or macro value with shift)
    const int32_t pod_inc = hw.encoder.Increment();
    if(pod_inc != 0)
    {
        input_detected = true;
        if(shift)
        {
            if(hw.button1.Pressed())
            {
                ModRoute& r = app.mod_routes_ui[app.mod_route_selected % kMaxModRoutes];
                r.dst = (r.dst == static_cast<uint8_t>(ModDest::FilterCutoff))
                            ? static_cast<uint8_t>(ModDest::Pitch)
                            : static_cast<uint8_t>(ModDest::FilterCutoff);
                mod_changed = true;
            }
            else
            {
                const uint8_t sel = app.macro_ui.selected % kNumMacros;
                float v = app.macro_ui.value[sel];
                v = Clamp01(v + (float)pod_inc * enc_step_);
                app.macro_ui.value[sel] = v;
                Macros_Publish(app, app.macro_ui);
                app.ui_dirty = true;
            }
        }
        else
        {
            ModRoute& r = app.mod_routes_ui[app.mod_route_selected % kMaxModRoutes];
            r.amount = ClampSigned(r.amount + (float)pod_inc * enc_step_);
            mod_changed = true;
        }
    }

    // External encoder click toggles delay_on (proof-of-life)
    const bool ext_click = ext_enc_.RisingEdge();
    if(ext_click)
    {
        input_detected = true;
        if(shift)
        {
            const uint32_t cur = app.loop_mode.load(std::memory_order_relaxed);
            const uint32_t next = (cur == 0) ? 1 : 0;
            app.loop_mode.store(next, std::memory_order_relaxed);
            app.ui_dirty = true;
        }
        else
        {
            t.delay_on = !t.delay_on;
            targets_changed = true;
        }
    }

    // External encoder rotate adjusts delay_mix (proof-of-life)
    const int32_t ext_inc = ext_enc_.Increment();
    if(ext_inc != 0)
    {
        input_detected = true;
        if(shift)
        {
            const float ratio = kLpfMaxHz / kLpfMinHz;
            float cur_hz = t.lpf_cutoff_hz;
            if(cur_hz < kLpfMinHz) cur_hz = kLpfMinHz;
            if(cur_hz > kLpfMaxHz) cur_hz = kLpfMaxHz;
            float norm = std::log(cur_hz / kLpfMinHz) / std::log(ratio);
            norm = Clamp01(norm + (float)ext_inc * enc_step_);
            t.lpf_cutoff_hz = kLpfMinHz * std::pow(ratio, norm);
            targets_changed = true;
        }
        else
        {
            t.delay_mix = Clamp01(t.delay_mix + (float)ext_inc * enc_step_);
            targets_changed = true;
        }
    }

    if(targets_changed)
        params.PublishTargets();

    if(mod_changed)
    {
        ModMatrix_Publish(app.mod_matrix, app.mod_routes_ui);
        app.ui_dirty = true;
    }
    else if(mod_sel_changed)
    {
        app.ui_dirty = true;
    }

    if(targets_changed)
        app.ui_dirty = true;

    if(input_detected)
        app.last_input_ms = now_ms;
}

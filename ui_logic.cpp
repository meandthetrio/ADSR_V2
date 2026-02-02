#include "ui_logic.h"
#include <atomic>
#include <cmath>

using namespace daisy;

float UILogic::Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
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

    constexpr uint8_t kVel   = 100;

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

    // Pod buttons
    if(b1_rise)
    {
        if(shift)
        {
            const Event evt = Event::TestPingEvent(1);
            if(evtq.Push(evt))
            {
                app.events_pushed.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                app.ui_dirty = true;
            }
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
    }

    // Pod encoder click toggles sat_on
    const bool enc_click = hw.encoder.RisingEdge();
    if(enc_click)
    {
        input_detected = true;
        if(shift)
        {
            // Hold10 test: 10 notes (60..69) + 11th note (72) to force one steal.
            for(uint8_t n = 60; n <= 69; n++)
            {
                const Event on_evt = Event::NoteOnEvent(n, kVel);
                if(evtq.Push(on_evt))
                    app.events_pushed.fetch_add(1, std::memory_order_relaxed);
                else
                {
                    app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                    app.ui_dirty = true;
                }
            }

            const Event extra_evt = Event::NoteOnEvent(72, kVel);
            if(evtq.Push(extra_evt))
                app.events_pushed.fetch_add(1, std::memory_order_relaxed);
            else
            {
                app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                app.ui_dirty = true;
            }

            app.ui_dirty = true;
        }
        else
        {
            t.sat_on = !t.sat_on;
            targets_changed = true;
        }
    }

    // Pod encoder rotate: adjust sat_drive (or master_level with shift)
    const int32_t pod_inc = hw.encoder.Increment();
    if(pod_inc != 0)
    {
        input_detected = true;
        if(shift)
            t.master_level = Clamp01(t.master_level + (float)pod_inc * enc_step_);
        else
            t.sat_drive = Clamp01(t.sat_drive + (float)pod_inc * enc_step_);
        targets_changed = true;
    }

    // External encoder click toggles delay_on (proof-of-life)
    const bool ext_click = ext_enc_.RisingEdge();
    if(ext_click)
    {
        input_detected = true;
        t.delay_on = !t.delay_on;
        targets_changed = true;
    }

    // External encoder rotate adjusts delay_mix (proof-of-life)
    const int32_t ext_inc = ext_enc_.Increment();
    if(ext_inc != 0)
    {
        input_detected = true;
        t.delay_mix = Clamp01(t.delay_mix + (float)ext_inc * enc_step_);
        targets_changed = true;
    }

    if(targets_changed)
        params.PublishTargets();

    if(targets_changed)
        app.ui_dirty = true;

    if(input_detected)
        app.last_input_ms = now_ms;
}

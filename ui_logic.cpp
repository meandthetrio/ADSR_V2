#include "ui_logic.h"
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

void UILogic::ControlTick(DaisyPod& hw, AppState& app, Params& params)
{
    bool dirty = false;

    // Update external controls each tick
    ext_enc_.Debounce();
    shift_btn_.Debounce();

    const bool shift = shift_btn_.Pressed();

    // Pod buttons
    if(hw.button1.RisingEdge())
    {
        params.targets.delay_on = !params.targets.delay_on;
        dirty = true;
    }
    if(hw.button2.RisingEdge())
    {
        params.targets.reverb_on = !params.targets.reverb_on;
        dirty = true;
    }

    // Pod encoder click toggles sat_on
    if(hw.encoder.RisingEdge())
    {
        params.targets.sat_on = !params.targets.sat_on;
        dirty = true;
    }

    // Pod encoder rotate: adjust sat_drive (or master_level with shift)
    const int32_t pod_inc = hw.encoder.Increment();
    if(pod_inc != 0)
    {
        if(shift)
            params.targets.master_level = Clamp01(params.targets.master_level + (float)pod_inc * enc_step_);
        else
            params.targets.sat_drive = Clamp01(params.targets.sat_drive + (float)pod_inc * enc_step_);
        dirty = true;
    }

    // External encoder click toggles delay_on (proof-of-life)
    if(ext_enc_.RisingEdge())
    {
        params.targets.delay_on = !params.targets.delay_on;
        dirty = true;
    }

    // External encoder rotate adjusts delay_mix (proof-of-life)
    const int32_t ext_inc = ext_enc_.Increment();
    if(ext_inc != 0)
    {
        params.targets.delay_mix = Clamp01(params.targets.delay_mix + (float)ext_inc * enc_step_);
        dirty = true;
    }

    if(dirty)
        app.ui_dirty = true;
}

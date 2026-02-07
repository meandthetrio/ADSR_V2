#include "controls.h"
#include <cstdint>
#include <limits>

using namespace daisy;

static UiInputEvent MakeButtonEvent(UiInputType type, uint8_t id, uint32_t now_ms)
{
    UiInputEvent e{};
    e.type = type;
    e.id = id;
    e.value = 0;
    e.t_ms = now_ms;
    return e;
}

static UiInputEvent MakeEncEvent(uint8_t id, int16_t delta, uint32_t now_ms)
{
    UiInputEvent e{};
    e.type = UiInputType::EncDelta;
    e.id = id;
    e.value = delta;
    e.t_ms = now_ms;
    return e;
}

static void PushEvent(AppState& app, const UiInputEvent& e)
{
    if(UiInput_Push(app.ui_in, e))
    {
        app.ui_in_push++;
    }
    else
    {
        app.ui_in_ovf = UiInput_Dropped(app.ui_in);
    }
}

void Controls_Init(ControlsState& cs)
{
    if(!cs.hw)
        return;

    // External Encoder2 wiring:
    // A: D7, B: D8, Click: A7/D22 to GND (internal pull-up)
    cs.ext_enc.Init(cs.hw->seed.GetPin(7),
                    cs.hw->seed.GetPin(8),
                    cs.hw->seed.GetPin(22));

    // Shift button wiring: D9 to GND (internal pull-up)
    cs.shift_btn.Init(cs.hw->seed.GetPin(9),
                      0.0f,
                      Switch::Type::TYPE_MOMENTARY,
                      Switch::Polarity::POLARITY_INVERTED,
                      Switch::Pull::PULL_UP);
}

void Controls_Tick(ControlsState& cs, AppState& app, uint32_t now_ms)
{
    if(!cs.hw)
        return;

    cs.hw->ProcessDigitalControls();
    cs.ext_enc.Debounce();
    cs.shift_btn.Debounce();

    const bool b1_rise = cs.hw->button1.RisingEdge();
    const bool b1_fall = cs.hw->button1.FallingEdge();
    const bool b2_rise = cs.hw->button2.RisingEdge();
    const bool b2_fall = cs.hw->button2.FallingEdge();

    if(b1_rise)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnDown, kUiBtnPod1, now_ms));
    if(b1_fall)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnUp, kUiBtnPod1, now_ms));
    if(b2_rise)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnDown, kUiBtnPod2, now_ms));
    if(b2_fall)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnUp, kUiBtnPod2, now_ms));

    const bool shift_rise = cs.shift_btn.RisingEdge();
    const bool shift_fall = cs.shift_btn.FallingEdge();
    if(shift_rise)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnDown, kUiBtnShift, now_ms));
    if(shift_fall)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnUp, kUiBtnShift, now_ms));

    const bool enc_click = cs.hw->encoder.RisingEdge();
    if(enc_click)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnDown, kUiBtnPodEnc, now_ms));

    const int32_t pod_inc = cs.hw->encoder.Increment();
    if(pod_inc != 0)
    {
        const int32_t max_i16 = std::numeric_limits<int16_t>::max();
        const int32_t min_i16 = std::numeric_limits<int16_t>::min();
        const int16_t delta = (pod_inc > max_i16) ? static_cast<int16_t>(max_i16)
                           : (pod_inc < min_i16) ? static_cast<int16_t>(min_i16)
                                                 : static_cast<int16_t>(pod_inc);
        PushEvent(app, MakeEncEvent(kUiEncPod, delta, now_ms));
    }

    const bool ext_click = cs.ext_enc.RisingEdge();
    if(ext_click)
        PushEvent(app, MakeButtonEvent(UiInputType::BtnDown, kUiBtnExtEnc, now_ms));

    const int32_t ext_inc = cs.ext_enc.Increment();
    if(ext_inc != 0)
    {
        const int32_t max_i16 = std::numeric_limits<int16_t>::max();
        const int32_t min_i16 = std::numeric_limits<int16_t>::min();
        const int16_t delta = (ext_inc > max_i16) ? static_cast<int16_t>(max_i16)
                           : (ext_inc < min_i16) ? static_cast<int16_t>(min_i16)
                                                 : static_cast<int16_t>(ext_inc);
        PushEvent(app, MakeEncEvent(kUiEncExt, delta, now_ms));
    }

    app.ui_in_ovf = UiInput_Dropped(app.ui_in);
}

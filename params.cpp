#include "params.h"

void Params::Init()
{
    targets = PerformParamsTargets{};
    current = PerformParamsCurrent{};
}

float Params::SmoothToward(float current_v, float target_v, float coeff)
{
    // current += (target - current) * coeff
    return current_v + (target_v - current_v) * coeff;
}

void Params::ControlTick(float dt_sec)
{
    if(dt_sec < 0.0f)
        dt_sec = 0.0f;

    // Convert smoothing_hz_ into a stable coefficient for this dt.
    // coeff = 1 - exp(-2*pi*hz*dt)
    const float coeff = 1.0f - std::exp(-2.0f * 3.1415926f * smoothing_hz_ * dt_sec);

    // Smooth floats
    current.master_level = SmoothToward(current.master_level, targets.master_level, coeff);
    current.delay_mix    = SmoothToward(current.delay_mix, targets.delay_mix, coeff);
    current.reverb_mix   = SmoothToward(current.reverb_mix, targets.reverb_mix, coeff);
    current.sat_drive    = SmoothToward(current.sat_drive, targets.sat_drive, coeff);

    // Bools: snap immediately (no smoothing). This is fine for on/off.
    current.delay_on  = targets.delay_on;
    current.reverb_on = targets.reverb_on;
    current.sat_on    = targets.sat_on;
}
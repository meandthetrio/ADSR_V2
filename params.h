#pragma once
#include <cstdint>
#include <cmath>

// Step 4: Smoothing (control-rate).
// - UI writes only `targets`.
// - Audio reads only `current`.
// - ControlTick moves current -> targets gradually.

struct PerformParamsTargets
{
    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
};

struct PerformParamsCurrent
{
    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
};

class Params
{
  public:
    void Init();

    // Call at control-rate (main loop). Pass elapsed time since last call.
    // Using dt seconds makes smoothing stable even if loop timing changes.
    void ControlTick(float dt_sec);

    PerformParamsTargets targets;
    PerformParamsCurrent current;

  private:
    // “How quickly do we catch up?” Higher = snappier, lower = smoother.
    // Think of it like a spring strength.
    float smoothing_hz_ = 15.0f;

    // Helper: one-pole smoothing toward target
    static float SmoothToward(float current, float target, float coeff);
};
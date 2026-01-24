#pragma once
#include <cstdint>

// Step 3: Define the PERFORM parameter set.
// Rule: UI writes only `targets`.
// Rule: Audio reads only `current`.
// Step 4 will add smoothing in ControlTick() so current moves toward targets.

struct PerformParamsTargets
{
    // Master output level (linear gain)
    float master_level = 1.0f;

    // Master FX enables (used later for hard bypass gating)
    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;

    // Placeholder FX amounts (0..1). We’ll wire controls in Step 5.
    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
};

struct PerformParamsCurrent
{
    // Audio uses these values (smoothed later)
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

    // Called at control-rate (e.g. in the main loop). Step 4 will smooth.
    void ControlTick(float dt_sec);

    PerformParamsTargets targets;
    PerformParamsCurrent current;
};

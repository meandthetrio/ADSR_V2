#include "params.h"

void Params::Init()
{
    // Reset everything to defaults
    targets = PerformParamsTargets{};
    current = PerformParamsCurrent{};
}

void Params::ControlTick(float dt_sec)
{
    (void)dt_sec;
    // Step 3: keep current == targets for now (no smoothing yet).
    // Step 4 will change this to "move current toward targets gradually".
    current.master_level = targets.master_level;

    current.delay_on  = targets.delay_on;
    current.reverb_on = targets.reverb_on;
    current.sat_on    = targets.sat_on;

    current.delay_mix  = targets.delay_mix;
    current.reverb_mix = targets.reverb_mix;
    current.sat_drive  = targets.sat_drive;
}

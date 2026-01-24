#include "params.h"

void Params::Init()
{
    targets = PerformParamsTargets{};
    current = PerformParamsCurrent{};
}

void Params::ControlTick()
{
    // Step 4 will implement smoothing here.
}
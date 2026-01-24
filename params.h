#pragma once

// Step 2: minimal placeholder params so the layers compile.
// Step 3 will define real PERFORM parameters (targets/current) and structs.

struct PerformParamsTargets
{
    float dummy = 0.0f;
};

struct PerformParamsCurrent
{
    float dummy = 0.0f;
};

class Params
{
  public:
    void Init();
    void ControlTick(); // later: smoothing current -> targets

    PerformParamsTargets targets;
    PerformParamsCurrent current;
};
#pragma once
#include "app_state.h"
#include "params.h"

// Step 2: stub. Step 5 will read controls and write params.targets only.
class UILogic
{
  public:
    void Init();
    void ControlTick(AppState& app, Params& params);
};
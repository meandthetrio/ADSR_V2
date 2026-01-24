#pragma once
#include "app_state.h"
#include "params.h"

// Step 2: stub. Step 6 will draw and use dirty flag + timer tick scheduling.
class UIRender
{
  public:
    void Init();
    void RenderIfDirty(AppState& app, const Params& params);
};
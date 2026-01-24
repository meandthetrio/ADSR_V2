#include "ui_render.h"

void UIRender::Init() {}

void UIRender::RenderIfDirty(AppState& app, const Params& params)
{
    (void)params;

    // Step 2: we don't touch your existing OLED code yet.
    // We only clear the dirty flag so the plumbing compiles.
    if(app.ui_dirty)
        app.ui_dirty = false;
}
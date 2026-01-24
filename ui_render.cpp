#include "ui_render.h"
#include <cstdio>

using namespace daisy;

void UIRender::Init(PodDisplay* display)
{
    display_     = display;
    last_ui_ms_  = 0;
}

int UIRender::ToPct01(float x)
{
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return (int)(x * 100.0f + 0.5f);
}

void UIRender::Render(const AppState& app, const Params& params)
{
    (void)app;

    display_->Fill(false);

    display_->SetCursor(0, 0);
    display_->WriteString("PERFORM", Font_7x10, true);

    display_->SetCursor(0, 16);
    display_->WriteString("ADSR V2", Font_6x8, true);

    char buf[32];

    std::snprintf(buf, sizeof(buf), "ML T:%3d C:%3d",
                  ToPct01(params.targets.master_level),
                  ToPct01(params.current.master_level));
    display_->SetCursor(0, 28);
    display_->WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "Dly T:%3d C:%3d %s",
                  ToPct01(params.targets.delay_mix),
                  ToPct01(params.current.delay_mix),
                  params.targets.delay_on ? "ON" : "OFF");
    display_->SetCursor(0, 40);
    display_->WriteString(buf, Font_6x8, true);

    std::snprintf(buf, sizeof(buf), "Rev %s Sat:%3d %s",
                  params.targets.reverb_on ? "ON" : "OFF",
                  ToPct01(params.targets.sat_drive),
                  params.targets.sat_on ? "ON" : "OFF");
    display_->SetCursor(0, 52);
    display_->WriteString(buf, Font_6x8, true);

    display_->Update();
}

void UIRender::Tick(AppState& app, const Params& params)
{
    if(!display_)
        return;

    const uint32_t now_ms = System::GetNow();

    // 30Hz timer gate
    if((now_ms - last_ui_ms_) < 33)
        return;

    // Only draw if dirty
    if(!app.ui_dirty)
        return;

    last_ui_ms_ = now_ms;

    Render(app, params);

    // Clear dirty after drawing
    app.ui_dirty = false;
}

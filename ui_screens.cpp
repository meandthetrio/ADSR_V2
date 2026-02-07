#include "ui_screens.h"

#include "app_state.h"
#include "params.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "mod_matrix.h"
#include "macros.h"
#include "ui_requests.h"

#include <cmath>
#include <cstdio>

using namespace daisy;

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static float ClampSigned(float x)
{
    if(x < -1.0f) return -1.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static int ToPct01(float x)
{
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return static_cast<int>(x * 100.0f + 0.5f);
}

static char DstChar(uint8_t dst)
{
    return (dst == static_cast<uint8_t>(ModDest::FilterCutoff)) ? 'C' : 'P';
}

static char ReqChar(UiReqType type)
{
    switch(type)
    {
        case UiReqType::RebuildCache:
            return 'R';
        case UiReqType::LoadSample:
            return 'L';
        case UiReqType::SavePreset:
            return 'S';
        default:
            return '-';
    }
}

UiScreenId UiNav_Active(const UiNav& nav)
{
    return nav.stack[nav.top];
}

bool UiNav_Push(UiNav& nav, UiScreenId next)
{
    if(nav.top + 1 >= kUiStackMax)
        return false;
    nav.top++;
    nav.stack[nav.top] = next;
    return true;
}

bool UiNav_Pop(UiNav& nav)
{
    if(nav.top == 0)
        return false;
    nav.top--;
    return true;
}

static const UiMenuItem kHudMenuItems[] = {
    {"FX", UiScreenId::Fx, UiReqType::None},
    {"MOD", UiScreenId::Mod, UiReqType::None},
    {"MACRO", UiScreenId::Macro, UiReqType::None},
    {"REBUILD", UiScreenId::COUNT, UiReqType::RebuildCache},
    {"LOAD", UiScreenId::COUNT, UiReqType::LoadSample},
    {"SAVE", UiScreenId::COUNT, UiReqType::SavePreset},
};

static void EnsureHudMenu(AppState& app)
{
    if(app.hud_menu_inited)
        return;
    UiListMenu_Init(app.hud_menu,
                    kHudMenuItems,
                    static_cast<uint8_t>(sizeof(kHudMenuItems) / sizeof(kHudMenuItems[0])),
                    3);
    app.hud_menu_inited = true;
}

static bool Hud_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    EnsureHudMenu(*ctx.app);
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(ctx.app->hud_menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const UiMenuItem& item = ctx.app->hud_menu.items[ctx.app->hud_menu.cursor];
        if(item.req != UiReqType::None)
        {
            UiReq req{item.req, 0, 0};
            UiReq_Push(*ctx.app, req);
            ctx.app->ui_dirty = true;
            return true;
        }
        if(item.screen != UiScreenId::COUNT && UiNav_Push(ctx.app->ui_nav, item.screen))
            ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static void Hud_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const AppState& app = *ctx.app;
    EnsureHudMenu(*ctx.app);

    const uint32_t peak_cycles   = app.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;
    const uint32_t late_cnt = app.audio_late_count.load(std::memory_order_relaxed);

    const uint32_t ovf_mod = app.ui_in_ovf % 1000;
    uint32_t hi = app.ui_in_hi;
    if(hi > 99u)
        hi = 99u;
    const uint32_t rq_fill = UiReq_Fill(app);
    const uint32_t rq_ovf = app.ui_req_ovf % 1000;
    const char job_char = app.ui_req_busy ? ReqChar(app.ui_req_active) : '-';
    const uint32_t job_pct = app.ui_req_busy ? app.ui_req_progress : 0;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "HUD", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf,
                  sizeof(buf),
                  "U:%02lu C:%04lu CPU:%03lu",
                  (unsigned long)app.ui_hz,
                  (unsigned long)app.ctrl_hz,
                  (unsigned long)cpu_pct);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "LATE:%lu UIQO:%03lu H:%02lu",
                  (unsigned long)late_cnt,
                  (unsigned long)ovf_mod,
                  (unsigned long)hi);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "J:%c%03lu Q:%02lu O:%03lu",
                  job_char,
                  (unsigned long)job_pct,
                  (unsigned long)rq_fill,
                  (unsigned long)rq_ovf);
    d.WriteString(buf, Font_6x8, true);

    UiListMenu_Render(ctx.app->hud_menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * 3,
                      layout.line_h);

    UiDraw_Footer(d, layout, "EXT:SEL EXT:ENT P2:BACK");
}

static bool Fx_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app || !ctx.params)
        return false;

    static constexpr uint8_t kFxFieldCount = 4;
    static constexpr float kLpfMinHz = 80.0f;
    static constexpr float kLpfMaxHz = 12000.0f;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && !ctx.app->value_edit.active)
    {
        int next = static_cast<int>(ctx.app->fx_field_cursor) + e.value;
        while(next < 0) next += kFxFieldCount;
        while(next >= kFxFieldCount) next -= kFxFieldCount;
        ctx.app->fx_field_cursor = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!ctx.app->value_edit.active)
        {
            const auto& t = ctx.params->TargetsForUI();
            int16_t start_i = 0;
            UiValueSpec spec{};
            const char* label = "";
            switch(ctx.app->fx_field_cursor)
            {
                case 0: // Delay On
                    label = "DLY";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = t.delay_on ? 1 : 0;
                    break;
                case 1: // Delay Mix
                    label = "MIX";
                    spec = {UiValueType::Norm01, 1, 0, 100, nullptr, 0};
                    start_i = (int16_t)(Clamp01(t.delay_mix) * 100.0f + 0.5f);
                    break;
                case 2: // Sat On
                    label = "SAT";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = t.sat_on ? 1 : 0;
                    break;
                case 3: // LPF
                default:
                {
                    label = "LPF";
                    spec = {UiValueType::Norm01, 1, 0, 100, nullptr, 0};
                    float hz = t.lpf_cutoff_hz;
                    if(hz < kLpfMinHz) hz = kLpfMinHz;
                    if(hz > kLpfMaxHz) hz = kLpfMaxHz;
                    const float ratio = kLpfMaxHz / kLpfMinHz;
                    float norm = std::log(hz / kLpfMinHz) / std::log(ratio);
                    start_i = (int16_t)(Clamp01(norm) * 100.0f + 0.5f);
                    break;
                }
            }
            UiValueEdit_Begin(ctx.app->value_edit, label, spec, start_i);
            ctx.app->ui_dirty = true;
        }
        else
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const int16_t v = ctx.app->value_edit.value_i;
            switch(ctx.app->fx_field_cursor)
            {
                case 0:
                    t.delay_on = (v != 0);
                    break;
                case 1:
                    t.delay_mix = Clamp01((float)v / 100.0f);
                    break;
                case 2:
                    t.sat_on = (v != 0);
                    break;
                case 3:
                default:
                {
                    const float ratio = kLpfMaxHz / kLpfMinHz;
                    const float norm = Clamp01((float)v / 100.0f);
                    t.lpf_cutoff_hz = kLpfMinHz * std::pow(ratio, norm);
                    break;
                }
            }
            ctx.params->PublishTargets();
            UiValueEdit_Commit(ctx.app->value_edit);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ctx.app->value_edit.active)
    {
        if(UiValueEdit_OnEnc(ctx.app->value_edit, e.value))
            ctx.app->ui_dirty = true;
        return true;
    }

    if(ctx.app->value_edit.active)
        return true;

    return false;
}

static void Fx_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.params || !ctx.display)
        return;

    const auto& t = ctx.params->TargetsForUI();

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "FX", status);

    char buf[32];
    const uint32_t lpf_hz = static_cast<uint32_t>(t.lpf_cutoff_hz + 0.5f);
    char lpf_buf[12];
    if(lpf_hz >= 1000)
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%2luk", (unsigned long)((lpf_hz + 500) / 1000));
    else
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%3lu", (unsigned long)lpf_hz);

    const uint8_t cursor = ctx.app->fx_field_cursor;
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "%c DLY:%c", (cursor == 0) ? '>' : ' ',
                  t.delay_on ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "%c MIX:%03d", (cursor == 1) ? '>' : ' ',
                  ToPct01(t.delay_mix));
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "%c SAT:%c", (cursor == 2) ? '>' : ' ',
                  t.sat_on ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "%c LPF:%s", (cursor == 3) ? '>' : ' ',
                  lpf_buf);
    d.WriteString(buf, Font_6x8, true);

    UiValueEdit_Render(ctx.app->value_edit, d, layout.x, layout.y_body + layout.line_h * 4);

    const char* hint = ctx.app->value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                   : "EXT:MOVE EXT:EDIT P2:BACK";
    UiDraw_Footer(d, layout, hint);
}

static bool Mod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    static constexpr uint8_t kModFieldCount = 4;
    static const char* kRouteLabels[kMaxModRoutes] = {"R0", "R1", "R2", "R3"};
    static const char* kDstLabels[2] = {"CUT", "PIT"};

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && !ctx.app->value_edit.active)
    {
        int next = static_cast<int>(ctx.app->mod_field_cursor) + e.value;
        while(next < 0) next += kModFieldCount;
        while(next >= kModFieldCount) next -= kModFieldCount;
        ctx.app->mod_field_cursor = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!ctx.app->value_edit.active)
        {
            const uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
            const ModRoute& r = ctx.app->mod_routes_ui[r_idx];
            int16_t start_i = 0;
            UiValueSpec spec{};
            const char* label = "";
            switch(ctx.app->mod_field_cursor)
            {
                case 0: // Route Select
                    label = "ROUTE";
                    spec = {UiValueType::Enum, 1, 0, (int16_t)(kMaxModRoutes - 1),
                            kRouteLabels, (uint8_t)kMaxModRoutes};
                    start_i = (int16_t)r_idx;
                    break;
                case 1: // Enable
                    label = "EN";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = r.enabled ? 1 : 0;
                    break;
                case 2: // Amount
                    label = "AMT";
                    spec = {UiValueType::Bipolar1, 1, -100, 100, nullptr, 0};
                    start_i = (int16_t)(ClampSigned(r.amount) * 100.0f);
                    break;
                case 3: // Destination
                default:
                    label = "DST";
                    spec = {UiValueType::Enum, 1, 0, 1, kDstLabels, 2};
                    start_i = (int16_t)(r.dst ? 1 : 0);
                    break;
            }
            UiValueEdit_Begin(ctx.app->value_edit, label, spec, start_i);
            ctx.app->ui_dirty = true;
        }
        else
        {
            const int16_t v = ctx.app->value_edit.value_i;
            uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
            ModRoute& r = ctx.app->mod_routes_ui[r_idx];
            bool publish = false;

            switch(ctx.app->mod_field_cursor)
            {
                case 0:
                    ctx.app->mod_route_selected = (uint8_t)v % kMaxModRoutes;
                    break;
                case 1:
                    r.enabled = (v != 0) ? 1 : 0;
                    publish = true;
                    break;
                case 2:
                    r.amount = ClampSigned((float)v / 100.0f);
                    publish = true;
                    break;
                case 3:
                default:
                    r.dst = (v != 0) ? static_cast<uint8_t>(ModDest::Pitch)
                                     : static_cast<uint8_t>(ModDest::FilterCutoff);
                    publish = true;
                    break;
            }

            if(publish)
                ModMatrix_Publish(ctx.app->mod_matrix, ctx.app->mod_routes_ui);
            UiValueEdit_Commit(ctx.app->value_edit);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ctx.app->value_edit.active)
    {
        if(UiValueEdit_OnEnc(ctx.app->value_edit, e.value))
            ctx.app->ui_dirty = true;
        return true;
    }

    if(ctx.app->value_edit.active)
        return true;

    return false;
}

static void Mod_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
    const ModRoute& r = ctx.app->mod_routes_ui[r_idx];
    int amt = (int)(r.amount * 100.0f);
    if(amt > 99) amt = 99;
    if(amt < -99) amt = -99;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "MOD", status);

    char buf[32];
    const uint8_t cursor = ctx.app->mod_field_cursor;
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "%c R:%u",
                  (cursor == 0) ? '>' : ' ',
                  (unsigned)r_idx);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "%c EN:%c",
                  (cursor == 1) ? '>' : ' ',
                  r.enabled ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "%c AMT:%+03d",
                  (cursor == 2) ? '>' : ' ',
                  amt);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "%c DST:%c",
                  (cursor == 3) ? '>' : ' ',
                  DstChar(r.dst));
    d.WriteString(buf, Font_6x8, true);

    UiValueEdit_Render(ctx.app->value_edit, d, layout.x, layout.y_body + layout.line_h * 4);

    const char* hint = ctx.app->value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                   : "EXT:MOVE EXT:EDIT P2:BACK";
    UiDraw_Footer(d, layout, hint);
}

static bool Macro_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    bool changed = false;
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        const uint8_t sel = ctx.app->macro_ui.selected % kNumMacros;
        float v = ctx.app->macro_ui.value[sel];
        v = Clamp01(v + (float)e.value * 0.02f);
        ctx.app->macro_ui.value[sel] = v;
        Macros_Publish(*ctx.app, ctx.app->macro_ui);
        changed = true;
    }

    if(changed)
    {
        ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static void Macro_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const uint8_t sel = ctx.app->macro_ui.selected % kNumMacros;
    uint32_t mac_val = (uint32_t)(ctx.app->macro_ui.value[sel] * 100.0f + 0.5f);
    if(mac_val > 100)
        mac_val = 100;

    const char sel_char = (sel == 0) ? 'A' : 'B';

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "MAC", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "MAC %c V:%03lu",
                  sel_char,
                  (unsigned long)mac_val);
    d.WriteString(buf, Font_6x8, true);

    UiDraw_Footer(d, layout, "POD:VAL P2:BACK");
}

const UiScreen& GetScreen(UiScreenId id)
{
    static const UiScreen hud{UiScreenId::Hud, nullptr, nullptr, Hud_OnEvent, Hud_Render};
    static const UiScreen fx{UiScreenId::Fx, nullptr, nullptr, Fx_OnEvent, Fx_Render};
    static const UiScreen mod{UiScreenId::Mod, nullptr, nullptr, Mod_OnEvent, Mod_Render};
    static const UiScreen macro{UiScreenId::Macro, nullptr, nullptr, Macro_OnEvent, Macro_Render};

    switch(id)
    {
        case UiScreenId::Hud:
            return hud;
        case UiScreenId::Fx:
            return fx;
        case UiScreenId::Mod:
            return mod;
        case UiScreenId::Macro:
            return macro;
        default:
            return hud;
    }
}

void UiRouter_DispatchEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return;
    const UiScreen& s = GetScreen(UiNav_Active(ctx.app->ui_nav));
    if(s.OnEvent)
        s.OnEvent(ctx, e);
}

void UiRouter_Render(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    const UiScreen& s = GetScreen(UiNav_Active(ctx.app->ui_nav));
    if(s.Render)
        s.Render(ctx);
}

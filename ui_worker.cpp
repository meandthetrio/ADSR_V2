#include "ui_worker.h"

#include "app_state.h"
#include "ui_requests.h"

static volatile uint32_t s_work_sink = 0;

static uint32_t WorkUnitsForType(UiReqType type)
{
    switch(type)
    {
        case UiReqType::RebuildCache:
            return 2000;
        case UiReqType::LoadSample:
            return 800;
        case UiReqType::SavePreset:
            return 200;
        case UiReqType::None:
        default:
            return 0;
    }
}

void UiWorker_Tick(AppState& app, uint32_t now_ms, uint16_t budget_us)
{
    (void)now_ms;
    const uint8_t prev_progress = app.ui_req_progress;
    const bool prev_busy = app.ui_req_busy;
    const UiReqType prev_active = app.ui_req_active;

    if(!app.ui_req_busy)
    {
        UiReq r{};
        if(!UiReq_Pop(app, r))
            return;

        app.ui_req_busy = true;
        app.ui_req_active = r.type;
        app.ui_req_progress = 0;
        app.ui_req_result = 0;
        app.ui_req_arg0 = r.a;
        app.ui_req_work_units_done = 0;
        app.ui_req_work_units_total = WorkUnitsForType(r.type);

        if(app.ui_req_work_units_total == 0)
        {
            app.ui_req_busy = false;
            app.ui_req_active = UiReqType::None;
            app.ui_req_progress = 100;
            app.ui_req_result = 0;
            app.ui_req_done_count++;
            if(app.ui_req_progress != prev_progress || app.ui_req_busy != prev_busy
               || app.ui_req_active != prev_active)
                app.ui_dirty = true;
            return;
        }
    }

    if(!app.ui_req_busy)
        return;

    const uint32_t total = app.ui_req_work_units_total;
    uint32_t done = app.ui_req_work_units_done;
    if(done >= total)
    {
        app.ui_req_busy = false;
        app.ui_req_active = UiReqType::None;
        app.ui_req_progress = 100;
        app.ui_req_result = 0;
        app.ui_req_done_count++;
        if(app.ui_req_progress != prev_progress || app.ui_req_busy != prev_busy
           || app.ui_req_active != prev_active)
            app.ui_dirty = true;
        return;
    }

    uint32_t units_left = total - done;
    uint32_t units_to_do = units_left;
    const uint32_t max_units = (budget_us == 0) ? 1u : (uint32_t)budget_us;
    if(units_to_do > max_units)
        units_to_do = max_units;

    for(uint32_t i = 0; i < units_to_do; ++i)
        s_work_sink += (i + done);

    done += units_to_do;
    app.ui_req_work_units_done = done;
    uint32_t pct = (done * 100u) / total;
    if(pct > 100u)
        pct = 100u;
    app.ui_req_progress = static_cast<uint8_t>(pct);

    if(done >= total)
    {
        app.ui_req_busy = false;
        app.ui_req_active = UiReqType::None;
        app.ui_req_progress = 100;
        app.ui_req_result = 0;
        app.ui_req_done_count++;
    }

    if(app.ui_req_progress != prev_progress || app.ui_req_busy != prev_busy
       || app.ui_req_active != prev_active)
        app.ui_dirty = true;
}

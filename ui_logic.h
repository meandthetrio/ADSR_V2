#pragma once
#include "app_state.h"
#include "params.h"
#include "daisy_pod.h"
#include "event_queue.h"

class UILogic
{
  public:
    void Init(daisy::DaisyPod& hw);
    void ControlTick(daisy::DaisyPod& hw, AppState& app, Params& params, EventQueueSPSC& evtq);

  private:
    struct PendingNoteOff
    {
        bool     active = false;
        uint8_t  note   = 0;
        uint8_t  _pad0  = 0;
        uint16_t _pad1  = 0;
        uint32_t due_ms = 0;
    };
    static constexpr size_t kMaxPendingNoteOffs = 16;

    daisy::Encoder ext_enc_;
    daisy::Switch  shift_btn_;
    bool           last_shift_ = false;
    PendingNoteOff pending_note_offs_[kMaxPendingNoteOffs];
    uint32_t       peak_window_start_ms_ = 0;
    uint32_t       peak_active_          = 0;

    const float enc_step_ = 0.02f;
    static float Clamp01(float x);

    static constexpr float kLpfMinHz = 80.0f;
    static constexpr float kLpfMaxHz = 12000.0f;
};

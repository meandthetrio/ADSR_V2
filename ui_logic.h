#pragma once
#include "app_state.h"
#include "params.h"
#include "daisy_pod.h"

class UILogic
{
  public:
    void Init(daisy::DaisyPod& hw);
    void ControlTick(daisy::DaisyPod& hw, AppState& app, Params& params);

  private:
    daisy::Encoder ext_enc_;
    daisy::Switch  shift_btn_;

    const float enc_step_ = 0.02f;
    static float Clamp01(float x);
};
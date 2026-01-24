#pragma once
#include <cstddef>
#include "params.h"

// Audio Engine Layer:
// - Only audio processing.
// - Reads Params::current (never targets).
// - Implements hard bypass (DSP code not run when OFF).

class AudioEngine
{
  public:
    void Init(float sample_rate, size_t block_size);

    void ProcessBlock(const float* inL,
                      const float* inR,
                      float* outL,
                      float* outR,
                      size_t size,
                      const PerformParamsCurrent& p);

  private:
    float  sample_rate_ = 48000.0f;
    size_t block_size_  = 48;

    static inline float SoftClip(float x);
};
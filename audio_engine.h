#pragma once
#include <cstddef>

extern "C" int ADSR_V2_AUDIO_ENGINE_LINKED;

class AudioEngine
{
  public:
    void Init(float sample_rate, size_t block_size);

    // Stereo in/out, block-based processing.
    void ProcessBlock(const float* inL,
                      const float* inR,
                      float* outL,
                      float* outR,
                      size_t size);

  private:
    float sample_rate_ = 48000.0f;
    size_t block_size_ = 48;
};

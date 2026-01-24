#include "audio_engine.h"

int ADSR_V2_AUDIO_ENGINE_LINKED = 1;

void AudioEngine::Init(float sample_rate, size_t block_size)
{
    sample_rate_ = sample_rate;
    block_size_  = block_size;
}

void AudioEngine::ProcessBlock(const float* inL,
                               const float* inR,
                               float* outL,
                               float* outR,
                               size_t size)
{
    // Step 2: passthrough (keeps Step 1 behavior).
    for(size_t i = 0; i < size; i++)
    {
        outL[i] = inL[i];
        outR[i] = inR[i];
    }
}

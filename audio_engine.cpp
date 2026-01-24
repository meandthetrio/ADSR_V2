#include "audio_engine.h"
#include <cmath>

void AudioEngine::Init(float sample_rate, size_t block_size)
{
    sample_rate_ = sample_rate;
    block_size_  = block_size;
}

// Cheap soft clipper: x / (1 + |x|)
// (fast, stable, and sounds “saturate-y” without tanh)
inline float AudioEngine::SoftClip(float x)
{
    const float ax = std::fabs(x);
    return x / (1.0f + ax);
}

void AudioEngine::ProcessBlock(const float* inL,
                               const float* inR,
                               float* outL,
                               float* outR,
                               size_t size,
                               const PerformParamsCurrent& p)
{
    // Master level (0..1 in your UI), map to linear gain.
    // Keep it simple for now: 0..1 => 0..1 gain.
    const float level = p.master_level;

    // Saturation parameters
    const bool  sat_on = p.sat_on;
    const float drive  = p.sat_drive; // 0..1

    // Hard bypass: if sat is off OR drive is basically zero, do not run sat DSP.
    if(!sat_on || drive < 0.0001f)
    {
        for(size_t i = 0; i < size; i++)
        {
            outL[i] = inL[i] * level;
            outR[i] = inR[i] * level;
        }
        return;
    }

    // When ON: run sat DSP
    // Map drive 0..1 to a useful pre-gain (1..11)
    const float pre = 1.0f + drive * 10.0f;

    for(size_t i = 0; i < size; i++)
    {
        float l = inL[i] * pre;
        float r = inR[i] * pre;

        l = SoftClip(l);
        r = SoftClip(r);

        outL[i] = l * level;
        outR[i] = r * level;
    }
}
#include "voice_engine.h"

#include "mem_regions.h"

#include <cmath>
#include <cstring>

static constexpr float kTwoPi              = 6.2831853071795864769f;
static constexpr float kReleaseTauSec      = 0.030f;
static constexpr float kReleaseAmpEpsilon  = 1e-4f;
static constexpr float kVoiceAmpScale      = 0.15f; // keep headroom for 10 voices + FX
static constexpr float kStealXfadeSec      = 0.001f;

// Voice pool lives in fast RAM (DTCM). Uses `mem_regions.h` section macro.
ADSR2_SECTION(".dtcmram") static Voice g_voice_pool[VoiceEngine::kMaxVoices];

void VoiceEngine::BindDebug(std::atomic<uint32_t>* events_popped,
                            std::atomic<uint32_t>* voices_active,
                            std::atomic<uint32_t>* voice_steals,
                            std::atomic<uint32_t>* last_voice_packed,
                            std::atomic<uint32_t>* last_stolen_voice_index,
                            std::atomic<uint32_t>* last_stolen_start_id,
                            std::atomic<uint32_t>* last_new_start_id)
{
    events_popped_     = events_popped;
    voices_active_     = voices_active;
    voice_steals_      = voice_steals;
    last_voice_packed_ = last_voice_packed;
    last_stolen_voice_index_out_ = last_stolen_voice_index;
    last_stolen_start_id_out_    = last_stolen_start_id;
    last_new_start_id_out_       = last_new_start_id;
}

uint32_t VoiceEngine::PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel)
{
    return (uint32_t)idx | ((uint32_t)note << 8) | ((uint32_t)vel << 16);
}

float VoiceEngine::MidiNoteToFreq_(uint8_t note)
{
    // freq = 440 * 2^((note-69)/12)
    return 440.0f * std::pow(2.0f, ((int)note - 69) / 12.0f);
}

void VoiceEngine::Init(float sample_rate, size_t block_size)
{
    voices_      = g_voice_pool;
    sample_rate_ = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    block_size_  = (block_size > 0) ? block_size : 48;

    const float dt_block = (float)block_size_ / sample_rate_;
    block_release_coeff_ = std::exp(-dt_block / kReleaseTauSec);

    note_start_counter_ = 0;
    active_last_block_ = 0;
    steals_total_      = 0;
    last_stolen_voice_index_ = 0;
    last_stolen_start_id_    = 0;
    last_new_start_id_       = 0;

    for(size_t i = 0; i < kMaxVoices; i++)
    {
        voices_[i] = Voice{};
        voices_[i].release_coeff = block_release_coeff_;
    }

    if(voices_active_)
        voices_active_->store(0, std::memory_order_relaxed);
    if(last_stolen_voice_index_out_)
        last_stolen_voice_index_out_->store(last_stolen_voice_index_,
                                            std::memory_order_relaxed);
    if(last_stolen_start_id_out_)
        last_stolen_start_id_out_->store(last_stolen_start_id_,
                                         std::memory_order_relaxed);
    if(last_new_start_id_out_)
        last_new_start_id_out_->store(last_new_start_id_,
                                      std::memory_order_relaxed);
}

void VoiceEngine::StartVoice_(Voice& v, uint8_t note, uint8_t velocity, uint32_t start_id)
{
    v.state    = VoiceState::Playing;
    v.note     = note;
    v.velocity = velocity;
    v.start_id = start_id;

    v.phase = 0.0f;

    const float freq = MidiNoteToFreq_(note);
    v.phase_inc = (kTwoPi * freq) / sample_rate_;

    const float vel01 = (velocity > 127) ? 1.0f : ((float)velocity / 127.0f);
    v.amp = vel01 * kVoiceAmpScale;

    v.release_coeff = block_release_coeff_;
}

int VoiceEngine::AllocateVoice_(bool& stole,
                                uint8_t& stolen_index,
                                uint32_t& stolen_start_id)
{
    stole = false;
    stolen_index = 0;
    stolen_start_id = 0;

    // Prefer the first idle voice.
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(voices_[i].state == VoiceState::Idle)
            return (int)i;
    }

    // No free voices: steal Oldest Note (smallest `start_id`).
    size_t   best_idx = 0;
    uint32_t best_start_id = 0xFFFFFFFFu;
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(voices_[i].state != VoiceState::Idle
           && voices_[i].start_id < best_start_id)
        {
            best_start_id = voices_[i].start_id;
            best_idx = i;
        }
    }

    if(voice_steals_)
        voice_steals_->fetch_add(1, std::memory_order_relaxed);
    steals_total_++;

    stole = true;
    stolen_index    = static_cast<uint8_t>(best_idx);
    stolen_start_id = best_start_id;

    return (int)best_idx;
}

void VoiceEngine::AllNotesOff_()
{
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        voices_[i] = Voice{};
        voices_[i].release_coeff = block_release_coeff_;
    }

    active_last_block_ = 0;
    if(voices_active_)
        voices_active_->store(0, std::memory_order_relaxed);
}

void VoiceEngine::NoteOff_(uint8_t note)
{
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        Voice& v = voices_[i];
        if(v.note != note)
            continue;
        if(v.state == VoiceState::Playing)
        {
            v.state = VoiceState::Releasing;
        }
        else if(v.state == VoiceState::StealXFade)
        {
            v.phase     = v.new_phase;
            v.phase_inc = v.new_inc;
            v.amp       = v.new_amp;
            v.state     = VoiceState::Releasing;
        }
    }
}

void VoiceEngine::ProcessEvents(EventQueueSPSC& q)
{
    Event e;
    while(q.Pop(e))
    {
        if(events_popped_)
            events_popped_->fetch_add(1, std::memory_order_relaxed);

        switch(e.type)
        {
            case EventType::TestPing: break;
            case EventType::NoteOn:
            {
                const uint8_t note = e.note;
                const uint8_t vel  = e.velocity;

                bool     stole = false;
                uint8_t  stolen_index = 0;
                uint32_t stolen_start_id = 0;
                const int idx = AllocateVoice_(stole, stolen_index, stolen_start_id);
                if(idx >= 0)
                {
                    const uint32_t start_id = ++note_start_counter_;
                    Voice& v = voices_[(size_t)idx];
                    if(stole)
                    {
                        const float freq = MidiNoteToFreq_(note);
                        const float vel01 = (vel > 127) ? 1.0f : ((float)vel / 127.0f);

                        v.old_phase = v.phase;
                        v.old_inc   = v.phase_inc;
                        v.old_amp   = v.amp;

                        v.new_phase = 0.0f;
                        v.new_inc   = (kTwoPi * freq) / sample_rate_;
                        v.new_amp   = vel01 * kVoiceAmpScale;

                        int xfade_samples = (int)(sample_rate_ * kStealXfadeSec);
                        if(xfade_samples < 16)
                            xfade_samples = 16;
                        if(xfade_samples > 128)
                            xfade_samples = 128;

                        v.xfade_pos  = 0.0f;
                        v.xfade_step = 1.0f / (float)xfade_samples;

                        v.state    = VoiceState::StealXFade;
                        v.note     = note;
                        v.velocity = vel;
                        v.start_id = start_id;
                    }
                    else
                    {
                        StartVoice_(v, note, vel, start_id);
                    }

                    if(stole)
                    {
                        last_stolen_voice_index_ = stolen_index;
                        last_stolen_start_id_    = stolen_start_id;
                        last_new_start_id_       = start_id;
                        if(last_stolen_voice_index_out_)
                            last_stolen_voice_index_out_->store(last_stolen_voice_index_,
                                                                std::memory_order_relaxed);
                        if(last_stolen_start_id_out_)
                            last_stolen_start_id_out_->store(last_stolen_start_id_,
                                                             std::memory_order_relaxed);
                        if(last_new_start_id_out_)
                            last_new_start_id_out_->store(last_new_start_id_,
                                                          std::memory_order_relaxed);
                    }

                    if(last_voice_packed_)
                        last_voice_packed_->store(PackVoiceDebug_((uint8_t)idx, note, vel),
                                                  std::memory_order_relaxed);
                }
            }
            break;
            case EventType::NoteOff:
                NoteOff_(e.note);
                break;
            case EventType::AllNotesOff:
                AllNotesOff_();
                break;
        }
    }
}

void VoiceEngine::RenderBlock(float* outL, float* outR, size_t size)
{
    if(!outL || !outR || size == 0)
        return;

    if(size != block_size_)
    {
        block_size_ = size;
        const float dt_block = (float)block_size_ / sample_rate_;
        block_release_coeff_ = std::exp(-dt_block / kReleaseTauSec);
        for(size_t i = 0; i < kMaxVoices; i++)
            voices_[i].release_coeff = block_release_coeff_;
    }

    std::memset(outL, 0, sizeof(float) * size);
    std::memset(outR, 0, sizeof(float) * size);

    uint32_t active = 0;

    for(size_t vi = 0; vi < kMaxVoices; vi++)
    {
        Voice& v = voices_[vi];
        if(v.state == VoiceState::Idle)
            continue;

        if(v.state == VoiceState::Releasing)
        {
            v.amp *= v.release_coeff;
            if(v.amp < kReleaseAmpEpsilon)
            {
                v.amp   = 0.0f;
                v.state = VoiceState::Idle;
                continue;
            }
        }

        active++;

        if(v.state == VoiceState::StealXFade)
        {
            float old_phase = v.old_phase;
            float new_phase = v.new_phase;
            const float old_inc = v.old_inc;
            const float new_inc = v.new_inc;
            const float old_amp = v.old_amp;
            const float new_amp = v.new_amp;
            float x = v.xfade_pos;
            const float x_step = v.xfade_step;

            for(size_t i = 0; i < size; i++)
            {
                float x_clamped = x;
                if(x_clamped > 1.0f)
                    x_clamped = 1.0f;
                const float s_old = sinf(old_phase) * old_amp;
                const float s_new = sinf(new_phase) * new_amp;
                const float s     = s_old * (1.0f - x_clamped) + s_new * x_clamped;
                outL[i] += s;
                outR[i] += s;

                old_phase += old_inc;
                if(old_phase >= kTwoPi)
                    old_phase -= kTwoPi;
                new_phase += new_inc;
                if(new_phase >= kTwoPi)
                    new_phase -= kTwoPi;

                x += x_step;
            }

            v.old_phase  = old_phase;
            v.new_phase  = new_phase;
            v.xfade_pos  = x;

            if(v.xfade_pos >= 1.0f)
            {
                v.phase     = v.new_phase;
                v.phase_inc = v.new_inc;
                v.amp       = v.new_amp;
                v.state     = VoiceState::Playing;
            }
        }
        else
        {
            float phase = v.phase;
            const float inc   = v.phase_inc;
            const float amp   = v.amp;

            for(size_t i = 0; i < size; i++)
            {
                const float s = sinf(phase) * amp;
                outL[i] += s;
                outR[i] += s;

                phase += inc;
                if(phase >= kTwoPi)
                    phase -= kTwoPi;
            }

            v.phase = phase;
        }
    }

    if(voices_active_)
        voices_active_->store(active, std::memory_order_relaxed);
    active_last_block_ = active;
}

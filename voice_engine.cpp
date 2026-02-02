#include "voice_engine.h"

#include "mem_regions.h"

#include <cmath>
#include <cstring>

static constexpr float kReleaseTauSec      = 0.030f;
static constexpr float kReleaseAmpEpsilon  = 1e-4f;
static constexpr float kVoiceAmpScale      = 0.15f; // keep headroom for 10 voices + FX
static constexpr float kStealXfadeSec      = 0.001f;
static constexpr float kFadeInMs           = 3.0f;
static constexpr float kTwoPi              = 6.2831853071795864769f;
static constexpr float kEnvAttackSec       = 0.005f;
static constexpr float kEnvDecaySec        = 0.060f;
static constexpr float kEnvSustainLevel    = 0.70f;
static constexpr float kEnvReleaseSec      = 0.030f;
static constexpr float kMinRatio           = 0.25f;
static constexpr float kMaxRatio           = 4.0f;

// Voice pool lives in fast RAM (DTCM). Uses `mem_regions.h` section macro.
ADSR2_SECTION(".dtcmram") static Voice g_voice_pool[VoiceEngine::kMaxVoices];

static inline float ComputeRatio(uint8_t note, uint8_t root_key)
{
    const float semitones = static_cast<float>((int)note - (int)root_key);
    float ratio = std::pow(2.0f, semitones / 12.0f);
    if(ratio < kMinRatio)
        ratio = kMinRatio;
    if(ratio > kMaxRatio)
        ratio = kMaxRatio;
    return ratio;
}

static inline float ComputeFadeStep(float sample_rate)
{
    int fade_samples = static_cast<int>(sample_rate * 0.001f * kFadeInMs);
    if(fade_samples < 1)
        fade_samples = 1;
    return 1.0f / static_cast<float>(fade_samples);
}

static inline void InitEnvelope(EnvStage& stage,
                                float&    level,
                                float&    a_step,
                                float&    d_step,
                                float&    r_step,
                                float&    sustain,
                                float     sample_rate)
{
    int a_samps = static_cast<int>(kEnvAttackSec * sample_rate);
    int d_samps = static_cast<int>(kEnvDecaySec * sample_rate);
    int r_samps = static_cast<int>(kEnvReleaseSec * sample_rate);
    if(a_samps < 1) a_samps = 1;
    if(d_samps < 1) d_samps = 1;
    if(r_samps < 1) r_samps = 1;

    sustain = kEnvSustainLevel;
    stage   = EnvStage::Attack;
    level   = 0.0f;
    a_step  = 1.0f / static_cast<float>(a_samps);
    d_step  = (1.0f - sustain) / static_cast<float>(d_samps);
    r_step  = sustain / static_cast<float>(r_samps);
    if(r_step < 1e-6f)
        r_step = 1e-6f;
}

static inline void SetEnvelopeRelease(EnvStage& stage,
                                      float&    level,
                                      float&    r_step,
                                      float     sample_rate)
{
    int r_samps = static_cast<int>(kEnvReleaseSec * sample_rate);
    if(r_samps < 1) r_samps = 1;
    stage = EnvStage::Release;
    r_step = level / static_cast<float>(r_samps);
    if(r_step < 1e-6f)
        r_step = 1e-6f;
}

static inline void StepEnvelope(EnvStage& stage,
                                float&    level,
                                float     a_step,
                                float     d_step,
                                float     sustain,
                                float     r_step)
{
    switch(stage)
    {
        case EnvStage::Attack:
            level += a_step;
            if(level >= 1.0f)
            {
                level = 1.0f;
                stage = EnvStage::Decay;
            }
            break;
        case EnvStage::Decay:
            level -= d_step;
            if(level <= sustain)
            {
                level = sustain;
                stage = EnvStage::Sustain;
            }
            break;
        case EnvStage::Sustain:
            level = sustain;
            break;
        case EnvStage::Release:
            level -= r_step;
            if(level <= 0.0f)
            {
                level = 0.0f;
                stage = EnvStage::Off;
            }
            break;
        case EnvStage::Off:
        default:
            level = 0.0f;
            break;
    }
}

static inline bool AdvancePos(float& pos,
                              int8_t& dir,
                              float ratio,
                              float len,
                              float ls,
                              float le,
                              bool loop_enabled,
                              bool gate,
                              LoopMode mode)
{
    if(!loop_enabled || !gate || le <= ls || le > len)
    {
        pos += ratio;
        if(pos >= len)
            return false;
        return true;
    }

    if(mode == LoopMode::Forward)
    {
        pos += ratio;
        if(pos >= le)
            pos = ls + (pos - le);
    }
    else
    {
        pos += ratio * static_cast<float>(dir);
        if(dir > 0 && pos >= le)
        {
            pos = le - (pos - le);
            dir = -1;
        }
        else if(dir < 0 && pos <= ls)
        {
            pos = ls + (ls - pos);
            dir = 1;
        }
    }
    return true;
}

static inline float SampleAtLinear(const Sample* s, float pos, bool wrap_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    const uint32_t i = static_cast<uint32_t>(pos);
    if(i >= s->length)
        return 0.0f;
    const float frac = pos - static_cast<float>(i);
    const int16_t a = s->pcm[i];
    const int16_t b = (i + 1 < s->length) ? s->pcm[i + 1] : (wrap_end ? s->pcm[0] : a);
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + frac * (fb - fa);
}

void VoiceEngine::BindDebug(std::atomic<uint32_t>* events_popped,
                            std::atomic<uint32_t>* voices_active,
                            std::atomic<uint32_t>* voice_steals,
                            std::atomic<uint32_t>* last_voice_packed,
                            std::atomic<uint32_t>* last_stolen_voice_index,
                            std::atomic<uint32_t>* last_stolen_start_id,
                            std::atomic<uint32_t>* last_new_start_id,
                            std::atomic<uint32_t>* clip_count)
{
    events_popped_     = events_popped;
    voices_active_     = voices_active;
    voice_steals_      = voice_steals;
    last_voice_packed_ = last_voice_packed;
    last_stolen_voice_index_out_ = last_stolen_voice_index;
    last_stolen_start_id_out_    = last_stolen_start_id;
    last_new_start_id_out_       = last_new_start_id;
    clip_count_        = clip_count;
}

uint32_t VoiceEngine::PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel)
{
    return (uint32_t)idx | ((uint32_t)note << 8) | ((uint32_t)vel << 16);
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
    current_sample_          = nullptr;

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
    const Sample* sample = current_sample_;
    if(sample == nullptr || sample->pcm == nullptr || sample->length == 0)
    {
        v.state    = VoiceState::Idle;
        v.note     = note;
        v.velocity = velocity;
        v.start_id = start_id;
        return;
    }

    v.state    = VoiceState::Playing;
    v.note     = note;
    v.velocity = velocity;
    v.start_id = start_id;

    v.sample = sample;
    v.pos    = 0.0f;
    v.ratio  = ComputeRatio(note, sample->root_key);
    const float vel01 = (velocity > 127) ? 1.0f : ((float)velocity / 127.0f);
    v.gain = vel01 * kVoiceAmpScale;
    v.lpf_z = 0.0f;
    v.gate = true;
    v.dir  = 1;
    v.fade_in = 0.0f;
    v.fade_in_step = ComputeFadeStep(sample_rate_);
    InitEnvelope(v.env_stage,
                 v.env_level,
                 v.env_a_step,
                 v.env_d_step,
                 v.env_r_step,
                 v.env_sustain,
                 sample_rate_);

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
        if(v.state == VoiceState::Playing || v.state == VoiceState::Releasing)
        {
            v.state = VoiceState::Releasing;
            SetEnvelopeRelease(v.env_stage, v.env_level, v.env_r_step, sample_rate_);
            v.gate = false;
            v.dir  = 1;
        }
        else if(v.state == VoiceState::StealXFade)
        {
            v.pos   = v.new_pos;
            v.gain  = v.new_gain;
            v.ratio = v.new_ratio;
            v.fade_in = v.new_fade_in;
            v.fade_in_step = v.new_fade_in_step;
            SetEnvelopeRelease(v.new_env_stage, v.new_env_level, v.new_env_r_step, sample_rate_);
            v.new_gate = false;
            v.new_dir  = 1;
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
                        const Sample* sample = current_sample_;
                        if(sample == nullptr || sample->pcm == nullptr || sample->length == 0)
                        {
                            v.state    = VoiceState::Idle;
                            v.note     = note;
                            v.velocity = vel;
                            v.start_id = start_id;
                            continue;
                        }

                        const float vel01 = (vel > 127) ? 1.0f : ((float)vel / 127.0f);

                        v.sample  = sample;
                        v.old_pos = v.pos;
                        v.old_ratio = v.ratio;
                        const float old_fin = (v.fade_in < 1.0f) ? v.fade_in : 1.0f;
                        const float old_env = (v.env_level < 1.0f) ? v.env_level : 1.0f;
                        v.old_gain = v.gain * old_fin * old_env;
                        v.old_gate = v.gate;
                        v.old_dir  = v.dir;

                        v.new_pos = 0.0f;
                        v.new_ratio = ComputeRatio(note, sample->root_key);
                        v.new_gain = vel01 * kVoiceAmpScale;
                        v.new_fade_in = 0.0f;
                        v.new_fade_in_step = ComputeFadeStep(sample_rate_);
                        v.new_gate = true;
                        v.new_dir  = 1;
                        InitEnvelope(v.new_env_stage,
                                     v.new_env_level,
                                     v.new_env_a_step,
                                     v.new_env_d_step,
                                     v.new_env_r_step,
                                     v.new_env_sustain,
                                     sample_rate_);

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

    const LoopMode loop_mode = GetLoopMode();
    float cutoff_hz = lpf_cutoff_hz_;
    if(cutoff_hz < 20.0f)
        cutoff_hz = 20.0f;
    if(cutoff_hz > 20000.0f)
        cutoff_hz = 20000.0f;
    float lpf_g = 1.0f - std::exp(-kTwoPi * cutoff_hz / sample_rate_);
    if(lpf_g < 0.001f)
        lpf_g = 0.001f;
    else if(lpf_g > 0.999f)
        lpf_g = 0.999f;

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
    uint32_t clip_block = 0;
    const float mix_scale = 0.7f / static_cast<float>(VoiceEngine::kMaxVoices);

    for(size_t vi = 0; vi < kMaxVoices; vi++)
    {
        Voice& v = voices_[vi];
        if(v.state == VoiceState::Idle)
            continue;

        active++;

        if(v.state == VoiceState::StealXFade)
        {
            if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
            {
                v.state = VoiceState::Idle;
                continue;
            }

            float old_pos = v.old_pos;
            float new_pos = v.new_pos;
            const float length_f = static_cast<float>(v.sample->length);
            const float ls = static_cast<float>(v.sample->loop_start);
            const float le = static_cast<float>(v.sample->loop_end);
            const bool loop_enabled = v.sample->loop_enabled;
            if(loop_enabled)
            {
                if(old_pos >= length_f)
                    old_pos -= length_f;
                if(new_pos >= length_f)
                    new_pos -= length_f;
            }
            const float old_ratio = v.old_ratio;
            const float new_ratio = v.new_ratio;
            const float old_gain = v.old_gain;
            const float new_gain = v.new_gain;
            bool old_gate = v.old_gate;
            bool new_gate = v.new_gate;
            int8_t old_dir = v.old_dir;
            int8_t new_dir = v.new_dir;
            float x = v.xfade_pos;
            const float x_step = v.xfade_step;
            float new_fade = v.new_fade_in;
            const float new_fade_step = v.new_fade_in_step;
            EnvStage new_env_stage = v.new_env_stage;
            float new_env_level = v.new_env_level;
            const float new_env_a_step = v.new_env_a_step;
            const float new_env_d_step = v.new_env_d_step;
            float new_env_r_step = v.new_env_r_step;
            const float new_env_sustain = v.new_env_sustain;
            float lpf_z = v.lpf_z;

            for(size_t i = 0; i < size; i++)
            {
                float x_clamped = x;
                if(x_clamped > 1.0f)
                    x_clamped = 1.0f;

                const bool old_wrap = (loop_enabled && old_gate
                                       && v.sample->loop_start == 0
                                       && v.sample->loop_end == v.sample->length);
                const bool new_wrap = (loop_enabled && new_gate
                                       && v.sample->loop_start == 0
                                       && v.sample->loop_end == v.sample->length);
                const float s_old = SampleAtLinear(v.sample, old_pos, old_wrap) * old_gain;
                float s_new = SampleAtLinear(v.sample, new_pos, new_wrap) * new_gain;
                s_new *= new_env_level;
                const float fin_new = (new_fade < 1.0f) ? new_fade : 1.0f;
                s_new *= fin_new;

                const float s     = s_old * (1.0f - x_clamped) + s_new * x_clamped;
                lpf_z += lpf_g * (s - lpf_z);
                outL[i] += lpf_z;
                outR[i] += lpf_z;

                if(!AdvancePos(old_pos,
                               old_dir,
                               old_ratio,
                               length_f,
                               ls,
                               le,
                               loop_enabled,
                               old_gate,
                               loop_mode))
                {
                    old_gate = false;
                }
                if(!AdvancePos(new_pos,
                               new_dir,
                               new_ratio,
                               length_f,
                               ls,
                               le,
                               loop_enabled,
                               new_gate,
                               loop_mode))
                {
                    new_env_stage = EnvStage::Off;
                    new_env_level = 0.0f;
                    new_gate = false;
                }

                x += x_step;
                if(new_fade < 1.0f)
                {
                    new_fade += new_fade_step;
                    if(new_fade > 1.0f)
                        new_fade = 1.0f;
                }

                StepEnvelope(new_env_stage,
                             new_env_level,
                             new_env_a_step,
                             new_env_d_step,
                             new_env_sustain,
                             new_env_r_step);
                if(new_env_stage == EnvStage::Off)
                    new_env_level = 0.0f;
            }

            v.old_pos   = old_pos;
            v.new_pos   = new_pos;
            v.xfade_pos  = x;
            v.new_fade_in = new_fade;
            v.new_env_stage = new_env_stage;
            v.new_env_level = new_env_level;
            v.new_env_r_step = new_env_r_step;
            v.old_gate = old_gate;
            v.old_dir  = old_dir;
            v.new_gate = new_gate;
            v.new_dir  = new_dir;
            v.lpf_z    = lpf_z;

            if(v.xfade_pos >= 1.0f)
            {
                v.pos  = v.new_pos;
                v.gain = v.new_gain;
                v.ratio = v.new_ratio;
                v.fade_in = v.new_fade_in;
                v.fade_in_step = v.new_fade_in_step;
                v.env_stage = v.new_env_stage;
                v.env_level = v.new_env_level;
                v.env_a_step = v.new_env_a_step;
                v.env_d_step = v.new_env_d_step;
                v.env_r_step = v.new_env_r_step;
                v.env_sustain = v.new_env_sustain;
                v.gate = v.new_gate;
                v.dir  = v.new_dir;
                if(v.env_stage == EnvStage::Off)
                    v.state = VoiceState::Idle;
                else if(v.env_stage == EnvStage::Release)
                    v.state = VoiceState::Releasing;
                else
                    v.state = VoiceState::Playing;
            }
        }
        else
        {
            if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
            {
                v.state = VoiceState::Idle;
                continue;
            }

            float pos = v.pos;
            const float ratio = v.ratio;
            const float gain  = v.gain;
            const float length_f = static_cast<float>(v.sample->length);
            const float ls = static_cast<float>(v.sample->loop_start);
            const float le = static_cast<float>(v.sample->loop_end);
            const bool loop_enabled = v.sample->loop_enabled;
            bool gate = v.gate;
            int8_t dir = v.dir;
            if(loop_enabled && gate && pos >= length_f)
                pos -= length_f;
            float fade = v.fade_in;
            const float fade_step = v.fade_in_step;
            EnvStage env_stage = v.env_stage;
            float env_level = v.env_level;
            const float env_a_step = v.env_a_step;
            const float env_d_step = v.env_d_step;
            float env_r_step = v.env_r_step;
            const float env_sustain = v.env_sustain;
            float lpf_z = v.lpf_z;

            for(size_t i = 0; i < size; i++)
            {
                const bool wrap_end = (loop_enabled && gate
                                       && v.sample->loop_start == 0
                                       && v.sample->loop_end == v.sample->length);
                float s = SampleAtLinear(v.sample, pos, wrap_end) * gain;
                s *= env_level;
                const float fin = (fade < 1.0f) ? fade : 1.0f;
                s *= fin;
                lpf_z += lpf_g * (s - lpf_z);
                outL[i] += lpf_z;
                outR[i] += lpf_z;

                if(!AdvancePos(pos,
                               dir,
                               ratio,
                               length_f,
                               ls,
                               le,
                               loop_enabled,
                               gate,
                               loop_mode))
                {
                    v.state = VoiceState::Idle;
                    break;
                }
                if(fade < 1.0f)
                {
                    fade += fade_step;
                    if(fade > 1.0f)
                        fade = 1.0f;
                }

                StepEnvelope(env_stage,
                             env_level,
                             env_a_step,
                             env_d_step,
                             env_sustain,
                             env_r_step);
                if(env_stage == EnvStage::Off)
                {
                    v.state = VoiceState::Idle;
                    break;
                }
            }

            if(v.state != VoiceState::Idle)
            {
                v.pos = pos;
                v.fade_in = fade;
                v.env_stage = env_stage;
                v.env_level = env_level;
                v.env_r_step = env_r_step;
                v.gate = gate;
                v.dir  = dir;
                v.lpf_z = lpf_z;
            }
        }
    }

    for(size_t i = 0; i < size; i++)
    {
        float mix = outL[i] * mix_scale;
        outL[i] = mix;
        outR[i] = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }

    if(clip_count_ && clip_block > 0)
        clip_count_->fetch_add(clip_block, std::memory_order_relaxed);

    if(voices_active_)
        voices_active_->store(active, std::memory_order_relaxed);
    active_last_block_ = active;
}

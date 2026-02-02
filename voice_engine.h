#pragma once

// `voice_engine.*` implements a fixed-size voice pool that runs entirely in the audio thread.
// It is driven by `EventQueueSPSC` note events (NoteOn/NoteOff) and renders PCM sample playback.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "event_queue.h"
#include "sampler_sample.h"

enum class VoiceState : uint8_t
{
    Idle = 0,
    Playing,
    Releasing,
    StealXFade,
};

enum class EnvStage : uint8_t
{
    Off = 0,
    Attack,
    Decay,
    Sustain,
    Release,
};

enum class LoopMode : uint8_t
{
    Forward = 0,
    PingPong,
};

struct Voice
{
    VoiceState state         = VoiceState::Idle;
    EnvStage   env_stage     = EnvStage::Off;
    uint8_t    note          = 0;
    uint8_t    velocity      = 0;
    uint8_t    _pad0         = 0;
    uint32_t   start_id      = 0; // monotonic allocation id (used for Oldest Note stealing)

    const Sample* sample = nullptr;
    float         pos    = 0.0f; // playback position in samples
    float         ratio  = 1.0f; // playback increment per output sample
    float         gain   = 0.0f; // 0..1
    float         lpf_z  = 0.0f; // 1-pole LPF state
    float         fade_in = 0.0f; // 0..1
    float         fade_in_step = 0.0f; // per-sample increment
    float         release_coeff = 1.0f; // per-block exp decay (30ms)
    float         env_level  = 0.0f; // 0..1
    float         env_a_step = 0.0f;
    float         env_d_step = 0.0f;
    float         env_r_step = 0.0f;
    float         env_sustain = 0.70f;
    bool          gate = false;
    int8_t        dir  = 1;

    float old_pos   = 0.0f;
    float old_ratio = 1.0f;
    float old_gain  = 0.0f;
    bool  old_gate  = false;
    int8_t old_dir  = 1;
    float new_pos   = 0.0f;
    float new_ratio = 1.0f;
    float new_gain  = 0.0f;
    float new_fade_in = 0.0f; // 0..1 for new head
    float new_fade_in_step = 0.0f; // per-sample increment for new head
    EnvStage new_env_stage = EnvStage::Off;
    float    new_env_level = 0.0f;
    float    new_env_a_step = 0.0f;
    float    new_env_d_step = 0.0f;
    float    new_env_r_step = 0.0f;
    float    new_env_sustain = 0.70f;
    bool     new_gate = false;
    int8_t   new_dir  = 1;
    float xfade_pos  = 0.0f; // 0..1
    float xfade_step = 0.0f; // per-sample increment
};

class VoiceEngine
{
  public:
    static constexpr size_t kMaxVoices = 10;

    void Init(float sample_rate, size_t block_size);

    // AUDIO THREAD ONLY: pops and handles all pending events for this block.
    void ProcessEvents(EventQueueSPSC& q);

    // AUDIO THREAD ONLY: clears and fills `outL/outR` with summed voice audio.
    void RenderBlock(float* outL, float* outR, size_t size);

    // Optional debug bindings:
    // - `events_popped`: incremented once per event popped from the queue
    // - `voices_active`: set once per block to active voice count
    // - `voice_steals`: incremented when allocating with no free voice
    // - `last_voice_packed`: packed {idx, note, vel} in low 24 bits for OLED
    void BindDebug(std::atomic<uint32_t>* events_popped,
                   std::atomic<uint32_t>* voices_active,
                   std::atomic<uint32_t>* voice_steals,
                   std::atomic<uint32_t>* last_voice_packed,
                   std::atomic<uint32_t>* last_stolen_voice_index,
                   std::atomic<uint32_t>* last_stolen_start_id,
                   std::atomic<uint32_t>* last_new_start_id,
                   std::atomic<uint32_t>* clip_count);

    void SetSample(const Sample* sample) { current_sample_ = sample; }
    void SetLpfCutoff(float hz) { lpf_cutoff_hz_ = hz; }
    void SetLoopMode(LoopMode mode)
    {
        loop_mode_.store(static_cast<uint8_t>(mode), std::memory_order_relaxed);
    }
    LoopMode GetLoopMode() const
    {
        return static_cast<LoopMode>(loop_mode_.load(std::memory_order_relaxed));
    }

    // AUDIO THREAD ONLY: last block counters (use AppState atomics for UI).
    uint32_t ActiveLastBlock() const { return active_last_block_; }
    uint32_t StealsTotal() const { return steals_total_; }

  private:
    Voice*  voices_     = nullptr;
    float   sample_rate_ = 48000.0f;
    size_t  block_size_  = 48;
    float   block_release_coeff_ = 1.0f;
    uint32_t note_start_counter_ = 0;
    uint32_t active_last_block_ = 0;
    uint32_t steals_total_      = 0;
    uint32_t last_stolen_voice_index_ = 0;
    uint32_t last_stolen_start_id_    = 0;
    uint32_t last_new_start_id_       = 0;
    const Sample* current_sample_     = nullptr;
    float lpf_cutoff_hz_              = 12000.0f;
    std::atomic<uint8_t> loop_mode_{static_cast<uint8_t>(LoopMode::Forward)};

    std::atomic<uint32_t>* events_popped_      = nullptr;
    std::atomic<uint32_t>* voices_active_      = nullptr;
    std::atomic<uint32_t>* voice_steals_       = nullptr;
    std::atomic<uint32_t>* last_voice_packed_  = nullptr;
    std::atomic<uint32_t>* last_stolen_voice_index_out_ = nullptr;
    std::atomic<uint32_t>* last_stolen_start_id_out_    = nullptr;
    std::atomic<uint32_t>* last_new_start_id_out_       = nullptr;
    std::atomic<uint32_t>* clip_count_         = nullptr;

    int  AllocateVoice_(bool& stole,
                        uint8_t& stolen_index,
                        uint32_t& stolen_start_id);
    void StartVoice_(Voice& v, uint8_t note, uint8_t velocity, uint32_t start_id);
    void NoteOff_(uint8_t note);
    void AllNotesOff_();

    static uint32_t PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel);
};

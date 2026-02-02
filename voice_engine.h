#pragma once

// `voice_engine.*` implements a fixed-size voice pool that runs entirely in the audio thread.
// It is driven by `EventQueueSPSC` note events (NoteOn/NoteOff) and renders simple sine tones.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "event_queue.h"

enum class VoiceState : uint8_t
{
    Idle = 0,
    Playing,
    Releasing,
};

struct Voice
{
    VoiceState state         = VoiceState::Idle;
    uint8_t    note          = 0;
    uint8_t    velocity      = 0;
    uint8_t    _pad0         = 0;
    uint32_t   age           = 0; // allocation timestamp (used for Oldest Note stealing)

    float phase         = 0.0f; // 0..2pi
    float phase_inc     = 0.0f; // radians/sample
    float amp           = 0.0f; // 0..1
    float release_coeff = 1.0f; // per-block exp decay (30ms)
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
                   std::atomic<uint32_t>* last_voice_packed);

    // AUDIO THREAD ONLY: last block counters (use AppState atomics for UI).
    uint32_t ActiveLastBlock() const { return active_last_block_; }
    uint32_t StealsTotal() const { return steals_total_; }

  private:
    Voice*  voices_     = nullptr;
    float   sample_rate_ = 48000.0f;
    size_t  block_size_  = 48;
    float   block_release_coeff_ = 1.0f;
    uint32_t age_counter_ = 0;
    uint32_t active_last_block_ = 0;
    uint32_t steals_total_      = 0;

    std::atomic<uint32_t>* events_popped_      = nullptr;
    std::atomic<uint32_t>* voices_active_      = nullptr;
    std::atomic<uint32_t>* voice_steals_       = nullptr;
    std::atomic<uint32_t>* last_voice_packed_  = nullptr;

    int  AllocateVoice_(uint8_t note, uint8_t velocity);
    void StartVoice_(Voice& v, uint8_t note, uint8_t velocity, uint32_t age);
    void NoteOff_(uint8_t note);
    void AllNotesOff_();

    static float MidiNoteToFreq_(uint8_t note);
    static uint32_t PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel);
};

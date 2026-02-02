#include "daisy_pod.h"
#include "daisy_core.h"

// OLED driver (SSD130x family) used in libDaisy examples
#include "dev/oled_ssd130x.h"
#include "hid/midi.h"

// --- NEW: layered headers ---
#include "app_state.h"
#include "params.h"
#include "audio_engine.h"
#include "ui_logic.h"
#include "ui_render.h"
#include "event_queue.h"
#include "voice_engine.h"

using namespace daisy;

// --- Hardware ---
static DaisyPod hw;

// --- OLED Types ---
using PodDisplay = OledDisplay<SSD130xI2c128x64Driver>;
static PodDisplay display;

// --- UI timing ---
static uint32_t last_ms = 0;
static int32_t  ctrl_accum_ms = 0;
static float    g_sample_rate_hz = 48000.0f;
static uint32_t ctrl_ticks_accum = 0;
static uint32_t ctrl_window_start_ms = 0;

// --- NEW: layered globals ---
static AppState g_app;
static Params g_params;
static AudioEngine g_audio;
static UILogic     g_ui;
static UIRender    g_render;
static VoiceEngine g_voice;

// --- Event queue (MAIN -> AUDIO) ---
static EventQueueSPSC g_evtq;

// --- Audio: passthrough (now via AudioEngine) ---
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    (void)in;

    g_params.AudioBlockTick(g_sample_rate_hz, size);

    g_voice.ProcessEvents(g_evtq);
    g_voice.RenderBlock(out[0], out[1], size);

    // FX / master level after voices (in-place).
    g_audio.ProcessBlock(out[0], out[1], out[0], out[1], size, g_params.current);
}

static void InitOled()
{
    PodDisplay::Config disp_cfg;

    // Configure SSD130x I2C transport (this matches this libDaisy header)
    auto& tcfg = disp_cfg.driver_config.transport_config;

    tcfg.i2c_address       = 0x3C; // common SSD1306/SSD1309 address
    tcfg.i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    tcfg.i2c_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    tcfg.i2c_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;

    // Daisy Pod wiring (per hardware map): SCL = D11, SDA = D12
    tcfg.i2c_config.pin_config.scl = hw.seed.GetPin(11); // D11
    tcfg.i2c_config.pin_config.sda = hw.seed.GetPin(12); // D12

    display.Init(disp_cfg);

    display.Fill(false);
    display.Update();
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);
    g_sample_rate_hz = hw.AudioSampleRate();

    InitOled();

    // --- NEW: init layered stubs ---
    g_params.Init();
    g_audio.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    g_ui.Init(hw);
    g_render.Init(&display, hw);
    hw.midi.StartReceive();
    g_voice.Init(g_sample_rate_hz, hw.AudioBlockSize());
    g_voice.BindDebug(&g_app.events_popped,
                      &g_app.voices_active,
                      &g_app.voice_steals,
                      &g_app.last_voice_packed);

    hw.StartAudio(AudioCallback);

    last_ms = System::GetNow();

    while(1)
    {
        uint32_t now_ms = System::GetNow();

        const uint32_t dt_ms = now_ms - last_ms;
        last_ms = now_ms;
        ctrl_accum_ms += static_cast<int32_t>(dt_ms);
        if(ctrl_accum_ms < 0)
            ctrl_accum_ms = 0;
        if(ctrl_accum_ms > 20)
            ctrl_accum_ms = 20;

        const int kMaxCtrlTicksPerLoop = 3;
        int ticks_to_run = ctrl_accum_ms;
        if(ticks_to_run > kMaxCtrlTicksPerLoop)
            ticks_to_run = kMaxCtrlTicksPerLoop;

        for(int i = 0; i < ticks_to_run; ++i)
        {
            // Predictable scan cadence so edges can't be "missed" between reads.
            hw.ProcessDigitalControls();
            g_ui.ControlTick(hw, g_app, g_params, g_evtq);

            ctrl_ticks_accum++;
            if(ctrl_window_start_ms == 0)
                ctrl_window_start_ms = now_ms;

            if((now_ms - ctrl_window_start_ms) >= 1000)
            {
                g_app.ctrl_hz = ctrl_ticks_accum;
                ctrl_ticks_accum = 0;
                ctrl_window_start_ms = now_ms;
                g_app.ui_dirty = true;
            }

            ctrl_accum_ms -= 1;
        }

        bool midi_activity = false;
        hw.midi.Listen();
        while(hw.midi.HasEvents())
        {
            auto msg = hw.midi.PopEvent();
            g_app.midi_rx_count.fetch_add(1, std::memory_order_relaxed);
            midi_activity = true;

            if(msg.type == MidiMessageType::NoteOn)
            {
                const auto note_on = msg.AsNoteOn();
                if(note_on.velocity == 0)
                {
                    const Event evt = Event::NoteOffEvent(note_on.note);
                    if(g_evtq.Push(evt))
                        g_app.events_pushed.fetch_add(1, std::memory_order_relaxed);
                    else
                    {
                        g_app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                        g_app.ui_dirty = true;
                    }
                }
                else
                {
                    const Event evt = Event::NoteOnEvent(note_on.note, note_on.velocity);
                    if(g_evtq.Push(evt))
                        g_app.events_pushed.fetch_add(1, std::memory_order_relaxed);
                    else
                    {
                        g_app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                        g_app.ui_dirty = true;
                    }
                }
            }
            else if(msg.type == MidiMessageType::NoteOff)
            {
                const auto note_off = msg.AsNoteOff();
                const Event evt     = Event::NoteOffEvent(note_off.note);
                if(g_evtq.Push(evt))
                    g_app.events_pushed.fetch_add(1, std::memory_order_relaxed);
                else
                {
                    g_app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                    g_app.ui_dirty = true;
                }
            }
        }
        if(midi_activity)
            g_app.last_input_ms = now_ms;

        const bool midi_busy = midi_activity || hw.midi.HasEvents();
        g_render.TickOledTransfer(now_ms, midi_busy);

        g_render.Tick(g_app, g_params);
    }
}

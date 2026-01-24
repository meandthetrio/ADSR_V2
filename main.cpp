#include "daisy_pod.h"
#include "daisy_core.h"

// OLED driver (SSD130x family) used in libDaisy examples
#include "dev/oled_ssd130x.h"

// --- NEW: layered headers ---
#include "app_state.h"
#include "params.h"
#include "audio_engine.h"
#include "ui_logic.h"
#include "ui_render.h"

using namespace daisy;

// --- Hardware ---
static DaisyPod hw;

// --- OLED Types ---
using PodDisplay = OledDisplay<SSD130xI2c128x64Driver>;
static PodDisplay display;

// --- UI timing ---
static uint32_t last_ui_ms = 0;
static bool     ui_dirty   = true; // (we’ll transition to AppState.ui_dirty in Step 6)

// --- NEW: layered globals ---
static AppState    g_app;
static Params      g_params;
static AudioEngine g_audio;
static UILogic     g_ui;
static UIRender    g_render;

// --- Audio: passthrough (now via AudioEngine) ---
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    // Step 2: call the audio engine (currently passthrough inside ProcessBlock)
    g_audio.ProcessBlock(in[0], in[1], out[0], out[1], size);
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

static void RenderPerformTitle()
{
    display.Fill(false);

    // Simple text in top-left
    display.SetCursor(0, 0);
    display.WriteString("PERFORM", Font_7x10, true);

    display.SetCursor(0, 16);
    display.WriteString("ADSR V2", Font_6x8, true);

    display.Update();
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    InitOled();

    // --- NEW: init layered stubs ---
    g_params.Init();
    g_audio.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    g_ui.Init();
    g_render.Init();

    hw.StartAudio(AudioCallback);

    while(1)
    {
        // Step 2: placeholder “control tick” (does nothing yet)
        // We keep your existing ui_dirty flag for now.
        g_ui.ControlTick(g_app, g_params);
        g_params.ControlTick();

        // Timer-driven UI tick (~30Hz). Only render when dirty.
        uint32_t now = System::GetNow();
        if(ui_dirty && (now - last_ui_ms) > 33)
        {
            RenderPerformTitle();
            ui_dirty   = false;
            last_ui_ms = now;
        }

        // Step 2: render layer stub (doesn’t draw yet)
        // Later (Step 6), this will *be* the drawing and it will use g_app.ui_dirty.
        g_render.RenderIfDirty(g_app, g_params);
    }
}
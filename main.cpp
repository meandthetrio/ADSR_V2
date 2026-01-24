#include "daisy_pod.h"
#include "daisy_core.h"

// OLED driver (SSD130x family) used in libDaisy examples
#include "dev/oled_ssd130x.h"

// --- NEW: layered headers ---
#include "app_state.h"
#include "params.h"
#include "audio_engine.h"
#include "mem_regions.h"
#include "ui_logic.h"
#include "ui_render.h"

using namespace daisy;

// --- Hardware ---
ADSR2_DTCM static DaisyPod hw;

// --- OLED Types ---
using PodDisplay = OledDisplay<SSD130xI2c128x64Driver>;
static PodDisplay display;

ADSR2_SDRAM ADSR2_ALIGN32 static uint32_t g_sdram_test[256];
static bool g_sdram_ok = false;

// --- UI timing ---
static uint32_t last_ctrl_ms = 0;

// --- NEW: layered globals ---
ADSR2_DTCM static AppState g_app;
ADSR2_DTCM static Params g_params;
static AudioEngine g_audio;
static UILogic     g_ui;
static UIRender    g_render;

// --- Audio: passthrough (now via AudioEngine) ---
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    // Step 2: call the audio engine (currently passthrough inside ProcessBlock)
    g_audio.ProcessBlock(in[0], in[1], out[0], out[1], size, g_params.current);
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

static bool RunSdramTest()
{
    const uint32_t pat1 = 0xA5A50000u;
    const uint32_t pat2 = 0x5A5A0000u;
    const size_t   n    = sizeof(g_sdram_test) / sizeof(g_sdram_test[0]);

    for(size_t i = 0; i < n; ++i)
        g_sdram_test[i] = pat1 ^ (uint32_t)i;
    for(size_t i = 0; i < n; ++i)
    {
        if(g_sdram_test[i] != (pat1 ^ (uint32_t)i))
            return false;
    }

    for(size_t i = 0; i < n; ++i)
        g_sdram_test[i] = pat2 + (uint32_t)(i * 3u);
    for(size_t i = 0; i < n; ++i)
    {
        if(g_sdram_test[i] != (pat2 + (uint32_t)(i * 3u)))
            return false;
    }

    return true;
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
    g_ui.Init(hw);
    g_render.Init(&display);

    g_sdram_ok    = RunSdramTest();
    g_app.sdram_ok = g_sdram_ok;
    g_app.ui_dirty = true;

    hw.StartAudio(AudioCallback);

    while(1)
    {
        uint32_t now_ms = System::GetNow();

        // Run control processing at ~1kHz (1ms) max.
        if(last_ctrl_ms == 0)
            last_ctrl_ms = now_ms;

        uint32_t elapsed_ms = now_ms - last_ctrl_ms;
        if(elapsed_ms >= 1)
        {
            float dt_sec = elapsed_ms * 0.001f;
            last_ctrl_ms = now_ms;

            hw.ProcessDigitalControls();
            g_ui.ControlTick(hw, g_app, g_params);
            g_params.ControlTick(dt_sec);
        }

        g_render.Tick(g_app, g_params);
    }
}

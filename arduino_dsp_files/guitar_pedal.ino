/**
 * Multi-FX Guitar Pedal
 * Target: ESP32-S3 (Arduino IDE)
 *
 * Required Libraries (install via Library Manager):
 *   - LiquidCrystal_I2C  by Frank de Brabander  (v1.1.2+)
 *   - ESP32 board package by Espressif           (v2.0.x or v3.x)
 *
 * Board settings (Tools menu):
 *   Board:            ESP32S3 Dev Module
 *   CPU Frequency:    240MHz
 *   Partition Scheme: Default 4MB with spiffs
 *   PSRAM:            Disabled (or OPI PSRAM if your board has it)
 *
 * ─── Pin Assignments ────────────────────────────────────────────────────────
 *
 *  PCM1808 ADC  (I2S RX)          PCM5102A DAC (I2S TX)
 *  ─────────────────────          ──────────────────────
 *  SCKI  (MCLK)  → GPIO 7         SCK  (BCLK) → GPIO 15
 *  BCK   (BCLK)  → GPIO 4         BCK  (BCLK) → GPIO 15
 *  LRCK  (WS)    → GPIO 5         LCK  (WS)   → GPIO 16
 *  DOUT  (DATA)  → GPIO 6         DIN  (DATA) → GPIO 17
 *  FMT1  → GND                    FMT  → GND  (I2S standard)
 *  FMT0  → GND                    XSMT → 3.3V (unmute)
 *
 *  Rotary Encoder                  I2C LCD (PCF8574 backpack)
 *  ──────────────                  ──────────────────────────
 *  CLK (A) → GPIO 10               SDA → GPIO 8
 *  DT  (B) → GPIO 11               SCL → GPIO 9
 *  SW      → GPIO 12               Addr: 0x27
 *
 *  Stomp Switch → GPIO 13 (active-low, pulled up internally)
 *
 * ─── Architecture ────────────────────────────────────────────────────────────
 *
 *  Core 0:  audioTask()   – Priority 24  – I2S read → DSP → I2S write
 *  Core 1:  uiTask()      – Priority 5   – Encoder, stomp, LCD @ 64 Hz
 *           (Arduino loop() also runs on Core 1 but does nothing)
 *
 *  FreeRTOS Queues:
 *    audioChunkQueue  : audio_chunk_t blocks  (I2S ISR → DSP, depth 4)
 *    paramsQueue      : param_update_t updates (UI → DSP,   depth 8)
 *
 *  Timers:
 *    hw_timer_t timer0 : Encoder quadrature sampling at 1 kHz (Core 0)
 *    hw_timer_t timer1 : I2S stall watchdog, 10 ms one-shot   (Core 0)
 *    esp_timer (sw)    : Encoder-SW debounce 5 ms             (Core 1)
 *    esp_timer (sw)    : Stomp-SW debounce 20 ms              (Core 1)
 */

#include "Arduino.h"
#include "config.h"
#include "shared_state.h"
#include "i2s_audio.h"
#include "dsp.h"
#include "encoder_driver.h"
#include "lcd_display.h"
#include "ui_task.h"
#include "audio_task.h"

// ── Global queue & mutex handles (defined here, extern'd in shared_state.h) ──
QueueHandle_t audioChunkQueue = nullptr;
QueueHandle_t paramsQueue     = nullptr;
SemaphoreHandle_t effectMutex = nullptr;

// ── Shared effect state ──────────────────────────────────────────────────────
volatile EffectState gEffectState;

// ────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println(F("=== Multi-FX Guitar Pedal Booting ==="));

    // ── Create queues ──
    audioChunkQueue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(AudioChunk));
    paramsQueue     = xQueueCreate(PARAMS_QUEUE_DEPTH, sizeof(ParamUpdate));
    configASSERT(audioChunkQueue);
    configASSERT(paramsQueue);

    // ── Shared state mutex ──
    effectMutex = xSemaphoreCreateMutex();
    configASSERT(effectMutex);

    // ── Default effect state ──
    gEffectState.selectedEffect              = EFFECT_DISTORTION;
    gEffectState.effectEnabled               = false;
    // Distortion defaults  (param1=drive, param2=mix, param3=shape 0=step 1=exp)
    gEffectState.params[EFFECT_DISTORTION].param1 = 0.5f;
    gEffectState.params[EFFECT_DISTORTION].param2 = 0.8f;
    gEffectState.params[EFFECT_DISTORTION].param3 = 0.5f;  // 0.5 = blend of both
    // Delay defaults  (param1=time_ms, param2=feedback, param3=mix)
    gEffectState.params[EFFECT_DELAY].param1      = 300.0f;
    gEffectState.params[EFFECT_DELAY].param2      = 0.4f;
    gEffectState.params[EFFECT_DELAY].param3      = 0.4f;

    // ── Initialise I2S ──
    i2sInit();

    // ── Launch audio DSP task on Core 0 ──
    xTaskCreatePinnedToCore(
        audioTask,          // function
        "audioTask",        // name
        8192,               // stack bytes
        nullptr,            // arg
        24,                 // priority (highest)
        nullptr,            // handle
        0                   // Core 0
    );

    // ── Launch UI task on Core 1 ──
    xTaskCreatePinnedToCore(
        uiTask,
        "uiTask",
        4096,
        nullptr,
        5,
        nullptr,
        1                   // Core 1
    );

    Serial.println(F("Tasks launched."));
}

// loop() runs on Core 1 at idle priority – nothing needed here
void loop() {
    vTaskDelay(portMAX_DELAY);
}

/**
 * audio_task.cpp
 *
 * Core 0, priority 24.
 * Pipeline:  I2S RX (DMA) → extract mono → DSP → stereo out → I2S TX
 *
 * Hardware timers (Arduino ESP32 hw_timer_t):
 *   Timer 0 – stall watchdog: one-shot 10 ms, reset on every good read
 *   Timer 1 – telemetry:      periodic 5 s, logs free heap
 */

#include "audio_task.h"
#include "shared_state.h"
#include "i2s_audio.h"
#include "dsp.h"
#include "Arduino.h"
#include <esp_timer.h>

// ── Static DMA buffers (placed in internal DRAM) ─────────────────────────────
// Stereo interleaved read buffer: 2 channels × AUDIO_BLOCK_SAMPLES × 4 bytes
static int32_t rxBuf[AUDIO_BLOCK_SAMPLES * 2];

// Mono extracted
static int32_t monoBuf[AUDIO_BLOCK_SAMPLES];

// Stereo output for PCM5102A
static int32_t txBuf[AUDIO_BLOCK_SAMPLES * 2];

// ── Local cached DSP parameters (updated from paramsQueue) ───────────────────
static EffectID     activeEffect  = EFFECT_DISTORTION;
static bool         effectEnabled = false;
static EffectParams activeParams[EFFECT_COUNT];

// ── Hardware timer handles ────────────────────────────────────────────────────
static hw_timer_t* watchdogTimer  = nullptr;
static hw_timer_t* telemetryTimer = nullptr;
static volatile bool i2sStalled   = false;

// ── Timer ISR callbacks ───────────────────────────────────────────────────────
void IRAM_ATTR onWatchdog() {
    i2sStalled = true;
}

void IRAM_ATTR onTelemetry() {
    // Telemetry: just set a flag; actual Serial.print in task context
    // (Serial is not safe in ISR, so use a flag)
    static volatile bool telemetryFlag = false;
    telemetryFlag = true;
}

// ─────────────────────────────────────────────────────────────────────────────
void audioTask(void* arg) {
    Serial.printf("audioTask running on Core %d\n", xPortGetCoreID());

    // ── Init hardware timers (ESP32 Arduino core 3.x API) ────────────────────
    // timerBegin(frequency_hz) — sets timer tick frequency directly
    // timerAlarm(timer, ticks, autoreload, reloadval)

    // Timer 0 – stall watchdog (one-shot, fires after 10 ms = 10,000 ticks at 1 MHz)
    watchdogTimer = timerBegin(1000000);              // 1 MHz → 1 µs per tick
    timerAttachInterrupt(watchdogTimer, &onWatchdog);
    // Armed manually before each I2S read; autoreload=false for one-shot
    timerAlarm(watchdogTimer, 10000, false, 0);
    timerStop(watchdogTimer);                         // don't fire until first read

    // Timer 1 – telemetry (periodic, 5 s = 5,000,000 ticks at 1 MHz)
    telemetryTimer = timerBegin(1000000);
    timerAttachInterrupt(telemetryTimer, &onTelemetry);
    timerAlarm(telemetryTimer, 5000000, true, 0);     // autoreload=true

    // ── Copy initial state from shared global ─────────────────────────────────
    if (xSemaphoreTake(effectMutex, portMAX_DELAY)) {
        activeEffect  = gEffectState.selectedEffect;
        effectEnabled = gEffectState.effectEnabled;
        memcpy(activeParams, (const void*)gEffectState.params, sizeof(activeParams));
        xSemaphoreGive(effectMutex);
    }

    dspReset();

    bool telemetryDue = false;

    while (true) {
        // ── 1. Drain param updates from UI task ──────────────────────────────
        ParamUpdate upd;
        while (xQueueReceive(paramsQueue, &upd, 0) == pdTRUE) {
            activeEffect  = upd.effect;
            effectEnabled = upd.enabled;
            memcpy(&activeParams[upd.effect], &upd.params, sizeof(EffectParams));
        }

        // ── 2. Arm watchdog and read I2S RX ──────────────────────────────────
        timerRestart(watchdogTimer);
        timerStart(watchdogTimer);

        size_t bytesRead = i2sRead(rxBuf, AUDIO_BLOCK_SAMPLES);

        // Disarm watchdog – read succeeded
        timerStop(watchdogTimer);
        i2sStalled = false;

        if (bytesRead == 0) {
            Serial.println(F("[Audio] I2S read returned 0 bytes"));
            vTaskDelay(1);
            continue;
        }

        // ── 3. Extract mono left channel from stereo interleaved ──────────────
        int frames = (int)(bytesRead / (2 * sizeof(int32_t)));
        if (frames > AUDIO_BLOCK_SAMPLES) frames = AUDIO_BLOCK_SAMPLES;

        for (int i = 0; i < frames; i++) {
            monoBuf[i] = rxBuf[i * 2];   // left channel
        }

        // ── 4. Post chunk to audioChunkQueue (non-blocking, drop if full) ─────
        AudioChunk chunk;
        memcpy(chunk.samples, monoBuf, frames * sizeof(int32_t));
        chunk.timestamp_us = (uint32_t)esp_timer_get_time();
        xQueueSendToBack(audioChunkQueue, &chunk, 0);

        // ── 5. Run DSP ────────────────────────────────────────────────────────
        dspProcessBlock(monoBuf, txBuf, frames,
                        activeEffect, effectEnabled,
                        activeParams[activeEffect]);

        // ── 6. Write I2S TX ───────────────────────────────────────────────────
        i2sWrite(txBuf, frames);

        // ── 7. Telemetry (safe to print here, flag set in ISR) ───────────────
        if (telemetryDue) {
            telemetryDue = false;
            Serial.printf("[DSP] Free heap: %lu B | Effect: %s | %s\n",
                          (unsigned long)ESP.getFreeHeap(),
                          EFFECT_NAMES[activeEffect],
                          effectEnabled ? "ON" : "OFF");
        }
    }
}

#pragma once

#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "config.h"

// ─── Effect IDs ───────────────────────────────────────────────────────────────
enum EffectID : uint8_t {
    EFFECT_DISTORTION = 0,
    EFFECT_DELAY,
    EFFECT_COUNT
};

static const char* const EFFECT_NAMES[EFFECT_COUNT] = {
    "Distortion",
    "Delay"
};

// ─── Per-effect parameter struct ─────────────────────────────────────────────
// Each effect uses 3 plain floats. Meaning by effect:
//   Distortion : param1=drive  param2=mix  param3=tone
//   Chorus     : param1=rate   param2=depth  param3=mix
//   Delay      : param1=time_ms  param2=feedback  param3=mix
struct EffectParams {
    float param1;
    float param2;
    float param3;
};

// ─── Top-level shared effect state ───────────────────────────────────────────
struct EffectState {
    EffectID    selectedEffect;
    bool        effectEnabled;
    EffectParams params[EFFECT_COUNT];
};

// ─── Queue message: UI → DSP ──────────────────────────────────────────────────
struct ParamUpdate {
    EffectID     effect;
    bool         enabled;
    EffectParams params;
};

// ─── Queue message: I2S → DSP ────────────────────────────────────────────────
struct AudioChunk {
    int32_t  samples[AUDIO_BLOCK_SAMPLES];
    uint32_t timestamp_us;
};

// ─── Globals defined in guitar_pedal.ino ─────────────────────────────────────
extern QueueHandle_t     audioChunkQueue;
extern QueueHandle_t     paramsQueue;
extern SemaphoreHandle_t effectMutex;
extern volatile EffectState gEffectState;

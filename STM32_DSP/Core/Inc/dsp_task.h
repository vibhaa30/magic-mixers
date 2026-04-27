#ifndef DSP_TASK_H
#define DSP_TASK_H

#include "cmsis_os.h"
#include <stdint.h>

/* Audio block size: stereo sample pairs transferred per I2S call */
#define AUDIO_BLOCK_SAMPLES  16

/* Delay line length per channel: ~64ms at 8 kHz */
#define DELAY_BUF_SAMPLES    512

/* Effect enable bitmask flags */
#define EFFECT_VOLUME  (1U << 0)
#define EFFECT_LPF     (1U << 1)
#define EFFECT_DELAY   (1U << 2)

/* Shared effect parameters - protected by g_params_mutex */
typedef struct {
    float    volume;          /* 0.0 to 1.0 */
    float    lpf_alpha;       /* 0.0 = max filter, 1.0 = bypass */
    uint16_t delay_samples;   /* 0 to DELAY_BUF_SAMPLES */
    uint8_t  effects_mask;    /* bitwise OR of EFFECT_* flags */
} AudioEffectParams_t;

extern AudioEffectParams_t g_audio_params;
extern osMutexId           g_params_mutex;

void StartDSPTask(void const *argument);

#endif /* DSP_TASK_H */

#ifndef DSP_TASK_H
#define DSP_TASK_H

#include "cmsis_os.h"
#include <stdint.h>

/* Audio block size: stereo sample pairs transferred per I2S call */
#define AUDIO_BLOCK_SAMPLES  16

/* Chorus delay-line length per channel: ~42 ms at 48 kHz */
#define CHORUS_BUF_SAMPLES   2048
#define CHORUS_BASE_DELAY    960     /* ~20 ms center delay */
#define CHORUS_DEPTH         240     /* ~5 ms LFO swing (15–25 ms range) */

/* Pitch shift delay-line length: ~85 ms at 48 kHz */
#define PITCH_BUF_SAMPLES   4096
#define PITCH_GRAIN         1024    /* ~21 ms grain size */
#define PITCH_OVERLAP       256     /* ~5 ms crossfade window */

/* Effect enable bitmask flags */
#define EFFECT_VOLUME   (1U << 0)
#define EFFECT_LPF      (1U << 1)
#define EFFECT_CHORUS   (1U << 2)
#define EFFECT_PITCH    (1U << 3)

/* Shared effect parameters - protected by g_params_mutex */
typedef struct {
    float   volume;           /* 0.0 to 1.0 */
    float   lpf_alpha;        /* 0.3 = ~2.3 kHz, 1.0 = bypass */
    float   chorus_rate;      /* LFO rate in Hz: 0.1 to 3.0 */
    float   pitch_semitones;  /* -12.0 to +12.0 semitones */
    uint8_t effects_mask;     /* bitwise OR of EFFECT_* flags */
} AudioEffectParams_t;

extern AudioEffectParams_t g_audio_params;
extern osMutexId           g_params_mutex;

void StartDSPTask(void const *argument);

#endif /* DSP_TASK_H */

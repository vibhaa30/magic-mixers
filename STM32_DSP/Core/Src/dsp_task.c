#include "dsp_task.h"
#include "main.h"
#include <string.h>
#include <math.h>

extern I2S_HandleTypeDef hi2s3;

/* Defaults: volume at 80%, LPF bypassed, chorus at 0.5 Hz, pitch shift at +7 semitones */
AudioEffectParams_t g_audio_params = {
    .volume          = 0.8f,
    .lpf_alpha       = 1.0f,
    .chorus_rate     = 0.5f,
    .pitch_semitones = 7.0f,
    .effects_mask    = EFFECT_VOLUME,
};
osMutexId g_params_mutex;

/* Ping-pong DMA buffers: two blocks back-to-back.
   DMA (circular) writes into one half while DSP reads the other.
   24-bit I2S layout per stereo pair: [L_MSW, L_LSW, R_MSW, R_LSW],
   so each block is AUDIO_BLOCK_SAMPLES * 4 uint16_t words. */
#define HALF_BUF_LEN  (AUDIO_BLOCK_SAMPLES * 4)
static uint16_t rx_dma_buf[HALF_BUF_LEN * 2];
static uint16_t tx_dma_buf[HALF_BUF_LEN * 2];

/* Chorus: circular delay lines, one per channel, plus LFO phase accumulator */
static int16_t  chorus_buf_L[CHORUS_BUF_SAMPLES];
static int16_t  chorus_buf_R[CHORUS_BUF_SAMPLES];
static uint32_t chorus_wr  = 0;
static float    chorus_lfo = 0.0f;

/* Pitch shift: circular delay lines and two read heads for granular OLA */
static int16_t  pitch_buf_L[PITCH_BUF_SAMPLES];
static int16_t  pitch_buf_R[PITCH_BUF_SAMPLES];
static uint32_t pitch_wr   = 0;
static float    pitch_rd1  = (float)(PITCH_BUF_SAMPLES - PITCH_GRAIN); /* primary read head */
static float    pitch_rd2  = 0.0f;                                      /* secondary read head */
static int32_t  pitch_xfade = 0;    /* crossfade samples remaining (0 = idle) */

/* Per-channel IIR state */
static float lpf_L = 0.0f;
static float lpf_R = 0.0f;

/* Ping-pong signaling: set by ISR callbacks, consumed by task */
#define SIG_DMA_RDY   0x0001
static volatile uint8_t active_half = 0;
static osThreadId       dsp_task_id;

/* HAL weak-symbol overrides — called from DMA ISR context.
   HalfCplt: first block is ready (DMA now filling second).
   TxRxCplt:  second block is ready (DMA wraps to first). */
void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    active_half = 0;
    osSignalSet(dsp_task_id, SIG_DMA_RDY);
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    active_half = 1;
    osSignalSet(dsp_task_id, SIG_DMA_RDY);
}

/* Scale every sample by vol.
   24-bit layout stride: MSW at i, LSW at i+1; step by 2 to hit every MSW. */
static void apply_volume(uint16_t *buf, float vol)
{
    for (int i = 0; i < HALF_BUF_LEN; i += 2) {
        float s    = (float)(int16_t)buf[i] * vol;
        buf[i]     = (uint16_t)(int16_t)s;
        buf[i + 1] = 0;
    }
}

/* First-order IIR: y[n] = alpha*x[n] + (1-alpha)*y[n-1]
   alpha=1.0 is all-pass; alpha near 0 cuts more high frequencies.
   24-bit layout: L_MSW at i, L_LSW at i+1, R_MSW at i+2, R_LSW at i+3; step by 4. */
static void apply_lpf(uint16_t *buf, float alpha)
{
    float beta = 1.0f - alpha;
    for (int i = 0; i < HALF_BUF_LEN; i += 4) {
        float xL = (float)(int16_t)buf[i];
        float xR = (float)(int16_t)buf[i + 2];
        lpf_L    = alpha * xL + beta * lpf_L;
        lpf_R    = alpha * xR + beta * lpf_R;
        buf[i]     = (uint16_t)(int16_t)lpf_L;
        buf[i + 1] = 0;
        buf[i + 2] = (uint16_t)(int16_t)lpf_R;
        buf[i + 3] = 0;
    }
}

/* Sinusoidal LFO chorus (Amposta Boquera 2016).
   A single LFO modulates the read-head delay sinusoidally around CHORUS_BASE_DELAY.
   Linear interpolation gives sub-sample accuracy.  Fixed wet mix of 50%.
   rate: LFO frequency in Hz (0.1–3.0).
   24-bit layout: L_MSW at i, L_LSW at i+1, R_MSW at i+2, R_LSW at i+3; step by 4. */
#define CHORUS_WET  0.5f

static void apply_chorus(uint16_t *buf, float rate)
{
    for (int i = 0; i < HALF_BUF_LEN; i += 4) {
        float inL = (float)(int16_t)buf[i];
        float inR = (float)(int16_t)buf[i + 2];

        chorus_buf_L[chorus_wr] = (int16_t)inL;
        chorus_buf_R[chorus_wr] = (int16_t)inR;

        float lfo   = sinf(chorus_lfo * 6.28318f);
        chorus_lfo += rate / 48000.0f;
        if (chorus_lfo >= 1.0f) chorus_lfo -= 1.0f;

        float delay = (float)CHORUS_BASE_DELAY + (float)CHORUS_DEPTH * lfo;
        float rd_f  = (float)chorus_wr - delay;
        if (rd_f < 0.0f) rd_f += (float)CHORUS_BUF_SAMPLES;

        uint32_t i0 = (uint32_t)rd_f % CHORUS_BUF_SAMPLES;
        uint32_t i1 = (i0 + 1) % CHORUS_BUF_SAMPLES;
        float    fr = rd_f - (float)(uint32_t)rd_f;

        float wetL = (float)chorus_buf_L[i0] * (1.0f - fr) + (float)chorus_buf_L[i1] * fr;
        float wetR = (float)chorus_buf_R[i0] * (1.0f - fr) + (float)chorus_buf_R[i1] * fr;

        chorus_wr = (chorus_wr + 1) % CHORUS_BUF_SAMPLES;

        float outL = inL * (1.0f - CHORUS_WET) + wetL * CHORUS_WET;
        float outR = inR * (1.0f - CHORUS_WET) + wetR * CHORUS_WET;

        if (outL >  32767.0f) outL =  32767.0f;
        if (outL < -32768.0f) outL = -32768.0f;
        if (outR >  32767.0f) outR =  32767.0f;
        if (outR < -32768.0f) outR = -32768.0f;

        buf[i]     = (uint16_t)(int16_t)outL;
        buf[i + 1] = 0;
        buf[i + 2] = (uint16_t)(int16_t)outR;
        buf[i + 3] = 0;
    }
}

/* Granular overlap-add (OLA) pitch shifter.
   Two read heads advance through a circular delay buffer at 'ratio' samples/step
   (ratio = 2^(semitones/12)).  When the primary head drifts too close to the write
   pointer (pitch up) or too far behind it (pitch down), a secondary head is placed
   PITCH_GRAIN samples away and crossfaded in over PITCH_OVERLAP samples, then the
   primary head snaps to the secondary position.  Linear interpolation gives sub-sample
   accuracy.  24-bit layout: L_MSW at i, L_LSW at i+1, R_MSW at i+2, R_LSW at i+3. */
static inline float pitch_read(int16_t *buf, float pos)
{
    uint32_t i0 = (uint32_t)pos % PITCH_BUF_SAMPLES;
    uint32_t i1 = (i0 + 1) % PITCH_BUF_SAMPLES;
    float    f  = pos - (float)(uint32_t)pos;
    return (float)buf[i0] * (1.0f - f) + (float)buf[i1] * f;
}

static void apply_pitch_shift(uint16_t *buf, float semitones)
{
    float ratio = powf(2.0f, semitones / 12.0f);

    for (int i = 0; i < HALF_BUF_LEN; i += 4) {
        /* Write input sample into delay line */
        pitch_buf_L[pitch_wr] = (int16_t)buf[i];
        pitch_buf_R[pitch_wr] = (int16_t)buf[i + 2];
        pitch_wr = (pitch_wr + 1) % PITCH_BUF_SAMPLES;

        /* Read primary grain */
        float outL = pitch_read(pitch_buf_L, pitch_rd1);
        float outR = pitch_read(pitch_buf_R, pitch_rd1);

        if (pitch_xfade > 0) {
            /* Blend secondary grain; alpha ramps 0→1 over PITCH_OVERLAP steps */
            float alpha = (float)(PITCH_OVERLAP - pitch_xfade) / (float)PITCH_OVERLAP;
            outL = outL * (1.0f - alpha) + pitch_read(pitch_buf_L, pitch_rd2) * alpha;
            outR = outR * (1.0f - alpha) + pitch_read(pitch_buf_R, pitch_rd2) * alpha;

            pitch_rd2 += ratio;
            if (pitch_rd2 >= (float)PITCH_BUF_SAMPLES) pitch_rd2 -= (float)PITCH_BUF_SAMPLES;

            if (--pitch_xfade == 0) {
                pitch_rd1 = pitch_rd2; /* snap primary to secondary (already advanced) */
            } else {
                pitch_rd1 += ratio;
                if (pitch_rd1 >= (float)PITCH_BUF_SAMPLES) pitch_rd1 -= (float)PITCH_BUF_SAMPLES;
            }
        } else {
            pitch_rd1 += ratio;
            if (pitch_rd1 >= (float)PITCH_BUF_SAMPLES) pitch_rd1 -= (float)PITCH_BUF_SAMPLES;
        }

        /* Check if a new crossfade is needed */
        if (pitch_xfade == 0) {
            float dist = (float)pitch_wr - pitch_rd1;
            if (dist < 0.0f) dist += (float)PITCH_BUF_SAMPLES;

            if (ratio >= 1.0f && dist < (float)PITCH_OVERLAP) {
                /* Pitch up: primary caught up to write ptr — jump back GRAIN samples */
                pitch_rd2   = pitch_rd1 - (float)PITCH_GRAIN;
                if (pitch_rd2 < 0.0f) pitch_rd2 += (float)PITCH_BUF_SAMPLES;
                pitch_xfade = PITCH_OVERLAP;
            } else if (ratio < 1.0f && dist > (float)(PITCH_BUF_SAMPLES - PITCH_OVERLAP)) {
                /* Pitch down: write ptr ran too far ahead — jump forward GRAIN samples */
                pitch_rd2   = pitch_rd1 + (float)PITCH_GRAIN;
                if (pitch_rd2 >= (float)PITCH_BUF_SAMPLES) pitch_rd2 -= (float)PITCH_BUF_SAMPLES;
                pitch_xfade = PITCH_OVERLAP;
            }
        }

        if (outL >  32767.0f) outL =  32767.0f;
        if (outL < -32768.0f) outL = -32768.0f;
        if (outR >  32767.0f) outR =  32767.0f;
        if (outR < -32768.0f) outR = -32768.0f;

        buf[i]     = (uint16_t)(int16_t)outL;
        buf[i + 1] = 0;
        buf[i + 2] = (uint16_t)(int16_t)outR;
        buf[i + 3] = 0;
    }
}

/* DSP task: highest priority.
   Receives audio from PCM1808 ADC (I2S RX) and sends processed audio
   to PCM5102A DAC (I2S TX) using full-duplex I2S3.

   Ping-pong: DMA (circular) continuously streams into rx_dma_buf and out
   of tx_dma_buf.  HalfCplt signals that the first HALF_BUF_LEN words are
   ready; TxRxCplt signals that the second half is ready.  The task wakes,
   copies the fresh RX half into the corresponding TX half, applies effects,
   and goes back to sleep — all before DMA wraps around to that half again. */
void StartDSPTask(void const *argument)
{
    AudioEffectParams_t params;

    dsp_task_id = osThreadGetId();

    memset(tx_dma_buf, 0, sizeof(tx_dma_buf));

    /* HAL doubles Size internally for 24-bit format (TxXferSize = Size << 1).
       Buffer holds HALF_BUF_LEN uint16_t per half = HALF_BUF_LEN/2 32-bit words per half.
       Full double-buffer = HALF_BUF_LEN/2 * 2 halves = HALF_BUF_LEN/2 WORD transfers pre-doubling.
       After HAL doubles: HALF_BUF_LEN WORD transfers = 256 bytes = exact buffer size. */
    HAL_I2SEx_TransmitReceive_DMA(&hi2s3, tx_dma_buf, rx_dma_buf,
                                  HALF_BUF_LEN / 2);

    for (;;) {
        osSignalWait(SIG_DMA_RDY, osWaitForever);

        /* Snapshot which half the ISR marked ready, then derive pointers */
        uint8_t   half    = active_half;
        uint16_t *rx_half = &rx_dma_buf[half * HALF_BUF_LEN];
        uint16_t *tx_half = &tx_dma_buf[half * HALF_BUF_LEN];

        /* Copy fresh ADC samples into the output half, then process in-place */
        memcpy(tx_half, rx_half, HALF_BUF_LEN * sizeof(uint16_t));

        osMutexWait(g_params_mutex, osWaitForever);
        params = g_audio_params;
        osMutexRelease(g_params_mutex);

        if (params.effects_mask & EFFECT_VOLUME)
            apply_volume(tx_half, params.volume);

        if (params.effects_mask & EFFECT_LPF)
            apply_lpf(tx_half, params.lpf_alpha);

        if (params.effects_mask & EFFECT_CHORUS)
            apply_chorus(tx_half, params.chorus_rate);

        if (params.effects_mask & EFFECT_PITCH)
            apply_pitch_shift(tx_half, params.pitch_semitones);
    }
}

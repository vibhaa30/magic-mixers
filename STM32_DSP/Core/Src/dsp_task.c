#include "dsp_task.h"
#include "main.h"
#include <string.h>

extern I2S_HandleTypeDef hi2s3;

/* Defaults: volume at 80%, LPF bypassed, 256-sample delay, only volume on */
AudioEffectParams_t g_audio_params = {
    .volume        = 0.8f,
    .lpf_alpha     = 1.0f,
    .delay_samples = 256,
    .effects_mask  = EFFECT_VOLUME,
};
osMutexId g_params_mutex;

/* I2S DMA buffers - interleaved stereo: [L0, R0, L1, R1, ...] */
static uint16_t rx_buf[AUDIO_BLOCK_SAMPLES * 2];
static uint16_t tx_buf[AUDIO_BLOCK_SAMPLES * 2];

/* Per-channel delay ring buffers */
static int16_t  delay_line_L[DELAY_BUF_SAMPLES];
static int16_t  delay_line_R[DELAY_BUF_SAMPLES];
static uint16_t delay_wr = 0;

/* Per-channel IIR state */
static float lpf_L = 0.0f;
static float lpf_R = 0.0f;

/* Scale every sample by vol */
static void apply_volume(uint16_t *buf, float vol)
{
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES * 2; i++) {
        float s    = (float)(int16_t)buf[i] * vol;
        buf[i]     = (uint16_t)(int16_t)s;
    }
}

/* First-order IIR: y[n] = alpha*x[n] + (1-alpha)*y[n-1]
   alpha=1.0 is all-pass; alpha near 0 cuts more high frequencies. */
static void apply_lpf(uint16_t *buf, float alpha)
{
    float beta = 1.0f - alpha;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES * 2; i += 2) {
        float xL = (float)(int16_t)buf[i];
        float xR = (float)(int16_t)buf[i + 1];
        lpf_L    = alpha * xL + beta * lpf_L;
        lpf_R    = alpha * xR + beta * lpf_R;
        buf[i]     = (uint16_t)(int16_t)lpf_L;
        buf[i + 1] = (uint16_t)(int16_t)lpf_R;
    }
}

/* 70% dry + 30% wet feedback delay using a ring buffer */
static void apply_delay(uint16_t *buf, uint16_t len)
{
    if (len == 0) len = 1;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES * 2; i += 2) {
        uint16_t rd    = (delay_wr + DELAY_BUF_SAMPLES - len) % DELAY_BUF_SAMPLES;
        int16_t  dryL  = (int16_t)buf[i];
        int16_t  dryR  = (int16_t)buf[i + 1];
        int16_t  wetL  = delay_line_L[rd];
        int16_t  wetR  = delay_line_R[rd];

        delay_line_L[delay_wr] = dryL;
        delay_line_R[delay_wr] = dryR;
        delay_wr = (delay_wr + 1) % DELAY_BUF_SAMPLES;

        buf[i]     = (uint16_t)(int16_t)(dryL * 0.7f + wetL * 0.3f);
        buf[i + 1] = (uint16_t)(int16_t)(dryR * 0.7f + wetR * 0.3f);
    }
}

/* DSP task: highest priority.
   Receives audio from PCM1808 ADC (I2S RX) and sends processed audio
   to PCM5102A DAC (I2S TX) using full-duplex I2S3. One block of latency. */
void StartDSPTask(void const *argument)
{
    AudioEffectParams_t params;

    memset(tx_buf, 0, sizeof(tx_buf));

    for (;;) {
        /* Transmit last processed block to DAC while receiving next block from ADC */
        HAL_I2SEx_TransmitReceive(&hi2s3, tx_buf, rx_buf,
                                  AUDIO_BLOCK_SAMPLES * 2, HAL_MAX_DELAY);

        /* Move raw ADC samples into the processing buffer */
        memcpy(tx_buf, rx_buf, sizeof(rx_buf));

        /* Snapshot shared params so we hold the mutex as briefly as possible */
        osMutexWait(g_params_mutex, osWaitForever);
        params = g_audio_params;
        osMutexRelease(g_params_mutex);

        if (params.effects_mask & EFFECT_VOLUME)
            apply_volume(tx_buf, params.volume);

        if (params.effects_mask & EFFECT_LPF)
            apply_lpf(tx_buf, params.lpf_alpha);

        if (params.effects_mask & EFFECT_DELAY)
            apply_delay(tx_buf, params.delay_samples);
    }
}

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

/* Ping-pong DMA buffers: two blocks back-to-back.
   DMA (circular) writes into one half while DSP reads the other.
   24-bit I2S layout per stereo pair: [L_MSW, L_LSW, R_MSW, R_LSW],
   so each block is AUDIO_BLOCK_SAMPLES * 4 uint16_t words. */
#define HALF_BUF_LEN  (AUDIO_BLOCK_SAMPLES * 4)
static uint16_t rx_dma_buf[HALF_BUF_LEN * 2];
static uint16_t tx_dma_buf[HALF_BUF_LEN * 2];

/* Per-channel delay ring buffers */
static int16_t  delay_line_L[DELAY_BUF_SAMPLES];
static int16_t  delay_line_R[DELAY_BUF_SAMPLES];
static uint16_t delay_wr = 0;

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

/* 70% dry + 30% wet feedback delay using a ring buffer.
   24-bit layout: L_MSW at i, L_LSW at i+1, R_MSW at i+2, R_LSW at i+3; step by 4. */
static void apply_delay(uint16_t *buf, uint16_t len)
{
    if (len == 0) len = 1;

    for (int i = 0; i < HALF_BUF_LEN; i += 4) {
        uint16_t rd    = (delay_wr + DELAY_BUF_SAMPLES - len) % DELAY_BUF_SAMPLES;
        int16_t  dryL  = (int16_t)buf[i];
        int16_t  dryR  = (int16_t)buf[i + 2];
        int16_t  wetL  = delay_line_L[rd];
        int16_t  wetR  = delay_line_R[rd];

        delay_line_L[delay_wr] = dryL;
        delay_line_R[delay_wr] = dryR;
        delay_wr = (delay_wr + 1) % DELAY_BUF_SAMPLES;

        buf[i]     = (uint16_t)(int16_t)(dryL * 0.7f + wetL * 0.3f);
        buf[i + 1] = 0;
        buf[i + 2] = (uint16_t)(int16_t)(dryR * 0.7f + wetR * 0.3f);
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

    /* Size = number of 32-bit DMA words in the full double buffer.
       Each uint16_t pair in the buffer maps to one 32-bit DMA word,
       so Size = HALF_BUF_LEN (uint16_t per half) / 2 * 2 halves
               = HALF_BUF_LEN = AUDIO_BLOCK_SAMPLES * 4.            */
    HAL_I2SEx_TransmitReceive_DMA(&hi2s3, tx_dma_buf, rx_dma_buf,
                                  HALF_BUF_LEN * 2);

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
        // params.effects_mask & EFFECT_VOLUME
        if (1)
            apply_volume(tx_half, params.volume);
            
        if (params.effects_mask & EFFECT_LPF)
            apply_lpf(tx_half, params.lpf_alpha);

        if (params.effects_mask & EFFECT_DELAY)
            apply_delay(tx_half, params.delay_samples);
    }
}

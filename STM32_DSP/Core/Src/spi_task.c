#include "spi_task.h"
#include "dsp_task.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

/* SPI task: dequeues [gesture, intensity] packets posted by HAL_SPI_RxCpltCallback
   and maps them to shared audio effect parameters.

   SPI2 is configured as a slave (SPI_MODE_0, MSB first, 8-bit, hardware NSS).
   The Raspberry Pi drives NSS and clocks two bytes per transaction:
     Byte 0 — gesture  (1=volume, 2=chorus, 3=LPF, 4=pitch, 0/other=clear)
     Byte 1 — intensity (0-255 mapped to the parameter range)

   The ISR packs both bytes into a uint32_t (gesture << 8 | intensity) and posts
   to spiQueueHandle.  Reading from the queue is atomic, so there is no race with
   the DMA circular buffer.

   LED assignments (GPIOD): LD3=volume, LD5=reverb, LD3+LD5=LPF, LD4=pitch, all off=clear. */

void StartSPITask(void const *argument)
{
    osEvent evt;

    HAL_SPI_Receive_IT(&hspi2, (uint8_t *)spi_rx_buf, SPI_PKT_SIZE);

    for (;;)
    {
        evt = osMessageGet(spiQueueHandle, osWaitForever);
        if (evt.status != osEventMessage)
            continue;

        uint8_t gesture   = (uint8_t)((evt.value.v >> 8) & 0xFF);
        uint8_t intensity = (uint8_t)( evt.value.v        & 0xFF);

        osMutexWait(g_params_mutex, osWaitForever);

        /* Clear all effect LEDs before lighting the active one */
        HAL_GPIO_WritePin(GPIOD, LD3_Pin | LD4_Pin | LD5_Pin, GPIO_PIN_RESET);

        switch (gesture)
        {
        case 1:   /* Volume: intensity maps to 0.0–1.0 */
            g_audio_params.volume        = intensity / 255.0f;
            g_audio_params.effects_mask |= EFFECT_VOLUME;
            HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET);
            break;

        case 2:   /* Chorus: intensity maps LFO rate to 0.1–3.0 Hz */
            g_audio_params.chorus_rate   = 0.1f + (intensity / 255.0f) * 2.9f;
            g_audio_params.effects_mask |= EFFECT_CHORUS;
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
            break;

        case 3:   /* LPF: intensity maps alpha to 0.3–1.0 (~2.3 kHz to bypass) */
            g_audio_params.lpf_alpha     = 0.3f + (intensity / 255.0f) * 0.7f;
            g_audio_params.effects_mask |= EFFECT_LPF;
            HAL_GPIO_WritePin(GPIOD, LD3_Pin | LD5_Pin, GPIO_PIN_SET);
            break;

        case 4:   /* Pitch: intensity maps to -12 to +12 semitones */
            g_audio_params.pitch_semitones = (intensity / 255.0f) * 24.0f - 12.0f;
            g_audio_params.effects_mask   |= EFFECT_PITCH;
            HAL_GPIO_WritePin(GPIOD, LD4_Pin, GPIO_PIN_SET);
            break;

        default:  /* Clear all effects */
            g_audio_params.effects_mask  = 0;
            break;
        }

        osMutexRelease(g_params_mutex);
    }
}

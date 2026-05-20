#include "spi_task.h"
#include "dsp_task.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

/* SPI task: dequeues [gesture, intensity] packets posted by HAL_SPI_RxCpltCallback
   and maps them to shared audio effect parameters.

   SPI2 is configured as a slave (SPI_MODE_0, MSB first, 8-bit, hardware NSS).
   The Raspberry Pi drives NSS and clocks two bytes per transaction:
     Byte 0 — gesture  (1 = volume, 2 = delay, 3 = LPF, other = clear effects)
     Byte 1 — intensity (0-255 mapped to each parameter's range)

   The ISR packs both bytes into a uint32_t (gesture << 8 | intensity) and posts
   to spiQueueHandle.  Reading from the queue is atomic, so there is no race with
   the DMA circular buffer. */

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

        switch (gesture)
        {
        case 1:
            g_audio_params.volume        = intensity / 255.0f;
            g_audio_params.effects_mask |= EFFECT_VOLUME;
            HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_RESET);
            break;

        case 2:
            g_audio_params.delay_samples = (uint16_t)(((uint32_t)intensity * DELAY_BUF_SAMPLES) / 255);
            g_audio_params.effects_mask |= EFFECT_DELAY;
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_RESET);
            break;

        case 3:
            g_audio_params.lpf_alpha     = intensity / 255.0f;
            g_audio_params.effects_mask |= EFFECT_LPF;
            HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
            break;

        default:
            g_audio_params.effects_mask  = 0;
            HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_RESET);
            break;
        }

        osMutexRelease(g_params_mutex);
    }
}

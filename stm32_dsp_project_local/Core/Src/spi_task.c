#include "spi_task.h"
#include "dsp_task.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

/* SPI task: receives 3-byte command packets from the Raspberry Pi 4 (SPI master)
   and updates the shared audio effect parameters.

   Packet format: [CMD] [VAL_HI] [VAL_LO]
   The RPi must be configured as SPI master (SPI_MODE_0, MSB first, 8-bit)
   to match the STM32 SPI2 slave configuration.

   NOTE: SPI2 is configured with software NSS. If you use a hardware CS line from
   the RPi, change hspi2.Init.NSS to SPI_NSS_HARD_INPUT in main.c for reliable
   transaction framing. */


void StartSPITask(void const * argument)
{
    for (;;)
    {
        if (osSemaphoreWait(spiSemHandle, osWaitForever) == osOK)
        {
            if (gesture == 1)
            {
                HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET);

                g_audio_params.volume = intensity / 10.0f;

                osDelay(100);

                HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_RESET);
            }
            else if (gesture == 2)
            {
                HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);

                g_audio_params.delay_samples = intensity * 48;

                osDelay(100);

                HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_RESET);
            }
            else
            {
                HAL_GPIO_TogglePin(GPIOD, LD5_Pin);
                HAL_GPIO_TogglePin(GPIOD, LD3_Pin);
                osDelay(100);
                HAL_GPIO_TogglePin(GPIOD, LD5_Pin);
                HAL_GPIO_TogglePin(GPIOD, LD3_Pin);


            }
        }
    }
}

// #include "spi_task.h"
// #include "dsp_task.h"
// #include "main.h"

// extern SPI_HandleTypeDef hspi2;

// /* SPI task: receives 3-byte command packets from the Raspberry Pi 4 (SPI master)
//    and updates the shared audio effect parameters.

//    Packet format: [CMD] [VAL_HI] [VAL_LO]
//    The RPi must be configured as SPI master (SPI_MODE_0, MSB first, 8-bit)
//    to match the STM32 SPI2 slave configuration.

//    NOTE: SPI2 is configured with software NSS. If you use a hardware CS line from
//    the RPi, change hspi2.Init.NSS to SPI_NSS_HARD_INPUT in main.c for reliable
//    transaction framing. */
// void StartSPITask(void const *argument)
// {
//     uint8_t pkt[SPI_PKT_SIZE];

//     for (;;) {
//         /* Block waiting for a complete 3-byte packet from the RPi (100 ms timeout) */
//         if (HAL_SPI_Receive(&hspi2, pkt, SPI_PKT_SIZE, 100) != HAL_OK) {
//             /* Clear overrun flag so SPI2 is ready for the next transaction */
//             __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
//             osDelay(1);
//             continue;
//         }

//         osMutexWait(g_params_mutex, osWaitForever);

//         switch (pkt[0]) {
//         case SPI_CMD_SET_VOLUME:
//             /* VAL_HI = 0-100 maps to 0.0-1.0 */
//             g_audio_params.volume = (float)pkt[1] / 100.0f;
//             break;

//         case SPI_CMD_SET_LPF:
//             /* VAL_HI = 0-255 maps to 0.0-1.0 alpha (0 = max cut, 255 = bypass) */
//             g_audio_params.lpf_alpha = (float)pkt[1] / 255.0f;
//             break;

//         case SPI_CMD_SET_DELAY:
//             {
//                 uint16_t samples = ((uint16_t)pkt[1] << 8) | pkt[2];
//                 if (samples > DELAY_BUF_SAMPLES)
//                     samples = DELAY_BUF_SAMPLES;
//                 g_audio_params.delay_samples = samples;
//             }
//             break;

//         case SPI_CMD_SET_EFFECTS:
//             /* VAL_HI = bitmask of EFFECT_VOLUME | EFFECT_LPF | EFFECT_DELAY */
//             g_audio_params.effects_mask = pkt[1];
//             break;

//         default:
//             /* Unknown command: ignore */
//             break;
//         }

//         osMutexRelease(g_params_mutex);
//     }
// }

#ifndef SPI_TASK_H
#define SPI_TASK_H

#include "cmsis_os.h"

/* Two-byte packet received from the Raspberry Pi SPI master.
   Byte 0: gesture code (1=volume, 2=chorus, 3=LPF, 4=pitch, 0/other=clear)
   Byte 1: intensity (0-255 mapped to the parameter range) */
#define SPI_PKT_SIZE  2

extern osMessageQId      spiQueueHandle;
extern volatile uint8_t  spi_rx_buf[SPI_PKT_SIZE];

void StartSPITask(void const *argument);

#endif /* SPI_TASK_H */

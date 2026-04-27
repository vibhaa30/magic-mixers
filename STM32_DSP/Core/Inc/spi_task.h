#ifndef SPI_TASK_H
#define SPI_TASK_H

#include "cmsis_os.h"

/* 3-byte packet format from Raspberry Pi: [CMD] [VAL_HI] [VAL_LO] */
#define SPI_PKT_SIZE        3

#define SPI_CMD_SET_VOLUME  0x01  /* VAL_HI = 0-100 (maps to 0.0-1.0) */
#define SPI_CMD_SET_LPF     0x02  /* VAL_HI = 0-255 (0 = max filter, 255 = bypass) */
#define SPI_CMD_SET_DELAY   0x03  /* (VAL_HI << 8 | VAL_LO) = delay in samples */
#define SPI_CMD_SET_EFFECTS 0x04  /* VAL_HI = effects_mask bitmask */

void StartSPITask(void const *argument);

#endif /* SPI_TASK_H */

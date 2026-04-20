#pragma once

#include "Arduino.h"
#include <driver/i2s.h>
#include "config.h"

// I2S port assignments
#define I2S_RX_PORT   I2S_NUM_0   // PCM1808 ADC
#define I2S_TX_PORT   I2S_NUM_1   // PCM5102A DAC

/**
 * Initialise both I2S peripherals.
 * Call once from setup() before tasks start.
 */
void i2sInit();

/**
 * Read one stereo block from PCM1808.
 * @param buf     Destination buffer, stereo interleaved int32_t
 * @param samples Number of STEREO frames to read
 * @param timeout Ticks to wait
 * @return        Number of bytes read, or 0 on timeout/error
 */
size_t i2sRead(int32_t* buf, size_t samples, TickType_t timeout = pdMS_TO_TICKS(20));

/**
 * Write one stereo block to PCM5102A.
 * @param buf     Source buffer, stereo interleaved int32_t
 * @param samples Number of STEREO frames to write
 * @param timeout Ticks to wait
 */
void i2sWrite(const int32_t* buf, size_t samples, TickType_t timeout = pdMS_TO_TICKS(20));

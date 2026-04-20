#pragma once

/**
 * audio_task.cpp
 * FreeRTOS task pinned to Core 0.
 * Reads I2S RX, runs DSP, writes I2S TX.
 *
 * Timers used (hw_timer_t – ESP32 Arduino hardware timers):
 *   Timer 0 : I2S stall watchdog  (10 ms one-shot, reset on every successful read)
 *   Timer 1 : CPU telemetry       (5 s periodic log)
 */

void audioTask(void* arg);

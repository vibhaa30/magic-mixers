#pragma once

/**
 * ui_task.h
 *
 * Core 1, priority 5.
 * Runs at ~64 Hz (vTaskDelayUntil 16 ms).
 * Handles: rotary encoder (effect select), encoder push (enable toggle),
 *          I2C LCD refresh, and publishes param updates to paramsQueue.
 *
 * Debounce:
 *   Encoder SW – software counter debounce, 5 ms
 *   (ISR + volatile flag + millis() stamp for debounce window)
 */

void uiTask(void* arg);

#pragma once

/**
 * encoder_driver.h
 *
 * Interrupt-driven rotary encoder.
 * CLK pin triggers on FALLING edge; DT pin is sampled to get direction.
 * 5 ms software debounce in the ISR.
 * Each physical detent = exactly 1 step returned by encoderGetDelta().
 */

void encoderInit();

/**
 * Return signed step count since last call.
 *  +1 = clockwise (next effect)
 *  -1 = counter-clockwise (previous effect)
 */
int encoderGetDelta();

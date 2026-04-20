#pragma once

#include "Arduino.h"

/**
 * lcd_display.h
 *
 * Thin wrapper around the LiquidCrystal_I2C library.
 * Requires: LiquidCrystal_I2C by Frank de Brabander (Library Manager)
 */

/** Initialise the I2C bus and LCD. Call once before lcdPrint(). */
void lcdInit();

/** Clear the display. */
void lcdClear();

/**
 * Print a string at the given row and column.
 * @param row  0 or 1
 * @param col  0–15
 * @param text Null-terminated string (truncated at 16 chars per line)
 */
void lcdPrint(uint8_t row, uint8_t col, const char* text);

/** Turn backlight on (true) or off (false). */
void lcdBacklight(bool on);

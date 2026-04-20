/**
 * lcd_display.cpp
 *
 * Uses LiquidCrystal_I2C library.
 * Install via: Sketch → Include Library → Manage Libraries → "LiquidCrystal I2C"
 * Author: Frank de Brabander
 */

#include "lcd_display.h"
#include "config.h"
#include "Arduino.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

void lcdInit() {
    Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("  Guitar  FX  "));
    lcd.setCursor(0, 1);
    lcd.print(F("  Loading...  "));
    delay(800);
    lcd.clear();
}

void lcdClear() {
    lcd.clear();
}

void lcdPrint(uint8_t row, uint8_t col, const char* text) {
    lcd.setCursor(col, row);
    lcd.print(text);
}

void lcdBacklight(bool on) {
    if (on) lcd.backlight();
    else    lcd.noBacklight();
}

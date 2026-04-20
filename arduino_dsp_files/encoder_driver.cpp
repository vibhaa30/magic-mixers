/**
 * encoder_driver.cpp
 *
 * Simple interrupt-driven encoder.
 * CLK (A) triggers an interrupt on FALLING edge.
 * DT  (B) is read at that moment to determine direction:
 *   DT HIGH → clockwise
 *   DT LOW  → counter-clockwise
 *
 * 5 ms debounce window ignores spurious re-triggers.
 * No timer or state machine needed.
 */

#include "encoder_driver.h"
#include "config.h"
#include "Arduino.h"

static volatile int32_t  encAccum   = 0;
static volatile uint32_t lastMoveMs = 0;

void IRAM_ATTR onEncoderCLK() {
    uint32_t now = millis();
    if (now - lastMoveMs < 5) return;   // debounce
    lastMoveMs = now;

    if (digitalRead(ENC_DT_PIN) == HIGH) {
        encAccum++;    // clockwise
    } else {
        encAccum--;    // counter-clockwise
    }
}

void encoderInit() {
    pinMode(ENC_CLK_PIN, INPUT_PULLUP);
    pinMode(ENC_DT_PIN,  INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), onEncoderCLK, FALLING);
}

int encoderGetDelta() {
    portDISABLE_INTERRUPTS();
    int32_t val = encAccum;
    encAccum = 0;
    portENABLE_INTERRUPTS();
    return (int)val;   // each detent = 1 step, no division needed
}

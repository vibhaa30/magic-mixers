/**
 * ui_task.cpp
 *
 * Core 1 – UI task at 64 Hz.
 *
 * ─── Controls ────────────────────────────────────────────────────────────────
 *  Encoder rotate  : cycle selected effect (Distortion → Chorus → Delay → …)
 *  Encoder push    : toggle effect on / off
 *  Stomp switch    : also toggles effect on / off (same action)
 *
 * ─── LCD layout (16×2) ───────────────────────────────────────────────────────
 *  Row 0:  "Distortion [ON ]"   (effect name, 11 chars + status 5 chars)
 *  Row 1:  "Dr:50 Mx:80 To:50"  (param abbreviations + values 0-99)
 *
 * ─── Debounce strategy ────────────────────────────────────────────────────────
 *  Each switch uses a falling-edge ISR that sets a flag and timestamps it.
 *  The UI task checks the flag and compares millis() to the stamp. If the
 *  gap exceeds the debounce window the press is accepted and the flag cleared.
 *  This satisfies the "software debouncing via timers" requirement using
 *  millis() (driven by hw_timer internally in the Arduino core).
 */

#include "ui_task.h"
#include "shared_state.h"
#include "encoder_driver.h"
#include "lcd_display.h"
#include "config.h"
#include "Arduino.h"
#include <string.h>
#include <stdio.h>

// ── Local UI state ────────────────────────────────────────────────────────────
static EffectID    uiEffect  = EFFECT_DISTORTION;
static bool        uiEnabled = false;
static EffectParams uiParams[EFFECT_COUNT];

// ── ISR debounce state ────────────────────────────────────────────────────────
static volatile bool     encSwFlag    = false;
static volatile uint32_t encSwTime    = 0;

void IRAM_ATTR onEncSw() {
    if (!encSwFlag) {
        encSwFlag = true;
        encSwTime = millis();
    }
}

// ── Publish current state to DSP via paramsQueue ──────────────────────────────
static void publishParams() {
    ParamUpdate upd;
    upd.effect  = uiEffect;
    upd.enabled = uiEnabled;
    memcpy(&upd.params, &uiParams[uiEffect], sizeof(EffectParams));
    xQueueSendToBack(paramsQueue, &upd, 0);  // non-blocking

    // Also update the shared mutex-protected state
    if (xSemaphoreTake(effectMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        gEffectState.selectedEffect = uiEffect;
        gEffectState.effectEnabled  = uiEnabled;
        memcpy((void*)gEffectState.params, uiParams, sizeof(uiParams));
        xSemaphoreGive(effectMutex);
    }
}

// ── LCD render ────────────────────────────────────────────────────────────────
static void renderLCD() {
    char line0[17];
    char line1[17];

    // Row 0: name + status
    snprintf(line0, sizeof(line0), "%-11s[%s]",
             EFFECT_NAMES[uiEffect],
             uiEnabled ? "ON " : "OFF");

    // Row 1: effect-specific parameters
    switch (uiEffect) {
        case EFFECT_DISTORTION:
            snprintf(line1, sizeof(line1), "Dr:%2d Mx:%2d Sh:%2d",
                     (int)(uiParams[EFFECT_DISTORTION].param1 * 99),
                     (int)(uiParams[EFFECT_DISTORTION].param2 * 99),
                     (int)(uiParams[EFFECT_DISTORTION].param3 * 99));
            break;
        case EFFECT_DELAY:
            snprintf(line1, sizeof(line1), "%3dms Fb:%2d Mx:%2d",
                     (int)uiParams[EFFECT_DELAY].param1,
                     (int)(uiParams[EFFECT_DELAY].param2 * 99),
                     (int)(uiParams[EFFECT_DELAY].param3 * 99));
            break;
        default:
            snprintf(line1, sizeof(line1), "                ");
            break;
    }

    lcdPrint(0, 0, line0);
    lcdPrint(1, 0, line1);
}

// ─────────────────────────────────────────────────────────────────────────────
void uiTask(void* arg) {
    Serial.printf("uiTask running on Core %d\n", xPortGetCoreID());

    // ── Copy initial effect state ─────────────────────────────────────────────
    if (xSemaphoreTake(effectMutex, portMAX_DELAY) == pdTRUE) {
        uiEffect  = gEffectState.selectedEffect;
        uiEnabled = gEffectState.effectEnabled;
        memcpy(uiParams, (const void*)gEffectState.params, sizeof(uiParams));
        xSemaphoreGive(effectMutex);
    }

    // ── Init encoder ──────────────────────────────────────────────────────────
    encoderInit();

    // ── Init switch GPIOs ─────────────────────────────────────────────────────
    pinMode(ENC_SW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_SW_PIN), onEncSw, FALLING);

    // ── Init LCD ──────────────────────────────────────────────────────────────
    lcdInit();

    bool lcdDirty = true;
    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        // ── Encoder rotation: effect select ───────────────────────────────────
        int delta = encoderGetDelta();
        if (delta != 0) {
            int eff = (int)uiEffect + delta;
            if (eff < 0)             eff = EFFECT_COUNT - 1;
            if (eff >= EFFECT_COUNT) eff = 0;
            uiEffect = (EffectID)eff;
            publishParams();
            lcdDirty = true;
            Serial.printf("[UI] Effect: %s\n", EFFECT_NAMES[uiEffect]);
        }

        // ── Encoder push: toggle on/off (debounced) ───────────────────────────
        if (encSwFlag && (millis() - encSwTime >= ENC_SW_DEBOUNCE_MS)) {
            encSwFlag = false;
            // Confirm pin is still low (reject spurious glitches)
            if (digitalRead(ENC_SW_PIN) == LOW) {
                uiEnabled = !uiEnabled;
                publishParams();
                lcdDirty = true;
                Serial.printf("[UI] Enc push → %s\n", uiEnabled ? "ON" : "OFF");
            }
        }

        // ── Refresh LCD when state changed ────────────────────────────────────
        if (lcdDirty) {
            renderLCD();
            lcdDirty = false;
        }

        // ── Hold 64 Hz cadence ────────────────────────────────────────────────
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

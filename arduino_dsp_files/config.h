#pragma once

// ─── Audio constants ─────────────────────────────────────────────────────────
#define SAMPLE_RATE          44100
#define AUDIO_BLOCK_SAMPLES  128      // samples per DMA block (mono)
#define AUDIO_QUEUE_DEPTH    4
#define PARAMS_QUEUE_DEPTH   8

// ─── I2S RX pins  (PCM1808 ADC) ──────────────────────────────────────────────
#define I2S_RX_BCLK   4
#define I2S_RX_WS     5
#define I2S_RX_DIN    6    // DATA from ADC into ESP32
#define I2S_RX_MCLK  7

// ─── I2S TX pins  (PCM5102A DAC) ─────────────────────────────────────────────
#define I2S_TX_BCLK   15
#define I2S_TX_WS     16
#define I2S_TX_DOUT   17   // DATA from ESP32 into DAC

// ─── Rotary encoder ───────────────────────────────────────────────────────────
#define ENC_CLK_PIN   10   // A
#define ENC_DT_PIN    11   // B
#define ENC_SW_PIN    12   // push button, active-low

// ─── I2C LCD ──────────────────────────────────────────────────────────────────
#define LCD_SDA_PIN   8
#define LCD_SCL_PIN   9
#define LCD_I2C_ADDR  0x27
#define LCD_COLS      16
#define LCD_ROWS      2

// ─── Debounce timings ─────────────────────────────────────────────────────────
#define ENC_SW_DEBOUNCE_MS   5

// ─── UI run rate ──────────────────────────────────────────────────────────────
#define UI_PERIOD_MS  16    // ~64 Hz

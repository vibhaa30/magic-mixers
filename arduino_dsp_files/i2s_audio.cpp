/**
 * i2s_audio.cpp
 *
 * PCM1808 ADC  (I2S_NUM_0) – 44.1 kHz, 24-bit, slave-mode clocks driven by ESP32
 * PCM5102A DAC (I2S_NUM_1) – 44.1 kHz, 32-bit stereo, master
 *
 * NOTE: Arduino ESP32 uses the legacy i2s driver (driver/i2s.h).
 *       The newer i2s_std API is ESP-IDF only and not exposed in Arduino.
 */

#include "i2s_audio.h"
#include <driver/i2s.h>

void i2sInit() {

    // ── RX: PCM1808 ADC ──────────────────────────────────────────────────────
    i2s_config_t rx_cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,  // 24-bit data in 32-bit slot
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT, // stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // Philips I2S
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = AUDIO_BLOCK_SAMPLES,
        .use_apll             = true,                        // APLL for accurate 44.1 kHz
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan         = I2S_BITS_PER_CHAN_32BIT,
    };

    i2s_pin_config_t rx_pins = {
        .mck_io_num   = I2S_RX_MCLK,   // PCM1808 requires MCLK on SCKI
        .bck_io_num   = I2S_RX_BCLK,
        .ws_io_num    = I2S_RX_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_RX_DIN,
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_RX_PORT, &rx_cfg, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_RX_PORT, &rx_pins));

    Serial.println(F("I2S RX (PCM1808) ready"));

    // ── TX: PCM5102A DAC ─────────────────────────────────────────────────────
    i2s_config_t tx_cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = AUDIO_BLOCK_SAMPLES,
        .use_apll             = true,
        .tx_desc_auto_clear   = true,   // silence on underrun
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan         = I2S_BITS_PER_CHAN_32BIT,
    };

    i2s_pin_config_t tx_pins = {
        .mck_io_num   = I2S_RX_MCLK,   // PCM5102A uses internal PLL
        .bck_io_num   = I2S_TX_BCLK,
        .ws_io_num    = I2S_TX_WS,
        .data_out_num = I2S_TX_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_TX_PORT, &tx_cfg, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_TX_PORT, &tx_pins));

    Serial.println(F("I2S TX (PCM5102A) ready"));
}

size_t i2sRead(int32_t* buf, size_t samples, TickType_t timeout) {
    size_t bytesRead = 0;
    i2s_read(I2S_RX_PORT, buf, samples * sizeof(int32_t) * 2, &bytesRead, timeout);
    return bytesRead;
}

void i2sWrite(const int32_t* buf, size_t samples, TickType_t timeout) {
    size_t bytesWritten = 0;
    i2s_write(I2S_TX_PORT, buf, samples * sizeof(int32_t) * 2, &bytesWritten, timeout);
}

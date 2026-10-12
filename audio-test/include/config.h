#pragma once

// Audio hardware map. Believed to be the LCDWIKI/Hosyond ES3C28P 2.8" board:
// ES8311 codec (mic ADC + DAC) -> power amp -> speaker header.
// The test verifies this at runtime (I2C probe + live mic samples), so if a
// line here is wrong the serial log says which one.

// Codec control bus (shared with the FT6336 touch controller)
#define CODEC_SDA        16
#define CODEC_SCL        15
#define CODEC_I2C_ADDR   0x18   // ES8311 (0x19 if CE pin is high)

// I2S link to the codec
#define I2S_MCLK_PIN     4
#define I2S_BCLK_PIN     5
#define I2S_WS_PIN       7
#define I2S_DOUT_PIN     8      // ESP32 -> codec DAC
#define I2S_DIN_PIN      6      // codec ADC (mic) -> ESP32

// Speaker power-amp enable
#define PA_ENABLE_PIN    1
#define PA_ACTIVE_LEVEL  HIGH   // the boot test also tries the opposite level

#define SAMPLE_RATE      16000
#define DEFAULT_VOLUME   70     // 0..100
#define MIC_GAIN_STEP    7      // ES8311 PGA step 0..10 (3 dB each)

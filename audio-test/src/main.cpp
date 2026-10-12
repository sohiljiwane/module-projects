#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>
#include <config.h>

#define I2S_PORT I2S_NUM_0
#define PI_F 3.14159265f

// ---------------------------------------------------------------- state

static bool codecFound = false;
static uint8_t codecAddr = CODEC_I2C_ADDR;
static int volumePct = DEFAULT_VOLUME;
static int micGain = MIC_GAIN_STEP;
static uint8_t paLevel = PA_ACTIVE_LEVEL;

// ---------------------------------------------------------------- ES8311

static bool codecWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(codecAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static int codecRead(uint8_t reg) {
  Wire.beginTransmission(codecAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)codecAddr, 1) != 1) return -1;
  return Wire.read();
}

static void codecSetVolume(int pct) {
  volumePct = constrain(pct, 0, 100);
  codecWrite(0x32, (uint8_t)(volumePct * 255 / 100));
}

static void codecSetMicGain(int step) {
  micGain = constrain(step, 0, 10);
  codecWrite(0x14, 0x10 | micGain);  // analog mic on MIC1P/N, PGA gain
}

// Slave-mode, 16-bit I2S, MCLK = 256 * fs (16 kHz -> 4.096 MHz).
static void codecInit() {
  codecWrite(0x00, 0x1F);  // reset
  delay(20);
  codecWrite(0x00, 0x00);
  codecWrite(0x00, 0x80);  // power on, slave mode
  codecWrite(0x01, 0x3F);  // clocks on, MCLK from pin
  codecWrite(0x02, 0x00);  // pre_div 1, pre_multi 1x
  codecWrite(0x03, 0x10);  // ADC OSR
  codecWrite(0x04, 0x10);  // DAC OSR
  codecWrite(0x05, 0x00);  // ADC/DAC div 1
  codecWrite(0x06, 0x03);  // BCLK div 4
  codecWrite(0x07, 0x00);  // LRCK high
  codecWrite(0x08, 0xFF);  // LRCK low (256)
  codecWrite(0x09, 0x0C);  // I2S in, 16 bit
  codecWrite(0x0A, 0x0C);  // I2S out, 16 bit
  codecWrite(0x0B, 0x00);
  codecWrite(0x0C, 0x00);
  codecWrite(0x0D, 0x01);  // power up analog
  codecWrite(0x0E, 0x02);  // enable PGA + ADC modulator
  codecWrite(0x10, 0x1F);
  codecWrite(0x11, 0x7F);
  codecWrite(0x12, 0x00);  // power up DAC
  codecWrite(0x13, 0x10);  // enable output driver
  codecWrite(0x16, 0x24);  // ADC scale
  codecWrite(0x17, 0xBF);  // ADC volume 0 dB
  codecWrite(0x1B, 0x0A);
  codecWrite(0x1C, 0x6A);  // ADC EQ bypass, DC cancel
  codecWrite(0x31, 0x00);  // DAC unmute
  codecWrite(0x37, 0x08);  // DAC EQ bypass
  codecSetMicGain(micGain);
  codecSetVolume(volumePct);
}

// ---------------------------------------------------------------- I2S

static bool i2sInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_MCLK_PIN;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_WS_PIN;
  pins.data_out_num = I2S_DOUT_PIN;
  pins.data_in_num = I2S_DIN_PIN;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) return false;
  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

static void paSet(bool on) {
  digitalWrite(PA_ENABLE_PIN, on ? paLevel : !paLevel);
}

// Play `ms` of a sine wave. freq 0 = silence.
static void playTone(float freq, int ms, float amp = 0.5f) {
  const int frames = SAMPLE_RATE * ms / 1000;
  static int16_t buf[256 * 2];
  static float phase = 0;
  int done = 0;
  while (done < frames) {
    int n = min(256, frames - done);
    for (int i = 0; i < n; i++) {
      int16_t s = freq > 0 ? (int16_t)(sinf(phase) * 32000 * amp) : 0;
      phase += 2 * PI_F * freq / SAMPLE_RATE;
      if (phase > 2 * PI_F) phase -= 2 * PI_F;
      buf[2 * i] = s;
      buf[2 * i + 1] = s;
    }
    size_t w;
    i2s_write(I2S_PORT, buf, n * 4, &w, portMAX_DELAY);
    done += n;
  }
}

// Read `frames` mono samples (max of both slots), returns count.
static size_t readMono(int16_t *out, size_t frames) {
  static int16_t buf[256 * 2];
  size_t got = 0;
  while (got < frames) {
    size_t n = min((size_t)256, frames - got);
    size_t r = 0;
    if (i2s_read(I2S_PORT, buf, n * 4, &r, 500) != ESP_OK || r == 0) break;
    size_t fr = r / 4;
    for (size_t i = 0; i < fr; i++) {
      int16_t l = buf[2 * i], rt = buf[2 * i + 1];
      out[got + i] = abs(l) >= abs(rt) ? l : rt;
    }
    got += fr;
  }
  return got;
}

struct Level { float rms; int peak; int dc; };

static Level measure(const int16_t *s, size_t n) {
  Level L = {0, 0, 0};
  if (!n) return L;
  double sum = 0;
  for (size_t i = 0; i < n; i++) sum += s[i];
  L.dc = (int)(sum / n);
  double sq = 0;
  for (size_t i = 0; i < n; i++) {
    int v = s[i] - L.dc;
    sq += (double)v * v;
    if (abs(v) > L.peak) L.peak = abs(v);
  }
  L.rms = sqrt(sq / n);
  return L;
}

static Level captureLevel(int ms) {
  size_t frames = SAMPLE_RATE * ms / 1000;
  int16_t *b = (int16_t *)malloc(frames * 2);
  if (!b) return {0, 0, 0};
  size_t n = readMono(b, frames);
  Level L = measure(b, n);
  free(b);
  return L;
}

// ---------------------------------------------------------------- tests

static void testI2C() {
  Serial.println("\n=== [1] I2C scan (SDA=" + String(CODEC_SDA) + " SCL=" + String(CODEC_SCL) + ") ===");
  int count = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      const char *name = "unknown";
      if (a == 0x38) name = "FT6336 touch controller";
      if (a == 0x18 || a == 0x19) name = "ES8311 audio codec (candidate)";
      if (a == 0x10) name = "ES7210/ES8388-class codec (candidate)";
      Serial.printf("  0x%02X  %s\n", a, name);
      count++;
    }
  }
  Serial.printf("  %d device(s) found\n", count);

  for (uint8_t a : {(uint8_t)0x18, (uint8_t)0x19}) {
    codecAddr = a;
    int id1 = codecRead(0xFD), id2 = codecRead(0xFE), ver = codecRead(0xFF);
    if (id1 == 0x83 && id2 == 0x11) {
      codecFound = true;
      Serial.printf("  ES8311 CONFIRMED at 0x%02X (chip ID %02X%02X, version %02X)\n", a, id1, id2, ver);
      return;
    }
  }
  codecAddr = CODEC_I2C_ADDR;
  Serial.println("  ES8311 NOT found. Check CODEC_SDA/SCL/ADDR in include/config.h,");
  Serial.println("  or the board uses a different codec / a plain I2S amp (see log below).");
}

static void testSpeaker() {
  Serial.println("\n=== [2] Speaker test (listen!) ===");
  Serial.printf("  volume %d%%, PA pin GPIO%d\n", volumePct, PA_ENABLE_PIN);
  // Try both amp polarities; the mic hears the speaker if it is near it.
  Level quiet = {0, 0, 0};
  paSet(false);
  quiet = captureLevel(300);
  float best = -1;
  uint8_t bestLevel = paLevel;
  for (int pass = 0; pass < 2; pass++) {
    uint8_t lvl = pass == 0 ? PA_ACTIVE_LEVEL : !PA_ACTIVE_LEVEL;
    paLevel = lvl;
    paSet(true);
    delay(50);
    Serial.printf("  beep: PA pin driven %s ...\n", lvl ? "HIGH" : "LOW");
    playTone(1000, 400, 0.6f);               // warm-up
    Level L = {0, 0, 0};
    // measure mic while tone plays
    {
      size_t frames = SAMPLE_RATE * 400 / 1000;
      int16_t *b = (int16_t *)malloc(frames * 2);
      if (b) {
        // play in small chunks while reading to keep both directions busy
        size_t got = 0;
        while (got < frames) {
          playTone(1000, 16, 0.6f);
          size_t n = readMono(b + got, min((size_t)256, frames - got));
          if (!n) break;
          got += n;
        }
        L = measure(b, got);
        free(b);
      }
    }
    Serial.printf("    mic RMS during tone: %.0f (silence baseline %.0f)\n", L.rms, quiet.rms);
    if (L.rms > best) { best = L.rms; bestLevel = lvl; }
    paSet(false);
    delay(300);
  }
  paLevel = bestLevel;
  paSet(true);
  Serial.printf("  -> mic heard the speaker louder with PA %s (mic RMS %.0f vs baseline %.0f)\n",
                paLevel ? "HIGH" : "LOW", best, quiet.rms);
  if (best < quiet.rms * 2 + 20)
    Serial.println("  (mic barely heard it: speaker may be far/quiet, so trust your ears.\n"
                   "   If you heard nothing, send 'a' to flip PA polarity or '+' for volume.)");
  Serial.println("  Playing 3-note scale (C5 E5 G5)...");
  playTone(523, 300);
  playTone(659, 300);
  playTone(784, 400);
  playTone(0, 100);
}

static void testMic() {
  Serial.println("\n=== [3] Microphone level meter (5 s) - talk or clap ===");
  Level first = {0, 0, 0};
  for (int i = 0; i < 25; i++) {  // 25 x 200 ms
    Level L = captureLevel(200);
    if (i == 0) first = L;
    int bars = constrain((int)(L.rms / 200), 0, 40);
    char line[48];
    memset(line, '#', bars);
    line[bars] = 0;
    Serial.printf("  rms %5.0f peak %5d dc %6d |%s\n", L.rms, L.peak, L.dc, line);
  }
  if (first.rms == 0 && first.peak == 0 && first.dc == 0)
    Serial.println("  ALL ZERO: no data from I2S DIN. Check I2S_DIN/BCLK/WS/MCLK pins and codec init.");
  else if (first.rms < 3)
    Serial.println("  Signal is flat: mic wired but silent/low. Try gain: 'g' / 'G'.");
}

static void testRecordPlay() {
  Serial.println("\n=== [4] Record 3 s then play back ===");
  const size_t frames = SAMPLE_RATE * 3;
  int16_t *rec = (int16_t *)ps_malloc(frames * 2);
  if (!rec) rec = (int16_t *)malloc(frames * 2);
  if (!rec) { Serial.println("  out of memory"); return; }
  paSet(true);
  Serial.println("  Get ready... beep, then speak for 3 s");
  delay(500);
  playTone(1200, 150, 0.5f);
  playTone(0, 200);
  i2s_zero_dma_buffer(I2S_PORT);
  Serial.println("  RECORDING NOW - speak loudly, close to the board");
  size_t n = readMono(rec, frames);
  Level L = measure(rec, n);
  Serial.printf("  recorded %u samples, rms %.0f peak %d\n", (unsigned)n, L.rms, L.peak);

  // normalise so quiet recordings are audible
  float gain = L.peak > 20 ? min(24000.0f / L.peak, 40.0f) : 1.0f;
  if (L.peak < 300) Serial.println("  Recording is very quiet: speak louder/closer, or press 'g' to raise mic gain.");
  Serial.printf("  Playing back (gain x%.1f)...\n", gain);
  static int16_t out[256 * 2];
  for (size_t i = 0; i < n;) {
    size_t c = min((size_t)256, n - i);
    for (size_t k = 0; k < c; k++) {
      int v = (int)((rec[i + k] - L.dc) * gain);
      v = constrain(v, -32768, 32767);
      out[2 * k] = out[2 * k + 1] = (int16_t)v;
    }
    size_t w;
    i2s_write(I2S_PORT, out, c * 4, &w, portMAX_DELAY);
    i += c;
  }
  playTone(0, 100);
  free(rec);
}

static void testLoopback(int seconds) {
  Serial.printf("\n=== [5] Live loopback %d s (keep speaker away from mic to avoid squeal) ===\n", seconds);
  static int16_t in[256 * 2];
  uint32_t end = millis() + seconds * 1000;
  while (millis() < end) {
    size_t r = 0;
    if (i2s_read(I2S_PORT, in, sizeof(in), &r, 100) != ESP_OK || !r) continue;
    size_t w;
    i2s_write(I2S_PORT, in, r, &w, portMAX_DELAY);
  }
  playTone(0, 100);
}

static void help() {
  Serial.println("\nCommands (send over serial):");
  Serial.println("  t  speaker test (PA polarity probe + tones)");
  Serial.println("  m  microphone level meter");
  Serial.println("  r  record 3 s and play back");
  Serial.println("  l  live mic -> speaker loopback (10 s)");
  Serial.println("  + / -  speaker volume   g / G  mic gain up / down");
  Serial.println("  a  flip amp-enable polarity   i  re-scan I2C   h  help   A  run everything");
}

static void runAll() {
  testI2C();
  testSpeaker();
  testMic();
  testRecordPlay();
  Serial.println("\n=== done. Type 'h' for commands ===");
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(1500);  // let the host open the monitor

  Serial.println("\n\n##### ESP32-S3 audio + mic test #####");
  Serial.printf("Chip %s rev %d, %d MHz, PSRAM %u bytes\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getCpuFreqMHz(), (unsigned)ESP.getPsramSize());

  pinMode(PA_ENABLE_PIN, OUTPUT);
  paSet(false);

  Wire.begin(CODEC_SDA, CODEC_SCL, 100000);
  testI2C();
  if (!codecFound) {
    Serial.println("Continuing without codec init (no I2C ack)...");
  } else {
    codecInit();
    Serial.println("  ES8311 initialised (16 kHz, 16-bit I2S slave)");
  }

  if (!i2sInit()) {
    Serial.println("I2S driver init FAILED");
    return;
  }
  Serial.printf("  I2S up: MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d\n", I2S_MCLK_PIN, I2S_BCLK_PIN,
                I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN);

  testSpeaker();
  testMic();
  testRecordPlay();
  Serial.println("\n=== done ===");
  help();
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  switch (Serial.read()) {
    case 't': testSpeaker(); break;
    case 'm': testMic(); break;
    case 'r': testRecordPlay(); break;
    case 'l': testLoopback(10); break;
    case 'A': runAll(); break;
    case 'i': testI2C(); break;
    case '+': codecSetVolume(volumePct + 10); Serial.printf("volume %d%%\n", volumePct); break;
    case '-': codecSetVolume(volumePct - 10); Serial.printf("volume %d%%\n", volumePct); break;
    case 'g': codecSetMicGain(micGain + 1); Serial.printf("mic gain step %d\n", micGain); break;
    case 'G': codecSetMicGain(micGain - 1); Serial.printf("mic gain step %d\n", micGain); break;
    case 'a': paLevel = !paLevel; paSet(true); Serial.printf("PA enable level now %s\n", paLevel ? "HIGH" : "LOW"); break;
    case 'h': help(); break;
    default: break;
  }
}

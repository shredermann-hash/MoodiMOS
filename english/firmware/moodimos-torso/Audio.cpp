// Shared ES8311 codec: microphone (Microphone.h) and speaker (Speaker.h) on the same I2S bus.
#include <math.h>
#include <atomic>
#include "Microphone.h"
#include "Speaker.h"
#include "SoundMeter.h"
#include "SpeechEnd.h"
#include "MouthEnvelope.h"
#include "MoodSounds.h"
#include "WavFile.h"
#include "DemoVoice.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <FFat.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Wire.h>

namespace {
// Reusing Waveshare examples (`04_es8311_example`, `01_audio_out`):
// GPIO16 → codec data (read), GPIO14 → codec data (write).
constexpr int PIN_MCLK = 12, PIN_BCLK = 13, PIN_LRCK = 15, PIN_TO_CODEC = 16, PIN_FROM_CODEC = 14;
constexpr int PIN_SD_CLK = 11, PIN_SD_CMD = 10, PIN_SD_D0 = 9;
constexpr uint32_t RATE = 16000;
// After a sound is played, the microphone remains quiet for a short time: the end of the sound waits in the speaker’s queue (~90 ms) before playback relinquishes control, and a loud scream would have been heard as a sudden loud noise.
constexpr uint32_t ECHO_MS = 250;
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t VOLUME_REG = 0xC8;   // Register 0x32 (0 … 255): 200, selected by ear on the test bench on 2026-10-03 with voice Sandy.
                                       // (speaker from the outdoor kit: to review once in the box)

I2SClass i2s;
bool audioReady = false;

bool reg(uint8_t r, uint8_t v) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(r);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool regRead(uint8_t r, uint8_t& v) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(r);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ES8311_ADDR, (uint8_t)1) != 1) return false;
  v = Wire.read();
  return true;
}

// driver sequence from Espressif/Waveshare (es8311_init, microphone_config, volume),
// reduced to 16 kHz with MCLK = 256 × 16 kHz = 4,096 MHz on the MCLK pin.
bool codecInit() {
  uint8_t v;
  if (!reg(0x00, 0x1F)) return false;           // <reset>
  delay(20);
  reg(0x00, 0x00);
  reg(0x00, 0x80);                              // Power on
  reg(0x31, 0x60);                              // Silent DAC, triggered by `VolumeRamp()` (no pop).
  reg(0x01, 0x3F);                              // All clocks, MCLK from the pin.
  if (regRead(0x06, v)) reg(0x06, uint8_t((v & 0xE0) | (0x04 - 1)));   // Uninverted SCLK, BCLK divider
  if (regRead(0x02, v)) reg(0x02, uint8_t(v & 0x07));                  // pre-divider 1, multiplier 1
  reg(0x03, 0x10);                              // Simple speed, OSR ADC
  reg(0x04, 0x10);                              // OSR DAC
  reg(0x05, 0x00);                              // ADC/DAC dividers = 1
  if (regRead(0x07, v)) reg(0x07, uint8_t(v & 0xC0));
  reg(0x08, 0xFF);                              // `LRCK = MCLK / 256`
  if (regRead(0x00, v)) reg(0x00, uint8_t(v & 0xBF));                  // Slave I²S
  reg(0x09, 0x0C);                              // 16-bit serial input
  reg(0x0A, 0x0C);                              // 16-bit serial output
  reg(0x0D, 0x01);                              // Analog power supply
  reg(0x0E, 0x02);                              // PGA & ADC Modulator
  reg(0x12, 0x00);                              // Powered DAC
  reg(0x13, 0x10);                              // speaker output to upper stage
  reg(0x1C, 0x6A);                              // AD converter equalizer, continuous offset bypassed
  reg(0x37, 0x08);                              // Bypass DAC equalizer
  reg(0x32, 0x00);                              // Volume at zero: smoothly mounted via `Volume()` (no pop)
  reg(0x17, 0xC8);                              // ADC gain
  return reg(0x14, 0x1A);                       // Analog micro, max PGA gain
}

// Increase volume from 0 to `VOLUME_REG` once the audio clock is running: the level jump at the converter’s power-on no longer clangs in the speaker.
void rampVolume() {
  reg(0x31, 0x00);                         // end of mute, volume still at zero.
  for (int v = 0; v <= VOLUME_REG; v += 6) {
    reg(0x32, uint8_t(v));
    delay(8);
  }
  reg(0x32, VOLUME_REG);
}

// ---
// Micro
// ---
SoundMeter meter;
volatile bool noise = false;
volatile float level = -90.3f;
// Micro task retains voice in PSRAM until SpeechEnd decides the end.
constexpr int LISTEN_MAX = int(RATE) * 10;   // 10 s
int16_t* listenBuf = nullptr;
volatile int listenLen = 0;
volatile uint32_t listenMaxMs = 0;
volatile bool listenRequest = false, listenOn = false, listenStop = false, listenByHand = false;
SpeechEnd speechEnd;
int listenBlock = 0;   // samples retained from the last 50 ms block

// Speaker
MouthEnvelope mouth;
volatile bool speaking = false, stopRequest = false;
volatile uint8_t mouthAmp = 0;
char requested[48] = {0};
volatile bool hasRequest = false;
volatile bool chimeRequest = false, demoRequest = false, sweepRequest = false;
const int16_t* volatile pcmData = nullptr;   // Received in memory (Mac announcement), read by the voice task.
// Its length, which can grow during reading (follow-up of a response); -1: reading complete, no more continuation.
std::atomic<int> pcmLimit{-1};
const volatile int* volatile pcmReady = nullptr;   // samples already received while more audio is still arriving
volatile bool pcmRequest = false;
volatile uint8_t cryRequest = 0, cryVariant = 0;   // Requested mood sound (`MoodSounds::Sound`) cry.
volatile bool crying = false;
uint8_t cryNotch = 5;                               // Cry volume range: 0…10 (repeated on boot via flash)
uint8_t cryOnNotch = 5;                             // last nonzero volume; the touchscreen button restores it
constexpr float CRY_GAIN_STEP = 0.2f;               // 5 = as loud as voice, 10 = twice as loud
bool sdReady = false;
bool flashReady = false;   // FAT partition of 9 MB on the torso: received phrases from Mac (command "deposer")
File storeFile;            // Current phrase in reception
char storePath[64];

// File: first on the TF card, otherwise in the torso’s flash memory.
fs::FS* voiceFs(const char* path) {
  if (sdReady && SD_MMC.exists(path)) return &SD_MMC;
  if (flashReady && FFat.exists(path)) return &FFat;
  return nullptr;
}

// `/voice/bonjour.wav`
bool voicePath(const char* name, char* path, size_t size) {
  if (!name || !*name || strlen(name) >= sizeof(requested)) return false;
  if (strchr(name, '/') || strstr(name, "..")) return false;
  snprintf(path, size, "/voix/%s.wav", name);
  return true;
}

// Keep a fragment of the question; listening ends at the silence following the spoken word, at the maximum duration or upon request.
void keepListening(const int16_t* s, int n) {
  if (n > LISTEN_MAX - listenLen) n = LISTEN_MAX - listenLen;
  memcpy(listenBuf + listenLen, s, size_t(n) * 2);
  listenLen += n;
  listenBlock += n;
  bool done = listenStop || listenLen >= LISTEN_MAX;
  for (; listenBlock >= SoundMeter::BLOCK; listenBlock -= SoundMeter::BLOCK) done |= speechEnd.feed(level);
  if (done) {
    listenByHand = listenStop;
    listenOn = false;
  }
}

void micTask(void*) {
  static int16_t buf[512];      // stereo interlaced
  static int16_t mono[256];
  uint32_t spokeAt = 0;
  bool echo = false;
  for (;;) {
    size_t got = i2s.readBytes((char*)buf, sizeof(buf));
    int frames = int(got / 4);
    // its own voice is not a loud sound (elapsed time, not a deadline: just even after 25 days silent)
    if (speaking) { spokeAt = millis(); echo = true; }
    if (echo && millis() - spokeAt < ECHO_MS) continue;
    echo = false;
    for (int i = 0; i < frames; i++) mono[i] = buf[2 * i];   // left channel
    if (meter.feed(mono, frames, millis())) noise = true;
    level = meter.levelDbfs();
    if (listenRequest) {                // The listening begins from the pre-existing ambient sound learned beforehand.
      speechEnd.start(meter.floorDbfs(), listenMaxMs);
      listenBlock = 0;
      listenOn = true;
      listenRequest = false;
    }
    if (listenOn) keepListening(mono, frames);
  }
}

void playFile(const char* path) {
  fs::FS* fs = voiceFs(path);
  if (!fs) return;
  File f = fs->open(path);
  if (!f) return;
  uint8_t head[1024];
  size_t n = f.read(head, sizeof(head));
  WavInfo w;
  if (!WavFile::parse(head, n, w) || !WavFile::playable(w) || !f.seek(w.dataOffset)) {
    f.close();
    return;
  }
  static int16_t in[512];
  static int16_t out[1024];
  mouth.reset();
  speaking = true;
  uint32_t left = w.dataSize;
  while (left > 0 && !stopRequest) {
    size_t want = left < sizeof(in) ? left : sizeof(in);
    size_t got = f.read((uint8_t*)in, want);
    if (got < 2) break;
    left -= got;
    int samples = int(got / 2);
    int frames = w.channels == 2 ? samples / 2 : samples;
    for (int i = 0; i < frames; i++) {   // Always sent in stereo, voice copied onto both channels.
      int16_t s = w.channels == 2 ? in[2 * i] : in[i];
      out[2 * i] = out[2 * i + 1] = s;
      in[i] = s;
    }
    mouth.feed(in, frames);
    mouthAmp = mouth.amplitude();
    i2s.write((const uint8_t*)out, size_t(frames) * 4);
  }
  f.close();
  memset(out, 0, sizeof(out));          // a little quiet: no click at end of sentence
  i2s.write((const uint8_t*)out, sizeof(out));
  mouthAmp = 0;
  speaking = false;
}

// Two gentle notes (sol followed by do, sine waves at 25% of full scale) with rounded attack and release:
// a deep, rounded tone to test the speaker without stressing it.
void playChime() {
  static int16_t out[512];                  // 256 stereo images
  const float notes[2] = {392.0f, 523.25f};
  const int total = RATE * 45 / 100;        // 0.45 seconds per note
  speaking = true;
  for (int n = 0; n < 2 && !stopRequest; n++) {
    float phase = 0;
    for (int done = 0; done < total; done += 256) {
      for (int i = 0; i < 256; i++) {
        int t = done + i;
        float env = t < 640 ? 0.5f - 0.5f * cosf(3.14159f * t / 640.0f) : expf(-3.0f * (t - 640) / float(total));
        float s = 0.25f * env * (sinf(phase) + 0.15f * sinf(2 * phase));
        phase += 6.28318f * notes[n] / RATE;
        out[2 * i] = out[2 * i + 1] = int16_t(s * 32767);
      }
      i2s.write((const uint8_t*)out, sizeof(out));
    }
  }
  memset(out, 0, sizeof(out));
  i2s.write((const uint8_t*)out, sizeof(out));
  speaking = false;
}

// The PCM mono 16 kHz data stored in memory: the test phrase (from DemoVoice.h) or a USB sound received (announcement, response), played while still receiving if `ready` counts received samples (reception is about six times faster).
// `limit`: its length, which can grow during reading (Speaker::extendPcm); upon reaching the end, reading stops abruptly (-1), or continues if a subsequent segment extends it—never both.
void playMono(const int16_t* pcm, int total, const volatile int* ready = nullptr, std::atomic<int>* limit = nullptr) {
  static int16_t in[256];
  static int16_t out[512];
  mouth.reset();
  speaking = true;
  bool closed = false;
  for (int pos = 0; !stopRequest;) {
    int end = limit ? limit->load() : total;
    if (pos >= end) {
      if (!limit) break;
      if (limit->compare_exchange_strong(end, -1)) { closed = true; break; }
      continue;                                  // a new set has arrived
    }
    int frames = end - pos < 256 ? end - pos : 256;
    for (uint32_t t = millis(); ready && *ready < pos + frames && !stopRequest && millis() - t < 1000;) vTaskDelay(1);
    if (ready && *ready < pos + frames) break;   // reception halted: no play of what hasn’t occurred.
    for (int i = 0; i < frames; i++) {
      in[i] = pcm[pos + i];
      out[2 * i] = out[2 * i + 1] = in[i];
    }
    mouth.feed(in, frames);
    mouthAmp = mouth.amplitude();
    i2s.write((const uint8_t*)out, size_t(frames) * 4);
    pos += frames;
  }
  if (limit && !closed) limit->store(-1);        // stopped or incomplete: no further action
  memset(out, 0, sizeof(out));
  i2s.write((const uint8_t*)out, sizeof(out));
  mouthAmp = 0;
  speaking = false;
}

void playDemo() { playMono(DemoVoice::DATA, DemoVoice::SAMPLES); }

// Synthetic mood cry generated during reading. Every requested sentence is cut with a 4 ms fade.
void playCry(uint8_t sound, uint8_t variant) {
  static MoodSounds::Synth synth;
  static int16_t in[256];
  static int16_t out[512];
  synth.start(MoodSounds::Sound(sound), variant, cryNotch * CRY_GAIN_STEP, esp_random());
  speaking = crying = true;
  int16_t last = 0;
  int n;
  while ((n = synth.render(in, 256)) > 0) {
    if (stopRequest || hasRequest || pcmRequest || demoRequest) {
      for (int i = 0; i < 64; i++) out[2 * i] = out[2 * i + 1] = int16_t(last * (63 - i) / 64);
      i2s.write((const uint8_t*)out, 64 * 4);
      break;
    }
    for (int i = 0; i < n; i++) out[2 * i] = out[2 * i + 1] = in[i];
    last = in[n - 1];
    i2s.write((const uint8_t*)out, size_t(n) * 4);
  }
  memset(out, 0, sizeof(out));
  i2s.write((const uint8_t*)out, sizeof(out));
  crying = false;
  speaking = false;
}

// Test Bench: Continuous sine wave at 300 Hz (80% of full scale) while codec volume ramps up in 2-second steps from 0xA0 to 0xDF. Each step is announced via serial port. Register 0x32 sets 0 dB to 0xBF—beyond that, digital gain is applied. Volume returns to normal at the end.
void playSweep() {
  static int16_t out[512];
  speaking = true;
  float phase = 0;
  for (int level = 0xA0; level <= 0xDF && !stopRequest; level += 0x08) {
    reg(0x32, uint8_t(level));
    Serial.printf("palier 0x%02X\n", level);
    for (int n = 0; n < RATE * 2 / 256 && !stopRequest; n++) {
      for (int i = 0; i < 256; i++) {
        float env = n == 0 && i < 128 ? i / 128.0f : 1.0f;   // small attack: no clacking sound
        out[2 * i] = out[2 * i + 1] = int16_t(0.8f * env * 32767 * sinf(phase));
        phase += 6.28318f * 300.0f / RATE;
      }
      i2s.write((const uint8_t*)out, sizeof(out));
    }
    memset(out, 0, sizeof(out));
    for (int k = 0; k < 2; k++) i2s.write((const uint8_t*)out, sizeof(out));
  }
  reg(0x32, VOLUME_REG);
  memset(out, 0, sizeof(out));
  i2s.write((const uint8_t*)out, sizeof(out));
  Serial.println("essai terminé, volume remis à la normale");
  speaking = false;
}

void speakerTask(void*) {
  for (;;) {
    // A stop between two sounds has already canceled requests (stop): it is forgotten before taking one,
    // and a stop arriving afterward cuts the sound that starts.
    stopRequest = false;
    if (sweepRequest) {
      sweepRequest = false;
      playSweep();
      continue;
    }
    if (demoRequest) {
      demoRequest = false;
      playDemo();
      continue;
    }
    if (pcmRequest) {
      pcmRequest = false;
      playMono(pcmData, 0, pcmReady, &pcmLimit);
      continue;
    }
    if (chimeRequest) {
      chimeRequest = false;
      playChime();
      continue;
    }
    if (!hasRequest && cryRequest) {
      uint8_t sound = cryRequest;
      cryRequest = 0;
      playCry(sound, cryVariant);
      continue;
    }
    if (!hasRequest) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    char path[64];
    snprintf(path, sizeof(path), "/voix/%s.wav", requested);
    hasRequest = false;
    playFile(path);
  }
}
}  // <source> namespace </source>

namespace Microphone {

bool begin() {
  // Measured sequence on the test bench: audio clock first, then codec reset to silent, zero volume, powered on, followed by gradual mute lift. Another sequence (codec cut before clock) causes a click.
  i2s.setPins(PIN_BCLK, PIN_LRCK, PIN_TO_CODEC, PIN_FROM_CODEC, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  if (!codecInit()) return false;
  rampVolume();
  audioReady = true;
  xTaskCreatePinnedToCore(micTask, "micro", 4096, nullptr, 1, nullptr, 0);
  return true;
}

bool takeNoise() {
  if (!noise) return false;
  noise = false;
  return true;
}

float levelDbfs() { return level; }

bool listen(uint32_t maxMs) {
  if (!audioReady || listening()) return false;
  if (!listenBuf) listenBuf = (int16_t*)ps_malloc(size_t(LISTEN_MAX) * 2);
  if (!listenBuf) return false;
  listenLen = 0;
  listenMaxMs = maxMs < 10000 ? maxMs : 10000;
  listenStop = listenByHand = false;
  listenRequest = true;
  return true;
}

void endListening() { listenStop = true; }
bool listening() { return listenRequest || listenOn; }

// Manual stop at UNO Q entry; the question remains even if uttered too softly for SpeechEnd.
const int16_t* heard(int& samples) {
  samples = listening() || !listenBuf || !(speechEnd.heard() || listenByHand) ? 0 : listenLen;
  return listenBuf;
}

}  // <source> namespace Microphone </source>

namespace Speaker {

bool begin() {
  if (!audioReady) return false;
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  sdReady = SD_MMC.begin("/sdcard", true);   // Bus 1-bit (as per Waveshare example)
  flashReady = FFat.begin(false);             // Unformatted; will be formatted on first received sentence.
  Preferences prefs;
  if (prefs.begin("moodimos", true)) {
    cryNotch = prefs.getUChar("cris", cryNotch);
    cryOnNotch = prefs.getUChar("cris_on", cryOnNotch);
    prefs.end();
  }
  if (cryNotch > 10) cryNotch = 10;
  if (cryOnNotch < 1 || cryOnNotch > 10) cryOnNotch = 5;
  xTaskCreatePinnedToCore(speakerTask, "voix", 6144, nullptr, 2, nullptr, 0);
  return sdReady || flashReady;
}

const char* storage() {
  static char s[64];
  if (sdReady) return "carte TF";
  if (!flashReady) return "aucune (ni carte TF ni phrase reçue)";
  snprintf(s, sizeof(s), "flash du torse, %u Ko libres sur %u", unsigned(FFat.freeBytes() / 1024),
           unsigned(FFat.totalBytes() / 1024));
  return s;
}

bool has(const char* name) {
  char path[64];
  return voicePath(name, path, sizeof(path)) && voiceFs(path);
}

bool storeOpen(const char* name) {
  if (storeFile || !audioReady || !voicePath(name, storePath, sizeof(storePath))) return false;
  if (!flashReady) flashReady = FFat.begin(true);   // First attempt: formatting, a few seconds.
  if (!flashReady) return false;
  FFat.mkdir("/voix");
  storeFile = FFat.open(storePath, FILE_WRITE);
  return bool(storeFile);
}

bool storeWrite(const uint8_t* data, size_t n) { return storeFile && storeFile.write(data, n) == n; }

bool storeClose(bool keep) {
  if (!storeFile) return false;
  storeFile.close();
  if (!keep) FFat.remove(storePath);   // reception failed: no cut phrase detected
  return keep;
}

bool play(const char* name) {
  char path[64];
  if (!voicePath(name, path, sizeof(path))) return false;
  fs::FS* fs = voiceFs(path);
  if (!fs) return false;
  File f = fs->open(path);
  if (!f) return false;
  uint8_t head[1024];
  size_t n = f.read(head, sizeof(head));
  f.close();
  WavInfo w;
  if (!WavFile::parse(head, n, w) || !WavFile::playable(w)) return false;
  if (speaking) stopRequest = true;         // the new phrase replaces the previous one
  strncpy(requested, name, sizeof(requested) - 1);
  hasRequest = true;
  return true;
}

bool playing() { return speaking || hasRequest || pcmRequest || cryRequest; }
bool talking() { return (speaking && !crying) || hasRequest || pcmRequest; }
bool cry(uint8_t sound, uint8_t variant) {
  if (!audioReady || cryNotch == 0 || speaking || hasRequest || pcmRequest || cryRequest) return false;
  if (sound == MoodSounds::NONE || sound >= MoodSounds::COUNT) return false;
  cryVariant = variant;
  cryRequest = sound;
  return true;
}
uint8_t cryLevel() { return cryNotch; }
void setCryLevel(uint8_t level) {
  cryNotch = level > 10 ? 10 : level;
  if (cryNotch) cryOnNotch = cryNotch;
  Preferences prefs;
  if (prefs.begin("moodimos", false)) {
    prefs.putUChar("cris", cryNotch);
    prefs.putUChar("cris_on", cryOnNotch);
    prefs.end();
  }
}
void toggleCries() { setCryLevel(cryNotch ? 0 : cryOnNotch); }
void chime() { chimeRequest = true; }
void sayDemo() { demoRequest = true; }
bool playPcm(const int16_t* pcm, int samples, const volatile int* ready) {
  if (!audioReady || !pcm || samples <= 0) return false;
  pcmData = pcm;
  pcmLimit.store(samples);
  pcmReady = ready;
  pcmRequest = true;
  return true;
}
bool extendPcm(int samples) {
  int cur = pcmLimit.load();
  while (cur >= 0 && cur < samples)
    if (pcmLimit.compare_exchange_weak(cur, samples)) return true;
  return cur >= samples;
}
void sweep() { sweepRequest = true; }
void setVolume(uint8_t reg32) { reg(0x32, reg32); }
uint8_t amplitude() { return mouthAmp; }
// Stop active audio and discard pending messages: phrase, announcement, scream, chime, demo, test run.
void stop() {
  hasRequest = false;
  pcmRequest = false;
  cryRequest = 0;
  chimeRequest = false;
  demoRequest = false;
  sweepRequest = false;
  stopRequest = true;
}

}  // <source> namespace Speaker </source>

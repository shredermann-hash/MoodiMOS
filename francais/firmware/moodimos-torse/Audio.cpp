// Codec ES8311 partagé : micro (Microphone.h) et haut-parleur (Speaker.h) sur le même bus I2S.
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
// Brochage repris des exemples Waveshare (04_es8311_example, 01_audio_out) :
// GPIO16 = données vers le codec (lecture), GPIO14 = données du codec (enregistrement).
constexpr int PIN_MCLK = 12, PIN_BCLK = 13, PIN_LRCK = 15, PIN_TO_CODEC = 16, PIN_FROM_CODEC = 14;
constexpr int PIN_SD_CLK = 11, PIN_SD_CMD = 10, PIN_SD_D0 = 9;
constexpr uint32_t RATE = 16000;
// Après un son joué, le micro reste sourd encore un peu : la fin du son attend dans la file du haut-parleur
// (≈ 90 ms) quand la lecture rend la main, et un cri fort se serait entendu lui-même comme un bruit fort.
constexpr uint32_t ECHO_MS = 250;
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t VOLUME_REG = 0xC8;   // registre 0x32 (0 … 255) : 200, choisi à l'oreille au banc le 2026-10-03 avec la voix Sandy
                                       // (haut-parleur du kit à l'air libre : à revoir une fois en boîte)

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

// Séquence du pilote Espressif/Waveshare (es8311_init, microphone_config, volume),
// réduite à 16 kHz avec MCLK = 256 × 16 kHz = 4,096 MHz sur la broche MCLK.
bool codecInit() {
  uint8_t v;
  if (!reg(0x00, 0x1F)) return false;           // reset
  delay(20);
  reg(0x00, 0x00);
  reg(0x00, 0x80);                              // mise sous tension
  reg(0x31, 0x60);                              // DAC en sourdine, levée par rampVolume() (pas de pop)
  reg(0x01, 0x3F);                              // toutes les horloges, MCLK depuis la broche
  if (regRead(0x06, v)) reg(0x06, uint8_t((v & 0xE0) | (0x04 - 1)));   // SCLK non inversé, diviseur BCLK
  if (regRead(0x02, v)) reg(0x02, uint8_t(v & 0x07));                  // pré-diviseur 1, multiplicateur 1
  reg(0x03, 0x10);                              // vitesse simple, OSR ADC
  reg(0x04, 0x10);                              // OSR DAC
  reg(0x05, 0x00);                              // diviseurs ADC et DAC = 1
  if (regRead(0x07, v)) reg(0x07, uint8_t(v & 0xC0));
  reg(0x08, 0xFF);                              // LRCK = MCLK / 256
  if (regRead(0x00, v)) reg(0x00, uint8_t(v & 0xBF));                  // esclave I2S
  reg(0x09, 0x0C);                              // entrée série 16 bits
  reg(0x0A, 0x0C);                              // sortie série 16 bits
  reg(0x0D, 0x01);                              // alimentation analogique
  reg(0x0E, 0x02);                              // PGA et modulateur ADC
  reg(0x12, 0x00);                              // DAC alimenté
  reg(0x13, 0x10);                              // sortie vers l'étage haut-parleur
  reg(0x1C, 0x6A);                              // égaliseur ADC contourné, offset continu retiré
  reg(0x37, 0x08);                              // égaliseur DAC contourné
  reg(0x32, 0x00);                              // volume à zéro : monté en douceur par rampVolume() (pas de pop)
  reg(0x17, 0xC8);                              // gain ADC
  return reg(0x14, 0x1A);                       // micro analogique, gain PGA maximal
}

// Monte le volume de 0 à VOLUME_REG une fois l'horloge audio lancée : le saut de niveau à l'allumage
// du convertisseur ne claque plus dans le haut-parleur.
void rampVolume() {
  reg(0x31, 0x00);                         // fin de sourdine, volume encore à zéro
  for (int v = 0; v <= VOLUME_REG; v += 6) {
    reg(0x32, uint8_t(v));
    delay(8);
  }
  reg(0x32, VOLUME_REG);
}

// ── Micro ──
SoundMeter meter;
volatile bool noise = false;
volatile float level = -90.3f;
// Écoute d'une question : la tâche du micro garde la voix en PSRAM jusqu'à la fin décidée par SpeechEnd.
constexpr int LISTEN_MAX = int(RATE) * 10;   // 10 s
int16_t* listenBuf = nullptr;
volatile int listenLen = 0;
volatile uint32_t listenMaxMs = 0;
volatile bool listenRequest = false, listenOn = false, listenStop = false, listenByHand = false;
SpeechEnd speechEnd;
int listenBlock = 0;   // échantillons gardés depuis le dernier bloc de 50 ms

// ── Haut-parleur ──
MouthEnvelope mouth;
volatile bool speaking = false, stopRequest = false;
volatile uint8_t mouthAmp = 0;
char requested[48] = {0};
volatile bool hasRequest = false;
volatile bool chimeRequest = false, demoRequest = false, sweepRequest = false;
const int16_t* volatile pcmData = nullptr;   // son reçu en mémoire (annonce du Mac), lu par la tâche voix
// Sa longueur, qui peut grandir pendant la lecture (suite d'une réponse) ; -1 : lecture finie, plus de suite.
std::atomic<int> pcmLimit{-1};
const volatile int* volatile pcmReady = nullptr;   // échantillons déjà reçus, quand le son arrive encore
volatile bool pcmRequest = false;
volatile uint8_t cryRequest = 0, cryVariant = 0;   // cri d'humeur demandé (MoodSounds::Sound)
volatile bool crying = false;
uint8_t cryNotch = 5;                               // volume des cris : 0 … 10, relu dans la flash au démarrage
uint8_t cryOnNotch = 5;                             // dernier volume non nul : le bouton de l'écran y revient
constexpr float CRY_GAIN_STEP = 0.2f;               // 5 = aussi fort que la voix, 10 = deux fois plus
bool sdReady = false;
bool flashReady = false;   // partition FAT de 9 Mo du torse : phrases reçues du Mac (commande « deposer »)
File storeFile;            // phrase en cours de réception
char storePath[64];

// Fichier d'une phrase : sur la carte TF d'abord, sinon dans la mémoire flash du torse.
fs::FS* voiceFs(const char* path) {
  if (sdReady && SD_MMC.exists(path)) return &SD_MMC;
  if (flashReady && FFat.exists(path)) return &FFat;
  return nullptr;
}

// Un nom de phrase, pas un chemin : « bonjour » → /voix/bonjour.wav
bool voicePath(const char* name, char* path, size_t size) {
  if (!name || !*name || strlen(name) >= sizeof(requested)) return false;
  if (strchr(name, '/') || strstr(name, "..")) return false;
  snprintf(path, size, "/voix/%s.wav", name);
  return true;
}

// Garde un morceau de la question ; l'écoute finit au silence qui suit la parole, à la durée maximale ou sur demande.
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
  static int16_t buf[512];      // stéréo entrelacé
  static int16_t mono[256];
  uint32_t spokeAt = 0;
  bool echo = false;
  for (;;) {
    size_t got = i2s.readBytes((char*)buf, sizeof(buf));
    int frames = int(got / 4);
    // sa propre voix n'est pas un bruit fort (durée écoulée, pas une échéance : juste même après 25 jours muet)
    if (speaking) { spokeAt = millis(); echo = true; }
    if (echo && millis() - spokeAt < ECHO_MS) continue;
    echo = false;
    for (int i = 0; i < frames; i++) mono[i] = buf[2 * i];   // voie gauche
    if (meter.feed(mono, frames, millis())) noise = true;
    level = meter.levelDbfs();
    if (listenRequest) {                // l'écoute part du fond sonore appris avant elle
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
    for (int i = 0; i < frames; i++) {   // toujours envoyé en stéréo, voix copiée sur les deux voies
      int16_t s = w.channels == 2 ? in[2 * i] : in[i];
      out[2 * i] = out[2 * i + 1] = s;
      in[i] = s;
    }
    mouth.feed(in, frames);
    mouthAmp = mouth.amplitude();
    i2s.write((const uint8_t*)out, size_t(frames) * 4);
  }
  f.close();
  memset(out, 0, sizeof(out));          // un peu de silence : pas de clic en fin de phrase
  i2s.write((const uint8_t*)out, sizeof(out));
  mouthAmp = 0;
  speaking = false;
}

// Deux notes douces (sol puis do, sinus à 25 % de la pleine échelle) avec attaque et fin arrondies :
// un son grave et rond pour essayer le haut-parleur sans le stresser.
void playChime() {
  static int16_t out[512];                  // 256 images stéréo
  const float notes[2] = {392.0f, 523.25f};
  const int total = RATE * 45 / 100;        // 0,45 s par note
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

// Son PCM mono 16 kHz en mémoire : la phrase de test (DemoVoice.h) ou un son reçu par l'USB (annonce, réponse), joué
// pendant qu'il arrive encore si `ready` compte les échantillons reçus (la réception va environ six fois plus vite).
// `limit` : sa longueur, qui peut grandir pendant la lecture (Speaker::extendPcm) ; arrivée au bout, la lecture la
// ferme (-1) d'un seul coup, ou continue si une suite vient de l'allonger : jamais les deux.
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
      continue;                                  // une suite vient d'arriver
    }
    int frames = end - pos < 256 ? end - pos : 256;
    for (uint32_t t = millis(); ready && *ready < pos + frames && !stopRequest && millis() - t < 1000;) vTaskDelay(1);
    if (ready && *ready < pos + frames) break;   // réception arrêtée : on ne joue pas ce qui n'est pas arrivé
    for (int i = 0; i < frames; i++) {
      in[i] = pcm[pos + i];
      out[2 * i] = out[2 * i + 1] = in[i];
    }
    mouth.feed(in, frames);
    mouthAmp = mouth.amplitude();
    i2s.write((const uint8_t*)out, size_t(frames) * 4);
    pos += frames;
  }
  if (limit && !closed) limit->store(-1);        // arrêtée ou son incomplet : plus de suite
  memset(out, 0, sizeof(out));
  i2s.write((const uint8_t*)out, sizeof(out));
  mouthAmp = 0;
  speaking = false;
}

void playDemo() { playMono(DemoVoice::DATA, DemoVoice::SAMPLES); }

// Cri d'humeur synthétisé au fil de la lecture. Toute phrase demandée le coupe, avec un fondu de 4 ms.
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

// Banc : sinus continu à 300 Hz (80 % de la pleine échelle) pendant que le volume du codec monte par
// paliers de 2 s, de 0xA0 à 0xDF. Chaque palier est annoncé sur le port série. Le registre 0x32 vaut 0 dB à
// 0xBF : au-delà, du gain numérique. On rend le volume normal à la fin.
void playSweep() {
  static int16_t out[512];
  speaking = true;
  float phase = 0;
  for (int level = 0xA0; level <= 0xDF && !stopRequest; level += 0x08) {
    reg(0x32, uint8_t(level));
    Serial.printf("palier 0x%02X\n", level);
    for (int n = 0; n < RATE * 2 / 256 && !stopRequest; n++) {
      for (int i = 0; i < 256; i++) {
        float env = n == 0 && i < 128 ? i / 128.0f : 1.0f;   // petite attaque : pas de claquement
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
    // Un arrêt reçu entre deux sons a déjà annulé les demandes (stop) : il est oublié avant d'en prendre une,
    // et un arrêt arrivé ensuite coupe le son qui démarre.
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
}  // namespace

namespace Microphone {

bool begin() {
  // Ordre mesuré au banc : horloge audio d'abord, puis codec remis à zéro, en sourdine et à volume zéro,
  // alimenté, puis sourdine levée en rampe. Un autre ordre (codec coupé avant l'horloge) fait claquer.
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

// Arrêtée à la main (Entrée sur la UNO Q), la question est gardée même si elle a été dite trop bas pour SpeechEnd.
const int16_t* heard(int& samples) {
  samples = listening() || !listenBuf || !(speechEnd.heard() || listenByHand) ? 0 : listenLen;
  return listenBuf;
}

}  // namespace Microphone

namespace Speaker {

bool begin() {
  if (!audioReady) return false;
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  sdReady = SD_MMC.begin("/sdcard", true);   // bus 1 bit, comme l'exemple Waveshare
  flashReady = FFat.begin(false);             // jamais formatée : elle le sera à la première phrase reçue
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
  if (!flashReady) flashReady = FFat.begin(true);   // la première fois : formatage, quelques secondes
  if (!flashReady) return false;
  FFat.mkdir("/voix");
  storeFile = FFat.open(storePath, FILE_WRITE);
  return bool(storeFile);
}

bool storeWrite(const uint8_t* data, size_t n) { return storeFile && storeFile.write(data, n) == n; }

bool storeClose(bool keep) {
  if (!storeFile) return false;
  storeFile.close();
  if (!keep) FFat.remove(storePath);   // réception ratée : pas de phrase coupée
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
  if (speaking) stopRequest = true;         // la nouvelle phrase remplace la précédente
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
// Arrête le son en cours et oublie ceux qui attendaient : phrase, annonce, cri, carillon, démo, essai.
void stop() {
  hasRequest = false;
  pcmRequest = false;
  cryRequest = 0;
  chimeRequest = false;
  demoRequest = false;
  sweepRequest = false;
  stopRequest = true;
}

}  // namespace Speaker

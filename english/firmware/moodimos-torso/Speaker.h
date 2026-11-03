// Torso speaker: WAV phrases (/voix/<name>.wav) read from the TF card, otherwise from the torso’s flash memory (9 MB FAT partition, populated by Mac via command « deposer »), 16-bit PCM at 16 kHz, mono/stereo. Played in its own task; the microphone is ignored while the robot speaks.
#pragma once
#include <stdint.h>

namespace Speaker {

bool begin();                   // Assemble and flash the TF card; ensure `Microphone::begin()` succeeds.
bool play(const char* name);    // `/voice/bonjour.wav` → `/voice/bonjour.wav` (plays if present and valid format); error if missing or invalid format.
bool has(const char* name);     // present phrase (TF card or flash)
const char* storage();          // Where are the state phrases for *« etat »*?
// Received phrase from Mac, stored in flash (formatted first); `storeClose(false)` erases it.
bool storeOpen(const char* name);
bool storeWrite(const uint8_t* data, size_t n);
bool storeClose(bool keep);
bool playing();                 // phrase or sound: the torso’s microphone and IMU are ignored.
bool talking();                 // A single phrase: the mouth of the head follows it, not the screams.
// Synthesized mood cry (MoodSounds.h), never over a sentence; a sentence cuts it off.
bool cry(uint8_t sound, uint8_t variant);
uint8_t cryLevel();             // sound volume (0 = silent) to 10 (max), stored in the torso’s flash memory.
void setCryLevel(uint8_t level);
void toggleCries();             // screen button: mute, or reset to last selected volume
void sayDemo();                 // Test phrase embedded in firmware (bench, without TF card)
// Stored audio, PCM 16-bit mono 16 kHz (announcement, response).
// `ready`: samples already received; if sound arrives while playing, playback waits for each segment (max 1 second) before proceeding.
bool playPcm(const int16_t* pcm, int samples, const volatile int* ready = nullptr);
// Continuation of `playPcm` sound: plays phrase by phrase; lasts `samples` without interruption; returns `false` if
// already finished (must play continuation itself).
bool extendPcm(int samples);
void setVolume(uint8_t reg32);  // register 0x32 of the codec, 0–255 (bank; reset to VOLUME_REG on reboot)
void sweep();                   // loud sine wave, codec volume step detection (bench: identify where it squeaks)
void chime();                   // Two gentle notes to test the speaker (test bench)
uint8_t amplitude();            // mouth opening 0…255 for SPEAK frame
void stop();

}  // <source> namespace Speaker </source>

// Torso microphone: ES8311 codec + I2S 16 kHz, read in its own task.
#pragma once
#include <stdint.h>

namespace Microphone {

bool begin();          // Board::begin() must have initialized I2C.
bool takeNoise();      // Execute once after each loud noise.
float levelDbfs();     // level of the latest 50 ms block, in dBFS

// Command "listen" (triggered by "ecoute"):
// Audio, mono, 16 kHz, stored in memory until the following silence (SpeechEnd.h), max duration 10 s or until `endListening()`.
bool listen(uint32_t maxMs);
void endListening();
bool listening();
const int16_t* heard(int& samples);   // After listening: question, samples = 0 if no one spoke

}  // <source> namespace Microphone </source>

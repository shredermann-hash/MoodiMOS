// Micro du torse : codec ES8311 + I2S 16 kHz, lu dans sa propre tâche.
#pragma once
#include <stdint.h>

namespace Microphone {

bool begin();          // Board::begin() doit avoir initialisé l'I2C
bool takeNoise();      // vrai une fois après chaque bruit fort
float levelDbfs();     // niveau du dernier bloc de 50 ms, dBFS

// Écoute d'une question (commande « ecoute ») : la voix, mono 16 kHz, est gardée en mémoire jusqu'au silence
// qui la suit (SpeechEnd.h), à maxMs au plus (10 s), ou jusqu'à endListening().
bool listen(uint32_t maxMs);
void endListening();
bool listening();
const int16_t* heard(int& samples);   // après l'écoute : la question, samples = 0 si personne n'a parlé

}  // namespace Microphone

// Enveloppe de la voix → ouverture de la bouche (0 … 255) pour la trame SPEAK.
// Blocs de 20 ms ; −45 dBFS = bouche fermée, −5 dBFS = grande ouverte ; ouverture rapide,
// fermeture plus douce pour éviter le claquement de mâchoire. Logique pure, testée.
#pragma once
#include <math.h>
#include <stdint.h>

class MouthEnvelope {
 public:
  static constexpr int BLOCK = 320;   // 20 ms à 16 kHz

  void reset() { sum_ = 0; count_ = 0; value_ = 0; }

  void feed(const int16_t* s, int n) {
    for (int i = 0; i < n; i++) {
      float v = s[i];
      sum_ += v * v;
      if (++count_ == BLOCK) {
        float rms = sqrtf(float(sum_ / BLOCK));
        float db = rms < 1 ? -90.3f : 20 * log10f(rms / 32768.0f);
        float target = (db + 45) / 40;
        target = target < 0 ? 0 : target > 1 ? 1 : target;
        value_ += (target > value_ ? 0.7f : 0.25f) * (target - value_);
        sum_ = 0;
        count_ = 0;
      }
    }
  }

  uint8_t amplitude() const { return uint8_t(lroundf(value_ * 255)); }

 private:
  double sum_ = 0;
  int count_ = 0;
  float value_ = 0;
};

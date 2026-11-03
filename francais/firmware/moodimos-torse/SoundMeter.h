// Niveau sonore et « bruit fort » à partir d'échantillons 16 bits. Logique pure, testée.
//
// Les échantillons sont regroupés en blocs de 50 ms ; chaque bloc donne un niveau en dBFS
// (0 = pleine échelle). Un fond sonore est appris lentement ; un bloc qui le dépasse
// nettement (et qui est fort dans l'absolu) déclenche un bruit fort.
#pragma once
#include <math.h>
#include <stdint.h>

class SoundMeter {
 public:
  static constexpr int BLOCK = 800;                 // 50 ms à 16 kHz
  static constexpr float JUMP_DB = 18;              // au-dessus du fond
  static constexpr float MIN_LOUD_DBFS = -30;       // en dessous : jamais un bruit fort
  static constexpr uint32_t COOLDOWN_MS = 3000;

  // Échantillons mono ; renvoie vrai quand un bruit fort vient d'être détecté.
  bool feed(const int16_t* s, int n, uint32_t now) {
    bool loud = false;
    for (int i = 0; i < n; i++) {
      float v = s[i];
      sum_ += v * v;
      if (++count_ == BLOCK) {
        loud |= endBlock(now);
        sum_ = 0;
        count_ = 0;
      }
    }
    return loud;
  }

  float levelDbfs() const { return level_; }        // dernier bloc
  float floorDbfs() const { return floor_; }        // fond sonore appris

 private:
  bool endBlock(uint32_t now) {
    float rms = sqrtf(float(sum_ / BLOCK));
    level_ = rms < 1 ? -90.3f : 20 * log10f(rms / 32768.0f);
    if (!primed_) {
      floor_ = level_;
      primed_ = true;
      return false;
    }
    bool loud = level_ > floor_ + JUMP_DB && level_ > MIN_LOUD_DBFS;
    // Le fond suit tout, bruits forts compris (≈ 2,5 s de constante de temps) : un claquement
    // le déplace à peine, mais une musique forte devient le nouveau fond en 2 s environ.
    floor_ += 0.02f * (level_ - floor_);
    if (loud && (!fired_ || uint32_t(now - firedAt_) >= COOLDOWN_MS)) {
      fired_ = true;
      firedAt_ = now;
      return true;
    }
    return false;
  }

  double sum_ = 0;
  int count_ = 0;
  float level_ = -90.3f, floor_ = -90.3f;
  bool primed_ = false, fired_ = false;
  uint32_t firedAt_ = 0;
};

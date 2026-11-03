// Estimation de la charge de la LiPo de la tête à partir de sa tension (pont diviseur, GPIO4).
// Courbe typique d'une LiPo au repos, tension lissée : c'est une estimation (±15 % environ),
// valable seulement tête détachée (sur le cou, la charge fausse la tension). Logique pure, testée.
#pragma once
#include <stdint.h>
#include "MoodiMOSHead.h"

class BatteryEstimate {
 public:
  void reset() { has_ = false; }

  void addSample(float volts) {
    if (volts < 2.5f || volts > 4.5f) return;           // batterie absente ou mesure aberrante
    v_ = has_ ? v_ + 0.1f * (volts - v_) : volts;   // ≈ 20 s à un échantillon toutes les 2 s
    has_ = true;
  }

  float volts() const { return has_ ? v_ : 0; }

  uint8_t percent() const {
    if (!has_) return MOODI_UNKNOWN;
    static const float V[] = {3.30f, 3.50f, 3.65f, 3.72f, 3.78f, 3.85f, 3.95f, 4.05f, 4.15f, 4.20f};
    static const float P[] = {0, 5, 12, 25, 40, 55, 70, 85, 97, 100};
    constexpr int N = sizeof(V) / sizeof(V[0]);
    if (v_ <= V[0]) return 0;
    if (v_ >= V[N - 1]) return 100;
    for (int i = 1; i < N; i++) {
      if (v_ <= V[i]) {
        float k = (v_ - V[i - 1]) / (V[i] - V[i - 1]);
        return uint8_t(P[i - 1] + k * (P[i] - P[i - 1]) + 0.5f);
      }
    }
    return 100;
  }

 private:
  float v_ = 0;
  bool has_ = false;
};

// Batterie de la tête (V2, LiPo 403040 ≈ 500 mAh) : détachée, la tension au repos donne la charge ;
// sur le cou, le chargeur de la carte la recharge par VBus et la tension mesurée dépasse celle au
// repos d'environ 0,1 V (retranchée), puis plafonne à 4,2 V en fin de charge (tension constante).
// Batterie faible sous 15 %, fin de l'alerte au-dessus de 20 % (hystérésis). Logique pure, testée.
class HeadBattery {
 public:
  static constexpr float CHARGE_OFFSET = 0.10f;   // V : surtension pendant la charge
  static constexpr float FULL_V = 4.17f;          // V : fin de charge (tension constante)
  static constexpr uint8_t LOW_PCT = 15, LOW_CLEAR_PCT = 20;

  void sample(float volts, bool docked) {
    if (docked != docked_) { est_.reset(); docked_ = docked; }   // la surtension change : on repart
    est_.addSample(docked ? volts - CHARGE_OFFSET : volts);
    full_ = docked && est_.volts() > 0 && volts >= FULL_V;
    uint8_t p = percent();
    if (docked_ || p == MOODI_UNKNOWN) low_ = false;
    else if (p <= LOW_PCT) low_ = true;
    else if (p >= LOW_CLEAR_PCT) low_ = false;
  }
  uint8_t percent() const { return full_ ? 100 : est_.percent(); }
  bool known() const { return percent() != MOODI_UNKNOWN; }
  bool charging() const { return docked_ && known() && !full_; }
  bool low() const { return low_; }   // détachée seulement : sur le cou, elle se recharge
  float volts() const { return est_.volts(); }

 private:
  BatteryEstimate est_;
  bool docked_ = false, full_ = false, low_ = false;
};

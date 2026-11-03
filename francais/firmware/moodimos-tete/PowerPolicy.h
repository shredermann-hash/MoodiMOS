// Économie d'énergie de la tête détachée (pendentif). Logique pure, testée.
//   Active : écran allumé, visage animé.
//   Doze   : après 20 s sans activité, rétroéclairage éteint et rendu arrêté ; l'IMU écoute.
//   Sleep  : après 5 min sans activité (1 min si posée à plat), veille profonde :
//            réveil au toucher, ou toutes les 30 s pour voir si elle est revenue sur le cou.
// Sur le cou, la tête reste active : le torse l'alimente et décide de son sommeil visuel.
#pragma once
#include <stdint.h>

class PowerPolicy {
 public:
  enum Mode : uint8_t { Active, Doze, Sleep };
  static constexpr uint32_t DOZE_MS = 20000;
  static constexpr uint32_t SLEEP_MS = 300000;
  static constexpr uint32_t SLEEP_LYING_MS = 60000;
  static constexpr uint32_t DOCK_CHECK_S = 30;   // réveil périodique en veille profonde

  // Toucher, geste IMU, commande série, cadran affiché…
  void activity(uint32_t now) { last_ = now; }

  Mode update(uint32_t now, bool docked, bool lying) {
    if (docked) {
      last_ = now;
      return mode_ = Active;
    }
    uint32_t idle = now - last_;
    if (idle >= (lying ? SLEEP_LYING_MS : SLEEP_MS)) mode_ = Sleep;
    else if (idle >= DOZE_MS) mode_ = Doze;
    else mode_ = Active;
    return mode_;
  }

  Mode mode() const { return mode_; }

 private:
  uint32_t last_ = 0;
  Mode mode_ = Active;
};

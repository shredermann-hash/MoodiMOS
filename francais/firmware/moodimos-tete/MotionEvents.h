// Gestes détectés à partir de l'IMU : soulevé, secoué, couché, inclinaison, et le geste
// « regarder l'heure » du pendentif. Logique pure, testée sur ordinateur.
// Copie identique dans moodimos-torse/ et moodimos-tete/, vérifiée par les tests.
//
// Repère du robot : droite (vu de face), haut (vers le sommet de la tête), avant (normale
// de l'écran, vers le spectateur). Un accéléromètre au repos mesure +1 g vers le haut.
// Axes : correspondance repère puce → repère robot, à vérifier au banc (commande « imu »).
#pragma once
#include <math.h>
#include <stdint.h>

class MotionEvents {
 public:
  // Axe de la puce pour chaque axe du robot : 1 = X, 2 = Y, 3 = Z ; négatif = sens inverse
  struct Axes { int8_t right, up, out; };

  struct Output {
    bool lift = false;           // soulevé
    bool shake = false;          // secoué
    bool jolt = false;           // à-coup : premier pic d'accélération (réaction immédiate, avant « secoué »)
    bool lookUp = false;         // pendentif levé pour regarder l'écran
    bool lyingChanged = false;   // passage couché ↔ debout (voir lying)
    bool lying = false;
    float tilt = 0;              // −1 … 1, > 0 = penché vers la droite vu de face
    float right = 0, up = 0, out = 0;   // accélération dans le repère robot (g), pour le banc
  };

  static constexpr uint32_t LYING_MS = 1500, STAND_MS = 300;
  static constexpr uint32_t LIFT_MS = 60, LIFT_COOLDOWN_MS = 2000;
  static constexpr uint32_t SHAKE_WINDOW_MS = 1000, SHAKE_COOLDOWN_MS = 3000, PEAK_GAP_MS = 80;
  static constexpr uint32_t LOOK_HOLD_MS = 250, LOOK_AFTER_HANG_MS = 1500, LOOK_COOLDOWN_MS = 3000;

  MotionEvents() : MotionEvents(Axes{1, 2, 3}) {}
  explicit MotionEvents(Axes axes) : ax_(axes) {}

  // Écart à 1 g qui compte comme un pic de secousse (1,0 par défaut : robot entier ; plus bas pour
  // la tête tenue en main, qu'on ne veut pas secouer violemment).
  void setShakeThreshold(float g) { shakeG_ = g; }
  // Délai entre deux secousses comptées (3 s par défaut ; plus court pour compter des secousses rapprochées)
  void setShakeCooldown(uint32_t ms) { shakeCooldown_ = ms; }

  // Échantillon IMU : a en g, g en °/s, repère de la puce. Environ 50 Hz.
  Output update(const float a[3], const float g[3], uint32_t now) {
    Output o;
    const float r = pick(a, ax_.right), u = pick(a, ax_.up), f = pick(a, ax_.out);
    o.right = r; o.up = u; o.out = f;
    const float mag = sqrtf(r * r + u * u + f * f);
    const float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    const bool still = fabsf(mag - 1) < 0.1f && spin < 12;

    // Couché : écran vers le ciel ou vers le sol, immobile.
    if (still && fabsf(f) > 0.85f) {
      if (!flat_) { flat_ = true; flatAt_ = now; }
    } else {
      flat_ = false;
    }
    if (!lying_ && flat_ && uint32_t(now - flatAt_) >= LYING_MS) { lying_ = true; o.lyingChanged = true; }
    if (lying_ && u > 0.6f) {
      if (!standing_) { standing_ = true; standAt_ = now; }
      if (uint32_t(now - standAt_) >= STAND_MS) {
        lying_ = false;
        o.lyingChanged = true;
        liftAt_ = now; lifted_ = true;   // relevé : c'est déjà l'événement, pas de « soulevé » en plus
      }
    } else {
      standing_ = false;
    }
    o.lying = lying_;

    // Secoué : au moins 3 pics d'accélération en 1 s. Un pic est compté au franchissement
    // du seuil (hystérésis) : une poussée longue ne compte qu'une fois.
    const float dev = fabsf(mag - 1);
    if (!peaking_ && dev > shakeG_ && uint32_t(now - peaks_[0]) >= PEAK_GAP_MS) {
      peaking_ = true;
      o.jolt = true;
      peaks_[2] = peaks_[1]; peaks_[1] = peaks_[0]; peaks_[0] = now;
      if (++peakCount_ > 3) peakCount_ = 3;
    } else if (peaking_ && dev < 0.6f * shakeG_) {
      peaking_ = false;
    }
    if (peakCount_ >= 3 && uint32_t(now - peaks_[2]) <= SHAKE_WINDOW_MS &&
        (!shaken_ || uint32_t(now - shakeAt_) >= shakeCooldown_)) {
      o.shake = true;
      shaken_ = true;
      shakeAt_ = now;
      peakCount_ = 0;
      liftAt_ = now; lifted_ = true;   // pas de « soulevé » pendant une secousse
    }

    // Soulevé : accélération vers le haut soutenue, robot debout.
    if (!lying_ && u - 1 > 0.25f && mag < 2.0f) {
      if (!rising_) { rising_ = true; riseAt_ = now; }
      if (uint32_t(now - riseAt_) >= LIFT_MS && (!lifted_ || uint32_t(now - liftAt_) >= LIFT_COOLDOWN_MS)) {
        o.lift = true;
        lifted_ = true;
        liftAt_ = now;
      }
    } else {
      rising_ = false;
    }

    // Inclinaison latérale, zone morte et lissage ; nulle couché.
    float target = 0;
    if (!lying_ && fabsf(r) > 0.07f) target = clamp(-r / 0.7f);
    tilt_ += 0.2f * (target - tilt_);
    o.tilt = tilt_;

    // Regarder l'heure : pendentif qui pendait (écran vertical) puis levé écran vers le haut.
    if (u > 0.8f && fabsf(f) < 0.4f) { hung_ = true; hangAt_ = now; }
    if (f > 0.5f && u < 0.85f && u > -0.3f) {
      if (!looking_) { looking_ = true; lookAt_ = now; }
      if (!lookFired_ && hung_ && uint32_t(now - lookAt_) >= LOOK_HOLD_MS &&
          uint32_t(lookAt_ - hangAt_) <= LOOK_AFTER_HANG_MS &&
          (!looked_ || uint32_t(now - lookedAt_) >= LOOK_COOLDOWN_MS)) {
        o.lookUp = true;
        lookFired_ = true;
        looked_ = true;
        lookedAt_ = now;
      }
    } else {
      looking_ = false;
      lookFired_ = false;
    }
    return o;
  }

 private:
  static float pick(const float v[3], int8_t axis) {
    int i = (axis < 0 ? -axis : axis) - 1;
    if (i < 0 || i > 2) return 0;
    return axis < 0 ? -v[i] : v[i];
  }
  static float clamp(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

  Axes ax_;
  float shakeG_ = 1.0f;
  uint32_t shakeCooldown_ = SHAKE_COOLDOWN_MS;
  bool flat_ = false, lying_ = false, standing_ = false;
  uint32_t flatAt_ = 0, standAt_ = 0;
  uint32_t peaks_[3] = {0, 0, 0};
  int peakCount_ = 0;
  bool peaking_ = false, shaken_ = false;
  uint32_t shakeAt_ = 0;
  bool rising_ = false, lifted_ = false;
  uint32_t riseAt_ = 0, liftAt_ = 0;
  float tilt_ = 0;
  bool hung_ = false, looking_ = false, lookFired_ = false, looked_ = false;
  uint32_t hangAt_ = 0, lookAt_ = 0, lookedAt_ = 0;
};

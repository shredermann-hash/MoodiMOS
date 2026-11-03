// Heure locale de la tête, recalée par le torse (trame TIME) ou à la main (moniteur série).
// Elle continue sur millis() quand la tête est détachée ; la RTC PCF85063 de la tête la garde
// pendant les coupures (sauvegarde à chaque recalage, relecture au démarrage).
#pragma once
#include <stdint.h>

class HeadClock {
 public:
  static constexpr uint32_t MIN_UNIX = 1700000000u;   // 2023 : en dessous, heure non réglée
  static constexpr int16_t MAX_TZ_MINUTES = 14 * 60;

  // Heure UTC + décalage local. Faux si les valeurs sont incohérentes.
  bool set(uint32_t unix, int16_t tzMinutes, uint32_t now) {
    if (unix < MIN_UNIX || tzMinutes > MAX_TZ_MINUTES || tzMinutes < -MAX_TZ_MINUTES) return false;
    return setLocal(uint32_t(int64_t(unix) + tzMinutes * 60), now);
  }

  // Heure locale complète (date comprise), par exemple relue dans la RTC.
  bool setLocal(uint32_t localEpoch, uint32_t now) {
    if (localEpoch < MIN_UNIX - MAX_TZ_MINUTES * 60) return false;
    localSec_ = localEpoch;
    setAt_ = now;
    valid_ = true;
    dateKnown_ = true;
    rev_++;
    return true;
  }

  // Réglage manuel : secondes depuis minuit, heure locale.
  bool setTimeOfDay(uint32_t secondsOfDay, uint32_t now) {
    if (secondsOfDay >= 86400) return false;
    localSec_ = secondsOfDay;
    setAt_ = now;
    valid_ = true;
    dateKnown_ = false;   // réglage d'essai : rien à sauvegarder dans la RTC
    rev_++;
    return true;
  }

  bool valid() const { return valid_; }
  uint32_t revision() const { return rev_; }   // change à chaque réglage

  // Heure locale complète pour la RTC ; faux si seule l'heure du jour est connue.
  bool localEpoch(uint32_t now, uint32_t& out) const {
    if (!valid_ || !dateKnown_) return false;
    out = localSec_ + (now - setAt_) / 1000;
    return true;
  }

  // Heures (0-24, fractionnaires), minutes et secondes fractionnaires pour le cadran ; le gousset
  // tourne modulo 12.
  bool hands(uint32_t now, float& h, float& m, float& s) const {
    if (!valid_) return false;
    uint32_t elapsed = now - setAt_;   // reste juste au rebouclage de millis()
    uint64_t ms = uint64_t(localSec_ % 86400) * 1000 + elapsed;
    ms %= 86400000ull;
    s = (ms % 60000) / 1000.0f;
    m = (ms % 3600000) / 60000.0f;
    h = float(ms / 3600000) + m / 60.0f;
    return true;
  }

 private:
  uint32_t localSec_ = 0, setAt_ = 0, rev_ = 0;
  bool valid_ = false, dateKnown_ = false;
};

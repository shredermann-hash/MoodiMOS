// Design du visage, choisi dans le menu de réglage de la tête (appui de 3 s) et gardé en mémoire :
// chacun le sien. Le design change le dessin (Face.cpp) et la couleur de repos (Mood.h) ; les
// humeurs, les gestes et les expressions restent les mêmes.
#pragma once
#include <stdint.h>
#include <string.h>
#include "MoodiMOSEdition.h"

enum class Design : uint8_t {
  Classique,   // visage MoodiMOS d'origine, bleu au repos
  Horloge,     // dessin animé à l'ancienne : cadran orangé, graduations, grands yeux à cils (édition complète)
  HorlogeEnPied,   // le même personnage en entier : bras à gants blancs, jambes, fond doré rayonnant
  Neon,        // édition complète : traits lumineux ambre sur fond noir, yeux en verre jaune
  Count
};

inline const char* designName(Design d) {
  static const char* const NAMES[] = {"classique", "horloge", "enpied", "neon"};
  return d < Design::Count ? NAMES[uint8_t(d)] : "?";
}

inline bool isClock(Design d) { return d == Design::Horloge || d == Design::HorlogeEnPied; }

// Designs proposés par cette édition : l'édition complète les a tous (néon par défaut), l'instructable n'a
// que le visage classique.
inline bool designAvailable(Design d) {
  if (d >= Design::Count) return false;
  if (d == Design::Neon) return Edition::NEON;
  return !isClock(d) || Edition::CLOCK_DESIGNS;
}

inline Design defaultDesign() { return Edition::NEON ? Design::Neon : Design::Classique; }

inline int designCount() {
  int n = 0;
  for (uint8_t i = 0; i < uint8_t(Design::Count); i++) n += designAvailable(Design(i));
  return n;
}

inline bool designFromName(const char* name, Design& out) {
  for (uint8_t i = 0; i < uint8_t(Design::Count); i++) {
    if (designAvailable(Design(i)) && strcmp(name, designName(Design(i))) == 0) { out = Design(i); return true; }
  }
  return false;
}

// Design proposé suivant (step = +1) ou précédent (−1), en boucle.
inline Design nextDesign(Design d, int step) {
  const int n = int(Design::Count);
  int k = int(d) < n ? int(d) : 0;
  for (int tries = 0; tries < n; tries++) {
    k = ((k + step) % n + n) % n;
    if (designAvailable(Design(k))) return Design(k);
  }
  return defaultDesign();
}

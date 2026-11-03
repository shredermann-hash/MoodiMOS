// Robot head face design selected from the head adjustment menu (hold for 3 sec) and retained in memory:
// each retains its own. The design updates the drawing (Face.cpp) and resting color (Mood.h); emotions, gestures, and expressions remain unchanged.
#pragma once
#include <stdint.h>
#include <string.h>
#include "MoodiMOSEdition.h"

enum class Design : uint8_t {
  Classique,   // Original MoodiMOS face, blue at rest.
  Horloge,     // Old-school animated design: orange dial, markings, large eyes with eyelashes (complete edition)
  HorlogeEnPied,   // entire figure: white-gloved arms, legs, golden radiant background
  Neon,        // Complete edition: amber glow lines on black background, yellow glass eyes
  Count
};

inline const char* designName(Design d) {
  static const char* const NAMES[] = {"classique", "horloge", "enpied", "neon"};
  return d < Design::Count ? NAMES[uint8_t(d)] : "?";
}

inline bool isClock(Design d) { return d == Design::Horloge || d == Design::HorlogeEnPied; }

// Designs proposed by this edition: Complete edition includes all designs (neon as default), while the Instructable only has the classic face.
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

// Proposed design based on (step = +1) or previous (−1), in a loop.
inline Design nextDesign(Design d, int step) {
  const int n = int(Design::Count);
  int k = int(d) < n ? int(d) : 0;
  for (int tries = 0; tries < n; tries++) {
    k = ((k + step) % n + n) % n;
    if (designAvailable(Design(k))) return Design(k);
  }
  return defaultDesign();
}

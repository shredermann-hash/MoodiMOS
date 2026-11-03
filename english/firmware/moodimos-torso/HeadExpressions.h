// Head expression names from `Expr` enum (in order) in `moodimos-head/Face.h`. The torso receives them by number (`NECK_STATE`) and applies them via number (`NECK_EXPRESSION`, command "tete"): names are used by the serial monitor. Verified for consistency with `exprName()` in the head via tests (`neck_tests`).
#pragma once
#include <stdint.h>
#include <string.h>

namespace HeadExpressions {

constexpr const char* NAMES[] = {"attentif", "content", "surpris", "rieur", "fache", "apaise",
                                 "mefiant", "dort", "baille", "etourdi", "clin",
                                 "emballe", "tenir", "curieux", "perplexe", "boude", "absent", "inquiet", "colere",
                                 "joie", "fatigue", "bisou"};
constexpr uint8_t COUNT = sizeof(NAMES) / sizeof(NAMES[0]);
constexpr uint8_t INQUIET = 17;   // light tremors (verified by neck_tests)
constexpr uint8_t MEFIANT = 6;    // MoodTriggers.h (decided by the torso)
constexpr uint8_t ETOURDI = 9;
constexpr uint8_t FATIGUE = 20;
constexpr uint8_t CONTENT = 1, SURPRIS = 2, RIEUR = 3, CLIN = 10, CURIEUX = 13, PERPLEXE = 14,
                  JOIE = 19;   // `GuessGame.h` (Guess Game header)

inline const char* name(uint8_t id) { return id < COUNT ? NAMES[id] : "?"; }

inline bool fromName(const char* s, uint8_t& id) {
  for (uint8_t i = 0; i < COUNT; i++) {
    if (!strcmp(s, NAMES[i])) { id = i; return true; }
  }
  return false;
}

}  // <source> namespace HeadExpressions </source>

// Face-head events sent by EV_FACE (from `moodimos-head/FaceEngine.h`), ordered by its enum Event (verified via neck_tests).
namespace HeadEvents {
constexpr int16_t TAP = 3;             // calms
constexpr int16_t UNANSWERABLE = 9;    // confused
constexpr int16_t IGNORED = 10;        // abandoned component
constexpr int16_t TICKLE = 12;         // *chatouilles* → tickles
constexpr int16_t KISS = 13;           // kiss
constexpr int16_t CHEER = 14;          // contented joy during an announcement
constexpr int16_t TIRED = 15;          // fatigued (dusk)
}  // <source> namespace HeadEvents </source>

// Noms des expressions de la tête, dans l'ordre de son enum Expr (moodimos-tete/Face.h). Le torse les reçoit
// par numéro (NECK_STATE) et les impose par numéro (NECK_EXPRESSION, commande « tete ») : les noms servent
// au moniteur série. Concordance avec exprName() de la tête vérifiée par les tests (neck_tests).
#pragma once
#include <stdint.h>
#include <string.h>

namespace HeadExpressions {

constexpr const char* NAMES[] = {"attentif", "content", "surpris", "rieur", "fache", "apaise",
                                 "mefiant", "dort", "baille", "etourdi", "clin",
                                 "emballe", "tenir", "curieux", "perplexe", "boude", "absent", "inquiet", "colere",
                                 "joie", "fatigue", "bisou"};
constexpr uint8_t COUNT = sizeof(NAMES) / sizeof(NAMES[0]);
constexpr uint8_t INQUIET = 17;   // secousses légères (vérifié par neck_tests)
constexpr uint8_t MEFIANT = 6;    // humeurs décidées par le torse (MoodTriggers.h)
constexpr uint8_t ETOURDI = 9;
constexpr uint8_t FATIGUE = 20;
constexpr uint8_t CONTENT = 1, SURPRIS = 2, RIEUR = 3, CLIN = 10, CURIEUX = 13, PERPLEXE = 14,
                  JOIE = 19;   // jeu « Devine l'animal » (GuessGame.h)

inline const char* name(uint8_t id) { return id < COUNT ? NAMES[id] : "?"; }

inline bool fromName(const char* s, uint8_t& id) {
  for (uint8_t i = 0; i < COUNT; i++) {
    if (!strcmp(s, NAMES[i])) { id = i; return true; }
  }
  return false;
}

}  // namespace HeadExpressions

// Événements du visage de la tête envoyés par EV_FACE, dans l'ordre de son enum Event
// (moodimos-tete/FaceEngine.h), vérifié par les tests (neck_tests).
namespace HeadEvents {
constexpr int16_t TAP = 3;             // apaise
constexpr int16_t UNANSWERABLE = 9;    // perplexe
constexpr int16_t IGNORED = 10;        // boude (partie abandonnée)
constexpr int16_t TICKLE = 12;         // chatouilles
constexpr int16_t KISS = 13;           // bisou
constexpr int16_t CHEER = 14;          // joie entretenue pendant une annonce
constexpr int16_t TIRED = 15;          // fatigué (pénombre)
}  // namespace HeadEvents

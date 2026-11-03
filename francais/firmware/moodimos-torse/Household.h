// Qui parle au robot (édition complète) : après le bouton bulle, l'écran « Qui parle ? » montre un rond par personne de la
// maison ; le rond touché envoie « bouton parler <nom> » au Mac (cerveau/conversation_robot.py), qui adapte ses
// réponses à la personne (cerveau/personnes.json). Les prénoms ne sont pas ici : Household_local.h, à côté de ce
// fichier, les donne (hors de tout ce qui se publie). Sans lui, pas d'écran : la bulle lance l'échange tout de suite.
//
//   // Household_local.h
//   constexpr Person PEOPLE[] = {
//       {"lea", "LÉA", 0xD8A8FF},   // nom envoyé au Mac, prénom affiché, couleur du rond
//       {"paul", "PAUL", 0x4CB8FF},
//   };
#pragma once
#include <stdint.h>
#include <string.h>

namespace Household {

struct Person {
  const char* id;      // nom envoyé au Mac, sans accent (personnes.json de cerveau/)
  const char* label;   // prénom affiché, en capitales
  uint32_t rgb;        // couleur du rond, 0xRRGGBB
};

#if defined(MOODIMOS_HOUSEHOLD)   // tests : une maison d'essai
#include MOODIMOS_HOUSEHOLD
constexpr int COUNT = sizeof(PEOPLE) / sizeof(PEOPLE[0]);
#elif __has_include("Household_local.h")
#include "Household_local.h"
constexpr int COUNT = sizeof(PEOPLE) / sizeof(PEOPLE[0]);
#else
constexpr Person PEOPLE[1] = {{"", "", 0}};
constexpr int COUNT = 0;
#endif
static_assert(COUNT <= 4, "l'écran « Qui parle ? » a quatre ronds");

}  // namespace Household

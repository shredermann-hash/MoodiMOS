// User interaction (q version): After the bubble button, the screen *"Qui parle?"* displays a circle per household member; touching a circle sends "bouton parler <nom>" to the Mac (brain/robot_conversation.py), which adjusts responses based on people.json (brain/). Prénoms are in Household_local.h (not published). Without it, no screen appears; the bubble triggers the exchange immediately.
// 
// ```
// // Household_local.h
// constexpr Person PEOPLE[] = {
//     {"lea", "LÉA", 0xD8A8FF},   // name sent to Mac, displayed name, circle color
//     {"paul", "PAUL", 0x4CB8FF},
// };
// ```
#pragma once
#include <stdint.h>
#include <string.h>

namespace Household {

struct Person {
  const char* id;      // `people.json` (brain/) sent to Mac, no accents
  const char* label;   // displayed first name, in uppercase letters
  uint32_t rgb;        // color of the round, 0xRRGGBB
};

#if defined(MOODIMOS_HOUSEHOLD)   // tests: test facility
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

}  // <source> namespace Household </source>

// Robot build documentation:
// - Copies identical in `moodimos-head/` and `moodimos-torso/`, verified via tests.
// - Head and torso compile with the same edition (body follows head).
// 
// Manual:
// - Neutral original edition. Adult/neutral colors (NeoCité palette), no cuddles: tap = calms, double tap = dial. Single face.
// 
// COMPLETE:
// - The robot of the published guide (cad/MoodiMOS_Robot). Inherits all from the old "small" edition (removed 2026-10-28): small interacts with it, always with an adult nearby, and is off when absent. Default neon amber face; clock-style (foot-level) and classic face designs in menu; lavender sleep (her choice); cuddles (kisses, pats); all moods. Torso dashboard and dial in neon.
// 
// Build options:
// - `-DMOODIMOS_EDITION=2` (manual) or `3` (COMPLETE), see `firmware/build.sh`/`firmware/flash.sh` (default: COMPLETE; `1` was the small edition). Default (Arduino IDE): COMPLETE.
#pragma once

#define MOODIMOS_INSTRUCTABLE 2
#define MOODIMOS_COMPLETE 3
#ifndef MOODIMOS_EDITION
#define MOODIMOS_EDITION MOODIMOS_COMPLETE
#endif
#if MOODIMOS_EDITION != MOODIMOS_INSTRUCTABLE && MOODIMOS_EDITION != MOODIMOS_COMPLETE
#error "MOODIMOS_EDITION : 2 (instructable) ou 3 (complete) ; l'édition petite (1) est devenue complete"
#endif

namespace Edition {
constexpr bool COMPLETE = MOODIMOS_EDITION == MOODIMOS_COMPLETE;
constexpr const char* NAME = COMPLETE ? "complete" : "instructable";
constexpr bool CUDDLES = COMPLETE;         // Quick taps (rapid taps, friction) and kiss (long press).
constexpr bool SOFT_SLEEP = COMPLETE;      // Mauve sleep, rosy cheeks and smile; otherwise, void-white mouth open
constexpr bool CLOCK_DESIGNS = COMPLETE;   // character clock (face and base): neutral edition lacks it
constexpr bool NEON = COMPLETE;            // Neon face (default), matching dashboard and control panel.
constexpr bool GAMES = COMPLETE;           // Game *"Guess the Animal"* on the torso screen
}  // namespace Edition

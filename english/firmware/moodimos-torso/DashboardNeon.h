// complete edition dashboard: the same measurements, head status, radar and 'Where is my head?' button as the other editions, drawn in amber neon on black to match the head's face. The head-removed and head-replaced sequences reuse the head's neon face, just as the other editions reuse theirs. Drawn into the screen's 320 × 480 buffer using the head canvas (Canvas.h, identical copy). The drawing code is independent of the display and can also be checked on a computer.
#pragma once
#include <stdint.h>
#include "Dashboard.h"

namespace DashboardNeon {

void draw(uint16_t* fb, const DashData& d, const DashTheme& th);
void drawWorried(uint16_t* fb, const DashTheme& th, float t);   // Head removed (t → s)
void drawJoy(uint16_t* fb, float t);                            // Head rests
void drawSleep(uint16_t* fb, float t);                          // sleeping head
bool ringButtonHit(int16_t x, int16_t y);
bool criesButtonHit(int16_t x, int16_t y);   // speaker button for rage cries
bool talkButtonHit(int16_t x, int16_t y);    // Bubble button: speak to MoodiMOS (Mac conversation)
// Game "Guess the Animal": trigger via foot pedal on control panel, followed by game screen (question + three buttons).
void drawGame(uint16_t* fb, const GameView& v);
bool playButtonHit(int16_t x, int16_t y);
int gameButtonHit(int16_t x, int16_t y);    // 0 yes, 1 no, 2 I don’t know, 3 end, −1 nothing
// After the bubble button: a round per household member (Household.h).
void drawWho(uint16_t* fb);
int whoButtonHit(int16_t x, int16_t y);     // person (Household::PEOPLE), Household::COUNT closes, −1 means none
void drawTimerDone(uint16_t* fb, float t);  // Finished timer: full screen, t in seconds
void drawAccess(uint16_t* fb, const char* cmd, float held);  // Access requested via serial port.

}  // namespace DashboardNeon

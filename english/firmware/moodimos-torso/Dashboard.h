// Dashboard for the torso: ST7796 320×480 portrait-mode display, drawn in a PSRAM buffer, then sent via a single block (no flickering).
#pragma once
#include <stdint.h>

struct DashData {
  bool climateValid, humidityValid, lightValid, soundValid;   // BME280, BH1750, microcontroller
  float tempC, humPct, pressHpa;
  uint16_t lux;
  uint8_t soundDb;
  bool cameraOn;              // Real-time capture from OV5640 camera
  bool powerValid, usb, charging, battery;   // battery: LiPo detected (V2; absent in V1)
  uint8_t batteryPct;
  bool headDocked, headOnline, headTalking;
  int headBatteryPct;         // −1 if unknown (V1: no battery)
  bool headCharging;          // V2: Head battery (LiPo) mounted on the neck *(with neck as support)*
  bool radarPresent, radarOnline;
  uint16_t radarCm;
  // Detachable head (pendant)
  bool awayStatus;            // Bluetooth status received
  uint8_t awayBatteryPct;     // `MOOD_UNKNOWN` if unknown
  uint32_t awaySeenS;         // … seconds
  uint8_t ringState;          // HeadFinder::Ring
  bool gameReady;             // Game "Guess the Animal" ready (complete edition, phrases stored in memory): foot pedal button.
  bool criesOn;               // audible mood cries: speaker button, crossed when silent
  uint8_t brain;              // Conversation (bubble button): 0 – no brain (grayed out), 1 – ready, 2 – in progress.
  uint32_t timerS;            // timer (complete edition): remaining seconds, in place of the title; `0` without timer
  uint8_t accessMin;          // Open access via long press: remaining minutes, instead of head state; 0 closed
  uint8_t wifi;               // TimeSource::wifi(): no icon without Wi-Fi, dimmed if it has not provided the time, lit otherwise
};

// Game screen "Devine l'animal" (complete edition, GuessGame.h)
struct GameView {
  const char* text;           // QUICK QUESTION, CURRENT PROPOSAL, OR PHRASE IN CAPS
  uint8_t round;              // question number (0: none)
  bool buttons;               // Active response buttons (otherwise off)
  int8_t pressed;             // button just pressed (0 = yes, 1 = no, 2 = don’t know) or −1
};

// Mood color scheme (MOOD pattern) for the head: retains the same dashboard design.
struct DashTheme {
  bool valid;                 // Default palette: default edition (prune or night blue)
  uint16_t bg, panel, accent; // RGB565
};

namespace Dashboard {
bool begin();
void draw(const DashData& d, const DashTheme& theme);
void drawWorried(const DashTheme& theme, float t);   // Head removed: inquisitive face (t → s)
void drawJoy(float t);                               // Head resting: extreme joy, gold of φ (t in s)
void drawSleep(const DashTheme& theme, float t);     // sleeping head: the torso sleeps with it (t in s)
const uint16_t* framebuffer();              // last drawing 320 × 480 RGB565 (test bench)
bool ringButtonHit(int16_t x, int16_t y);   // “Where is my head?” button, for a detached head
bool criesButtonHit(int16_t x, int16_t y);  // Speaker button to the right of the volume level: silent clicks or non-clicks
void drawGame(const GameView& v);
bool playButtonHit(int16_t x, int16_t y);   // Dashboard foot switch button: triggers clearance
bool talkButtonHit(int16_t x, int16_t y);   // Bubble button: speak to MoodiMOS (computer conversation)
int gameButtonHit(int16_t x, int16_t y);    // 0 yes, 1 no, 2 I don’t know, 3 end, −1 nothing
void drawWho();                             // “Who is speaking?” selection after the talk bubble (complete edition, Household.h)
int whoButtonHit(int16_t x, int16_t y);     // person, Household::COUNT close, −1 nothing
void drawTimerDone(float t);
void drawAccess(const char* cmd, float held);   // Access requested via serial port; hold: long support part (finished timer, complete edition); t in seconds.
}

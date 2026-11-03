// Neck dial (480×480): ivory enamel, steel track, Roman numerals traced,
// small second hand at 6 o’clock, Breguet-style blue needles. The fixed part is drawn once in `dial()`; each image only redraws the hands.
// 
// Neon (complete edition): the face displays time. Its glass eyes track time, replacing the mouth with large yellow glass digits (tableau de bord torso font); the two dots mark seconds, the bezel fills with dots per minute; the date appears below ("JEUDI 29", then month).
#pragma once
#include <stdint.h>
#include "Canvas.h"

class Watch {
 public:
  bool begin(int size, bool neon = false);   // Allocate the head dial and torso flip pad.
  uint16_t* dial() { return dial_; }
  uint16_t* scratch() { return scratch_; }
  // - h: 0–24 fractional (the collar rotates modulo 12)
  // - m: 0–60
  // - s: 0–60 fractional (the runner operates at 5 Hz)
  // - weekday: 0 = Sunday … 6 = Saturday; −1 if date is unknown
  // - day: month day
  // - month: 1 … 12
  void render(Canvas& c, float h, float m, float s, int weekday = -1, int day = 0, int month = 0);

 private:
  void renderDial(Canvas& c);
  void renderHands(Canvas& c, float h, float m, float s);
  void renderNeonDial(Canvas& c);
  void renderNeon(Canvas& c, float h, float m, float s, int weekday, int day, int month);
  bool neon_ = false;
  uint16_t* dial_ = nullptr;
  uint16_t* scratch_ = nullptr;
  int size_ = 0;
};

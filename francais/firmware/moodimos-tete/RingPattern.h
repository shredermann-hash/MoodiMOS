// Sonnerie « où est ma tête ? » : trois bips (150 ms, 100 ms de pause) puis 600 ms de
// silence, en boucle pendant la durée demandée. Logique pure, testée.
#pragma once
#include <stdint.h>

class RingPattern {
 public:
  static constexpr uint32_t BEEP_MS = 150, GAP_MS = 100, PAUSE_MS = 600;
  static constexpr uint32_t CYCLE_MS = 3 * BEEP_MS + 2 * GAP_MS + PAUSE_MS;   // 1,25 s

  void start(uint32_t now, uint32_t durationMs) {
    startAt_ = now;
    duration_ = durationMs;
    active_ = durationMs > 0;
  }
  void stop() { active_ = false; }

  bool active(uint32_t now) {
    if (active_ && uint32_t(now - startAt_) >= duration_) active_ = false;
    return active_;
  }

  bool buzzerOn(uint32_t now) {
    if (!active(now)) return false;
    uint32_t t = uint32_t(now - startAt_) % CYCLE_MS;
    for (int i = 0; i < 3; i++) {
      uint32_t beepStart = i * (BEEP_MS + GAP_MS);
      if (t >= beepStart && t < beepStart + BEEP_MS) return true;
    }
    return false;
  }

 private:
  uint32_t startAt_ = 0, duration_ = 0;
  bool active_ = false;
};

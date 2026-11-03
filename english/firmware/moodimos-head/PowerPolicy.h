// Energy savings for the detachable pendant head (logic tested).
// Active: screen on, animated face.
// Doze: after 20 s of inactivity, backlight off, rendering stopped; IMU idle.
// Sleep: after 5 min of inactivity (1 min if flat), deep sleep:
//        wake on touch, or every 30 s to check if it’s back on the neck.
// On the neck, the head remains active: the torso powers it and controls visual sleep.
#pragma once
#include <stdint.h>

class PowerPolicy {
 public:
  enum Mode : uint8_t { Active, Doze, Sleep };
  static constexpr uint32_t DOZE_MS = 20000;
  static constexpr uint32_t SLEEP_MS = 300000;
  static constexpr uint32_t SLEEP_LYING_MS = 60000;
  static constexpr uint32_t DOCK_CHECK_S = 30;   // Periodic wake-up from deep sleep

  // Touch, IMU gesture, serial command, displayed dial…
  void activity(uint32_t now) { last_ = now; }

  Mode update(uint32_t now, bool docked, bool lying) {
    if (docked) {
      last_ = now;
      return mode_ = Active;
    }
    uint32_t idle = now - last_;
    if (idle >= (lying ? SLEEP_LYING_MS : SLEEP_MS)) mode_ = Sleep;
    else if (idle >= DOZE_MS) mode_ = Doze;
    else mode_ = Active;
    return mode_;
  }

  Mode mode() const { return mode_; }

 private:
  uint32_t last_ = 0;
  Mode mode_ = Active;
};

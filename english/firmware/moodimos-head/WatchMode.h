// Head/bezel face alternation: piece flip (0.5 s), bezel 6 s, return with a wink. Pure logic, tested on computer.
#pragma once
#include <math.h>
#include <stdint.h>

class WatchMode {
 public:
  enum Scene : uint8_t { Face, Watch };
  static constexpr uint32_t FLIP_MS = 500;
  static constexpr uint32_t HOLD_MS = 6000;

  // Display time; no effect during flip or if dial is already present.
  bool show(uint32_t now) {
    if (flipping_ || scene_ == Watch) return false;
    startFlip(Watch, false, now);
    return true;
  }

  // Return to face (tapped, event); wink = blink on arrival.
  bool dismiss(uint32_t now, bool wink) {
    if (flipping_ || scene_ == Face) return false;
    startFlip(Face, wink, now);
    return true;
  }

  // Call per image. Return `true` only once when the face reappears and a wink is requested.
  bool update(uint32_t now) {
    bool winkNow = false;
    if (flipping_) {
      uint32_t t = now - flipAt_;
      if (t >= FLIP_MS / 2 && scene_ != target_) {
        scene_ = target_;
        winkNow = scene_ == Face && wink_;
      }
      if (t >= FLIP_MS) {
        flipping_ = false;
        if (scene_ == Watch) shownAt_ = now;
      }
    } else if (scene_ == Watch && uint32_t(now - shownAt_) >= HOLD_MS) {
      startFlip(Face, true, now);
    }
    return winkNow;
  }

  Scene scene() const { return scene_; }
  bool flipping() const { return flipping_; }

  // Apparent image width: 1 → 0 → 1 during flip.
  float squash(uint32_t now) const {
    if (!flipping_) return 1;
    float p = float(uint32_t(now - flipAt_)) / FLIP_MS;
    if (p > 1) p = 1;
    float sx = fabsf(cosf(3.14159265f * p));
    return sx < 0.02f ? 0.02f : sx;
  }

 private:
  void startFlip(Scene to, bool wink, uint32_t now) {
    target_ = to;
    wink_ = wink;
    flipping_ = true;
    flipAt_ = now;
  }

  Scene scene_ = Face, target_ = Face;
  bool flipping_ = false, wink_ = false;
  uint32_t flipAt_ = 0, shownAt_ = 0;
};

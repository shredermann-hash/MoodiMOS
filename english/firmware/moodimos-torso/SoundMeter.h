// Sound level and "loud noise" from 16-bit samples. Pure logic, tested.
// 
// Samples grouped in 50 ms blocks; each block yields a dBFS level (0 = full scale). A background noise profile is learned incrementally; a block exceeding it significantly (and being loud in absolute terms) triggers a loud noise event.
#pragma once
#include <math.h>
#include <stdint.h>

class SoundMeter {
 public:
  static constexpr int BLOCK = 800;                 // 50 ms @ 16 kHz
  static constexpr float JUMP_DB = 18;              // above the noise floor
  static constexpr float MIN_LOUD_DBFS = -30;       // Below: never a loud noise
  static constexpr uint32_t COOLDOWN_MS = 3000;

  // Single-channel samples; returns `true` when a loud noise has just been detected.
  bool feed(const int16_t* s, int n, uint32_t now) {
    bool loud = false;
    for (int i = 0; i < n; i++) {
      float v = s[i];
      sum_ += v * v;
      if (++count_ == BLOCK) {
        loud |= endBlock(now);
        sum_ = 0;
        count_ = 0;
      }
    }
    return loud;
  }

  float levelDbfs() const { return level_; }        // last block
  float floorDbfs() const { return floor_; }        // Ambient sound learned

 private:
  bool endBlock(uint32_t now) {
    float rms = sqrtf(float(sum_ / BLOCK));
    level_ = rms < 1 ? -90.3f : 20 * log10f(rms / 32768.0f);
    if (!primed_) {
      floor_ = level_;
      primed_ = true;
      return false;
    }
    bool loud = level_ > floor_ + JUMP_DB && level_ > MIN_LOUD_DBFS;
    // The background adapts fully, including loud sounds (≈ 2.5 s time constant): a sharp *clap* barely shifts it, but a loud music becomes the new baseline in ~2 s.
    floor_ += 0.02f * (level_ - floor_);
    if (loud && (!fired_ || uint32_t(now - firedAt_) >= COOLDOWN_MS)) {
      fired_ = true;
      firedAt_ = now;
      return true;
    }
    return false;
  }

  double sum_ = 0;
  int count_ = 0;
  float level_ = -90.3f, floor_ = -90.3f;
  bool primed_ = false, fired_ = false;
  uint32_t firedAt_ = 0;
};

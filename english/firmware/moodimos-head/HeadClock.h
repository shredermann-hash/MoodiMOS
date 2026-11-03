// Local time of the head, adjusted by the torso (frame `TIME`) or manually (serial monitor).
// It persists via `millis()` when the head is detached; the RTC PCF85063 on the head retains it during power cuts (saved on recalibration, read on startup).
#pragma once
#include <stdint.h>

class HeadClock {
 public:
  static constexpr uint32_t MIN_UNIX = 1700000000u;   // 2023: time not set (unadjusted)
  static constexpr int16_t MAX_TZ_MINUTES = 14 * 60;

  // UTC time + local offset. False if values are inconsistent.
  bool set(uint32_t unix, int16_t tzMinutes, uint32_t now) {
    if (unix < MIN_UNIX || tzMinutes > MAX_TZ_MINUTES || tzMinutes < -MAX_TZ_MINUTES) return false;
    return setLocal(uint32_t(int64_t(unix) + tzMinutes * 60), now);
  }

  // Complete local time (including date), as read from the RTC.
  bool setLocal(uint32_t localEpoch, uint32_t now) {
    if (localEpoch < MIN_UNIX - MAX_TZ_MINUTES * 60) return false;
    localSec_ = localEpoch;
    setAt_ = now;
    valid_ = true;
    dateKnown_ = true;
    rev_++;
    return true;
  }

  // Manual adjustment: seconds since midnight, local time.
  bool setTimeOfDay(uint32_t secondsOfDay, uint32_t now) {
    if (secondsOfDay >= 86400) return false;
    localSec_ = secondsOfDay;
    setAt_ = now;
    valid_ = true;
    dateKnown_ = false;   // test adjustment: no data to save in RTC
    rev_++;
    return true;
  }

  bool valid() const { return valid_; }
  uint32_t revision() const { return rev_; }   // change at each adjustment

  // Complete local time for RTC; false if only the day’s time is known.
  bool localEpoch(uint32_t now, uint32_t& out) const {
    if (!valid_ || !dateKnown_) return false;
    out = localSec_ + (now - setAt_) / 1000;
    return true;
  }

  // Clock hands (0–24, fractional), fractional minutes and seconds for the dial; the bezel rotates modulo 12.
  bool hands(uint32_t now, float& h, float& m, float& s) const {
    if (!valid_) return false;
    uint32_t elapsed = now - setAt_;   // Just the re-entry of `millis()` loop.
    uint64_t ms = uint64_t(localSec_ % 86400) * 1000 + elapsed;
    ms %= 86400000ull;
    s = (ms % 60000) / 1000.0f;
    m = (ms % 3600000) / 60000.0f;
    h = float(ms / 3600000) + m / 60.0f;
    return true;
  }

 private:
  uint32_t localSec_ = 0, setAt_ = 0, rev_ = 0;
  bool valid_ = false, dateKnown_ = false;
};

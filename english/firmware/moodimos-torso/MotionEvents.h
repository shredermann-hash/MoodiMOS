// Detected gestures from IMU: lifted, shaken, laid down, tilt, and the "check time" gesture from the pendant. Pure logic, tested on computer.
// 
// Identical copy in `moodimos-torso/` and `moodimos-head/`, verified by tests.
// 
// Robot reference frame: right (front view), up (toward the head’s summit), front (screen normal, toward the viewer). A resting accelerometer measures +1 g upward.
// 
// Axes: correspondence between chip reference → robot reference frame; verify at the test bench (command: `imu`).
#pragma once
#include <math.h>
#include <stdint.h>

class MotionEvents {
 public:
  // Robot axis shaft for each axis: 1 = X, 2 = Y, 3 = Z; negative = reverse direction
  struct Axes { int8_t right, up, out; };

  struct Output {
    bool lift = false;           // lifted
    bool shake = false;          // shaken
    bool jolt = false;           // jolt: the first acceleration peak, triggering an immediate response before the shake event
    bool lookUp = false;         // Earring raised to view the screen.
    bool lyingChanged = false;   // passage lying ↔ standing (see lying)
    bool lying = false;
    float tilt = 0;              // −1 … 1, > 0 = tilted to the right when viewed from the front
    float right = 0, up = 0, out = 0;   // Acceleration in the robot reference frame (g), for the test bench.
  };

  static constexpr uint32_t LYING_MS = 1500, STAND_MS = 300;
  static constexpr uint32_t LIFT_MS = 60, LIFT_COOLDOWN_MS = 2000;
  static constexpr uint32_t SHAKE_WINDOW_MS = 1000, SHAKE_COOLDOWN_MS = 3000, PEAK_GAP_MS = 80;
  static constexpr uint32_t LOOK_HOLD_MS = 250, LOOK_AFTER_HANG_MS = 1500, LOOK_COOLDOWN_MS = 3000;

  MotionEvents() : MotionEvents(Axes{1, 2, 3}) {}
  explicit MotionEvents(Axes axes) : ax_(axes) {}

  // Clearance of 1 g counts as a shock peak (default: 1.0 for the entire robot; lower for the head held by hand, to avoid violent shaking).
  void setShakeThreshold(float g) { shakeG_ = g; }
  // Delay between two counted shocks (default 3 s; shorter for counting closely spaced shocks)
  void setShakeCooldown(uint32_t ms) { shakeCooldown_ = ms; }

  // Sample IMU: acceleration in g, angular velocity in °/s, sensor reference frame. ~50 Hz.
  Output update(const float a[3], const float g[3], uint32_t now) {
    Output o;
    const float r = pick(a, ax_.right), u = pick(a, ax_.up), f = pick(a, ax_.out);
    o.right = r; o.up = u; o.out = f;
    const float mag = sqrtf(r * r + u * u + f * f);
    const float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    const bool still = fabsf(mag - 1) < 0.1f && spin < 12;

    // Laying: screen facing up or down, stationary.
    if (still && fabsf(f) > 0.85f) {
      if (!flat_) { flat_ = true; flatAt_ = now; }
    } else {
      flat_ = false;
    }
    if (!lying_ && flat_ && uint32_t(now - flatAt_) >= LYING_MS) { lying_ = true; o.lyingChanged = true; }
    if (lying_ && u > 0.6f) {
      if (!standing_) { standing_ = true; standAt_ = now; }
      if (uint32_t(now - standAt_) >= STAND_MS) {
        lying_ = false;
        o.lyingChanged = true;
        liftAt_ = now; lifted_ = true;   // reported: already the event, no additional "lift" (e.g., deadlift) included.
      }
    } else {
      standing_ = false;
    }
    o.lying = lying_;

    // Shaken: at least 3 acceleration peaks in 1 s. A peak is counted at threshold crossing (hysteresis): a sustained push counts once only.
    const float dev = fabsf(mag - 1);
    if (!peaking_ && dev > shakeG_ && uint32_t(now - peaks_[0]) >= PEAK_GAP_MS) {
      peaking_ = true;
      o.jolt = true;
      peaks_[2] = peaks_[1]; peaks_[1] = peaks_[0]; peaks_[0] = now;
      if (++peakCount_ > 3) peakCount_ = 3;
    } else if (peaking_ && dev < 0.6f * shakeG_) {
      peaking_ = false;
    }
    if (peakCount_ >= 3 && uint32_t(now - peaks_[2]) <= SHAKE_WINDOW_MS &&
        (!shaken_ || uint32_t(now - shakeAt_) >= shakeCooldown_)) {
      o.shake = true;
      shaken_ = true;
      shakeAt_ = now;
      peakCount_ = 0;
      liftAt_ = now; lifted_ = true;   // no "lifted" during a shock (or jolt)
    }

    // Lifted: sustained upward acceleration, robot standing upright.
    if (!lying_ && u - 1 > 0.25f && mag < 2.0f) {
      if (!rising_) { rising_ = true; riseAt_ = now; }
      if (uint32_t(now - riseAt_) >= LIFT_MS && (!lifted_ || uint32_t(now - liftAt_) >= LIFT_COOLDOWN_MS)) {
        o.lift = true;
        lifted_ = true;
        liftAt_ = now;
      }
    } else {
      rising_ = false;
    }

    // Lateral tilt, dead zone, and smoothing; zero resting position.
    float target = 0;
    if (!lying_ && fabsf(r) > 0.07f) target = clamp(-r / 0.7f);
    tilt_ += 0.2f * (target - tilt_);
    o.tilt = tilt_;

    // Displaying time: Watch pendant (vertical screen) → raise screen upward.
    if (u > 0.8f && fabsf(f) < 0.4f) { hung_ = true; hangAt_ = now; }
    if (f > 0.5f && u < 0.85f && u > -0.3f) {
      if (!looking_) { looking_ = true; lookAt_ = now; }
      if (!lookFired_ && hung_ && uint32_t(now - lookAt_) >= LOOK_HOLD_MS &&
          uint32_t(lookAt_ - hangAt_) <= LOOK_AFTER_HANG_MS &&
          (!looked_ || uint32_t(now - lookedAt_) >= LOOK_COOLDOWN_MS)) {
        o.lookUp = true;
        lookFired_ = true;
        looked_ = true;
        lookedAt_ = now;
      }
    } else {
      looking_ = false;
      lookFired_ = false;
    }
    return o;
  }

 private:
  static float pick(const float v[3], int8_t axis) {
    int i = (axis < 0 ? -axis : axis) - 1;
    if (i < 0 || i > 2) return 0;
    return axis < 0 ? -v[i] : v[i];
  }
  static float clamp(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

  Axes ax_;
  float shakeG_ = 1.0f;
  uint32_t shakeCooldown_ = SHAKE_COOLDOWN_MS;
  bool flat_ = false, lying_ = false, standing_ = false;
  uint32_t flatAt_ = 0, standAt_ = 0;
  uint32_t peaks_[3] = {0, 0, 0};
  int peakCount_ = 0;
  bool peaking_ = false, shaken_ = false;
  uint32_t shakeAt_ = 0;
  bool rising_ = false, lifted_ = false;
  uint32_t riseAt_ = 0, liftAt_ = 0;
  float tilt_ = 0;
  bool hung_ = false, looking_ = false, lookFired_ = false, looked_ = false;
  uint32_t hangAt_ = 0, lookAt_ = 0, lookedAt_ = 0;
};

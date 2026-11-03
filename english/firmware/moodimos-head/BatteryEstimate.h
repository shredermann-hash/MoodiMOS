// Estimation of the LiPo battery charge from the head’s voltage (divider circuit, GPIO4).
// Typical curve for a LiPo at rest, smoothed voltage: ±15% approximation, valid only when the head is detached (neck-mounted operation distorts readings).
// Pure logic, tested.
#pragma once
#include <stdint.h>
#include "MoodiMOSHead.h"

class BatteryEstimate {
 public:
  void reset() { has_ = false; }

  void addSample(float volts) {
    if (volts < 2.5f || volts > 4.5f) return;           // battery missing or erroneous measurement
    v_ = has_ ? v_ + 0.1f * (volts - v_) : volts;   // ≈ 20 s per 2 s sample interval
    has_ = true;
  }

  float volts() const { return has_ ? v_ : 0; }

  uint8_t percent() const {
    if (!has_) return MOODI_UNKNOWN;
    static const float V[] = {3.30f, 3.50f, 3.65f, 3.72f, 3.78f, 3.85f, 3.95f, 4.05f, 4.15f, 4.20f};
    static const float P[] = {0, 5, 12, 25, 40, 55, 70, 85, 97, 100};
    constexpr int N = sizeof(V) / sizeof(V[0]);
    if (v_ <= V[0]) return 0;
    if (v_ >= V[N - 1]) return 100;
    for (int i = 1; i < N; i++) {
      if (v_ <= V[i]) {
        float k = (v_ - V[i - 1]) / (V[i] - V[i - 1]);
        return uint8_t(P[i - 1] + k * (P[i] - P[i - 1]) + 0.5f);
      }
    }
    return 100;
  }

 private:
  float v_ = 0;
  bool has_ = false;
};

// Head battery (V2, LiPo 403040 ≈ 500 mAh): detached, resting voltage indicates charge level;
// on the neck, the charger on the board recharges via VBus; measured voltage exceeds resting voltage by ~0.1 V (subtracted), then caps at 4.2 V at full charge (constant voltage).
// Low battery below 15%, alert ends above 20% (hysteresis). Pure logic, tested.
class HeadBattery {
 public:
  static constexpr float CHARGE_OFFSET = 0.10f;   // Voltage overvoltage during charging
  static constexpr float FULL_V = 4.17f;          // V: end-of-charge (constant voltage)
  static constexpr uint8_t LOW_PCT = 15, LOW_CLEAR_PCT = 20;

  void sample(float volts, bool docked) {
    if (docked != docked_) { est_.reset(); docked_ = docked; }   // Overvoltage resets: restart.
    est_.addSample(docked ? volts - CHARGE_OFFSET : volts);
    full_ = docked && est_.volts() > 0 && volts >= FULL_V;
    uint8_t p = percent();
    if (docked_ || p == MOODI_UNKNOWN) low_ = false;
    else if (p <= LOW_PCT) low_ = true;
    else if (p >= LOW_CLEAR_PCT) low_ = false;
  }
  uint8_t percent() const { return full_ ? 100 : est_.percent(); }
  bool known() const { return percent() != MOODI_UNKNOWN; }
  bool charging() const { return docked_ && known() && !full_; }
  bool low() const { return low_; }   // Only detachable: neck-mounted, recharges via battery pack.
  float volts() const { return est_.volts(); }

 private:
  BatteryEstimate est_;
  bool docked_ = false, full_ = false, low_ = false;
};

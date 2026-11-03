// Decisions driven by the torso, sensing what the head cannot perceive: camera, radar, light, gyroscope, touch.
// Pure logic, tested on computer; the loop sends to the head what emerges from it.
// 
// - Cautious – Someone near the camera (wide target) >1 sec
// - Confused – Radar detects movement someone the camera doesn’t see, 15 sec (max every 3 min)
// - Dazed – Torso spins rapidly (one full turn)
// - Tired – Low light (<30 lux, not yet night) for 30 sec (max every 3 min)
// 
// A silent presence (working nearby) keeps it calm—it only sulks if a *GuessGame* game is abandoned mid-session.
#pragma once
#include <stdint.h>

class MoodTriggers {
 public:
  enum Action : uint8_t { NONE, MEFIANT, PERPLEXE, ETOURDI, FATIGUE };

  struct In {
    bool docked = false;
    bool radarMoving = false;   // Current high-frequency radar output (not active presence mode).
    bool present = false;       // presence sensor (radar or camera)
    bool cameraOn = false, seen = false;
    uint8_t size = 0;           // target width, 0 … 255 pixel range of the image
    bool lightOk = false, dark = false;
    uint16_t lux = 0;
    float gyroDps = 0;          // rotation speed of the torso, °/s
    bool moving = false;        // The torso has moved: the camera is useless.
  };

  static constexpr uint8_t CLOSE_SIZE = 200;
  static constexpr uint32_t CLOSE_MS = 1000, CLOSE_COOLDOWN_MS = 30000;
  static constexpr uint32_t UNSEEN_MS = 15000, PUZZLED_COOLDOWN_MS = 180000;
  static constexpr float SPIN_DPS = 60, DIZZY_DEG = 300;
  static constexpr uint32_t SPIN_GAP_MS = 400, DIZZY_COOLDOWN_MS = 5000;
  static constexpr uint16_t DIM_LUX = 30;
  static constexpr uint32_t DIM_MS = 30000, TIRED_COOLDOWN_MS = 180000;

  static const char* name(Action a) {
    static const char* const N[] = {"", "méfiant", "perplexe", "étourdi", "fatigue"};
    return a <= FATIGUE ? N[a] : "";
  }

  Action update(const In& in, uint32_t now, float dt) {
    if (!in.docked) { reset(); return NONE; }

    // confused: cumulative rotation while the torso spins rapidly
    if (in.gyroDps > SPIN_DPS) { spun_ += in.gyroDps * dt; spinAt_ = now; }
    else if (uint32_t(now - spinAt_) >= SPIN_GAP_MS) spun_ = 0;
    if (spun_ >= DIZZY_DEG && ready(now, dizzyOk_)) {
      spun_ = 0;
      dizzyOk_ = now + DIZZY_COOLDOWN_MS;
      return ETOURDI;
    }

    bool close = in.cameraOn && in.seen && in.size >= CLOSE_SIZE && !in.moving;
    if (!close) closeSince_ = 0;
    else if (!closeSince_) closeSince_ = now | 1;
    else if (uint32_t(now - closeSince_) >= CLOSE_MS && ready(now, closeOk_)) {
      closeOk_ = now + CLOSE_COOLDOWN_MS;
      return MEFIANT;
    }

    bool unseen = in.radarMoving && in.cameraOn && !in.seen && !in.moving;
    if (!unseen) unseenSince_ = 0;
    else if (!unseenSince_) unseenSince_ = now | 1;
    else if (uint32_t(now - unseenSince_) >= UNSEEN_MS && ready(now, puzzledOk_)) {
      unseenSince_ = 0;
      puzzledOk_ = now + PUZZLED_COOLDOWN_MS;
      return PERPLEXE;
    }

    bool dim = in.present && in.lightOk && !in.dark && in.lux < DIM_LUX;
    if (!dim) dimSince_ = 0;
    else if (!dimSince_) dimSince_ = now | 1;
    else if (uint32_t(now - dimSince_) >= DIM_MS && ready(now, tiredOk_)) {
      tiredOk_ = now + TIRED_COOLDOWN_MS;
      return FATIGUE;
    }
    return NONE;
  }

 private:
  static bool ready(uint32_t now, uint32_t okAt) { return int32_t(now - okAt) >= 0; }
  void reset() {
    spun_ = 0;
    closeSince_ = unseenSince_ = dimSince_ = 0;
  }
  float spun_ = 0;
  uint32_t spinAt_ = 0, closeSince_ = 0, unseenSince_ = 0, dimSince_ = 0;
  uint32_t dizzyOk_ = 0, closeOk_ = 0, puzzledOk_ = 0, tiredOk_ = 0;
};

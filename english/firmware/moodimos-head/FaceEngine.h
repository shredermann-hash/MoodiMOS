// Facial behavior: state machine, smoothing, spring, blinking, gaze, speech.
// Logic port from `face_preview.html`.
#pragma once
#include "Face.h"
#include "Mood.h"

// Each facial line’s spring velocity, zero at rest.
struct FaceVelocity {
#define MOODI_VEL(name, def, tau, kind) float name = 0;
  FACE_PARAMS(MOODI_VEL)
#undef MOODI_VEL
};

// NewFace: Face recognition detected (curiosity);
// Detached: Head detaches from neck (3 sec anxiety);
// Docked: Reconnects to torso (3 sec extreme joy);
// Unanswerable/Ignored: Reserved for voice commands.
// 
// Tickle: Rapid taps → laughter;
// Kiss: Long press → kiss + small hearts;
// Cheer: Torso sustains prolonged joy during announcements;
// Tired: Torso views dim light → fatigue state.
// 
// Ignored: Mid-game abandonment (torso), sulks.
enum class Event : uint8_t { Lift, Shake, Noise, Tap, Dark, Light, Arrive, NewFace, Detached, Unanswerable, Ignored,
                             Docked, Tickle, Kiss, Cheer, Tired };
constexpr Event LAST_EVENT = Event::Tired;

struct FaceInputs {
  bool presence = false;   // Radar (torso), unknown at startup
  bool lying = false;      // IMU: laid flat
  bool dark = false;       // light sensor (BH1750 from torso)
  bool talking = false;    // The voice speaks (torso).
  float talkAmplitude = -1; // `0 … 1 received from torso; -1 = serial simulation mode`
  bool faceSeen = false;   // target viewed by the OV5640 camera of the torso
  float faceX = 0, faceY = 0;   // Face position view: −1 … 1
  float tilt = 0;          // IMU: lateral tilt, −1 … 1
  float distance = 2;      // radar, meters
  bool lowBattery = false; // Detached head, low battery (HeadBattery)
};

class FaceEngine {
 public:
  static constexpr float SLEEP_TIMEOUT = 120;   // without presence before falling asleep

  void begin();
  void update(float dt);
  void onEvent(Event e);
  void play(Expr e, float seconds);   // mandatory expression (voice tests, vocal AI)

  FaceInputs in;
  MoodEngine mood;   // emotion (NeoCité laws): sets the base expression and face color.

  const FaceParams& params() const { return cur_; }
  float blink() const { return blink_; }
  float bounce() const { return bounce_; }
  float clock() const { return clock_; }
  Expr state() const { return state_; }
  bool awake() const { return awake_; }

 private:
  struct Step { Expr e; float d; };
  void playSeq(const Step* steps, int n);
  Expr baseState() const;
  void updateState(float dt);
  float blinkFactor(float dt);
  FaceParams computeTarget(float dt);

  FaceParams cur_;
  FaceVelocity vel_;
  Expr state_ = Expr::Dort;
  float stateTimer_ = 0;
  Step chain_[3];
  int chainLen_ = 0, chainIdx_ = 0;
  bool awake_ = false;
  float absentFor_ = 0;

  float clock_ = 0, blink_ = 1, bounce_ = 0;
  float blinkT_ = 0, nextBlink_ = 2, blinkPhase_ = -1;
  float saccadeT_ = 0, idleX_ = 0, idleY_ = 0;
  float talkT_ = 0, talkAmp_ = 0;
  float talkPeak_ = 0, talkLow_ = 0;   // syllables: the envelope falls, then rises again
  bool talkFalling_ = true;
  uint8_t vowel_ = 0;
  float hopT_ = 0;   // joy: jumps (or jerks)
  float yawnT_ = 4;  // fatigue: yawns
};

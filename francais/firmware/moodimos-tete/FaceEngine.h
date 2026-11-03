// Comportement du visage : machine à états, lissage et ressort, clignement, regard, parole.
// Portage de la logique de face_preview.html.
#pragma once
#include "Face.h"
#include "Mood.h"

// Vitesse de chaque trait du visage (ressort), nulle au repos
struct FaceVelocity {
#define MOODI_VEL(name, def, tau, kind) float name = 0;
  FACE_PARAMS(MOODI_VEL)
#undef MOODI_VEL
};

// NewFace : la caméra retrouve quelqu'un (curiosité) ; Detached : la tête quitte le cou (inquiète 3 s) ;
// Docked : elle retrouve le torse (joie extrême 3 s) ; Unanswerable / Ignored : réservés à la voix.
// Tickle : chatouilles (taps rapides) → rire ; Kiss : appui long → bisou et petits cœurs.
// Cheer : joie entretenue par le torse pendant une annonce ; Tired : pénombre vue par le torse, fatigué.
// Ignored : partie de jeu abandonnée en cours (le torse), il boude.
enum class Event : uint8_t { Lift, Shake, Noise, Tap, Dark, Light, Arrive, NewFace, Detached, Unanswerable, Ignored,
                             Docked, Tickle, Kiss, Cheer, Tired };
constexpr Event LAST_EVENT = Event::Tired;

struct FaceInputs {
  bool presence = false;   // radar (torse), inconnue au démarrage
  bool lying = false;      // IMU : couché à plat
  bool dark = false;       // capteur de lumière (BH1750 du torse)
  bool talking = false;    // la voix parle (torse)
  float talkAmplitude = -1; // 0 … 1 reçu du torse ; -1 = simulation série
  bool faceSeen = false;   // cible vue par la caméra OV5640 du torse
  float faceX = 0, faceY = 0;   // position du visage vu, −1 … 1
  float tilt = 0;          // IMU : inclinaison latérale, −1 … 1
  float distance = 2;      // radar, mètres
  bool lowBattery = false; // tête détachée, batterie faible (HeadBattery)
};

class FaceEngine {
 public:
  static constexpr float SLEEP_TIMEOUT = 120;   // s sans présence avant de s'endormir

  void begin();
  void update(float dt);
  void onEvent(Event e);
  void play(Expr e, float seconds);   // expression imposée (tests, IA vocale)

  FaceInputs in;
  MoodEngine mood;   // humeur (lois NeoCité) : fixe l'expression de fond et la couleur du visage

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
  float talkPeak_ = 0, talkLow_ = 0;   // syllabes : l'enveloppe retombe puis remonte
  bool talkFalling_ = true;
  uint8_t vowel_ = 0;
  float hopT_ = 0;   // joie : sautillements
  float yawnT_ = 4;  // fatigue : bâillements
};

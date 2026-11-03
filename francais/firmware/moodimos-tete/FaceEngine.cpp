#include "FaceEngine.h"
#include <Arduino.h>
#include <math.h>

namespace {
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float rnd() { return esp_random() / 4294967296.0f; }   // 0 … 1

// Signature de mouvement de l'expression visée : raideur du ressort (× 1/tau du trait) et amortissement.
// Amortissement 1 : le trait arrive sans dépasser ; plus bas, il dépasse puis revient (0,55 ≈ 12 %).
struct Motion { float stiffness, damping; };
Motion motionOf(Expr e) {
  switch (e) {
    case Expr::Surpris: case Expr::Emballe: return {2.0f, 0.38f};   // sursaut marqué, ≈ 27 %
    case Expr::Fache: case Expr::Colere: return {2.6f, 0.7f};       // contraction brusque, ≈ 5 %
    case Expr::Fatigue: case Expr::Baille: case Expr::Dort: case Expr::Apaise: return {1.0f, 1.0f};   // lent, amorti
    default: return {1.9f, 0.55f};                                  // ressort léger, ≈ 12 %
  }
}

// Ressort amorti, intégré par pas de 2 ms pour rester stable sur les traits rapides (tau 0,06 s).
// Après une longue pause (plus de 0,2 s), le trait est simplement posé sur sa cible.
void spring(float& x, float& v, float target, float tau, Motion m, float dt) {
  if (dt > 0.2f) { x = target; v = 0; return; }
  const float w = m.stiffness / tau;
  const int n = int(dt / 0.002f) + 1;
  const float h = dt / n;
  for (int i = 0; i < n; i++) {
    v += (w * w * (target - x) - 2 * m.damping * w * v) * h;
    x += v * h;
  }
}

// Plancher d'un trait poussé par le ressort : il s'y arrête au lieu de passer au-delà
void floorAt(float& x, float& v, float lo) {
  if (x < lo) { x = lo; v = 0; }
}
}  // namespace

void FaceEngine::begin() {
  cur_ = expression(Expr::Dort);
  vel_ = {};
  state_ = Expr::Dort;
}

void FaceEngine::playSeq(const Step* steps, int n) {
  if (n > 3) n = 3;
  for (int i = 0; i < n; i++) chain_[i] = steps[i];
  chainLen_ = n;
  chainIdx_ = 1;
  state_ = steps[0].e;
  stateTimer_ = steps[0].d;
}

void FaceEngine::play(Expr e, float seconds) {
  awake_ = true;
  absentFor_ = 0;
  Step s[] = {{e, seconds}};
  playSeq(s, 1);
}

Expr FaceEngine::baseState() const {
  if (mood.phase == MOOD_INQUIET) return Expr::Inquiet;   // même endormie, la tête qui part s'inquiète
  if (mood.phase == MOOD_JOIE) return Expr::Joie;
  if (mood.phase == MOOD_FATIGUE) return Expr::Fatigue;
  if (in.lying || in.dark || !awake_) return Expr::Dort;
  if (mood.dizzy > 0) return Expr::Etourdi;               // secoué : la tête tourne, la colère pâlit en fond
  switch (mood.phase) {
    case MOOD_COLERE: case MOOD_SE_CALME: return Expr::Colere;
    case MOOD_TENIR: return Expr::Tenir;
    case MOOD_CURIEUX: return Expr::Curieux;
    case MOOD_APAISE: return Expr::Apaise;
    case MOOD_BOUDE: return Expr::Boude;
    case MOOD_PERPLEXE: return Expr::Perplexe;
    case MOOD_ABSENCE: return Expr::Absent;
    default: return Expr::Attentif;
  }
}

void FaceEngine::onEvent(Event e) {
  switch (e) {
    case Event::Lift: {   // surpris, puis tient bon (secousse absorbée)
      awake_ = true; absentFor_ = 0;
      mood.shakeSoft();
      Step s[] = {{Expr::Surpris, 0.8f}};
      playSeq(s, 1);
      break;
    }
    case Event::Shake:    // la colère s'auto-alimente : elle se voit dans l'expression de fond
      awake_ = true; absentFor_ = 0; bounce_ = 1;
      mood.shakeHard();
      chainLen_ = chainIdx_ = 0; stateTimer_ = 0;
      break;
    case Event::Noise: {
      awake_ = true; absentFor_ = 0; bounce_ = 1;
      mood.noise();
      Step s[] = {{Expr::Surpris, 0.7f}};
      playSeq(s, 1);
      break;
    }
    case Event::Tap:      // le geste apaise (et calme la colère)
      absentFor_ = 0;
      mood.tap();
      if (!awake_) {
        awake_ = true;
        Step s[] = {{Expr::Surpris, 0.6f}};
        playSeq(s, 1);
      }
      break;
    case Event::NewFace: awake_ = true; absentFor_ = 0; mood.newFace(); break;
    case Event::Detached: mood.detached(); chainLen_ = chainIdx_ = 0; stateTimer_ = 0; break;
    case Event::Unanswerable: awake_ = true; absentFor_ = 0; mood.unanswerable(); break;
    case Event::Ignored: awake_ = true; absentFor_ = 0; mood.ignored(); break;
    case Event::Tickle: {   // chatouilles : il éclate de rire (et ça chasse la colère)
      awake_ = true; absentFor_ = 0; bounce_ = 1; hopT_ = 0;
      mood.A *= 0.5f;
      Step s[] = {{Expr::Rieur, 1.6f}};
      playSeq(s, 1);
      break;
    }
    case Event::Kiss: {     // bisou : petits cœurs, puis tout content et apaisé
      awake_ = true; absentFor_ = 0;
      mood.tap();
      mood.calm = 5;          // encore apaisé 2 s après l'animation (3 s)
      Step s[] = {{Expr::Bisou, 2.0f}, {Expr::Content, 1.0f}};
      playSeq(s, 2);
      break;
    }
    case Event::Docked:   // de retour sur le torse : joie extrême
      awake_ = true; absentFor_ = 0; bounce_ = 1; hopT_ = 0;
      mood.docked();
      chainLen_ = chainIdx_ = 0; stateTimer_ = 0;
      break;
    case Event::Cheer:    // annonce du Mac : le torse entretient la joie tant qu'il parle, même endormie
      awake_ = true; absentFor_ = 0;
      if (mood.joy <= 0) { bounce_ = 1; hopT_ = 0; chainLen_ = chainIdx_ = 0; stateTimer_ = 0; }
      mood.cheer();
      break;
    case Event::Tired:    // pénombre : fatigué (couleurs acier, bâillements), comme la batterie faible
      mood.tire();
      break;
    case Event::Dark: {
      Step s[] = {{Expr::Baille, 2.2f}};
      playSeq(s, 1);
      break;
    }
    case Event::Light:
      if (awake_) {
        Step s[] = {{Expr::Surpris, 0.6f}};
        playSeq(s, 1);
      }
      break;
    case Event::Arrive:
      if (!in.lying && !in.dark) {
        if (!awake_) {
          Step s[] = {{Expr::Surpris, 0.8f}};
          playSeq(s, 1);
        }
        awake_ = true;
      }
      break;
  }
}

void FaceEngine::updateState(float dt) {
  if (in.presence) absentFor_ = 0;
  else absentFor_ += dt;
  if (absentFor_ > SLEEP_TIMEOUT) awake_ = false;

  if (stateTimer_ > 0) {
    stateTimer_ -= dt;
    if (stateTimer_ <= 0) {
      if (chainIdx_ < chainLen_) {
        state_ = chain_[chainIdx_].e;
        stateTimer_ = chain_[chainIdx_].d;
        chainIdx_++;
      } else {
        state_ = baseState();
      }
    }
  } else {
    state_ = baseState();
  }
}

float FaceEngine::blinkFactor(float dt) {
  if (blinkPhase_ < 0) {
    blinkT_ += dt;
    if (blinkT_ > nextBlink_) {
      blinkPhase_ = 0;
      blinkT_ = 0;
      nextBlink_ = 1.8f + rnd() * 4.5f;
      if (rnd() < 0.15f) nextBlink_ = 0.25f;   // double clignement
    }
    return 1;
  }
  blinkPhase_ += dt / 0.16f;                   // 160 ms en tout
  if (blinkPhase_ >= 1) { blinkPhase_ = -1; return 1; }
  return fabsf(1 - 2 * blinkPhase_);
}

FaceParams FaceEngine::computeTarget(float dt) {
  FaceParams t = expression(state_);
  if (state_ == Expr::Colere) {   // attentif → fâché → emballé, dosé par la charge
    float a = clampf(mood.I / MoodEngine::BOIL, 0, 1), b = clampf((mood.I - MoodEngine::BOIL) / (1 - MoodEngine::BOIL), 0, 1);
    FaceParams base, f = expression(Expr::Fache), e = expression(Expr::Emballe);
#define MOODI_MIX(name, def, tau, kind) { float v = base.name + (f.name - base.name) * a; t.name = v + (e.name - v) * b; }
    FACE_PARAMS(MOODI_MIX)
#undef MOODI_MIX
  }

  bool gazeFree = !(state_ == Expr::Mefiant || state_ == Expr::Etourdi || state_ == Expr::Dort ||
                    state_ == Expr::Rieur || state_ == Expr::Apaise || state_ == Expr::Baille ||
                    state_ == Expr::Curieux || state_ == Expr::Perplexe || state_ == Expr::Boude ||
                    state_ == Expr::Inquiet || state_ == Expr::Fatigue);
  if (gazeFree) {
    if (in.faceSeen && awake_) {
      t.pupilX = in.faceX;
      t.pupilY = in.faceY;
    } else {
      saccadeT_ -= dt;
      if (saccadeT_ <= 0) {
        saccadeT_ = 0.8f + rnd() * 2.5f;
        idleX_ = (rnd() - 0.5f) * 0.7f;
        idleY_ = (rnd() - 0.5f) * 0.4f;
      }
      t.pupilX = idleX_;
      t.pupilY = idleY_;
    }
    t.pupilX = clampf(t.pupilX + in.tilt * 0.9f, -1, 1);          // penché : les pupilles glissent
    t.pupilY = clampf(t.pupilY + fabsf(in.tilt) * 0.3f, -1, 1);
  }
  if (state_ == Expr::Curieux) {    // le regard fait le tour du visage
    t.pupilX = cosf(clock_ * 1.6f) * 0.7f;
    t.pupilY = sinf(clock_ * 1.6f) * 0.5f;
  }
  if (state_ == Expr::Inquiet) t.pupilX = sinf(clock_ * 7) * 0.6f;   // cherche le corps du regard

  if (awake_ && state_ == Expr::Attentif) {                         // quelqu'un très près
    float nearK = clampf((1.0f - in.distance) / 0.7f, 0, 1);
    t.eyeSize += 0.12f * nearK;
    t.pupilSize -= 0.15f * nearK;
    t.browY -= 0.05f * nearK;
  }

  if (in.talking && awake_ && state_ != Expr::Dort && state_ != Expr::Baille) {
    talkT_ -= dt;
    if (talkT_ <= 0) {
      talkT_ = 0.08f + rnd() * 0.14f;
      talkAmp_ = rnd() < 0.2f ? 0 : 0.2f + rnd() * 0.8f;
    }
    float amplitude = in.talkAmplitude < 0 ? talkAmp_ : clampf(in.talkAmplitude, 0, 1);
    // Chaque syllabe (l'enveloppe retombe puis remonte) prend une voyelle, jamais deux fois la même :
    // a grande ouverte, é étirée, o ronde, ou petite et ronde. La courbe des lèvres reste celle de
    // l'humeur (il parle en souriant, ou fâché), les sourcils se lèvent sur les syllabes fortes.
    struct Vowel { float open, width, round; };
    static constexpr Vowel VOWELS[] = {{1.0f, 1.0f, 0.25f}, {0.6f, 1.25f, 0.1f}, {0.85f, 0.7f, 0.85f}, {0.65f, 0.5f, 1.0f}};
    if (talkFalling_) {
      talkLow_ = fminf(talkLow_, amplitude);
      if (amplitude - talkLow_ > 0.12f) {
        float r = rnd();
        uint8_t v = r < 0.35f ? 0 : r < 0.65f ? 1 : r < 0.85f ? 2 : 3;
        vowel_ = v == vowel_ ? (v + 1) % 4 : v;
        talkFalling_ = false;
        talkPeak_ = amplitude;
      }
    } else {
      talkPeak_ = fmaxf(talkPeak_, amplitude);
      if (talkPeak_ - amplitude > 0.12f) { talkFalling_ = true; talkLow_ = amplitude; }
    }
    const Vowel& v = VOWELS[vowel_];
    t.mouthOpen = 0.06f + amplitude * 1.2f * v.open;
    t.mouthWidth *= 1 + (v.width - 1) * clampf(amplitude * 2, 0, 1);   // la forme se voit bouche ouverte
    t.mouthRound = v.round;
    t.browY -= 0.05f * amplitude;
  } else {
    talkFalling_ = true;   // la première syllabe tire sa voyelle
    talkLow_ = 0;
  }
  return t;
}

void FaceEngine::update(float dt) {
  clock_ += dt;
  mood.lowBattery = in.lowBattery;
  mood.talking = in.talking;
  mood.update(dt, awake_ && !in.lying && !in.dark, in.presence);
  if (mood.phase == MOOD_FATIGUE && stateTimer_ <= 0 && (yawnT_ -= dt) <= 0) {   // bâille toutes les 12 s
    Step s[] = {{Expr::Baille, 1.8f}};
    playSeq(s, 1);
    yawnT_ = 12;
  }
  // sautille de joie, ou se tord de rire sous les chatouilles
  if ((mood.phase == MOOD_JOIE || state_ == Expr::Rieur) && (hopT_ -= dt) <= 0) { bounce_ = 1; hopT_ = 0.3f; }
  updateState(dt);
  FaceParams t = computeTarget(dt);
  // Les traits suivent un ressort propre à l'expression visée (surprise qui dépasse, fatigue lente, colère
  // brusque) ; couleurs, joues et effets gardent le lissage. Le clignement est calculé à part et reste vif.
  const Motion m = motionOf(state_);
#define MOODI_SMOOTH(name, def, tau, kind)                                       \
  if (kind == 1 || (kind == 2 && !in.talking)) {                                 \
    spring(cur_.name, vel_.name, t.name, tau, m, dt);                            \
  } else {                                                                       \
    cur_.name += (t.name - cur_.name) * (1 - expf(-dt / (tau)));                 \
    vel_.name = 0;                                                               \
  }
  FACE_PARAMS(MOODI_SMOOTH)
#undef MOODI_SMOOTH
  floorAt(cur_.eyeOpen, vel_.eyeOpen, 0);
  floorAt(cur_.mouthOpen, vel_.mouthOpen, 0);
  floorAt(cur_.eyeSize, vel_.eyeSize, 0.3f);
  floorAt(cur_.pupilSize, vel_.pupilSize, 0.3f);
  floorAt(cur_.mouthWidth, vel_.mouthWidth, 0.1f);
  blink_ = state_ == Expr::Dort ? 1 : blinkFactor(dt);
  bounce_ = fmaxf(0, bounce_ - dt * 4);
}

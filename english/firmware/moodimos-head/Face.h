// MoodiMOS Face: standardized parameters, expressions, and rendering.
// Direct firmware transfer to head/preview/face_preview.html (same values).
#pragma once
#include <stdint.h>
#include "Canvas.h"
#include "Design.h"

// name, default value, smoothing constant (s), motion:
// - 0: smoothing only
// - 1: spring
// - 2: spring except during speech (mouth sticks to syllables)
#define FACE_PARAMS(X)                                                         \
  X(eyeOpen, 1.0f, 0.12f, 1)     /* 0 closed … 1 open */                        \
  X(eyeSize, 1.0f, 0.12f, 1)                                                      \
  X(closedCurve, 1.0f, 0.12f, 0) /* closed eye: +1 arc ∩ (laughing), −1 arc ∪ (sleeping) */ \
  X(lidAngle, 0.0f, 0.12f, 1)    /* Inclined eyelid toward the nose (angry) */     \
  X(lidBottom, 0.0f, 0.12f, 0)                                                    \
  X(pupilX, 0.0f, 0.07f, 1)      /* view –1 … 1 */                             \
  X(pupilY, 0.0f, 0.07f, 1)                                                       \
  X(pupilSize, 1.0f, 0.12f, 1)                                                    \
  X(spin, 0.0f, 0.12f, 0)        /* eyes that swivel (dazed) */           \
  X(browY, 0.0f, 0.12f, 1)       /* + = lower */                              \
  X(browAngle, 0.0f, 0.12f, 1)   /* + = V concave */                              \
  X(browCurve, 0.35f, 0.12f, 1)                                                   \
  X(browAsym, 0.0f, 0.12f, 1)    /* + = left eyebrow raised (suspicious) */         \
  X(mouthOpen, 0.0f, 0.06f, 2)                                                    \
  X(mouthWidth, 0.7f, 0.12f, 2)                                                   \
  X(mouthCurve, 0.5f, 0.12f, 2)  /* +1 smile … −1 grimace */                      \
  X(mouthRound, 0.15f, 0.12f, 0) /* 0 D-shaped mouth, 1 oval mouth */              \
  X(blush, 0.45f, 0.12f, 0)                                                       \
  X(zzz, 0.0f, 0.4f, 0)                                                           \
  X(wink, 0.0f, 0.12f, 0)        /* Close the right eye alone. */                 \
  X(sparkle, 0.0f, 0.2f, 0)      /* Sparkles around the face (joy). */    \
  X(hearts, 0.0f, 0.2f, 0)       /* small hearts that fly away (kiss) */

struct FaceParams {
#define MOODI_DECL(name, def, tau, kind) float name = def;
  FACE_PARAMS(MOODI_DECL)
#undef MOODI_DECL
};

enum class Expr : uint8_t {
  Attentif, Content, Surpris, Rieur, Fache, Apaise, Mefiant, Dort, Baille, Etourdi, Clin,
  // Mood (Mood.h): Anger → irritated → overwhelmed, modulated by charge
  Emballe, Tenir, Curieux, Perplexe, Boude, Absent, Inquiet, Colere, Joie, Fatigue, Bisou, Count
};

FaceParams expression(Expr e);
const char* exprName(Expr e);
bool exprFromName(const char* name, Expr& out);

struct Palette;

class FaceRenderer {
 public:
  void renderBackground(uint16_t* bg, int size);
  // Documentation for handmade robot:
  // 
  // Color mood: The gradient index (0…255 per pixel) is computed once, then each recoloring performs a lookup in a 256-color table.
  static void buildGradient(uint8_t* idx, int size);
  static void paintBackground(uint16_t* bg, const uint8_t* idx, int count, const Palette& pal);
  void setInk(uint16_t ink) { ink_ = ink; }
  void setDesign(Design d) { design_ = d; }
  bool fullBody() const { return design_ == Design::HorlogeEnPied; }
  bool neon() const { return design_ == Design::Neon; }
  // Emotion colors (light, medium, dark) – in the base, it’s the disc that carries them, not the background.
  void setSkin(uint16_t light, uint16_t mid, uint16_t dark) { skinLight_ = light; skinMid_ = mid; skinDark_ = dark; }
  static void paintBackdrop(uint16_t* bg, int size);   // Golden baseplate of the character in full (foot)
  static void paintNeon(uint16_t* bg, int count);       // Dark neon background: the lines carry the mood.
  Design design() const { return design_; }
  // Fixed design decoration to sketch in the background after repainting (clock face graduations):
  // Sketching in every image would force a full-screen restoration.
  void drawDecor(Canvas& c);
  // Adjustment Menu: Left/right arrows + design dot (pick = the displayed one)
  void drawMenu(Canvas& c, Design pick);
  // blink: 1 = open, 0 = closed; bounce: rebound 0…1; clock: seconds
  // state: current expression, for body pose (character standing)
  void render(Canvas& c, const FaceParams& p, float blink, float bounce, float clock, Expr state = Expr::Attentif);

 private:
  void renderFace(Canvas& c, const FaceParams& p, float blink, float bounce, float clock);
  void renderBody(Canvas& c, const FaceParams& p, float blink, float bounce, float clock, Expr state);
  void drawTicks(Canvas& c);
  Paint lidPaint() const { return fullBody() ? Paint::solid(skinMid_) : Paint::background(); }
  void drawLashes(Canvas& c, int side, float cx, float lashY, float rx, float ry);
  void drawEye(Canvas& c, int side, const FaceParams& p, float blink, float bounce, float clock);
  void drawBrow(Canvas& c, int side, const FaceParams& p, float bounce);
  void drawBlush(Canvas& c, const FaceParams& p);
  void drawMouth(Canvas& c, const FaceParams& p);
  void drawZzz(Canvas& c, const FaceParams& p, float clock);
  void drawSparkles(Canvas& c, const FaceParams& p, float clock);
  void drawHearts(Canvas& c, const FaceParams& p, float clock);
  // Neon (complete edition):
  // - eye glass (verre des yeux) = fair skin
  // - sound shadow (son ombre) = medium skin
  // - mouth (bouche) = dark skin
  // - eyebrows (sourcils) = ink
  // - background (fond) = black
  void renderNeon(Canvas& c, const FaceParams& p, float blink, float bounce, float clock);
  void drawNeonEye(Canvas& c, int side, const FaceParams& p, float blink, float bounce, float clock);
  void drawClosedLens(Canvas& c, float cx, float cy, float rx, float ry, float curve, float alpha);
  void drawNeonBrow(Canvas& c, int side, const FaceParams& p, float bounce);
  void drawNeonBlush(Canvas& c, const FaceParams& p);
  void drawNeonMouth(Canvas& c, const FaceParams& p);
  void drawNeonZzz(Canvas& c, const FaceParams& p, float clock);
  uint16_t ink_ = rgb565(0x0F, 0x1D, 0x38);
  Design design_ = Design::Classique;
  uint16_t skinLight_ = rgb565(0xFF, 0x9E, 0x3E), skinMid_ = rgb565(0xF4, 0x7A, 0x20), skinDark_ = rgb565(0xBA, 0x4E, 0x10);
  // Body pose (smooth):
  // - Left arm and right arm relative to the disk’s center
  // - Elbows, gait (kinematic sequence)
  float lx_ = -180, ly_ = -15, rx_ = 150, ry_ = -55, ex_ = 10, ey_ = 30, walk_ = 1, bodyClock_ = 0;
};

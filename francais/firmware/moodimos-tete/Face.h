// Visage MoodiMOS : paramètres normalisés, expressions et rendu.
// Portage direct de firmware/tete/preview/face_preview.html (mêmes valeurs).
#pragma once
#include <stdint.h>
#include "Canvas.h"
#include "Design.h"

// nom, valeur par défaut, constante de lissage (s), mouvement : 0 lissage seul, 1 ressort,
// 2 ressort sauf pendant la parole (la bouche colle alors aux syllabes)
#define FACE_PARAMS(X)                                                         \
  X(eyeOpen, 1.0f, 0.12f, 1)     /* 0 fermé … 1 ouvert */                        \
  X(eyeSize, 1.0f, 0.12f, 1)                                                      \
  X(closedCurve, 1.0f, 0.12f, 0) /* œil fermé : +1 arc ∩ (rieur), −1 arc ∪ (dort) */ \
  X(lidAngle, 0.0f, 0.12f, 1)    /* paupière inclinée vers le nez (fâché) */     \
  X(lidBottom, 0.0f, 0.12f, 0)                                                    \
  X(pupilX, 0.0f, 0.07f, 1)      /* regard −1 … 1 */                             \
  X(pupilY, 0.0f, 0.07f, 1)                                                       \
  X(pupilSize, 1.0f, 0.12f, 1)                                                    \
  X(spin, 0.0f, 0.12f, 0)        /* pupilles qui tournent (étourdi) */           \
  X(browY, 0.0f, 0.12f, 1)       /* + = plus bas */                              \
  X(browAngle, 0.0f, 0.12f, 1)   /* + = V froncé */                              \
  X(browCurve, 0.35f, 0.12f, 1)                                                   \
  X(browAsym, 0.0f, 0.12f, 1)    /* + = sourcil gauche levé (méfiant) */         \
  X(mouthOpen, 0.0f, 0.06f, 2)                                                    \
  X(mouthWidth, 0.7f, 0.12f, 2)                                                   \
  X(mouthCurve, 0.5f, 0.12f, 2)  /* +1 sourire … −1 moue */                      \
  X(mouthRound, 0.15f, 0.12f, 0) /* 0 bouche en D, 1 bouche en o */              \
  X(blush, 0.45f, 0.12f, 0)                                                       \
  X(zzz, 0.0f, 0.4f, 0)                                                           \
  X(wink, 0.0f, 0.12f, 0)        /* ferme l'œil droit seul */                 \
  X(sparkle, 0.0f, 0.2f, 0)      /* étincelles autour du visage (joie) */    \
  X(hearts, 0.0f, 0.2f, 0)       /* petits cœurs qui s'envolent (bisou) */

struct FaceParams {
#define MOODI_DECL(name, def, tau, kind) float name = def;
  FACE_PARAMS(MOODI_DECL)
#undef MOODI_DECL
};

enum class Expr : uint8_t {
  Attentif, Content, Surpris, Rieur, Fache, Apaise, Mefiant, Dort, Baille, Etourdi, Clin,
  // humeurs (Mood.h) : Colere est un mélange attentif → fâché → emballé dosé par la charge
  Emballe, Tenir, Curieux, Perplexe, Boude, Absent, Inquiet, Colere, Joie, Fatigue, Bisou, Count
};

FaceParams expression(Expr e);
const char* exprName(Expr e);
bool exprFromName(const char* name, Expr& out);

struct Palette;

class FaceRenderer {
 public:
  void renderBackground(uint16_t* bg, int size);
  // Fond aux couleurs de l'humeur : l'index du dégradé (0 … 255 par pixel) est calculé une fois,
  // puis chaque repeinture ne fait qu'une recherche dans une table de 256 couleurs.
  static void buildGradient(uint8_t* idx, int size);
  static void paintBackground(uint16_t* bg, const uint8_t* idx, int count, const Palette& pal);
  void setInk(uint16_t ink) { ink_ = ink; }
  void setDesign(Design d) { design_ = d; }
  bool fullBody() const { return design_ == Design::HorlogeEnPied; }
  bool neon() const { return design_ == Design::Neon; }
  // Couleurs de l'humeur (clair, moyen, sombre) : en pied, c'est le disque qui les porte, pas le fond
  void setSkin(uint16_t light, uint16_t mid, uint16_t dark) { skinLight_ = light; skinMid_ = mid; skinDark_ = dark; }
  static void paintBackdrop(uint16_t* bg, int size);   // fond doré du personnage en pied
  static void paintNeon(uint16_t* bg, int count);       // fond noir du néon : les traits portent l'humeur
  Design design() const { return design_; }
  // Décor fixe du design, à dessiner dans le fond après l'avoir repeint (graduations du cadran) :
  // dessiné à chaque image, il obligerait à restaurer tout l'écran.
  void drawDecor(Canvas& c);
  // Menu de réglage : flèches gauche / droite et une pastille par design (pick = celui montré)
  void drawMenu(Canvas& c, Design pick);
  // blink : 1 = ouvert, 0 = fermé ; bounce : sursaut 0 … 1 ; clock : secondes
  // state : expression en cours, pour la pose du corps (personnage en pied)
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
  // Néon (édition complète) : verre des yeux = peau claire, son ombre = peau moyenne, bouche = peau sombre,
  // sourcils = encre ; le fond reste noir.
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
  // pose du corps, lissée : mains gauche et droite par rapport au centre du disque, coude, marche
  float lx_ = -180, ly_ = -15, rx_ = 150, ry_ = -55, ex_ = 10, ey_ = 30, walk_ = 1, bodyClock_ = 0;
};

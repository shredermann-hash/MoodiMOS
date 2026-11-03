#include "Face.h"
#include "Mood.h"
#include "MoodiMOSEdition.h"
#include <math.h>
#include <string.h>

namespace {
constexpr float R = 240;   // rayon du visage en pixels (écran 480×480)

constexpr uint16_t EYE_WHITE = rgb565(0xF7, 0xF9, 0xFC);
constexpr uint16_t EYE_RIM = rgb565(0x33, 0x48, 0x70);
constexpr uint16_t PUPIL = rgb565(0x10, 0x11, 0x14);
constexpr uint16_t WHITE = rgb565(0xFF, 0xFF, 0xFF);
constexpr uint16_t MOUTH = rgb565(0x4A, 0x0D, 0x17);
constexpr uint16_t TONGUE = rgb565(0xE5, 0x53, 0x5F);
constexpr uint16_t TEETH = rgb565(0xF4, 0xF4, 0xF0);
constexpr uint16_t BLUSH = rgb565(0xFF, 0x78, 0x96);

constexpr uint16_t CREAM = rgb565(0xFD, 0xF6, 0xE6);

// Néon : pupilles et paupières closes gravées en brun très sombre dans le verre, reflets, joues
constexpr uint16_t NEON_DARK = rgb565(0x2A, 0x10, 0x00);
constexpr uint16_t NEON_GLINT = rgb565(0xFF, 0xF6, 0xD8);
constexpr uint16_t NEON_BLUSH = rgb565(0xFF, 0x5A, 0x4C);
constexpr uint16_t NEON_TONGUE = rgb565(0xFF, 0xD8, 0xC0);   // éclaircit la bouche pour la langue

// Proportions du visage par design (fractions du rayon R)
struct Look {
  float eyeDx, eyeCy, eyeRx, eyeRy;   // yeux : écart au centre, hauteur, demi-axes
  float browGap, browHalf, browW;     // sourcils : distance au-dessus de l'œil, demi-longueur, épaisseur (px)
  float blushDx, blushDy;
  float mouthCy, mouthW, mouthH;
};
constexpr Look LOOKS[] = {
  {0.40f, -0.10f, 0.220f, 0.270f, 0.10f, 0.17f, 11, 0.60f, 0.20f, 0.42f, 0.27f, 0.30f},   // classique
  {0.32f, -0.14f, 0.185f, 0.275f, 0.13f, 0.13f, 8, 0.50f, 0.27f, 0.37f, 0.20f, 0.22f},    // horloge
  {0.32f, -0.14f, 0.185f, 0.275f, 0.13f, 0.13f, 8, 0.50f, 0.27f, 0.37f, 0.20f, 0.22f},    // horloge en pied
  {0.375f, -0.035f, 0.280f, 0.240f, 0.17f, 0.13f, 7, 0.53f, 0.32f, 0.37f, 0.25f, 0.30f},  // néon (rendu)
};
inline const Look& look(Design d) { return LOOKS[d < Design::Count ? uint8_t(d) : 0]; }

inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

uint16_t mix565(uint16_t a, uint16_t b, float t) {   // t = 1 → b
  auto ch = [t](int x, int y) { return int(x + (y - x) * t + 0.5f); };
  return uint16_t((ch(a >> 11, b >> 11) << 11) | (ch((a >> 5) & 63, (b >> 5) & 63) << 5) | ch(a & 31, b & 31));
}

// Néon : tout se dessine sur fond noir, les halos s'écrivent donc en teintes assombries calculées une
// fois, sans relire l'image pixel par pixel (en PSRAM, un fondu coûte plusieurs fois un remplissage).
inline Paint dimmed(uint16_t col, float a) { return Paint::solid(a >= 0.99f ? col : mix565(0, col, a)); }

// Trait lumineux : deux halos de plus en plus serrés, puis le trait.
void neonStroke(Canvas& c, const Pt* p, int n, float w, uint16_t col, float a = 1) {
  const uint16_t outer = mix565(0, col, 0.10f * a), mid = mix565(outer, col, 0.20f * a);
  c.fillStroke(p, n, w + 16, Paint::solid(outer));
  c.fillStroke(p, n, w + 8, Paint::solid(mid));
  c.fillStroke(p, n, w, dimmed(col, a));
}

const char* const NAMES[] = {"attentif", "content", "surpris", "rieur", "fache", "apaise",
                             "mefiant", "dort", "baille", "etourdi", "clin",
                             "emballe", "tenir", "curieux", "perplexe", "boude", "absent", "inquiet", "colere", "joie", "fatigue", "bisou"};
}  // namespace

FaceParams expression(Expr e) {
  FaceParams p;
  switch (e) {
    case Expr::Attentif:
    case Expr::Colere:     // mélange calculé par FaceEngine
    case Expr::Count:
      break;
    // valeurs reprises de neocite_preview.html (EXPR)
    case Expr::Emballe:
      p.eyeOpen = 1; p.eyeSize = 1.1f; p.lidAngle = 0.7f; p.pupilSize = 0.6f; p.browY = 0.09f; p.browAngle = 1;
      p.browCurve = -0.2f; p.mouthOpen = 0.45f; p.mouthWidth = 0.75f; p.mouthCurve = -0.8f; p.mouthRound = 0.05f; p.blush = 0;
      break;
    case Expr::Tenir:
      p.eyeOpen = 0.7f; p.lidBottom = 0.35f; p.browY = 0.03f; p.browAngle = 0.25f; p.browCurve = 0.1f;
      p.mouthOpen = 0; p.mouthWidth = 0.6f; p.mouthCurve = 0; p.blush = 0.15f;
      break;
    case Expr::Curieux:
      p.eyeSize = 1.14f; p.pupilSize = 0.8f; p.browY = -0.08f; p.browCurve = 0.7f; p.mouthOpen = 0.3f;
      p.mouthWidth = 0.35f; p.mouthCurve = 0.2f; p.mouthRound = 0.9f; p.blush = 0.4f;
      break;
    case Expr::Perplexe:
      p.eyeOpen = 0.85f; p.pupilX = 0.5f; p.pupilY = -0.35f; p.browAsym = 0.7f; p.browCurve = 0.5f; p.mouthOpen = 0.06f;
      p.mouthWidth = 0.4f; p.mouthCurve = -0.3f; p.mouthRound = 0.4f; p.blush = 0.1f;
      break;
    case Expr::Boude:
      p.eyeOpen = 0.75f; p.lidAngle = -0.15f; p.pupilX = 0.95f; p.pupilY = 0.2f; p.browY = 0.04f; p.browAsym = -0.3f;
      p.mouthOpen = 0.12f; p.mouthWidth = 0.3f; p.mouthCurve = -0.45f; p.mouthRound = 0.75f; p.blush = 0.5f;
      break;
    case Expr::Absent:
      p.eyeOpen = 0.75f; p.browY = 0.03f; p.mouthCurve = 0.1f; p.mouthWidth = 0.5f; p.blush = 0.1f;
      break;
    case Expr::Inquiet:   // tête détachée : yeux grands ouverts, sourcils relevés au centre, bouche serrée
      p.eyeSize = 1.12f; p.pupilSize = 0.8f; p.browY = -0.06f; p.browAngle = -0.6f; p.browCurve = 0.15f;
      p.mouthOpen = 0.08f; p.mouthWidth = 0.4f; p.mouthCurve = -0.45f; p.mouthRound = 0.35f; p.blush = 0.15f;
      break;
    case Expr::Joie:      // tête reposée sur le torse : yeux rieurs, grand sourire, étincelles (et sautille)
      p.eyeOpen = 0; p.closedCurve = 1; p.browY = -0.09f; p.browCurve = 0.7f; p.mouthOpen = 1; p.mouthWidth = 1.1f;
      p.mouthCurve = 1; p.mouthRound = 0.1f; p.blush = 1; p.sparkle = 1;
      break;
    case Expr::Fatigue:   // batterie faible : paupières lourdes, regard bas, sourcils tombants
      p.eyeOpen = 0.45f; p.pupilY = 0.35f; p.pupilSize = 0.9f; p.browY = 0.05f; p.browAngle = -0.35f;
      p.browCurve = 0.1f; p.mouthOpen = 0.05f; p.mouthWidth = 0.45f; p.mouthCurve = -0.2f; p.mouthRound = 0.4f;
      p.blush = 0.05f;
      break;
    case Expr::Bisou:     // appui long : yeux clos rieurs, petite bouche ronde, joues rouges, cœurs
      p.eyeOpen = 0; p.closedCurve = 1; p.browY = -0.04f; p.browCurve = 0.6f; p.mouthOpen = 0.18f;
      p.mouthWidth = 0.18f; p.mouthCurve = 0; p.mouthRound = 1; p.blush = 1; p.hearts = 1;
      break;
    case Expr::Content:
      p.mouthOpen = 0.55f; p.mouthWidth = 0.95f; p.mouthCurve = 0.75f; p.blush = 0.55f;
      break;
    case Expr::Surpris:
      p.eyeSize = 1.12f; p.pupilSize = 0.75f; p.browY = -0.07f; p.browCurve = 0.6f;
      p.mouthOpen = 0.5f; p.mouthWidth = 0.3f; p.mouthCurve = 0; p.mouthRound = 1; p.blush = 0.3f;
      break;
    case Expr::Rieur:
      p.eyeOpen = 0; p.closedCurve = 1; p.browY = -0.03f; p.mouthOpen = 0.85f; p.mouthWidth = 1.05f;
      p.mouthCurve = 0.85f; p.mouthRound = 0.1f; p.blush = 0.85f;
      break;
    case Expr::Fache:
      p.eyeOpen = 0.8f; p.lidAngle = 0.5f; p.pupilSize = 0.85f; p.browY = 0.07f; p.browAngle = 0.7f;
      p.browCurve = 0; p.mouthOpen = 0; p.mouthWidth = 0.55f; p.mouthCurve = -0.7f; p.blush = 0.05f;
      break;
    case Expr::Apaise:
      p.eyeOpen = 0; p.closedCurve = 1; p.mouthOpen = 0; p.mouthWidth = 0.85f; p.mouthCurve = 0.9f;
      p.blush = 0.65f;
      break;
    case Expr::Mefiant:
      p.eyeOpen = 0.85f; p.pupilX = 0.85f; p.pupilY = -0.1f; p.browAsym = 0.6f; p.mouthOpen = 0.08f;
      p.mouthWidth = 0.25f; p.mouthCurve = -0.15f; p.mouthRound = 0.7f; p.blush = 0.25f;
      break;
    case Expr::Dort:
      p.eyeOpen = 0; p.closedCurve = -1; p.browY = 0.04f; p.zzz = 1;
      if (Edition::SOFT_SLEEP) {   // édition complète : dort paisiblement, petit sourire, joues roses
        p.mouthOpen = 0; p.mouthWidth = 0.45f; p.mouthCurve = 0.55f; p.mouthRound = 0.2f; p.blush = 0.65f;
      } else {                     // édition instructable : bouche en o du prototype
        p.mouthOpen = 0.1f; p.mouthWidth = 0.3f; p.mouthCurve = 0.1f; p.mouthRound = 0.9f; p.blush = 0.3f;
      }
      break;
    case Expr::Baille:
      p.eyeOpen = 0; p.closedCurve = -0.6f; p.browY = -0.05f; p.browCurve = 0.6f; p.mouthOpen = 1;
      p.mouthWidth = 0.5f; p.mouthCurve = 0; p.mouthRound = 1; p.blush = 0.3f;
      break;
    case Expr::Etourdi:
      p.eyeOpen = 0.9f; p.spin = 1; p.browAsym = 0.3f; p.mouthOpen = 0.15f; p.mouthWidth = 0.5f;
      p.mouthCurve = -0.25f; p.mouthRound = 0.5f; p.blush = 0.2f;
      break;
    case Expr::Clin:
      p.wink = 1; p.browAsym = 0.35f; p.pupilX = -0.2f; p.mouthOpen = 0; p.mouthWidth = 0.85f;
      p.mouthCurve = 0.9f; p.blush = 0.7f;
      break;
  }
  return p;
}

const char* exprName(Expr e) {
  return e < Expr::Count ? NAMES[uint8_t(e)] : "?";
}

bool exprFromName(const char* name, Expr& out) {
  for (uint8_t i = 0; i < uint8_t(Expr::Count); i++) {
    if (strcmp(name, NAMES[i]) == 0) { out = Expr(i); return true; }
  }
  return false;
}

// ── Rendu ─────────────────────────────────────────────────────────────

void FaceRenderer::renderBackground(uint16_t* bg, int size) {
  // Dégradé radial décentré, comme l'aperçu navigateur
  const float fx = 0.8f * R, fy = 0.7f * R, span = 1.15f * R;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      float t = sqrtf((x - fx) * (x - fx) + (y - fy) * (y - fy)) / span;
      t = clampf(t, 0, 1);
      float r, g, b;
      if (t < 0.65f) {
        float k = t / 0.65f;
        r = 0x6D + (0x40 - 0x6D) * k; g = 0x93 + (0x67 - 0x93) * k; b = 0xCF + (0x9F - 0xCF) * k;
      } else {
        float k = (t - 0.65f) / 0.35f;
        r = 0x40 + (0x1D - 0x40) * k; g = 0x67 + (0x37 - 0x67) * k; b = 0x9F + (0x64 - 0x9F) * k;
      }
      bg[y * size + x] = rgb565(uint8_t(r), uint8_t(g), uint8_t(b));
    }
  }
}

void FaceRenderer::buildGradient(uint8_t* idx, int size) {
  const float fx = 0.8f * R, fy = 0.7f * R, span = 1.15f * R;   // même dégradé que renderBackground
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      float t = clampf(sqrtf((x - fx) * (x - fx) + (y - fy) * (y - fy)) / span, 0, 1);
      idx[y * size + x] = uint8_t(t * 255 + 0.5f);
    }
}

void FaceRenderer::paintBackground(uint16_t* bg, const uint8_t* idx, int count, const Palette& pal) {
  uint16_t lut[256];
  for (int i = 0; i < 256; i++) {
    float t = i / 255.0f;
    const Rgb& a = t < 0.65f ? pal.skin[0] : pal.skin[1];
    const Rgb& b = t < 0.65f ? pal.skin[1] : pal.skin[2];
    float k = t < 0.65f ? t / 0.65f : (t - 0.65f) / 0.35f;
    lut[i] = toRgb565({a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k});
  }
  for (int i = 0; i < count; i++) bg[i] = lut[idx[i]];
}

void FaceRenderer::paintBackdrop(uint16_t* bg, int size) {
  // dégradé : or clair derrière le personnage, brun, puis presque noir au bord
  static const float STOPS[3][3] = {{0xF7, 0xB2, 0x4A}, {0x7A, 0x4A, 0x12}, {0x14, 0x0B, 0x04}};
  uint16_t lut[256];
  for (int i = 0; i < 256; i++) {
    float t = i / 255.0f, k = t < 0.3f ? t / 0.3f : (t - 0.3f) / 0.7f;
    const float* a = STOPS[t < 0.3f ? 0 : 1];
    const float* b = STOPS[t < 0.3f ? 1 : 2];
    lut[i] = rgb565(uint8_t(a[0] + (b[0] - a[0]) * k), uint8_t(a[1] + (b[1] - a[1]) * k), uint8_t(a[2] + (b[2] - a[2]) * k));
  }
  const float fx = R, fy = 0.88f * R, span = 1.05f * R;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      float t = clampf(sqrtf((x - fx) * (x - fx) + (y - fy) * (y - fy)) / span, 0, 1);
      bg[y * size + x] = lut[int(t * 255)];
    }
}

void FaceRenderer::paintNeon(uint16_t* bg, int count) {
  for (int i = 0; i < count; i++) bg[i] = 0x0000;
}

void FaceRenderer::drawDecor(Canvas& c) {
  if (design_ == Design::Horloge) drawTicks(c);
  if (design_ != Design::HorlogeEnPied) return;
  constexpr uint16_t GOLD = rgb565(0xE9, 0xB4, 0x4C), GOLD_PALE = rgb565(0xFF, 0xE9, 0xA8), GOLD_DEEP = rgb565(0xD9, 0xA4, 0x41);
  for (int i = 0; i < 150; i++) {   // pastilles dorées en quinconce, plus ou moins vives
    float x = (i % 15) * 34 + ((i / 15) % 2) * 17 - 8, y = (i / 15) * 50 + 6;
    float v = sinf(i * 127.1f + 311.7f) * 43758.5453f;
    c.fillCircle(x, y, 15, Paint::blend(GOLD, 0.08f + 0.16f * (v - floorf(v))));
  }
  c.softEllipse(R, 0.88f * R, 0.95f * R, 0.95f * R, GOLD_PALE, 0.55f);   // halo derrière le personnage
  for (int i = 0; i < 36; i++) {    // rayons : une barre sur trois plus longue et plus claire
    bool big = i % 3 == 0;
    float a = i * 0.1745329f, r0 = R * (big ? 0.62f : 0.84f), r1 = R * 0.98f;
    Pt ray[2] = {{R + cosf(a) * r0, R + sinf(a) * r0}, {R + cosf(a) * r1, R + sinf(a) * r1}};
    c.strokePolyline(ray, 2, big ? 9 : 4, Paint::blend(big ? GOLD_PALE : GOLD_DEEP, big ? 0.8f : 0.5f));
  }
}

void FaceRenderer::drawTicks(Canvas& c) {
  for (int i = 0; i < 12; i++) {   // graduations du cadran : plus longues à 12, 3, 6 et 9
    bool big = i % 3 == 0;
    float a = i * 0.5235988f, r0 = R * (big ? 0.76f : 0.84f), r1 = R * 0.94f;
    Pt tick[2] = {{R + sinf(a) * r0, R - cosf(a) * r0}, {R + sinf(a) * r1, R - cosf(a) * r1}};
    c.strokePolyline(tick, 2, big ? 9 : 5, Paint::blend(ink_, big ? 0.9f : 0.7f));
  }
}

void FaceRenderer::drawMenu(Canvas& c, Design pick) {
  for (int side = -1; side <= 1; side += 2) {   // flèches : design précédent / suivant
    float tip = R + side * (R - 22), base = R + side * (R - 58);
    Pt outer[3] = {{tip - side * -6.0f, R}, {base - side * 5.0f, R - 36}, {base - side * 5.0f, R + 36}};
    Pt inner[3] = {{tip, R}, {base, R - 28}, {base, R + 28}};
    c.fillPolygon(outer, 3, Paint::blend(ink_, 0.8f));
    c.fillPolygon(inner, 3, Paint::solid(WHITE));
  }
  const int n = designCount();
  for (int d = 0, i = 0; d < int(Design::Count); d++) {   // une pastille par design proposé, en bas
    if (!designAvailable(Design(d))) continue;
    float x = R + (i++ - (n - 1) / 2.0f) * 34, y = R + 0.80f * R;
    bool on = d == int(pick);
    c.fillCircle(x, y, on ? 13 : 9, Paint::blend(ink_, 0.8f));
    c.fillCircle(x, y, on ? 10 : 6, on ? Paint::solid(WHITE) : Paint::blend(WHITE, 0.45f));
  }
}

namespace {
// Poses du corps : mains gauche et droite par rapport au centre du disque, coude (écart, chute), marche
struct Pose { float lx, ly, rx, ry, ex, ey, walk; };
constexpr Pose POSE_IDLE = {-180, -15, 150, -55, 10, 30, 1};       // bras ouverts, marche sur place
constexpr Pose POSE_UP = {-165, -95, 165, -95, 25, 20, 0};         // bras en l'air : surprise, joie, inquiétude
constexpr Pose POSE_HIPS = {-92, 62, 92, 62, 75, -5, 0};           // poings sur les hanches : colère, bouderie
constexpr Pose POSE_BELLY = {-70, 78, 70, 78, 70, 30, 0.4f};       // se tient le ventre : rire
constexpr Pose POSE_KISS = {-150, 25, 48, 62, 30, 40, 0};          // main devant la bouche
constexpr Pose POSE_REST = {-128, 100, 128, 100, 18, 10, 0};       // bras le long du corps : sommeil, fatigue

const Pose& poseFor(Expr e) {
  switch (e) {
    case Expr::Surpris: case Expr::Inquiet: case Expr::Joie: return POSE_UP;
    case Expr::Fache: case Expr::Emballe: case Expr::Colere: case Expr::Boude: case Expr::Tenir: return POSE_HIPS;
    case Expr::Rieur: return POSE_BELLY;
    case Expr::Bisou: return POSE_KISS;
    case Expr::Dort: case Expr::Baille: case Expr::Fatigue: case Expr::Absent: return POSE_REST;
    default: return POSE_IDLE;
  }
}
}  // namespace

void FaceRenderer::render(Canvas& c, const FaceParams& p, float blink, float bounce, float clock, Expr state) {
  if (fullBody()) renderBody(c, p, blink, bounce, clock, state);
  else renderFace(c, p, blink, bounce, clock);
}

// Personnage en pied (portage de extras/horloge/horloge_preview.html) : jambes et bras
// derrière le disque, visage réduit dans le disque, gants par-dessus.
void FaceRenderer::renderBody(Canvas& c, const FaceParams& p, float blink, float bounce, float clock, Expr state) {
  constexpr uint16_t GLOVE = rgb565(0xFF, 0xFA, 0xF0), LEG = rgb565(0x3A, 0x1A, 0x0C);
  constexpr float RB = 120, GROUND = 436, PI = 3.14159265f;
  const Pose& t = poseFor(state);
  float dt = clampf(clock - bodyClock_, 0, 0.1f), k = 1 - expf(-dt / 0.14f);
  bodyClock_ = clock;
  lx_ += (t.lx - lx_) * k; ly_ += (t.ly - ly_) * k; rx_ += (t.rx - rx_) * k; ry_ += (t.ry - ry_) * k;
  ex_ += (t.ex - ex_) * k; ey_ += (t.ey - ey_) * k; walk_ += (t.walk - walk_) * k;

  const bool laughing = state == Expr::Rieur, asleep = state == Expr::Dort;
  const bool fists = &t == &POSE_HIPS, waving = state == Expr::Content || state == Expr::Clin;
  const float ph = clock * 5;
  const float cx = R + (fists && state != Expr::Boude && state != Expr::Tenir ? sinf(clock * 47) * 2 : 0);
  const float cy = 212 - fabsf(sinf(ph)) * 7 * walk_ - (laughing ? fabsf(sinf(clock * 14)) * 7 : 0) - bounce * 12 +
                   (asleep ? sinf(clock * 1.2f) * 3 : 0);

  for (int s = -1; s <= 1; s += 2) {   // jambes fines et souliers blancs
    float sw = sinf(ph + (s < 0 ? 0 : PI)) * walk_, lift = fmaxf(0, sw) * 18;
    float fx = cx + s * 30 + sw * 20, fy = GROUND - lift, hx = cx + s * 20, hy = cy + RB - 8;
    c.strokeQuad({hx, hy}, {(hx + fx) / 2 + s * 10 + sw * 14, (hy + fy) / 2}, {fx, fy - 8}, 8, Paint::solid(LEG), 10);
    c.fillEllipse(fx + s * 10, fy, 27.5f, 15.5f, Paint::solid(ink_));
    c.fillEllipse(fx + s * 10, fy, 25, 13, Paint::solid(GLOVE));
  }

  float hand[2][3];                    // x, y, angle de l'avant-bras
  const float wave = waving ? sinf(clock * 7) : 0;
  for (int s = -1; s <= 1; s += 2) {   // bras : contour puis couleur du disque
    float sx = cx + s * RB * 0.9f, sy = cy + 14;
    float hx = cx + (s < 0 ? lx_ : rx_ + wave * 12), hy = cy + (s < 0 ? ly_ : ry_ - fabsf(wave) * 6);
    Pt q = {(sx + hx) / 2 + s * ex_, (sy + hy) / 2 + ey_};
    c.strokeQuad({sx, sy}, q, {hx, hy}, 24, Paint::solid(ink_), 10);
    c.strokeQuad({sx, sy}, q, {hx, hy}, 16, Paint::solid(skinMid_), 10);
    float* h = hand[s < 0 ? 0 : 1];
    h[0] = hx; h[1] = hy; h[2] = atan2f(hy - q.y, hx - q.x);
  }

  // disque : tranche sombre décalée, puis cadran à plat
  // Tracé ligne à ligne, chaque pixel écrit une seule fois (quatre disques superposés, ou des contours
  // en traits, coûtaient ≈ 13 ms par image) : la tranche ne dépasse qu'à gauche du cadran.
  constexpr float RO = RB + 2.5f, TX = -9, TY = 3;
  auto half = [](float r, float dy) { return fabsf(dy) < r ? sqrtf(r * r - dy * dy) : -1.0f; };
  const Paint inkP = Paint::solid(ink_), darkP = Paint::solid(skinDark_), midP = Paint::solid(skinMid_);
  for (int y = int(cy - RO) - 1; y <= int(cy + RO + TY) + 1; y++) {
    float dy = y + 0.5f - cy, dt2 = dy - TY;
    float fo = half(RO, dy), fi = half(RB, dy), to = half(RO, dt2), ti = half(RB, dt2);
    float stop = fo >= 0 ? cx - fo : 1e9f;                    // la tranche s'arrête au bord du cadran
    if (to >= 0) {
      float l = cx + TX - to, r = fminf(cx + TX + to, stop);
      if (ti >= 0) {
        c.hline(y, l, fminf(cx + TX - ti, stop), inkP);
        c.hline(y, cx + TX - ti, fminf(cx + TX + ti, stop), darkP);
        c.hline(y, cx + TX + ti, r, inkP);
      } else {
        c.hline(y, l, r, inkP);
      }
    }
    if (fo < 0) continue;
    if (fi >= 0) {
      c.hline(y, cx - fo, cx - fi, inkP);
      c.hline(y, cx - fi, cx + fi, midP);
      c.hline(y, cx + fi, cx + fo, inkP);
    } else {
      c.hline(y, cx - fo, cx + fo, inkP);
    }
  }
  const float scale = RB / R;
  c.setTransform(scale, cx - R * scale, cy - R * scale);
  drawTicks(c);
  renderFace(c, p, blink, 0, clock);   // le sursaut déplace tout le corps, pas les yeux
  c.clearTransform();

  // gants : paume et doigts ronds (poing fermé en colère), contour puis blanc
  static const float FINGERS[5][3] = {{0, 0, 15}, {15, -11, 7.5f}, {21, -2, 8}, {17, 9, 7.5f}, {-1, -15, 7}};
  for (int pass = 0; pass < 2; pass++)
    for (auto& h : hand) {
      float ca = cosf(h[2]), sa = sinf(h[2]);
      for (int i = 0; i < (fists ? 1 : 5); i++) {
        float x = h[0] + FINGERS[i][0] * ca - FINGERS[i][1] * sa, y = h[1] + FINGERS[i][0] * sa + FINGERS[i][1] * ca;
        float r = fists ? 17 : FINGERS[i][2];
        c.fillCircle(x, y, pass ? r : r + 2.5f, Paint::solid(pass ? GLOVE : ink_));
      }
    }
}

void FaceRenderer::renderFace(Canvas& c, const FaceParams& p, float blink, float bounce, float clock) {
  if (neon()) { renderNeon(c, p, blink, bounce, clock); return; }
  if (isClock(design_)) c.fillCircle(R, R + 0.03f * R, 7, Paint::solid(ink_));   // nez : l'axe des aiguilles
  drawBlush(c, p);
  drawEye(c, -1, p, blink, bounce, clock);
  drawEye(c, 1, p, blink, bounce, clock);
  drawBrow(c, -1, p, bounce);
  drawBrow(c, 1, p, bounce);
  drawMouth(c, p);
  drawZzz(c, p, clock);
  drawSparkles(c, p, clock);
  drawHearts(c, p, clock);
}

void FaceRenderer::drawEye(Canvas& c, int side, const FaceParams& p, float blink, float bounce, float clock) {
  const Look& L = look(design_);
  const bool clockFace = isClock(design_);
  float size = p.eyeSize * (1 + bounce * 0.15f);
  float rx = L.eyeRx * R * size, ry = L.eyeRy * R * size;
  float cx = R + side * L.eyeDx * R + p.pupilX * 0.04f * R;
  float cy = R + L.eyeCy * R + p.pupilY * 0.03f * R - bounce * 10;
  float open = clampf(p.eyeOpen * blink * (side > 0 ? 1 - p.wink : 1), 0, 1);

  if (open < 0.1f) {   // œil fermé : un arc
    float curve = (blink < 0.5f && p.eyeOpen > 0.3f) ? -0.25f : p.closedCurve;
    float y = cy + ry * 0.1f;
    c.strokeQuad({cx - rx * 0.9f, y}, {cx, y - curve * ry * 0.8f}, {cx + rx * 0.9f, y}, 9, Paint::solid(ink_));
    if (clockFace) drawLashes(c, side, cx, y - fmaxf(0, curve) * ry * 0.4f, rx, ry * 0.2f);
    return;
  }

  c.setClipEllipse(cx, cy, rx, ry);
  c.fillEllipse(cx, cy, rx, ry, Paint::solid(clockFace ? CREAM : EYE_WHITE));

  float pr = 0.52f * rx * p.pupilSize;
  float px = p.pupilX, py = p.pupilY;
  if (p.spin > 0.01f) {
    float a = clock * 9 + (side > 0 ? 3.14159f : 0);
    px = px * (1 - p.spin) + cosf(a) * 0.6f * p.spin;
    py = py * (1 - p.spin) + sinf(a) * 0.6f * p.spin;
  }
  float tall = clockFace ? 1.45f : 1.12f;   // pupille haute des vieux dessins animés
  float ppx = cx + px * (rx - pr) * 0.85f, ppy = cy + py * (ry - pr * tall) * 0.7f + ry * 0.08f;
  c.fillEllipse(ppx, ppy, pr, pr * tall, Paint::solid(PUPIL));
  if (clockFace) {   // reflet « en part de tarte », en haut à droite de la pupille
    Pt cut[6] = {{ppx, ppy}};
    for (int i = 0; i < 5; i++) {
      float a = -1.32f + i * 0.205f;
      cut[i + 1] = {ppx + cosf(a) * pr * 1.6f, ppy + sinf(a) * pr * 1.6f};
    }
    c.fillPolygon(cut, 6, Paint::solid(CREAM));
  } else {
    c.fillCircle(ppx - pr * 0.32f, ppy - pr * 0.4f, pr * 0.3f, Paint::solid(WHITE));
  }

  // paupières, peintes avec le fond pour se fondre dans la peau
  float lidY = cy - ry + (1 - open) * ry;
  float slope = p.lidAngle * 0.9f * -side;
  Pt lid[4] = {{cx - rx - 2, cy - ry - 2}, {cx + rx + 2, cy - ry - 2},
               {cx + rx + 2, lidY + slope * rx}, {cx - rx - 2, lidY - slope * rx}};
  c.fillPolygon(lid, 4, lidPaint());
  if (open < 0.98f || p.lidAngle > 0.05f) {
    Pt edge[2] = {{cx - rx, lidY - slope * rx}, {cx + rx, lidY + slope * rx}};
    c.strokePolyline(edge, 2, 6, Paint::solid(ink_));
  }
  float botY = cy + ry - (1 - open) * ry * 0.8f - p.lidBottom * ry * 0.6f;
  Pt bot[4] = {{cx - rx - 2, botY}, {cx + rx + 2, botY}, {cx + rx + 2, cy + ry + 2}, {cx - rx - 2, cy + ry + 2}};
  c.fillPolygon(bot, 4, lidPaint());
  c.clearClip();

  if (clockFace) {
    c.strokeEllipse(cx, cy, rx, ry, 6, Paint::solid(ink_));
    drawLashes(c, side, cx, lidY, rx, ry);
  } else {
    c.strokeEllipse(cx, cy, rx, ry, 3, Paint::solid(EYE_RIM));
  }
}

// Trois cils en éventail vers l'extérieur, posés sur le bord de la paupière (lashY).
void FaceRenderer::drawLashes(Canvas& c, int side, float cx, float lashY, float rx, float ry) {
  for (int k = -1; k <= 1; k++) {
    float a = -1.5708f + side * (0.35f + k * 0.32f);
    float x0 = cx + cosf(a) * rx * 0.7f, y0 = lashY + (1 + sinf(a)) * ry * 0.25f - 2;
    Pt lash[2] = {{x0, y0}, {x0 + cosf(a) * 20, y0 + sinf(a) * 20}};
    c.strokePolyline(lash, 2, 5, Paint::solid(ink_));
  }
}

void FaceRenderer::drawBrow(Canvas& c, int side, const FaceParams& p, float bounce) {
  const Look& L = look(design_);
  float cx = R + side * L.eyeDx * R + p.pupilX * 0.04f * R;
  float baseY = R + L.eyeCy * R - L.eyeRy * R * p.eyeSize - L.browGap * R + p.browY * R - bounce * 16;
  float half = L.browHalf * R;
  float asym = -side * p.browAsym * 0.07f * R;
  float inner = cx - side * half, outer = cx + side * half;
  float yIn = baseY + p.browAngle * 0.1f * R + asym;
  float yOut = baseY - p.browAngle * 0.03f * R + asym;
  c.strokeQuad({inner, yIn}, {cx, (yIn + yOut) / 2 - p.browCurve * 0.07f * R}, {outer, yOut}, L.browW, Paint::solid(ink_));
}

void FaceRenderer::drawBlush(Canvas& c, const FaceParams& p) {
  if (p.blush < 0.02f) return;
  for (int side = -1; side <= 1; side += 2) {
    c.softEllipse(R + side * look(design_).blushDx * R, R + look(design_).blushDy * R, 0.16f * R, 0.09f * R, BLUSH, 0.45f * p.blush);
  }
}

void FaceRenderer::drawMouth(Canvas& c, const FaceParams& p) {
  constexpr int N = 28;
  const Look& L = look(design_);
  const float cx = R, cy = R + L.mouthCy * R;
  const float halfW = L.mouthW * R * p.mouthWidth, H = L.mouthH * R;
  const float open = clampf(p.mouthOpen, 0, 1.2f);
  Pt top[N + 1], bot[N + 1];
  for (int i = 0; i <= N; i++) {
    float t = -1 + 2.0f * i / N, s = sqrtf(fmaxf(0, 1 - t * t));
    float x = cx + t * halfW;
    float base = cy + p.mouthCurve * 0.08f * R * (1 - t * t) - p.mouthCurve * 0.03f * R;
    top[i] = {x, base - open * H * 0.5f * p.mouthRound * s};
    bot[i] = {x, base + open * H * (1 - 0.5f * p.mouthRound) * s};
  }

  if (open < 0.04f) {   // bouche fermée : un trait
    c.strokePolyline(top, N + 1, 8, Paint::solid(ink_));
    return;
  }

  Pt poly[2 * (N + 1)];
  int n = 0;
  for (int i = 0; i <= N; i++) poly[n++] = top[i];
  for (int i = N; i >= 0; i--) poly[n++] = bot[i];
  c.fillPolygon(poly, n, Paint::solid(MOUTH));

  c.setClipPolygon(poly, n);
  float lowY = bot[N / 2].y, depth = lowY - top[N / 2].y;
  c.fillEllipse(cx, lowY - depth * 0.05f, halfW * 0.6f, depth * 0.42f, Paint::solid(TONGUE));
  if (p.mouthRound < 0.5f && open > 0.3f) {   // dents du haut, bouche en D
    Pt teeth[2 * (N + 1)];
    int m = 0;
    for (int i = 0; i <= N; i++) teeth[m++] = {top[i].x, top[i].y + depth * 0.16f};
    for (int i = N; i >= 0; i--) teeth[m++] = {top[i].x, top[i].y - 4};
    c.fillPolygon(teeth, m, Paint::solid(TEETH));
  }
  c.clearClip();
  c.strokePolyline(poly, n, 6, Paint::solid(ink_), true);
}

void FaceRenderer::drawHearts(Canvas& c, const FaceParams& p, float clock) {
  if (p.hearts < 0.05f) return;
  constexpr uint16_t HEART = rgb565(0xFF, 0x4D, 0x7A);
  for (int i = 0; i < 4; i++) {   // quatre cœurs qui montent de chaque côté, grossissent puis s'effacent
    float k = fmodf(clock * 0.55f + i * 0.25f, 1.0f);
    float side = (i % 2) ? 1.0f : -1.0f;
    float x = R + side * (0.52f * R + 10 * sinf(k * 6.28f + i)), y = R + 0.18f * R - k * 0.62f * R;
    float s = (11 + 11 * sinf(k * 3.14159f)) * p.hearts;
    Pt h[24];
    for (int j = 0; j < 24; j++) {   // courbe du cœur : x = 16 sin³t, y = 13 cos t − 5 cos 2t − 2 cos 3t − cos 4t
      float t = j * 6.28319f / 24, st = sinf(t);
      h[j] = {x + s / 16 * 16 * st * st * st,
              y - s / 16 * (13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t))};
    }
    c.fillPolygon(h, 24, Paint::blend(HEART, sinf(k * 3.14159f) * p.hearts));
  }
}

void FaceRenderer::drawSparkles(Canvas& c, const FaceParams& p, float clock) {
  if (p.sparkle < 0.05f) return;
  for (int i = 0; i < 8; i++) {   // étoiles à quatre branches qui tournent et scintillent
    float a = i * 0.785398f + clock * 0.6f;
    float x = R + cosf(a) * 0.8f * R, y = R + sinf(a) * 0.8f * R;
    float s = (12 + 16 * fabsf(sinf(clock * 6 + i * 1.3f))) * p.sparkle, w = s * 0.3f;
    Pt star[8] = {{x, y - s}, {x + w, y - w}, {x + s, y}, {x + w, y + w},
                  {x, y + s}, {x - w, y + w}, {x - s, y}, {x - w, y - w}};
    c.fillPolygon(star, 8, neon() ? dimmed(skinLight_, 0.9f * p.sparkle) : Paint::blend(WHITE, 0.9f * p.sparkle));
  }
}

void FaceRenderer::drawZzz(Canvas& c, const FaceParams& p, float clock) {
  if (p.zzz < 0.05f) return;
  for (int i = 0; i < 3; i++) {
    float k = fmodf(clock * 0.35f + i / 3.0f, 1.0f);
    float s = 28 + 24 * k;                                   // grandit en montant
    float a = sinf(k * 3.14159f) * 0.95f * p.zzz;
    // au-dessus du sourcil droit, vers le haut de la tête (pas sur la paupière), dans le rond de l'écran
    float x = R + 0.30f * R + k * 24, y = R - 0.535f * R - k * 60;
    Pt z[4] = {{x, y - s * 0.7f}, {x + s * 0.55f, y - s * 0.7f}, {x, y}, {x + s * 0.55f, y}};
    c.strokePolyline(z, 4, 6, Paint::blend(ink_, a));        // couleur du trait : lisible sur le fond clair du sommeil
  }
}

// ── Néon (édition complète) ──────────────────────────────────────────────────
// Visage du rendu : yeux en verre jaune lumineux, sourcils en fil orangé, joues « /// »,
// bouche orange et langue saumon, tout sur fond noir avec un halo.

void FaceRenderer::renderNeon(Canvas& c, const FaceParams& p, float blink, float bounce, float clock) {
  drawNeonBlush(c, p);
  drawNeonEye(c, -1, p, blink, bounce, clock);
  drawNeonEye(c, 1, p, blink, bounce, clock);
  drawNeonBrow(c, -1, p, bounce);
  drawNeonBrow(c, 1, p, bounce);
  drawNeonMouth(c, p);
  drawNeonZzz(c, p, clock);
  drawSparkles(c, p, clock);
  drawHearts(c, p, clock);
}

void FaceRenderer::drawNeonEye(Canvas& c, int side, const FaceParams& p, float blink, float bounce, float clock) {
  const Look& L = look(design_);
  float size = p.eyeSize * (1 + bounce * 0.15f);
  float rx = L.eyeRx * R * size, ry = L.eyeRy * R * size;
  float cx = R + side * L.eyeDx * R + p.pupilX * 0.04f * R;
  float cy = R + L.eyeCy * R + p.pupilY * 0.03f * R - bounce * 10;
  // Œil fermé par l'expression (rire, sommeil, clin d'œil) : le verre reste allumé, barré d'un arc sombre,
  // et arrive en fondu. Le clignement, lui, éteint le verre derrière des paupières noires.
  float want = clampf(p.eyeOpen * (side > 0 ? 1 - p.wink : 1), 0, 1);
  float shut = clampf((0.35f - want) / 0.3f, 0, 1);
  if (shut >= 1) {
    drawClosedLens(c, cx, cy, rx, ry, p.closedCurve, 1);
    return;
  }

  const float glow = rx * 0.2f;
  float open = clampf(want * blink, 0, 1);
  if (open < 0.08f) {   // fin du clignement : un fil lumineux à peine arrondi
    Pt line[9];
    for (int i = 0; i < 9; i++) {
      float t = -1 + i / 4.0f;
      line[i] = {cx + t * rx * 0.85f, cy + ry * 0.15f + (1 - t * t) * ry * 0.1f};
    }
    neonStroke(c, line, 9, 8, skinLight_);
  } else {
    float lidY = cy - ry + (1 - open) * ry;
    float slope = p.lidAngle * 0.9f * -side;
    float botY = cy + ry - (1 - open) * ry * 0.8f - p.lidBottom * ry * 0.6f;
    // halo coupé par les paupières : pas de lueur au-dessus d'un verre caché
    float e = rx + glow + 4;
    float top = open > 0.97f && fabsf(slope) < 0.02f ? cy - ry - glow - 4 : lidY;
    float bottom = botY > cy + ry - 1 ? cy + ry + glow + 4 : botY;
    Pt band[4] = {{cx - e, top - slope * e}, {cx + e, top + slope * e}, {cx + e, bottom}, {cx - e, bottom}};
    c.setClipPolygon(band, 4);
    c.glowEllipse(cx, cy, rx, ry, 0, glow, skinLight_, 0.55f, true);

    c.setClipEllipse(cx, cy, rx, ry);
    c.fillEllipse(cx, cy, rx, ry, Paint::solid(skinLight_));
    // ombre basse du verre : mélange calculé une fois (le verre dessous est uni), pas de fondu par pixel
    c.fillEllipse(cx, cy + ry * 0.62f, rx * 0.9f, ry * 0.5f, Paint::solid(mix565(skinLight_, skinMid_, 0.5f)));
    float pr = 0.40f * rx * p.pupilSize, tall = 1.15f;
    float px = p.pupilX, py = p.pupilY;
    if (p.spin > 0.01f) {
      float a = clock * 9 + (side > 0 ? 3.14159f : 0);
      px = px * (1 - p.spin) + cosf(a) * 0.6f * p.spin;
      py = py * (1 - p.spin) + sinf(a) * 0.6f * p.spin;
    }
    float ppx = cx + px * (rx - pr) * 0.8f, ppy = cy + py * (ry - pr * tall) * 0.7f + ry * 0.05f;
    c.fillEllipse(ppx, ppy, pr, pr * tall, Paint::solid(NEON_DARK));
    c.fillCircle(ppx - pr * 0.34f, ppy - pr * 0.42f, pr * 0.30f, Paint::solid(NEON_GLINT));
    c.fillCircle(ppx + pr * 0.36f, ppy + pr * 0.42f, pr * 0.12f, Paint::blend(NEON_GLINT, 0.8f));
    Pt lid[4] = {{cx - rx - 2, cy - ry - 2}, {cx + rx + 2, cy - ry - 2},
                 {cx + rx + 2, lidY + slope * rx}, {cx - rx - 2, lidY - slope * rx}};
    c.fillPolygon(lid, 4, Paint::background());
    Pt bot[4] = {{cx - rx - 2, botY}, {cx + rx + 2, botY}, {cx + rx + 2, cy + ry + 2}, {cx - rx - 2, cy + ry + 2}};
    c.fillPolygon(bot, 4, Paint::background());
    c.clearClip();
  }
  if (shut > 0) drawClosedLens(c, cx, cy, rx, ry, p.closedCurve, shut);
}

// Œil clos du rendu : verre allumé, ombre orangée en bas qui remonte en dôme sous un arc sombre épais
// (curve > 0 : ∩ rieur ; curve < 0 : ∪ endormi, sans dôme). alpha < 1 : fondu par-dessus l'œil ouvert.
void FaceRenderer::drawClosedLens(Canvas& c, float cx, float cy, float rx, float ry, float curve, float alpha) {
  auto paint = [alpha](uint16_t col, float a) {
    return a * alpha >= 0.99f ? Paint::solid(col) : Paint::blend(col, a * alpha);
  };
  const float happy = clampf(curve, 0, 1), y0 = cy + ry * (0.06f + 0.13f * curve);   // pieds de l'arc
  c.glowEllipse(cx, cy, rx, ry, 0, rx * 0.2f, skinLight_, 0.55f * alpha, alpha >= 0.99f);   // en fondu : par-dessus l'œil ouvert
  c.setClipEllipse(cx, cy, rx, ry);
  c.fillEllipse(cx, cy, rx, ry, paint(skinLight_, 1));
  c.fillEllipse(cx, cy + ry * 0.49f, rx * 0.75f, ry * 0.45f, paint(skinMid_, 0.6f + 0.4f * happy));
  if (happy > 0.05f) c.fillEllipse(cx, y0, rx * 0.35f, ry * 0.31f * happy, paint(skinMid_, 1));
  c.clearClip();
  Pt arc[13];
  for (int i = 0; i <= 12; i++) {
    float t = 3.1415927f * i / 12;
    arc[i] = {cx - cosf(t) * rx * 0.46f, y0 - sinf(t) * ry * 0.45f * curve};
  }
  c.fillStroke(arc, 13, rx * 0.25f, paint(NEON_DARK, 1), false);
}

void FaceRenderer::drawNeonBrow(Canvas& c, int side, const FaceParams& p, float bounce) {
  const Look& L = look(design_);
  float cx = R + side * L.eyeDx * R + p.pupilX * 0.04f * R;
  float baseY = R + L.eyeCy * R - L.eyeRy * R * p.eyeSize - L.browGap * R + p.browY * R - bounce * 16;
  float half = L.browHalf * R, asym = -side * p.browAsym * 0.07f * R;
  float inner = cx - side * half, outer = cx + side * half;
  float yIn = baseY + p.browAngle * 0.1f * R + asym, yOut = baseY - p.browAngle * 0.03f * R + asym;
  Pt a = {inner, yIn}, ctrl = {cx, (yIn + yOut) / 2 - p.browCurve * 0.30f * R}, b = {outer, yOut};
  Pt pts[11];
  for (int i = 0; i <= 10; i++) {   // arc en fil, plus bombé que le sourcil classique
    float t = i / 10.0f, u = 1 - t;
    pts[i] = {u * u * a.x + 2 * u * t * ctrl.x + t * t * b.x, u * u * a.y + 2 * u * t * ctrl.y + t * t * b.y};
  }
  neonStroke(c, pts, 11, L.browW, ink_);
}

void FaceRenderer::drawNeonBlush(Canvas& c, const FaceParams& p) {
  if (p.blush < 0.02f) return;
  const Look& L = look(design_);
  const Paint paint = dimmed(NEON_BLUSH, clampf(p.blush * 1.3f, 0, 1));
  for (int side = -1; side <= 1; side += 2)
    for (int k = -1; k <= 1; k++) {   // trois traits penchés : « /// »
      float x = R + side * L.blushDx * R + k * 0.055f * R, y = R + L.blushDy * R;
      Pt s[2] = {{x - 0.022f * R, y + 0.042f * R}, {x + 0.022f * R, y - 0.042f * R}};
      c.fillStroke(s, 2, 5, paint);
    }
}

void FaceRenderer::drawNeonMouth(Canvas& c, const FaceParams& p) {
  constexpr int N = 28;
  const Look& L = look(design_);
  const float cx = R, cy = R + L.mouthCy * R;
  const float halfW = L.mouthW * R * p.mouthWidth, H = L.mouthH * R;
  const float open = clampf(p.mouthOpen, 0, 1.2f);
  // forme de la bouche classique ; ouverte, la lèvre du haut reste presque droite comme sur le rendu
  const float bend = 0.08f * R * (1 - 0.65f * clampf(open, 0, 1));
  Pt top[N + 1], bot[N + 1];
  for (int i = 0; i <= N; i++) {
    float t = -1 + 2.0f * i / N, s = sqrtf(fmaxf(0, 1 - t * t));
    float x = cx + t * halfW;
    float base = cy + p.mouthCurve * bend * (1 - t * t) - p.mouthCurve * 0.03f * R;
    top[i] = {x, base - open * H * 0.5f * p.mouthRound * s};
    bot[i] = {x, base + open * H * (1 - 0.5f * p.mouthRound) * s};
  }

  if (open < 0.04f) {   // bouche fermée : un fil lumineux
    Pt line[N / 2 + 1];
    for (int i = 0; i <= N / 2; i++) line[i] = top[2 * i];
    neonStroke(c, line, N / 2 + 1, 8, skinDark_);
    return;
  }

  Pt poly[2 * (N + 1)];
  int n = 0;
  for (int i = 0; i <= N; i++) poly[n++] = top[i];
  for (int i = N; i >= 0; i--) poly[n++] = bot[i];
  const float lowY = bot[N / 2].y, depth = lowY - top[N / 2].y, my = (lowY + top[N / 2].y) / 2;
  uint16_t haloCol = 0;
  for (int g = 3; g >= 1; g--) {   // halo : la bouche agrandie de 9, 6 puis 3 px, de plus en plus vive
    float gx = (halfW + 3 * g) / halfW, gy = (depth / 2 + 3 * g) / (depth / 2);
    Pt halo[2 * (N + 1)];
    for (int i = 0; i < n; i++) halo[i] = {cx + (poly[i].x - cx) * gx, my + (poly[i].y - my) * gy};
    haloCol = mix565(haloCol, skinDark_, g == 3 ? 0.07f : g == 2 ? 0.14f : 0.28f);
    c.fillPolygon(halo, n, Paint::solid(haloCol));
  }
  c.fillPolygon(poly, n, Paint::solid(skinDark_));
  c.setClipPolygon(poly, n);
  c.fillEllipse(cx, lowY + depth * 0.35f, halfW * 0.85f, depth * 0.75f, Paint::solid(mix565(skinDark_, NEON_TONGUE, 0.45f)));
  c.clearClip();
}

void FaceRenderer::drawNeonZzz(Canvas& c, const FaceParams& p, float clock) {
  if (p.zzz < 0.05f) return;
  for (int i = 0; i < 3; i++) {   // mêmes « z » que le visage classique, en fil lumineux
    float k = fmodf(clock * 0.35f + i / 3.0f, 1.0f);
    float s = 28 + 24 * k;
    float a = sinf(k * 3.14159f) * 0.95f * p.zzz;
    float x = R + 0.30f * R + k * 24, y = R - 0.56f * R - k * 60;
    Pt z[4] = {{x, y - s * 0.7f}, {x + s * 0.55f, y - s * 0.7f}, {x, y}, {x + s * 0.55f, y}};
    c.fillStroke(z, 4, 6, dimmed(skinLight_, a));
  }
}

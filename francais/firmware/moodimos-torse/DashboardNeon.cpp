#include "DashboardNeon.h"
#include "Canvas.h"
#include "HeadFinder.h"
#include "Household.h"
#include "MoodiMOSHead.h"
#include "NeckProtocol.h"
#include "StrokeFont.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace DashboardNeon {

namespace {
constexpr int W = 320, H = 480;
constexpr float PI_F = 3.1415927f;

// Couleurs du visage néon de la tête ; le trait sous l'en-tête prend l'accent de l'humeur envoyé par
// la tête, comme les couleurs du tableau de bord des autres éditions.
constexpr uint16_t BG = 0x0000;                          // noir, comme l'écran de la tête
constexpr uint16_t ORANGE = rgb565(0xFF, 0x96, 0x1E);   // titre, sourcils de la tête
constexpr uint16_t YELLOW = rgb565(0xFF, 0xC4, 0x22);   // verre des yeux de la tête : valeurs, icônes
constexpr uint16_t GOLD = rgb565(0xFF, 0xD0, 0x3A);     // verre de la joie
constexpr uint16_t AMBER_DEEP = rgb565(0xE0, 0x70, 0x10);
constexpr uint16_t MUTED = rgb565(0xB8, 0x74, 0x2C);    // étiquettes et unités
constexpr uint16_t DIM = rgb565(0x4A, 0x2E, 0x10);      // éteint
constexpr uint16_t RED = rgb565(0xFF, 0x4A, 0x14);      // bouche, alerte
constexpr uint16_t TONGUE = rgb565(0xFF, 0x9A, 0x70);
constexpr uint16_t BLUSH = rgb565(0xFF, 0x5A, 0x4C);
constexpr uint16_t DARK = rgb565(0x2A, 0x10, 0x00);     // pupilles et paupières closes
constexpr uint16_t GLINT = rgb565(0xFF, 0xF6, 0xD8);
constexpr uint16_t GREEN = rgb565(0x4C, 0xE8, 0x6A);    // jeu : bouton « oui »
// Sommeil doux de la tête (palette néon mauve de Mood.h) : verre, ombre du verre, sourcils, bouche
constexpr uint16_t SLEEP_LENS = rgb565(0xEC, 0xC4, 0xFF), SLEEP_SHADE = rgb565(0xAE, 0x76, 0xDC);
constexpr uint16_t SLEEP_INK = rgb565(0xD8, 0xA8, 0xFF), SLEEP_MOUTH = rgb565(0xFF, 0x8C, 0xC0);

// Jeu « Devine l'animal »
constexpr float PLAY_X = 262, PLAY_Y = 244, PLAY_R = 40;               // bouton patte, à droite des mesures
constexpr float ANSWER_Y = 392, ANSWER_R = 46;                         // trois boutons de réponse
constexpr float ANSWER_X[3] = {58, 160, 262};
constexpr float QUIT_X = 290, QUIT_Y = 28, QUIT_R = 17;               // fin de partie
constexpr float CRIES_X = 262, CRIES_Y = 379, CRIES_R = 26;            // cris d'humeur, à droite du niveau sonore
constexpr float TALK_X = 262, TALK_Y = 116, TALK_R = 40;               // conversation, au-dessus de la patte
constexpr float WHO_X[2] = {84, 236}, WHO_Y[2] = {178, 348}, WHO_R = 62;  // « Qui parle ? » : quatre ronds en carré

uint16_t mix565(uint16_t a, uint16_t b, float t) {   // t = 1 → b
  auto ch = [t](int x, int y) { return int(x + (y - x) * t + 0.5f); };
  return uint16_t((ch(a >> 11, b >> 11) << 11) | (ch((a >> 5) & 63, (b >> 5) & 63) << 5) | ch(a & 31, b & 31));
}

Canvas canvas(W, H);

// ── Traits lumineux ───────────────────────────────────────────────────

Paint ink(uint16_t c, float a = 1) { return a >= 0.99f ? Paint::solid(c) : Paint::blend(c, a); }

// Trait et halo proportionné au trait (le halo fixe du visage noierait un texte fin).
void glow(const Pt* p, int n, float w, uint16_t col, float a = 1, bool caps = true) {
  canvas.fillStroke(p, n, w * 2.6f + 3, Paint::blend(col, 0.09f * a), caps);
  canvas.fillStroke(p, n, w * 1.7f + 1, Paint::blend(col, 0.18f * a), caps);
  canvas.fillStroke(p, n, w, ink(col, a), caps);
}

void line(float x0, float y0, float x1, float y1, float w, uint16_t col, float a = 1) {
  Pt p[2] = {{x0, y0}, {x1, y1}};
  glow(p, 2, w, col, a);
}

void ring(float cx, float cy, float r, float w, uint16_t col) {
  canvas.glowEllipse(cx, cy, r, r, w / 2 + 6, w / 2 + 9, col, 0.4f);
  canvas.strokeRing(cx, cy, r, r, w, Paint::solid(col));
}

void dot(float x, float y, float r, uint16_t col, bool halo = true) {
  if (halo) canvas.glowEllipse(x, y, r, r, 0, r * 0.8f + 3, col, 0.45f);
  canvas.fillCircle(x, y, r, Paint::solid(col));
}

// Arc d'ellipse de a0 à a1 (radians, y vers le bas : sin > 0 = dessous)
void arc(float cx, float cy, float rx, float ry, float a0, float a1, float w, uint16_t col, bool halo = true) {
  Pt p[17];
  for (int i = 0; i <= 16; i++) {
    float a = a0 + (a1 - a0) * i / 16;
    p[i] = {cx + cosf(a) * rx, cy + sinf(a) * ry};
  }
  if (halo) glow(p, 17, w, col);
  else canvas.fillStroke(p, 17, w, Paint::solid(col));
}

void rect(float x0, float y0, float x1, float y1, uint16_t col) {  // plein
  Pt p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
  canvas.fillPolygon(p, 4, Paint::solid(col));
}

// Rectangle à coins ronds, en trait fermé
void frame(float x0, float y0, float x1, float y1, float r, float w, uint16_t col) {
  Pt p[21];
  int n = 0;
  const float cx[4] = {x1 - r, x1 - r, x0 + r, x0 + r}, cy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
  for (int k = 0; k < 4; k++)
    for (int i = 0; i <= 4; i++) {
      float a = -PI_F / 2 + (k + i / 4.0f) * PI_F / 2;
      p[n++] = {cx[k] + cosf(a) * r, cy[k] + sinf(a) * r};
    }
  p[n++] = p[0];
  glow(p, n, w, col);
}

// ── Police au trait (StrokeFont.h, partagée avec le cadran de la tête) ──
using StrokeFont::textWidth;

// Texte de hauteur de capitale h, (x, y) = haut gauche ; align : 0 à gauche, 1 centré, 2 à droite.
void text(float x, float y, const char* s, float h, float stroke, uint16_t col, int align = 0) {
  if (align) x -= textWidth(s, h) * (align == 1 ? 0.5f : 1);
  StrokeFont::strokes(x, y, s, h, [&](const Pt* p, int n) { glow(p, n, stroke, col); });
}

enum class Icon : uint8_t { Thermo, Drop, Gauge, Sun, Speaker, Muted };

void rowIcon(Icon kind, float cx, float cy, uint16_t col, float aux) {
  switch (kind) {
    case Icon::Thermo: {
      Pt tube[7] = {{cx - 3.5f, cy + 3}, {cx - 3.5f, cy - 9}, {cx - 2, cy - 11.5f}, {cx, cy - 12},
                    {cx + 2, cy - 11.5f}, {cx + 3.5f, cy - 9}, {cx + 3.5f, cy + 3}};
      glow(tube, 7, 2, col);
      ring(cx, cy + 7.5f, 5.5f, 2, col);
      dot(cx, cy + 7.5f, 2.5f, col, false);
      line(cx, cy + 6, cx, cy - 5, 2, col);
      break;
    }
    case Icon::Drop: {
      Pt p[16];
      int n = 0;
      p[n++] = {cx, cy - 12};
      for (int i = 0; i <= 12; i++) {
        float a = -0.45f + (PI_F + 0.9f) * i / 12;
        p[n++] = {cx + cosf(a) * 7.5f, cy + 3.5f + sinf(a) * 7.5f};
      }
      p[n++] = p[0];
      glow(p, n, 2, col);
      break;
    }
    case Icon::Gauge: {
      arc(cx, cy, 11, 11, 0.75f * PI_F, 2.25f * PI_F, 2, col);
      float f = aux < 0 ? 0 : aux > 1 ? 1 : aux, a = (0.75f + 1.5f * f) * PI_F;
      line(cx, cy, cx + cosf(a) * 8, cy + sinf(a) * 8, 2, col);
      dot(cx, cy, 2.5f, col, false);
      break;
    }
    case Icon::Sun:
      ring(cx, cy, 5, 2, col);
      for (int i = 0; i < 8; i++) {
        float a = i * PI_F / 4;
        line(cx + cosf(a) * 8.5f, cy + sinf(a) * 8.5f, cx + cosf(a) * 12, cy + sinf(a) * 12, 2, col);
      }
      break;
    case Icon::Speaker:
    case Icon::Muted: {
      Pt body[7] = {{cx - 11, cy - 4}, {cx - 6, cy - 4}, {cx, cy - 10}, {cx, cy + 10}, {cx - 6, cy + 4}, {cx - 11, cy + 4}, {cx - 11, cy - 4}};
      glow(body, 7, 2, col);
      if (kind == Icon::Muted) {   // muet : une croix rouge à la place des ondes
        line(cx + 4, cy - 5, cx + 13, cy + 5, 2, RED);
        line(cx + 4, cy + 5, cx + 13, cy - 5, 2, RED);
        break;
      }
      arc(cx + 1, cy, 6, 6, -0.3f * PI_F, 0.3f * PI_F, 2, col);
      arc(cx + 1, cy, 11, 11, -0.3f * PI_F, 0.3f * PI_F, 2, col);
      break;
    }
  }
}

// ── Tableau de bord ───────────────────────────────────────────────────
// Même disposition que le tableau de bord des autres éditions (Dashboard.cpp).

void batteryIcon(float x, float y, uint8_t pct, bool charging) {
  const uint16_t col = pct > 20 ? YELLOW : RED;
  frame(x, y, x + 30, y + 15, 3, 2, col);
  rect(x + 31, y + 5, x + 34, y + 10, col);
  rect(x + 3, y + 3, x + 3 + 24 * (pct > 100 ? 100 : pct) / 100.0f, y + 12, col);
  if (charging) text(x - 13, y + 3, "+", 9, 2, YELLOW);
}

// Wi-Fi : un point et deux ondes au-dessus ; (x, y) = le point
void wifiIcon(float x, float y, uint16_t col) {
  dot(x, y, 2, col, false);
  arc(x, y, 6, 6, -0.75f * PI_F, -0.25f * PI_F, 2, col);
  arc(x, y, 11, 11, -0.75f * PI_F, -0.25f * PI_F, 2, col);
}

// Une mesure : icône, puis valeur et unité centrées sur elle, sans case ni étiquette (l'icône suffit à la repérer).
void row(float y, Icon kind, float aux, const char* value, const char* unit, bool valid) {
  rowIcon(kind, 42, y + 31, valid ? YELLOW : DIM, aux);
  const char* v = valid ? value : "--";
  text(76, y + 20, v, 22, 3.2f, valid ? YELLOW : DIM);
  if (valid) text(76 + textWidth(v, 22) + 8, y + 31, unit, 11, 1.8f, MUTED);
}

// ── Visage néon de la tête, pour les moments tête retirée / tête reposée ──
// Mêmes formes que FaceRenderer::renderNeon (Face.cpp), à l'échelle R du torse.
struct NeonFace {
  float cx, cy, R;
  float eyeX(int side) const { return cx + side * 0.375f * R; }
  float eyeY() const { return cy - 0.035f * R; }
  float rx() const { return 0.28f * R; }
  float ry() const { return 0.24f * R; }
};

void openLens(const NeonFace& f, int side, float look, uint16_t lens) {
  const float x = f.eyeX(side), y = f.eyeY(), rx = f.rx(), ry = f.ry(), pr = 0.40f * rx;
  canvas.glowEllipse(x, y, rx, ry, 0, rx * 0.2f, lens, 0.55f, true);
  canvas.setClipEllipse(x, y, rx, ry);
  canvas.fillEllipse(x, y, rx, ry, Paint::solid(lens));
  canvas.fillEllipse(x, y + ry * 0.62f, rx * 0.9f, ry * 0.5f, Paint::solid(mix565(lens, mix565(lens, AMBER_DEEP, 0.45f), 0.5f)));
  const float px = x + look * (rx - pr) * 0.8f, py = y + ry * 0.05f;
  canvas.fillEllipse(px, py, pr, pr * 1.15f, Paint::solid(DARK));
  canvas.fillCircle(px - pr * 0.34f, py - pr * 0.42f, pr * 0.30f, Paint::solid(GLINT));
  canvas.fillCircle(px + pr * 0.36f, py + pr * 0.42f, pr * 0.12f, Paint::solid(GLINT));
  canvas.clearClip();
}

void closedLens(const NeonFace& f, int side, uint16_t lens) {   // œil rieur du rendu
  const float x = f.eyeX(side), y = f.eyeY(), rx = f.rx(), ry = f.ry(), y0 = y + ry * 0.19f;
  const uint16_t shade = mix565(lens, AMBER_DEEP, 0.45f);
  canvas.glowEllipse(x, y, rx, ry, 0, rx * 0.2f, lens, 0.55f, true);
  canvas.setClipEllipse(x, y, rx, ry);
  canvas.fillEllipse(x, y, rx, ry, Paint::solid(lens));
  canvas.fillEllipse(x, y + ry * 0.49f, rx * 0.75f, ry * 0.45f, Paint::solid(shade));
  canvas.fillEllipse(x, y0, rx * 0.35f, ry * 0.31f, Paint::solid(shade));
  canvas.clearClip();
  Pt a[13];
  for (int i = 0; i <= 12; i++) {
    float t = PI_F * i / 12;
    a[i] = {x - cosf(t) * rx * 0.46f, y0 - sinf(t) * ry * 0.45f};
  }
  canvas.fillStroke(a, 13, rx * 0.25f, Paint::solid(DARK), false);
}

void sleepyLens(const NeonFace& f, int side) {   // œil endormi : verre allumé barré d'un arc ∪ (drawClosedLens)
  const float x = f.eyeX(side), y = f.eyeY(), rx = f.rx(), ry = f.ry(), y0 = y - ry * 0.07f;
  canvas.glowEllipse(x, y, rx, ry, 0, rx * 0.2f, SLEEP_LENS, 0.55f, true);
  canvas.setClipEllipse(x, y, rx, ry);
  canvas.fillEllipse(x, y, rx, ry, Paint::solid(SLEEP_LENS));
  canvas.fillEllipse(x, y + ry * 0.49f, rx * 0.75f, ry * 0.45f, Paint::solid(mix565(SLEEP_LENS, SLEEP_SHADE, 0.6f)));
  canvas.clearClip();
  Pt a[13];
  for (int i = 0; i <= 12; i++) {
    float t = PI_F * i / 12;
    a[i] = {x - cosf(t) * rx * 0.46f, y0 + sinf(t) * ry * 0.45f};
  }
  canvas.fillStroke(a, 13, rx * 0.25f, Paint::solid(DARK), false);
}

// Sourcil en fil : lift > 0 relève le bout intérieur (inquiet), curve bombe l'arc
void neonBrow(const NeonFace& f, int side, float lift, float curve, uint16_t col = ORANGE) {
  const float x = f.eyeX(side), base = f.eyeY() - f.ry() - 0.17f * f.R, half = 0.13f * f.R;
  Pt a = {x - side * half, base - lift * 0.1f * f.R}, b = {x + side * half, base + lift * 0.03f * f.R};
  Pt c = {x, (a.y + b.y) / 2 - curve * 0.3f * f.R};
  Pt p[11];
  for (int i = 0; i <= 10; i++) {
    float t = i / 10.0f, u = 1 - t;
    p[i] = {u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y};
  }
  glow(p, 11, 0.03f * f.R, col);
}

void neonBlush(const NeonFace& f) {
  for (int s = -1; s <= 1; s += 2)
    for (int k = -1; k <= 1; k++) {   // « /// »
      float x = f.cx + s * 0.53f * f.R + k * 0.055f * f.R, y = f.cy + 0.32f * f.R;
      Pt st[2] = {{x - 0.022f * f.R, y + 0.042f * f.R}, {x + 0.022f * f.R, y - 0.042f * f.R}};
      canvas.fillStroke(st, 2, 0.021f * f.R, Paint::solid(BLUSH));
    }
}

void sparkle(float x, float y, float s, uint16_t col) {
  const float w = s * 0.3f;
  Pt star[8] = {{x, y - s}, {x + w, y - w}, {x + s, y}, {x + w, y + w}, {x, y + s}, {x - w, y + w}, {x - s, y}, {x - w, y - w}};
  canvas.fillPolygon(star, 8, Paint::solid(col));
}

// Patte : coussinet et quatre doigts (bouton du jeu)
void paw(float cx, float cy, float s, uint16_t col) {
  canvas.glowEllipse(cx, cy + 0.32f * s, 0.46f * s, 0.38f * s, 0, 4, col, 0.45f, true);
  canvas.fillEllipse(cx, cy + 0.32f * s, 0.46f * s, 0.38f * s, Paint::solid(col));
  const float toes[4][2] = {{-0.62f, -0.2f}, {-0.24f, -0.58f}, {0.24f, -0.58f}, {0.62f, -0.2f}};
  for (auto& t : toes) {
    canvas.glowEllipse(cx + t[0] * s, cy + t[1] * s, 0.19f * s, 0.24f * s, 0, 3, col, 0.45f, true);
    canvas.fillEllipse(cx + t[0] * s, cy + t[1] * s, 0.19f * s, 0.24f * s, Paint::solid(col));
  }
}

// Bulle de parole : ovale ouvert en bas à gauche pour sa queue, trois points dedans (bouton de la conversation)
void bubble(float cx, float cy, float s, uint16_t col) {
  const float rx = s, ry = 0.7f * s, a0 = 2.6f, a1 = 2.05f + 2 * PI_F;
  arc(cx, cy, rx, ry, a0, a1, 3, col);
  Pt tail[3] = {{cx + cosf(a1) * rx, cy + sinf(a1) * ry}, {cx - 0.62f * s, cy + 0.98f * s},
                {cx + cosf(a0) * rx, cy + sinf(a0) * ry}};
  glow(tail, 3, 3, col);
  for (int i = -1; i <= 1; i++) dot(cx + i * 0.4f * s, cy, 0.1f * s, col, false);
}

// Sablier du minuteur : deux barres et deux diagonales. Taille s ≈ demi-largeur.
void hourglass(float cx, float cy, float s, float w, uint16_t col) {
  const float top = cy - 1.25f * s, bottom = cy + 1.25f * s;
  line(cx - s, top, cx + s, top, w, col);
  line(cx - s, bottom, cx + s, bottom, w, col);
  line(cx - 0.8f * s, top, cx + 0.8f * s, bottom, w, col);
  line(cx + 0.8f * s, top, cx - 0.8f * s, bottom, w, col);
}

// Pouce levé (down = baissé) en trait néon : manche, main, pouce et trois doigts repliés. Taille s ≈ demi-hauteur.
void thumb(float cx, float cy, float s, bool down, uint16_t col) {
  const float k = down ? -1 : 1;
  auto P = [&](float x, float y) { return Pt{cx + x * s, cy + k * y * s}; };
  const float w = 0.11f * s;
  Pt cuff[5] = {P(-0.98f, -0.02f), P(-0.66f, -0.02f), P(-0.66f, 0.86f), P(-0.98f, 0.86f), P(-0.98f, -0.02f)};
  glow(cuff, 5, w, col);
  Pt hand[16] = {P(-0.52f, 0.86f), P(-0.52f, 0.02f), P(-0.30f, -0.18f), P(-0.16f, -0.50f), P(-0.12f, -0.82f),
                 P(0.02f, -0.94f), P(0.18f, -0.86f), P(0.24f, -0.60f), P(0.18f, -0.30f), P(0.10f, -0.06f),
                 P(0.60f, -0.06f), P(0.76f, 0.06f), P(0.76f, 0.72f), P(0.62f, 0.86f), P(-0.52f, 0.86f)};
  glow(hand, 15, w, col);
  for (int i = 0; i < 3; i++) {   // doigts repliés
    const float y = 0.22f + i * 0.22f;
    line(cx + 0.10f * s, cy + k * y * s, cx + 0.76f * s, cy + k * y * s, w * 0.8f, col);
  }
}

// Texte coupé aux espaces en lignes de largeur width au plus, chacune centrée sur cx (draw faux : on compte
// seulement). Rend le nombre de lignes ; elles sont espacées de LINE_STEP fois la hauteur.
constexpr float LINE_STEP = 1.75f;
int wrapped(float cx, float y, const char* s, float h, float stroke, float width, uint16_t col, bool draw = true) {
  char line[96];
  int lines = 0;
  for (const char* p = s; *p;) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* end = nullptr;   // fin de la plus longue ligne qui tient
    for (const char* q = p; *q;) {
      // un mot va jusqu'à l'espace suivante ; « ? » et « ! » restent collés au mot d'avant (« NOUS ? »)
      while (*q && !(*q == ' ' && q[1] != '?' && q[1] != '!')) q++;
      const size_t n = size_t(q - p);
      if (n >= sizeof(line)) break;
      memcpy(line, p, n);
      line[n] = 0;
      if (end && textWidth(line, h) > width) break;
      end = q;   // le premier mot entre toujours, même trop long
      while (*q == ' ') q++;
    }
    if (!end) end = p + 1;
    const size_t n = size_t(end - p) < sizeof(line) ? size_t(end - p) : sizeof(line) - 1;
    memcpy(line, p, n);
    line[n] = 0;
    if (draw) text(cx, y + lines * h * LINE_STEP, line, h, stroke, col, 1);
    lines++;
    p = end;
  }
  return lines;
}

void clear(uint16_t* fb) {
  canvas.setTarget(fb);
  canvas.clearClip();
  for (int i = 0; i < W * H; i++) fb[i] = BG;
}
}  // namespace

void draw(uint16_t* fb, const DashData& d, const DashTheme& th) {
  char buf[32];
  clear(fb);
  const uint16_t acc = th.valid ? th.accent : ORANGE;

  // En-tête : le titre, ou le minuteur en cours à sa place, en grand pour se lire de loin
  if (d.timerS) {
    const unsigned long s = d.timerS, h = s / 3600;
    if (h) snprintf(buf, sizeof(buf), "%luH%02lu", h, s / 60 % 60);
    else snprintf(buf, sizeof(buf), "%lu:%02lu", s / 60, s % 60);
    hourglass(26, 28, 9, 2.6f, YELLOW);
    if (h) text(44, 14, buf, 28, 4.2f, YELLOW);   // plus large : il reste avant le voyant CAM
    else text(44, 11, buf, 34, 5, YELLOW);
  } else {
    text(16, 17, "MOODIMOS", 22, 3.6f, ORANGE);
    // Wi-Fi à mi-chemin entre la fin du titre et le voyant CAM (le minuteur prend toute cette place)
    if (d.wifi) wifiIcon((16 + textWidth("MOODIMOS", 22) + 200) / 2, 34, d.wifi == 2 ? YELLOW : DIM);
  }
  dot(200, 28, 6, d.cameraOn ? acc : DIM, d.cameraOn);
  text(212, 24, "CAM", 9, 1.6f, d.cameraOn ? YELLOW : MUTED);
  // V1 : pas de batterie, la jauge de l'AXP2101 n'a pas de sens → on affiche la source, USB
  if (d.powerValid && d.battery) batteryIcon(W - 54, 21, d.batteryPct, d.charging || d.usb);
  else if (d.powerValid && d.usb) text(W - 16, 23, "USB", 11, 2, YELLOW, 2);
  line(14, 56, W - 14, 56, 2, acc);

  // Mesures
  float y = 68;
  snprintf(buf, sizeof(buf), "%.1f", d.tempC);
  row(y, Icon::Thermo, 0, buf, "°C", d.climateValid); y += 70;
  snprintf(buf, sizeof(buf), "%.0f", d.humPct);
  row(y, Icon::Drop, 0, buf, "%", d.humidityValid); y += 70;
  snprintf(buf, sizeof(buf), "%.0f", d.pressHpa);
  row(y, Icon::Gauge, (d.pressHpa - 960.0f) / 80.0f, buf, "HPA", d.climateValid); y += 70;
  snprintf(buf, sizeof(buf), "%u", d.lux);
  row(y, Icon::Sun, 0, buf, "LX", d.lightValid); y += 70;
  snprintf(buf, sizeof(buf), "%u", d.soundDb);
  row(y, Icon::Speaker, 0, buf, "DB", d.soundValid); y += 70;

  // Cris d'humeur : bouton haut-parleur dans la colonne libre, à droite du niveau sonore, barré quand ils sont muets
  ring(CRIES_X, CRIES_Y, CRIES_R, 3, d.criesOn ? ORANGE : DIM);
  rowIcon(d.criesOn ? Icon::Speaker : Icon::Muted, CRIES_X, CRIES_Y, d.criesOn ? YELLOW : MUTED, 0);

  // Jeu « Devine l'animal » : bouton patte dans la colonne libre à droite des mesures
  if (d.gameReady && d.headDocked) {
    ring(PLAY_X, PLAY_Y, PLAY_R, 3, ORANGE);
    paw(PLAY_X, PLAY_Y - 2, 22, YELLOW);
    text(PLAY_X, PLAY_Y + PLAY_R + 12, "JOUER", 11, 2, ORANGE, 1);
  }

  // Conversation : bulle au-dessus de la patte, grisée sans cerveau (Mac éteint), éclairée pendant l'échange
  if (d.brain == 2) {
    canvas.glowEllipse(TALK_X, TALK_Y, TALK_R, TALK_R, 0, 14, ORANGE, 0.6f, true);
    canvas.fillCircle(TALK_X, TALK_Y, TALK_R, Paint::solid(mix565(BG, ORANGE, 0.35f)));
  }
  ring(TALK_X, TALK_Y, TALK_R, 3, d.brain == 2 ? YELLOW : d.brain ? ORANGE : DIM);
  bubble(TALK_X, TALK_Y - 4, 24, d.brain ? YELLOW : MUTED);
  text(TALK_X, TALK_Y + TALK_R + 12, "PARLER", 11, 2, d.brain ? ORANGE : MUTED, 1);

  // État du robot
  y += 4;
  if (d.headDocked) {
    if (!d.headOnline) snprintf(buf, sizeof(buf), "TÊTE : SANS LIAISON");
    else if (d.headBatteryPct >= 0 && d.headCharging) snprintf(buf, sizeof(buf), "TÊTE : EN CHARGE %d %%", d.headBatteryPct);
    else if (d.headBatteryPct >= 0) snprintf(buf, sizeof(buf), "TÊTE : EN PLACE %d %%", d.headBatteryPct);
    else snprintf(buf, sizeof(buf), "TÊTE : CONNECTÉE");
    if (d.accessMin) snprintf(buf, sizeof(buf), "ACCÈS OUVERT : %u MIN", d.accessMin);
    text(16, y, buf, 12, 2.1f, d.accessMin || !d.headOnline ? RED : YELLOW);
    y += 24;
    if (!d.radarOnline && !d.radarPresent) {
      text(16, y, "RADAR : --", 12, 2.1f, MUTED);
    } else if (d.radarPresent) {
      if (d.radarOnline && d.radarCm) snprintf(buf, sizeof(buf), "RADAR : PRÉSENCE À %.1f M", d.radarCm / 100.0f);
      else snprintf(buf, sizeof(buf), "RADAR : PRÉSENCE");
      text(16, y, buf, 12, 2.1f, YELLOW);
    } else {
      text(16, y, "RADAR : ABSENCE", 12, 2.1f, MUTED);
    }
    return;
  }
  // Pendentif : dernier statut Bluetooth et bouton de sonnerie à la place du radar
  if (d.awayStatus && d.awayBatteryPct <= 100) snprintf(buf, sizeof(buf), "TÊTE : PARTIE %u %%", d.awayBatteryPct);
  else snprintf(buf, sizeof(buf), "TÊTE : PARTIE");
  if (d.accessMin) snprintf(buf, sizeof(buf), "ACCÈS OUVERT : %u MIN", d.accessMin);
  text(16, y, buf, 12, 2.1f, d.accessMin ? RED : ORANGE);
  if (d.awayStatus) {
    if (d.awaySeenS < 60) snprintf(buf, sizeof(buf), "VUE IL Y A %lu S", (unsigned long)d.awaySeenS);
    else snprintf(buf, sizeof(buf), "VUE IL Y A %lu MIN", (unsigned long)(d.awaySeenS / 60));
    text(W - 16, y + 3, buf, 8, 1.5f, MUTED, 2);
  }
  static const char* const RING_TEXT[] = {"OÙ EST MA TÊTE ?", "APPEL EN COURS...", "ELLE SONNE !", "INTROUVABLE"};
  const uint16_t col = d.ringState == HeadFinder::Failed ? RED : acc;
  frame(12, 446, W - 12, 476, 8, 2.5f, col);
  text(W / 2, 455, RING_TEXT[d.ringState < 4 ? d.ringState : 0], 12, 2.1f, col, 1);
}

// Même moment que drawWorried des autres éditions : le visage de la tête (ici néon), inquiet, cherche le corps.
void drawWorried(uint16_t* fb, const DashTheme& th, float t) {
  clear(fb);
  const NeonFace f = {160, 236, 186};
  const uint16_t lens = th.valid ? th.accent : YELLOW;
  for (int s = -1; s <= 1; s += 2) {
    openLens(f, s, sinf(t * 7) * 0.6f, lens);
    neonBrow(f, s, 0.6f, 0.15f);
  }
  Pt m[13];   // bouche ondulée
  for (int i = 0; i <= 12; i++) {
    float u = -1 + i / 6.0f;
    m[i] = {f.cx + u * 0.18f * f.R, f.cy + 0.40f * f.R + sinf(u * 6 + t * 10) * 0.025f * f.R};
  }
  glow(m, 13, 7, RED);
}

// Même moment que drawJoy des autres éditions : le visage rieur de la tête, qui sautille, et des étincelles.
void drawJoy(uint16_t* fb, float t) {
  clear(fb);
  for (int i = 0; i < 10; i++) {
    float a = i * 0.628f + t * 0.8f;
    sparkle(160 + cosf(a) * 138, 236 + sinf(a) * 196, 8 + 10 * fabsf(sinf(t * 6 + i * 1.3f)), mix565(BG, GOLD, 0.9f));
  }
  const NeonFace f = {160, 236 - fabsf(sinf(t * 8.7f)) * 20, 186};
  neonBlush(f);
  for (int s = -1; s <= 1; s += 2) {
    closedLens(f, s, GOLD);
    neonBrow(f, s, 0, 0.7f);
  }
  // grande bouche ouverte, lèvre du haut presque droite, langue saumon
  Pt m[19];
  int n = 0;
  const float mx = f.cx, my = f.cy + 0.37f * f.R, hw = 0.275f * f.R, depth = 0.29f * f.R;
  m[n++] = {mx - hw, my - 0.02f * f.R};
  for (int i = 0; i <= 16; i++) {
    float a = PI_F * i / 16;
    m[n++] = {mx - cosf(a) * hw, my + 0.01f * f.R + sinf(a) * depth};
  }
  m[n++] = {mx + hw, my - 0.02f * f.R};
  canvas.fillPolygon(m, n, Paint::solid(RED));
  canvas.setClipPolygon(m, n);
  canvas.fillEllipse(mx, my + depth * 1.1f, hw * 0.85f, depth * 0.62f, Paint::solid(TONGUE));
  canvas.clearClip();
}

// La tête dort : le torse dort avec elle, même visage néon (yeux clos, petits « z » qui montent, respiration
// lente), dans le mauve du sommeil doux de la tête.
void drawSleep(uint16_t* fb, float t) {
  clear(fb);
  const NeonFace f = {160, 236 + sinf(t * 1.2f) * 3, 186};
  neonBlush(f);
  for (int s = -1; s <= 1; s += 2) {
    sleepyLens(f, s);
    neonBrow(f, s, 0, 0.1f, SLEEP_INK);
  }
  Pt m[13];   // petite bouche fermée, à peine souriante
  for (int i = 0; i <= 12; i++) {
    float u = -1 + i / 6.0f;
    m[i] = {f.cx + u * 0.11f * f.R, f.cy + 0.35f * f.R + 0.044f * f.R * (1 - u * u)};
  }
  glow(m, 13, 6, SLEEP_MOUTH);
  const float q = f.R / 240;   // « z » de la tête (drawNeonZzz), à l'échelle du torse
  for (int i = 0; i < 3; i++) {
    float k = fmodf(t * 0.35f + i / 3.0f, 1.0f), s = (28 + 24 * k) * q, a = sinf(k * PI_F) * 0.95f;
    float x = f.cx + 0.30f * f.R + k * 24 * q, y = f.cy - 0.56f * f.R - k * 60 * q;
    Pt z[4] = {{x, y - s * 0.7f}, {x + s * 0.55f, y - s * 0.7f}, {x, y}, {x + s * 0.55f, y}};
    canvas.fillStroke(z, 4, 6 * q, ink(SLEEP_LENS, a));
  }
}

bool ringButtonHit(int16_t x, int16_t y) { return x >= 12 && x < W - 12 && y >= 440 && y < H; }

namespace {
bool near(int16_t x, int16_t y, float cx, float cy, float r) {
  const float dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}
}  // namespace

// Écran du jeu : titre et fin de partie, numéro de question, la phrase du moment en grand, et trois boutons
// à toucher (pouce vert : oui, pouce rouge : non, point d'interrogation orange : je ne sais pas).
namespace {
void header(const char* title) {   // titre, croix de fermeture en haut à droite et trait
  text(16, 19, title, 18, 3, ORANGE);
  ring(QUIT_X, QUIT_Y, QUIT_R, 2, MUTED);
  line(QUIT_X - 6, QUIT_Y - 6, QUIT_X + 6, QUIT_Y + 6, 2.2f, MUTED);
  line(QUIT_X + 6, QUIT_Y - 6, QUIT_X - 6, QUIT_Y + 6, 2.2f, MUTED);
  line(14, 56, W - 14, 56, 2, ORANGE);
}
}  // namespace

void drawGame(uint16_t* fb, const GameView& v) {
  clear(fb);
  header("DEVINE L'ANIMAL");
  if (v.round) {
    char buf[24];
    snprintf(buf, sizeof(buf), "QUESTION %u", v.round);
    text(W / 2, 76, buf, 11, 2, MUTED, 1);
  }
  // la phrase, centrée dans la zone entre l'en-tête et les boutons
  const char* s = v.text ? v.text : "";
  const float h = 21, stroke = 3.2f, width = W - 36;
  const int lines = wrapped(0, 0, s, h, stroke, width, YELLOW, false);
  const float top = 100, bottom = ANSWER_Y - ANSWER_R - 24, block = lines * h * LINE_STEP - h * (LINE_STEP - 1);
  wrapped(W / 2, top + (bottom - top - block) / 2, s, h, stroke, width, YELLOW);

  static const uint16_t COLOR[3] = {GREEN, RED, ORANGE};
  for (int b = 0; b < 3; b++) {
    const float x = ANSWER_X[b];
    const uint16_t col = v.buttons ? COLOR[b] : DIM;
    if (v.pressed == b) {
      canvas.glowEllipse(x, ANSWER_Y, ANSWER_R, ANSWER_R, 0, 14, COLOR[b], 0.6f, true);
      canvas.fillCircle(x, ANSWER_Y, ANSWER_R, Paint::solid(mix565(BG, COLOR[b], 0.35f)));
    }
    ring(x, ANSWER_Y, ANSWER_R, 3.5f, col);
    if (b < 2) thumb(x + 3, ANSWER_Y + (b ? -4 : 4), 25, b == 1, col);
    else text(x, ANSWER_Y - 19, "?", 38, 5, col, 1);
  }
}

bool playButtonHit(int16_t x, int16_t y) { return near(x, y, PLAY_X, PLAY_Y + 8, PLAY_R + 20); }

bool criesButtonHit(int16_t x, int16_t y) { return near(x, y, CRIES_X, CRIES_Y, CRIES_R + 14); }

bool talkButtonHit(int16_t x, int16_t y) { return near(x, y, TALK_X, TALK_Y + 8, TALK_R + 20); }

int gameButtonHit(int16_t x, int16_t y) {
  for (int b = 0; b < 3; b++)
    if (near(x, y, ANSWER_X[b], ANSWER_Y, ANSWER_R + 4)) return b;
  if (near(x, y, QUIT_X, QUIT_Y, QUIT_R + 14)) return 3;
  return -1;
}

// « Qui parle ? » : un rond teinté par personne de la maison (Household.h), sa première lettre en grand dedans (la
// petite reconnaît sa couleur et son A) et son prénom dessous.
void drawWho(uint16_t* fb) {
  clear(fb);
  header("QUI PARLE ?");
  for (int i = 0; i < Household::COUNT; i++) {
    const Household::Person& p = Household::PEOPLE[i];
    const float x = WHO_X[i % 2], y = WHO_Y[i / 2];
    const uint16_t col = rgb565(p.rgb >> 16, (p.rgb >> 8) & 0xFF, p.rgb & 0xFF);
    canvas.fillCircle(x, y, WHO_R, Paint::solid(mix565(BG, col, 0.18f)));
    ring(x, y, WHO_R, 4, col);
    const char initial[2] = {p.label[0], 0};
    text(x, y - 27, initial, 54, 6.5f, col, 1);
    text(x, y + WHO_R + 13, p.label, 15, 2.4f, col, 1);
  }
}

// Minuteur fini : un grand anneau qui bat autour du sablier, jusqu'à ce qu'on touche l'écran.
// Accès demandé par le port série (moodimos-torse.ino) : la commande, et l'appui long qui l'autorise.
void drawAccess(uint16_t* fb, const char* cmd, float held) {
  clear(fb);
  char up[12];
  size_t i = 0;
  for (; cmd[i] && i < sizeof(up) - 1; i++) up[i] = char(toupper((unsigned char)cmd[i]));
  up[i] = 0;
  text(W / 2, 70, "AUTORISER ?", 32, 5, YELLOW, 1);
  text(W / 2, 145, up, 28, 4, ORANGE, 1);
  ring(W / 2, 275, 60, 5, ORANGE);
  if (held > 0) canvas.fillCircle(W / 2, 275, 54 * held, Paint::solid(YELLOW));
  text(W / 2, 362, "DOIGT POSÉ", 24, 3.6f, YELLOW, 1);
  text(W / 2, 406, "2 SECONDES", 24, 3.6f, YELLOW, 1);
  text(W / 2, 455, "RÉSERVÉ À UN ADULTE", 12, 2.1f, RED, 1);
}

void drawTimerDone(uint16_t* fb, float t) {
  clear(fb);
  const float beat = 0.5f + 0.5f * sinf(t * 6);
  canvas.glowEllipse(W / 2, 170, 96, 96, 0, 22, ORANGE, 0.25f + 0.55f * beat, true);
  ring(W / 2, 170, 96, 5, YELLOW);
  hourglass(W / 2, 170, 40, 6, YELLOW);
  text(W / 2, 300, "FINI !", 62, 9, YELLOW, 1);
  text(W / 2, 420, "TOUCHE L'ÉCRAN", 18, 3, ORANGE, 1);
}

int whoButtonHit(int16_t x, int16_t y) {
  for (int i = 0; i < Household::COUNT; i++)
    if (near(x, y, WHO_X[i % 2], WHO_Y[i / 2], WHO_R + 10)) return i;
  if (near(x, y, QUIT_X, QUIT_Y, QUIT_R + 14)) return Household::COUNT;
  return -1;
}

}  // namespace DashboardNeon

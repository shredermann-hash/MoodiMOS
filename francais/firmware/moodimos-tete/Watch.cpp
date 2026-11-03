#include "Watch.h"
#include "StrokeFont.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr float PI_F = 3.14159265f;
constexpr uint16_t INK = rgb565(0x1B, 0x1B, 0x1B);
constexpr uint16_t BLUED = rgb565(0x1D, 0x3A, 0x9E);
constexpr uint16_t SUBDIAL = rgb565(0xEA, 0xE0, 0xC8);
constexpr uint16_t SHADOW = rgb565(0xC6, 0xB9, 0x9C);   // émail assombri
constexpr uint16_t PIN = rgb565(0xC9, 0xCE, 0xD8);
// Néon : les couleurs du visage de l'édition complète et du tableau de bord du torse
constexpr uint16_t NEON_YELLOW = rgb565(0xFF, 0xC4, 0x22);   // verre des yeux, chiffres
constexpr uint16_t NEON_AMBER = rgb565(0xFF, 0x96, 0x1E);    // sourcils, secondes passées
constexpr uint16_t NEON_SHADE = rgb565(0xFF, 0x8C, 0x1A);    // ombre basse du verre
constexpr uint16_t NEON_DARK = rgb565(0x2A, 0x10, 0x00);     // pupilles
constexpr uint16_t NEON_GLINT = rgb565(0xFF, 0xF6, 0xD8);
constexpr uint16_t NEON_BLUSH = rgb565(0xFF, 0x5A, 0x4C);
constexpr uint16_t NEON_MUTED = rgb565(0xB8, 0x74, 0x2C);    // date, comme les étiquettes du torse
constexpr uint16_t NEON_DIM = rgb565(0x4A, 0x2E, 0x10);      // secondes à venir

uint16_t mix565(uint16_t a, uint16_t b, float t) {   // t = 1 → b
  auto ch = [t](int x, int y) { return int(x + (y - x) * t + 0.5f); };
  return uint16_t((ch(a >> 11, b >> 11) << 11) | (ch((a >> 5) & 63, (b >> 5) & 63) << 5) | ch(a & 31, b & 31));
}
const uint16_t GLASS_LOW = mix565(NEON_YELLOW, NEON_SHADE, 0.55f);   // bas des chiffres en verre

// Point à la distance d du centre, dans la direction de l'angle a (0 = midi, sens horaire)
inline Pt polar(float cx, float cy, float a, float d) { return {cx + sinf(a) * d, cy - cosf(a) * d}; }

// Chiffres romains tracés au trait, avec empattements, centrés sur (cx, cy)
void romanGlyph(Canvas& c, char g, float x, float top, float h, const Paint& p) {
  const float w = 3.2f, serif = 2.4f, half = h * 0.31f;
  const float bottom = top + h;
  switch (g) {
    case 'I': {
      Pt s[2] = {{x, top}, {x, bottom}};
      c.strokePolyline(s, 2, w, p);
      Pt a[2] = {{x - h * 0.14f, top}, {x + h * 0.14f, top}};
      Pt b[2] = {{x - h * 0.14f, bottom}, {x + h * 0.14f, bottom}};
      c.strokePolyline(a, 2, serif, p);
      c.strokePolyline(b, 2, serif, p);
      break;
    }
    case 'V': {
      Pt s[3] = {{x - half, top}, {x, bottom}, {x + half, top}};
      c.strokePolyline(s, 3, w, p);
      Pt a[2] = {{x - half - h * 0.12f, top}, {x - half + h * 0.12f, top}};
      Pt b[2] = {{x + half - h * 0.12f, top}, {x + half + h * 0.12f, top}};
      c.strokePolyline(a, 2, serif, p);
      c.strokePolyline(b, 2, serif, p);
      break;
    }
    case 'X': {
      Pt s1[2] = {{x - half, top}, {x + half, bottom}};
      Pt s2[2] = {{x + half, top}, {x - half, bottom}};
      c.strokePolyline(s1, 2, w, p);
      c.strokePolyline(s2, 2, w, p);
      const float rows[2] = {top, bottom};
      for (float yy : rows) {
        Pt l[2] = {{x - half - h * 0.12f, yy}, {x - half + h * 0.12f, yy}};
        Pt r[2] = {{x + half - h * 0.12f, yy}, {x + half + h * 0.12f, yy}};
        c.strokePolyline(l, 2, serif, p);
        c.strokePolyline(r, 2, serif, p);
      }
      break;
    }
  }
}

float glyphAdvance(char g, float h) { return g == 'I' ? h * 0.36f : h * 0.78f; }

void roman(Canvas& c, const char* s, float cx, float cy, float h, const Paint& p) {
  float total = 0;
  for (const char* q = s; *q; q++) total += glyphAdvance(*q, h);
  float x = cx - total / 2;
  for (const char* q = s; *q; q++) {
    float adv = glyphAdvance(*q, h);
    romanGlyph(c, *q, x + adv / 2, cy - h / 2, h, p);
    x += adv;
  }
}

// Aiguille Breguet : tige, « pomme » évidée aux 72 % et pointe effilée
void breguet(Canvas& c, float cx, float cy, float a, float len, float ringR, float width, const Paint& p) {
  const float ringD = len * 0.72f;
  Pt stem[2] = {polar(cx, cy, a + PI_F, len * 0.12f), polar(cx, cy, a, ringD - ringR)};
  c.strokePolyline(stem, 2, width, p);
  Pt ring = polar(cx, cy, a, ringD);
  c.strokeEllipse(ring.x, ring.y, ringR, ringR, width * 0.8f, p);
  Pt base = polar(cx, cy, a, ringD + ringR);
  float nx = cosf(a) * width * 0.7f, ny = sinf(a) * width * 0.7f;
  Pt tip[3] = {{base.x - nx, base.y - ny}, polar(cx, cy, a, len), {base.x + nx, base.y + ny}};
  c.fillPolygon(tip, 3, p);
}

// ── Néon ──
// Trait lumineux du visage : deux halos assombris (le dessous est noir), puis le trait
void neonStroke(Canvas& c, const Pt* p, int n, float w, float glow, uint16_t col) {
  const uint16_t outer = mix565(0, col, 0.10f), mid = mix565(outer, col, 0.20f);
  c.fillStroke(p, n, w + glow, Paint::solid(outer));
  c.fillStroke(p, n, w + glow / 2, Paint::solid(mid));
  c.fillStroke(p, n, w, Paint::solid(col));
}

void neonDot(Canvas& c, float x, float y, float r, uint16_t col) {
  c.glowEllipse(x, y, r, r, 0, r * 0.6f + 4, col, 0.5f, true);
  c.fillCircle(x, y, r, Paint::solid(col));
}

// Texte au trait avec un halo proportionné au trait, comme le tableau de bord du torse
void label(Canvas& c, float cx, float y, const char* s, float h, float w, uint16_t col) {
  StrokeFont::strokes(cx - StrokeFont::textWidth(s, h) / 2, y, s, h, [&](const Pt* p, int n) {
    c.fillStroke(p, n, w * 2.6f + 3, Paint::solid(mix565(0, col, 0.09f)));
    c.fillStroke(p, n, w * 1.7f + 1, Paint::solid(mix565(0, col, 0.20f)));
    c.fillStroke(p, n, w, Paint::solid(col));
  });
}

// Chiffres en verre : halo, verre jaune, puis l'ombre orangée du verre sur le bas, comme les yeux
void glassText(Canvas& c, float cx, float y, const char* s, float h, float w) {
  const float x = cx - StrokeFont::textWidth(s, h) / 2;
  StrokeFont::strokes(x, y, s, h, [&](const Pt* p, int n) {
    c.fillStroke(p, n, w + 0.075f * h * 2.2f, Paint::solid(mix565(0, NEON_YELLOW, 0.07f)));
    c.fillStroke(p, n, w + 0.075f * h, Paint::solid(mix565(0, NEON_YELLOW, 0.16f)));
  });
  StrokeFont::strokes(x, y, s, h, [&](const Pt* p, int n) { c.fillStroke(p, n, w, Paint::solid(NEON_YELLOW)); });
  const Pt low[4] = {{0, y + h * 0.6f}, {float(c.width()), y + h * 0.6f}, {float(c.width()), float(c.height())},
                     {0, float(c.height())}};
  c.setClipPolygon(low, 4);
  StrokeFont::strokes(x, y, s, h, [&](const Pt* p, int n) { c.fillStroke(p, n, w, Paint::solid(GLASS_LOW)); });
  c.clearClip();
}

// Œil de verre du visage néon qui regarde l'heure, en bas ; open : 1 ouvert … 0 fermé (clignement)
void glassEye(Canvas& c, float cx, float cy, float rx, float ry, float open) {
  if (open < 0.12f) {   // fin du clignement : un fil lumineux à peine arrondi
    Pt line[9];
    for (int i = 0; i < 9; i++) {
      float t = -1 + i / 4.0f;
      line[i] = {cx + t * rx * 0.85f, cy + ry * 0.15f + (1 - t * t) * ry * 0.1f};
    }
    neonStroke(c, line, 9, rx * 0.12f, rx * 0.4f, NEON_YELLOW);
    return;
  }
  const float glow = rx * 0.2f, lidY = cy - ry + (1 - open) * ry * 1.6f, botY = cy + ry - (1 - open) * ry * 0.4f;
  const float e = rx + glow + 4;
  const Pt band[4] = {{cx - e, open > 0.97f ? cy - e : lidY}, {cx + e, open > 0.97f ? cy - e : lidY},
                      {cx + e, open > 0.97f ? cy + e : botY}, {cx - e, open > 0.97f ? cy + e : botY}};
  c.setClipPolygon(band, 4);
  c.glowEllipse(cx, cy, rx, ry, 0, glow, NEON_YELLOW, 0.55f, true);
  c.setClipEllipse(cx, cy, rx, ry);
  c.fillEllipse(cx, cy, rx, ry, Paint::solid(NEON_YELLOW));
  c.fillEllipse(cx, cy + ry * 0.62f, rx * 0.9f, ry * 0.5f, Paint::solid(mix565(NEON_YELLOW, NEON_SHADE, 0.5f)));
  const float pr = 0.40f * rx, tall = 1.15f;
  const float px = cx, py = cy + 0.75f * (ry - pr * tall) * 0.7f + ry * 0.05f;
  c.fillEllipse(px, py, pr, pr * tall, Paint::solid(NEON_DARK));
  c.fillCircle(px - pr * 0.34f, py - pr * 0.42f, pr * 0.30f, Paint::solid(NEON_GLINT));
  c.fillCircle(px + pr * 0.36f, py + pr * 0.42f, pr * 0.12f, Paint::solid(mix565(0, NEON_GLINT, 0.8f)));
  c.clearClip();
  if (open > 0.97f) return;
  const Pt lid[4] = {{cx - rx - 2, cy - ry - 2}, {cx + rx + 2, cy - ry - 2}, {cx + rx + 2, lidY}, {cx - rx - 2, lidY}};
  c.fillPolygon(lid, 4, Paint::background());
  const Pt bot[4] = {{cx - rx - 2, botY}, {cx + rx + 2, botY}, {cx + rx + 2, cy + ry + 2}, {cx - rx - 2, cy + ry + 2}};
  c.fillPolygon(bot, 4, Paint::background());
}

// Disposition du cadran néon, en fractions du rayon
constexpr float EYE_DX = 0.27f, EYE_Y = -0.40f, EYE_RX = 0.15f, EYE_RY = 0.13f;
constexpr float DIGITS_Y = -0.08f, DIGITS_H = 0.45f, DIGITS_W = 0.08f, SECONDS_R = 0.93f;
constexpr float DATE_Y = 0.51f, MONTH_Y = 0.67f, DATE_H = 0.11f;   // « JEUDI 29 », puis le mois dessous

// Point des secondes i (0 … 59) ; les quarts sont plus gros
void secondDot(Canvas& c, float R, int i, bool lit) {
  const Pt p = polar(R, R, i / 60.0f * 2 * PI_F, SECONDS_R * R);
  const float k = R / 240;
  if (i % 15 == 0) neonDot(c, p.x, p.y, 4.5f * k, lit ? NEON_YELLOW : NEON_DIM);
  else c.fillCircle(p.x, p.y, 2.6f * k, Paint::solid(lit ? NEON_AMBER : NEON_DIM));
}
}  // namespace

bool Watch::begin(int size, bool neon) {
  size_ = size;
  neon_ = neon;
  dial_ = (uint16_t*)heap_caps_malloc(size_t(size) * size * 2, MALLOC_CAP_SPIRAM);
  scratch_ = (uint16_t*)heap_caps_malloc(size_t(size) * size * 2, MALLOC_CAP_SPIRAM);
  if (!dial_ || !scratch_) return false;
  Canvas c(size, size);
  c.setTarget(dial_);
  if (neon_) renderNeonDial(c);
  else renderDial(c);
  return true;
}

void Watch::renderDial(Canvas& c) {
  const float R = size_ / 2.0f;
  uint16_t* fb = dial_;

  // Émail ivoire en dégradé radial décentré
  const float fx = 0.75f * R, fy = 0.65f * R, span = 1.15f * R;
  for (int y = 0; y < size_; y++) {
    for (int x = 0; x < size_; x++) {
      float t = sqrtf((x - fx) * (x - fx) + (y - fy) * (y - fy)) / span;
      if (t > 1) t = 1;
      float r, g, b;
      if (t < 0.7f) {
        float k = t / 0.7f;
        r = 0xFD + (0xF1 - 0xFD) * k; g = 0xFA + (0xE9 - 0xFA) * k; b = 0xF2 + (0xD6 - 0xF2) * k;
      } else {
        float k = (t - 0.7f) / 0.3f;
        r = 0xF1 + (0xD6 - 0xF1) * k; g = 0xE9 + (0xC9 - 0xE9) * k; b = 0xD6 + (0xAA - 0xD6) * k;
      }
      fb[y * size_ + x] = rgb565(uint8_t(r), uint8_t(g), uint8_t(b));
    }
  }

  const Paint ink = Paint::solid(INK);
  // Chemin de fer des minutes
  c.strokeEllipse(R, R, 0.93f * R, 0.93f * R, 1.6f, ink);
  c.strokeEllipse(R, R, 0.86f * R, 0.86f * R, 1.6f, ink);
  for (int i = 0; i < 60; i++) {
    float a = i / 60.0f * 2 * PI_F;
    bool big = i % 5 == 0;
    Pt t[2] = {polar(R, R, a, (big ? 0.82f : 0.86f) * R), polar(R, R, a, 0.93f * R)};
    c.strokePolyline(t, 2, big ? 4 : 1.6f, ink);
  }

  // Chiffres romains droits ; le VI laisse la place à la petite seconde
  static const char* const ROMAN[12] = {"XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
  for (int i = 0; i < 12; i++) {
    if (i == 6) continue;
    Pt at = polar(R, R, i / 12.0f * 2 * PI_F, 0.68f * R);
    roman(c, ROMAN[i], at.x, at.y, 34, ink);
  }

  // Petite seconde à 6 h
  const float sx = R, sy = R + 0.44f * R, sr = 0.17f * R;
  c.fillCircle(sx, sy, sr, Paint::solid(SUBDIAL));
  c.strokeEllipse(sx, sy, sr, sr, 1.4f, ink);
  for (int i = 0; i < 60; i += 5) {
    float a = i / 60.0f * 2 * PI_F;
    Pt t[2] = {polar(sx, sy, a, sr * (i % 15 == 0 ? 0.72f : 0.82f)), polar(sx, sy, a, sr)};
    c.strokePolyline(t, 2, 1.4f, ink);
  }

  // Reflet du verre
  c.softEllipse(0.55f * R, 0.45f * R, 0.8f * R, 0.8f * R, rgb565(0xFF, 0xFF, 0xFF), 0.3f);
}

void Watch::render(Canvas& c, float h, float m, float s, int weekday, int day, int month) {
  if (neon_) renderNeon(c, h, m, s, weekday, day, month);
  else renderHands(c, h, m, s);
}

void Watch::renderNeonDial(Canvas& c) {
  const float R = size_ / 2.0f, k = R / 240;
  for (int i = 0; i < size_ * size_; i++) dial_[i] = 0x0000;
  for (int i = 0; i < 60; i++) secondDot(c, R, i, false);
  for (int side = -1; side <= 1; side += 2) {
    const float cx = R + side * EYE_DX * R, cy = R + EYE_Y * R;
    Pt brow[9];   // sourcils en fil, en arc content
    for (int i = 0; i < 9; i++) {
      float t = i / 8.0f;
      brow[i] = {cx + side * (t - 0.5f) * 0.22f * R, cy - EYE_RY * R - 0.08f * R - sinf(PI_F * t) * 0.07f * R};
    }
    neonStroke(c, brow, 9, 9 * k, 16 * k, NEON_AMBER);
    for (int i = 0; i < 3; i++) {   // joues « /// »
      float x = R + side * 0.47f * R + (i - 1) * 13 * k, y = cy + 0.17f * R;
      Pt l[2] = {{x - 4 * k, y + 9 * k}, {x + 4 * k, y - 9 * k}};
      neonStroke(c, l, 2, 4 * k, 16 * k, NEON_BLUSH);
    }
  }
}

void Watch::renderNeon(Canvas& c, float h, float m, float s, int weekday, int day, int month) {
  const float R = size_ / 2.0f, k = R / 240;
  for (int i = 0; i <= int(s) && i < 60; i++) secondDot(c, R, i, true);
  // Il cligne des yeux toutes les 3,7 s (0,16 s fermé puis rouvert)
  const float b = fmodf(s, 3.7f), open = b < 0.16f ? fabsf(b - 0.08f) / 0.08f : 1;
  for (int side = -1; side <= 1; side += 2)
    glassEye(c, R + side * EYE_DX * R, R + EYE_Y * R, EYE_RX * R, EYE_RY * R, open);

  char hh[4], mm[4];
  snprintf(hh, sizeof hh, "%d", int(h) % 24);
  snprintf(mm, sizeof mm, "%02d", int(m) % 60);
  const float th = DIGITS_H * R, ty = R + DIGITS_Y * R, u = th / 9, gap = 4.2f * u;
  const float wh = StrokeFont::textWidth(hh, th), wm = StrokeFont::textWidth(mm, th);
  const float x0 = R - (wh + gap + wm) / 2;
  glassText(c, x0 + wh / 2, ty, hh, th, DIGITS_W * R);
  glassText(c, x0 + wh + gap + wm / 2, ty, mm, th, DIGITS_W * R);
  if (fmodf(s, 1) < 0.5f) {   // les deux points battent la seconde
    neonDot(c, x0 + wh + gap / 2, ty + th * 0.30f, 9 * k, NEON_YELLOW);
    neonDot(c, x0 + wh + gap / 2, ty + th * 0.72f, 9 * k, GLASS_LOW);
  }
  if (weekday >= 0 && weekday < 7 && month >= 1 && month <= 12) {
    static const char* const DAYS[7] = {"DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI"};
    static const char* const MONTHS[12] = {"JANVIER", "FÉVRIER", "MARS", "AVRIL", "MAI", "JUIN", "JUILLET",
                                           "AOÛT", "SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DÉCEMBRE"};
    char date[16];
    snprintf(date, sizeof date, "%s %d", DAYS[weekday], day);
    label(c, R, R + DATE_Y * R, date, DATE_H * R, 3.6f * k, NEON_MUTED);
    label(c, R, R + MONTH_Y * R, MONTHS[month - 1], DATE_H * R, 3.6f * k, NEON_MUTED);
  }
}

void Watch::renderHands(Canvas& c, float h, float m, float s) {
  const float R = size_ / 2.0f;
  const float ha = h / 12.0f * 2 * PI_F, ma = m / 60.0f * 2 * PI_F;

  // Petite seconde : 5 battements par seconde, comme un mouvement mécanique
  const float sx = R, sy = R + 0.44f * R, sr = 0.17f * R;
  float beat = floorf(s * 5) / 5;
  float sa = beat / 60.0f * 2 * PI_F;
  Pt sec[2] = {polar(sx, sy, sa + PI_F, sr * 0.25f), polar(sx, sy, sa, sr * 0.9f)};
  c.strokePolyline(sec, 2, 2, Paint::solid(INK));
  c.fillCircle(sx, sy, 4, Paint::solid(INK));

  // Ombres portées (teinte unie : un fondu se superposerait aux jonctions), puis aiguilles bleuies
  const Paint shadow = Paint::solid(SHADOW);
  breguet(c, R + 3, R + 4, ha, 0.5f * R, 0.065f * R, 8, shadow);
  breguet(c, R + 3, R + 4, ma, 0.8f * R, 0.045f * R, 6, shadow);
  const Paint blued = Paint::solid(BLUED);
  breguet(c, R, R, ha, 0.5f * R, 0.065f * R, 8, blued);
  breguet(c, R, R, ma, 0.8f * R, 0.045f * R, 6, blued);
  c.fillCircle(R, R, 0.04f * R, blued);
  c.fillCircle(R, R, 0.012f * R, Paint::solid(PIN));
}

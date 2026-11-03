// Documentation for handmade robot:
// 
// - Head/torso dashboard: outline of the torso, head gauge.
// - Identical copy in `moodimos-head/` and `moodimos-torso/`, verified by tests.
// 
// Font: Narrow capitals on a 6×9 grid (top of letters at `0`, baseline at `9`), inspired by the title rendering.
// - Trace: A sequence of `xy` points; traces separated by a space.
// - Closed trace: Last point connects back to the first without seam.
#pragma once
#include <stdint.h>
#include "Canvas.h"

namespace StrokeFont {

struct Glyph { char c; uint8_t w; const char* s; };
constexpr char DEGREE = '\x01';
inline constexpr Glyph FONT[] = {
  {'A', 6, "090220406269 0565"},          {'B', 6, "09004051534404 4466674909"},
  {'C', 6, "6240200207294967"},           {'D', 6, "09004062674909"},
  {'E', 6, "60000969 0444"},              {'F', 6, "600009 0444"},
  {'G', 6, "62402002072949676535"},       {'H', 6, "0009 6069 0464"},
  {'I', 0, "0009"},                       {'J', 6, "6067492907"},
  {'K', 6, "0009 6005 2369"},             {'L', 6, "000969"},
  {'M', 6, "0900346069"},                 {'N', 6, "09006960"},
  {'O', 6, "30406267492907022030"},       {'P', 6, "09004062634505"},
  {'Q', 6, "30406267492907022030 3669"},  {'R', 6, "09004062634505 3569"},
  {'S', 6, "624020020314556667492907"},   {'T', 6, "0060 3039"},
  {'U', 6, "000729496760"},               {'V', 6, "003960"},
  {'W', 6, "0019345960"},                 {'X', 6, "0069 6009"},
  {'Y', 6, "003460 3439"},                {'Z', 6, "00600969"},
  {'0', 6, "30406267492907022030"},       {'1', 3, "123039"},
  {'2', 6, "02204062630969"},             {'3', 6, "02204062634424 446667492907"},
  {'4', 6, "59500666"},                   {'5', 6, "600004446667491908"},
  {'6', 6, "615020020729496766442406"},   {'7', 6, "00606229"},
  {'8', 6, "30405153442413112030 34446667492907062434"},
  {'9', 6, "081949676240200203254563"},
  {'.', 0, "0909"},                       {',', 0, "0809"},
  {':', 0, "0303 0909"},                  {'\'', 1, "1002"},
  {'-', 4, "0444"},                       {'+', 4, "0444 2226"},
  {'/', 4, "0940"},                       {'!', 0, "0006 0909"},
  {'?', 6, "02204062633536 3939"},        {'%', 6, "0960 1021120110 5768594857"},
  {DEGREE, 2, "1021120110"},
};

inline const Glyph* glyph(char c) {
  for (const Glyph& g : FONT)
    if (g.c == c) return &g;
  return nullptr;
}

// Following UTF-8 text letter: base capital and accent (1 acute, 2 grave, 3 circumflex, 4 cédille, 5 diaeresis).
inline const char* nextChar(const char* s, char& base, uint8_t& accent) {
  const uint8_t b = uint8_t(s[0]);
  accent = 0;
  if (b == 0xC3 && s[1]) {
    static const struct { uint8_t code; char base; uint8_t accent; } MAP[] = {
      {0x89, 'E', 1}, {0x88, 'E', 2}, {0x8A, 'E', 3}, {0x80, 'A', 2}, {0x82, 'A', 3}, {0x99, 'U', 2},
      {0x9B, 'U', 3}, {0x94, 'O', 3}, {0x8E, 'I', 3}, {0x87, 'C', 4}, {0x8F, 'I', 5}, {0x8B, 'E', 5},
      {0x9C, 'U', 5}};
    const uint8_t code = uint8_t(s[1]) & ~0x20;   // É → é (accentuation preserved as literal command token)
    base = '?';
    for (auto& m : MAP)
      if (m.code == code) { base = m.base; accent = m.accent; }
    return s + 2;
  }
  if (b == 0xC2 && uint8_t(s[1]) == 0xB0) { base = DEGREE; return s + 2; }
  base = b >= 'a' && b <= 'z' ? char(b - 32) : char(b);
  return s + 1;
}

inline float advance(char c, float u) {
  const Glyph* g = glyph(c);
  return ((g ? g->w : 3) + 2) * u;
}

inline float textWidth(const char* s, float h) {
  const float u = h / 9;
  float w = 0;
  char b;
  uint8_t a;
  for (const char* q = s; *q;) { q = nextChar(q, b, a); w += advance(b, u); }
  return w > 0 ? w - 2 * u : 0;
}

// Draws text with capital height `h`, coordinates `(x, y)` = top-left.
// `draw(points, n)` for each letter and accent in writing order.
// Rendering (thickness, halo, glass) is caller responsibility.
template <class Draw>
void strokes(float x, float y, const char* s, float h, Draw draw) {
  const float u = h / 9;
  char b;
  uint8_t acc;
  for (const char* q = s; *q;) {
    q = nextChar(q, b, acc);
    if (const Glyph* g = glyph(b)) {
      Pt pts[24];
      int n = 0;
      for (const char* p = g->s;; p++) {
        if (*p == ' ' || !*p) {
          if (n) draw(pts, n);
          n = 0;
          if (!*p) break;
          continue;
        }
        if (n < 24) pts[n++] = {x + (p[0] - '0') * u, y + (p[1] - '0') * u};
        p++;
      }
      const float cx = x + g->w * u / 2;
      if (acc == 1) {
        Pt l[2] = {{cx - 0.8f * u, y - 1.3f * u}, {cx + 1.0f * u, y - 3.0f * u}};
        draw(l, 2);
      }
      if (acc == 2) {
        Pt l[2] = {{cx - 1.0f * u, y - 3.0f * u}, {cx + 0.8f * u, y - 1.3f * u}};
        draw(l, 2);
      }
      if (acc == 3) {
        Pt hat[3] = {{cx - 1.7f * u, y - 1.3f * u}, {cx, y - 3.0f * u}, {cx + 1.7f * u, y - 1.3f * u}};
        draw(hat, 3);
      }
      if (acc == 4) {
        Pt hook[3] = {{cx, y + 9 * u}, {cx + 0.8f * u, y + 10.2f * u}, {cx - 0.6f * u, y + 11.2f * u}};
        draw(hook, 3);
      }
      if (acc == 5)   // diaeresis dots (Ï, Ë, Ü, as in the French examples “naïf” and “Noël”)
        for (int k = -1; k <= 1; k += 2) {
          Pt p[2] = {{cx + k * 1.6f * u - 0.2f * u, y - 2.0f * u}, {cx + k * 1.6f * u + 0.2f * u, y - 2.0f * u}};
          draw(p, 2);
        }
    }
    x += advance(b, u);
  }
}

}  // <source> namespace StrokeFont </source>

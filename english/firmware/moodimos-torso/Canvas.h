// Small RGB565 line-scan drawing motor, no library.
// Each shape fills line by line; a cut (ellipse or polygon) can restrict each line, and the "background paint" copies the background image (useful for eyelids, which must match the exact skin tone).
#pragma once
#include <stdint.h>

struct Pt {
  float x, y;
};

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Paint {
  enum Mode : uint8_t { Solid, Background, Blend };
  Mode mode;
  uint16_t color;
  uint8_t alpha;   // Blend 0…255
  static Paint solid(uint16_t c) { return {Solid, c, 255}; }
  static Paint background() { return {Background, 0, 255}; }
  static Paint blend(uint16_t c, float a) {
    return {Blend, c, uint8_t(a <= 0 ? 0 : a >= 1 ? 255 : a * 255)};
  }
};

class Canvas {
 public:
  static constexpr int MAX_POLY = 64;

  Canvas(int w, int h) : w_(w), h_(h) {}
  bool begin();                         // Allocate the background in PSRAM.
  uint16_t* background() { return bg_; }
  void setBackground(uint16_t* bg) { bg_ = bg; }   // base of another stage (clock face)
  // Single unified (neon: black) background: clear and paint "background" becomes a simple fill, without re-reading the background in PSRAM. The background buffer must then be of this color.
  void setFlatBackground(bool flat, uint16_t color = 0) { flat_ = flat; flatColor_ = color; }
  void setTarget(uint16_t* fb);
  int width() const { return w_; }
  int height() const { return h_; }

  // Restore the background. Only the areas drawn since the last erasure of this target are copied (copying 460 KB takes ≈ 39 ms in PSRAM); the entire screen the first time, or after `invalidate()` or a background change.
  void clearToBackground();
  void invalidate();                    // The background was repainted, or a target written outside the canvas.

  // Scale and offset applied to all subsequent drawings (reduced face in the character’s base disk): x' = x · s + tx. Line thicknesses follow the scale.
  void setTransform(float s, float tx, float ty) { ts_ = s; tx_ = tx; ty_ = ty; }
  void clearTransform() { ts_ = 1; tx_ = ty_ = 0; }

  void setClipEllipse(float cx, float cy, float rx, float ry);
  void setClipPolygon(const Pt* p, int n);
  void clearClip() { clip_ = ClipNone; }

  void fillEllipse(float cx, float cy, float rx, float ry, const Paint& p);
  void fillCircle(float cx, float cy, float r, const Paint& p) { fillEllipse(cx, cy, r, r, p); }
  void fillPolygon(const Pt* p, int n, const Paint& paint);
  void strokePolyline(const Pt* p, int n, float width, const Paint& paint, bool closed = false);
  void strokeQuad(Pt a, Pt ctrl, Pt b, float width, const Paint& paint, int segments = 20);
  // A horizontal line in screen pixels (unscaled): for line-drawn shapes.
  void hline(int y, float x0, float x1, const Paint& p) { if (x1 > x0) span(y, x0, x1, p); }
  void strokeEllipse(float cx, float cy, float rx, float ry, float width, const Paint& paint);
  void softEllipse(float cx, float cy, float rx, float ry, uint16_t color, float alpha);

  // Neon line (face of complete edition, dashboard of the torso): each shape filled in one pass, no blending overlaps joints like `strokePolyline`.
  // Single continuous stroke; rounded (or straight) ends; ≤24 points. Can self-intersect or close (last point = first: seamless loop).
  void fillStroke(const Pt* p, int n, float width, const Paint& paint, bool roundCaps = true);
  // Same outline with its aura: two increasingly tight fuses, then the outline.
  void glowStroke(const Pt* p, int n, float width, uint16_t color, float alpha = 1, bool roundCaps = true);
  // Ellipse ring (rx, ry at the center of the arc).
  void strokeRing(float cx, float cy, float rx, float ry, float width, const Paint& paint);
  // Halo: melted from alpha along the ellipse edge until 0, outer px outside, inner px inside.
  // Respect the ongoing cut (eye partially closed).
  // OverBlack: bottom is black; halo color written without re-scanning image (faster in PSRAM).
  void glowEllipse(float cx, float cy, float rx, float ry, float inner, float outer, uint16_t color, float alpha,
                   bool overBlack = false);

  // Horizontal crush of `src` to `dst` around the central vertical axis (piece reversal):
  // - `sx = 1` → full image, `sx → 0` → slice.
  // Unconstrained edges fill.
  static void blitSquash(uint16_t* dst, const uint16_t* src, int w, int h, float sx, uint16_t fill);

 private:
  enum ClipMode : uint8_t { ClipNone, ClipEllipse, ClipPolygon };

  void span(int y, float x0, float x1, const Paint& p);
  bool clipSpan(int y, float& x0, float& x1) const;
  static int crossings(const Pt* p, int n, float sy, float* xs, int8_t* dirs = nullptr);
  void fillPolygonNonZero(const Pt* p, int n, const Paint& paint);   // mechanical clearances remain full.

  // Drawn zones by target (screen clamps, flip-over clamp)
  struct Dirty { uint16_t* fb = nullptr; const uint16_t* bg = nullptr; int16_t* lo = nullptr; int16_t* hi = nullptr; bool full = true; };
  static constexpr int MAX_TARGETS = 4;
  Dirty targets_[MAX_TARGETS];
  Dirty* cur_ = nullptr;
  void mark(int y, int x0, int x1) {
    if (!cur_ || !cur_->lo) return;
    if (x0 < cur_->lo[y]) cur_->lo[y] = int16_t(x0);
    if (x1 > cur_->hi[y]) cur_->hi[y] = int16_t(x1);
  }

  int w_, h_;
  uint16_t* fb_ = nullptr;
  uint16_t* bg_ = nullptr;
  ClipMode clip_ = ClipNone;
  float ts_ = 1, tx_ = 0, ty_ = 0;
  bool flat_ = false;
  uint16_t flatColor_ = 0;
  void fillRow(uint16_t* dst, int n) const;
  float ccx_ = 0, ccy_ = 0, crx_ = 1, cry_ = 1;
  Pt cpoly_[MAX_POLY];
  int cpolyN_ = 0;
};

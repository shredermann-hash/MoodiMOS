#include "Canvas.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
// v / 255 sans division, exact pour v < 65535 : compilé pour la taille (-Os), « / 255 » reste une vraie
// division, trois par pixel fondu.
inline uint32_t div255(uint32_t v) { return (v + 1 + (v >> 8)) >> 8; }

inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {   // t = 255 → b
  uint32_t ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
  uint32_t rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  uint32_t r = div255(ra * (255 - t) + rb * t);
  uint32_t g = div255(ga * (255 - t) + gb * t);
  uint32_t bl = div255(ba * (255 - t) + bb * t);
  return uint16_t((r << 11) | (g << 5) | bl);
}
}  // namespace

bool Canvas::begin() {
  bg_ = (uint16_t*)heap_caps_malloc(size_t(w_) * h_ * 2, MALLOC_CAP_SPIRAM);
  return bg_ != nullptr;
}

void Canvas::setTarget(uint16_t* fb) {
  fb_ = fb;
  cur_ = nullptr;
  for (Dirty& d : targets_) {
    if (d.fb == fb) { cur_ = &d; return; }
  }
  for (Dirty& d : targets_) {
    if (d.fb) continue;
    d.fb = fb;
    d.lo = (int16_t*)malloc(sizeof(int16_t) * h_ * 2);
    d.hi = d.lo ? d.lo + h_ : nullptr;
    d.full = true;
    cur_ = &d;
    return;
  }
}

void Canvas::invalidate() {
  for (Dirty& d : targets_) d.full = true;
}

void Canvas::fillRow(uint16_t* dst, int n) const {
  if (flatColor_ == 0) {
    memset(dst, 0, size_t(n) * 2);
  } else {
    for (int i = 0; i < n; i++) dst[i] = flatColor_;
  }
}

void Canvas::clearToBackground() {
  Dirty* d = cur_;
  if (!d || !d->lo || d->full || d->bg != bg_) {
    if (flat_) fillRow(fb_, w_ * h_);
    else memcpy(fb_, bg_, size_t(w_) * h_ * 2);
  } else {
    for (int y = 0; y < h_; y++) {
      if (d->lo[y] > d->hi[y]) continue;
      size_t at = size_t(y) * w_ + d->lo[y];
      if (flat_) fillRow(fb_ + at, d->hi[y] - d->lo[y] + 1);
      else memcpy(fb_ + at, bg_ + at, size_t(d->hi[y] - d->lo[y] + 1) * 2);
    }
  }
  if (d && d->lo) {
    for (int y = 0; y < h_; y++) { d->lo[y] = int16_t(w_); d->hi[y] = -1; }
    d->full = false;
    d->bg = bg_;
  }
}

void Canvas::setClipEllipse(float cx, float cy, float rx, float ry) {
  clip_ = ClipEllipse;
  ccx_ = cx * ts_ + tx_; ccy_ = cy * ts_ + ty_; crx_ = rx * ts_; cry_ = ry * ts_;
}

void Canvas::setClipPolygon(const Pt* p, int n) {
  if (n > MAX_POLY) n = MAX_POLY;
  for (int i = 0; i < n; i++) cpoly_[i] = {p[i].x * ts_ + tx_, p[i].y * ts_ + ty_};
  cpolyN_ = n;
  clip_ = ClipPolygon;
}

int Canvas::crossings(const Pt* p, int n, float sy, float* xs, int8_t* dirs) {
  int k = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float yi = p[i].y, yj = p[j].y;
    if ((yi <= sy && yj > sy) || (yj <= sy && yi > sy)) {
      if (k < MAX_POLY) {
        if (dirs) dirs[k] = yj > yi ? 1 : -1;   // sens de l'arête (règle non nulle)
        xs[k++] = p[i].x + (sy - yi) * (p[j].x - p[i].x) / (yj - yi);
      }
    }
  }
  for (int a = 1; a < k; a++) {   // tri par insertion, k reste petit
    float v = xs[a];
    int8_t d = dirs ? dirs[a] : 0;
    int b = a - 1;
    while (b >= 0 && xs[b] > v) {
      xs[b + 1] = xs[b];
      if (dirs) dirs[b + 1] = dirs[b];
      b--;
    }
    xs[b + 1] = v;
    if (dirs) dirs[b + 1] = d;
  }
  return k;
}

bool Canvas::clipSpan(int y, float& x0, float& x1) const {
  if (clip_ == ClipEllipse) {
    float dy = (y + 0.5f - ccy_) / cry_;
    if (dy <= -1 || dy >= 1) return false;
    float hw = crx_ * sqrtf(1 - dy * dy);
    if (x0 < ccx_ - hw) x0 = ccx_ - hw;
    if (x1 > ccx_ + hw) x1 = ccx_ + hw;
  } else if (clip_ == ClipPolygon) {
    // Pour la bouche : on garde l'étendue entre la première et la dernière arête.
    float xs[MAX_POLY];
    int k = crossings(cpoly_, cpolyN_, y + 0.5f, xs);
    if (k < 2) return false;
    if (x0 < xs[0]) x0 = xs[0];
    if (x1 > xs[k - 1]) x1 = xs[k - 1];
  }
  return x0 < x1;
}

void Canvas::span(int y, float x0, float x1, const Paint& p) {
  if (y < 0 || y >= h_) return;
  if (clip_ != ClipNone && !clipSpan(y, x0, x1)) return;
  int ix0 = (int)ceilf(x0 - 0.5f);
  int ix1 = (int)ceilf(x1 - 0.5f) - 1;
  if (ix0 < 0) ix0 = 0;
  if (ix1 >= w_) ix1 = w_ - 1;
  if (ix0 > ix1) return;
  mark(y, ix0, ix1);
  uint16_t* row = fb_ + size_t(y) * w_;
  switch (p.mode) {
    case Paint::Solid:
      for (int x = ix0; x <= ix1; x++) row[x] = p.color;
      break;
    case Paint::Background:
      if (flat_) fillRow(row + ix0, ix1 - ix0 + 1);
      else memcpy(row + ix0, bg_ + size_t(y) * w_ + ix0, size_t(ix1 - ix0 + 1) * 2);
      break;
    case Paint::Blend:
      for (int x = ix0; x <= ix1; x++) row[x] = blend565(row[x], p.color, p.alpha);
      break;
  }
}

void Canvas::fillEllipse(float cx, float cy, float rx, float ry, const Paint& p) {
  cx = cx * ts_ + tx_; cy = cy * ts_ + ty_; rx *= ts_; ry *= ts_;
  if (rx <= 0.1f || ry <= 0.1f) return;
  int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
  for (int y = y0; y <= y1; y++) {
    float dy = (y + 0.5f - cy) / ry;
    if (dy <= -1 || dy >= 1) continue;
    float hw = rx * sqrtf(1 - dy * dy);
    span(y, cx - hw, cx + hw, p);
  }
}

void Canvas::fillPolygon(const Pt* src, int n, const Paint& paint) {
  if (n < 3) return;
  if (n > MAX_POLY) n = MAX_POLY;
  Pt p[MAX_POLY];
  for (int i = 0; i < n; i++) p[i] = {src[i].x * ts_ + tx_, src[i].y * ts_ + ty_};
  float ymin = p[0].y, ymax = p[0].y;
  for (int i = 1; i < n; i++) {
    if (p[i].y < ymin) ymin = p[i].y;
    if (p[i].y > ymax) ymax = p[i].y;
  }
  int y0 = (int)floorf(ymin), y1 = (int)ceilf(ymax);
  if (y0 < 0) y0 = 0;
  if (y1 > h_ - 1) y1 = h_ - 1;
  float xs[MAX_POLY];
  for (int y = y0; y <= y1; y++) {
    int k = crossings(p, n, y + 0.5f, xs);
    for (int m = 0; m + 1 < k; m += 2) span(y, xs[m], xs[m + 1], paint);
  }
}

void Canvas::fillPolygonNonZero(const Pt* src, int n, const Paint& paint) {
  if (n < 3) return;
  if (n > MAX_POLY) n = MAX_POLY;
  Pt p[MAX_POLY];
  for (int i = 0; i < n; i++) p[i] = {src[i].x * ts_ + tx_, src[i].y * ts_ + ty_};
  float ymin = p[0].y, ymax = p[0].y;
  for (int i = 1; i < n; i++) {
    if (p[i].y < ymin) ymin = p[i].y;
    if (p[i].y > ymax) ymax = p[i].y;
  }
  int y0 = (int)floorf(ymin), y1 = (int)ceilf(ymax);
  if (y0 < 0) y0 = 0;
  if (y1 > h_ - 1) y1 = h_ - 1;
  float xs[MAX_POLY];
  int8_t ds[MAX_POLY];
  for (int y = y0; y <= y1; y++) {
    int k = crossings(p, n, y + 0.5f, xs, ds), wind = 0;
    for (int m = 0; m + 1 < k; m++) {   // une seule passe par ligne, même là où le trait se recouvre
      wind += ds[m];
      if (wind == 0) continue;
      int e = m + 1;
      while (e + 1 < k && wind + ds[e] != 0) wind += ds[e++];
      span(y, xs[m], xs[e], paint);
      wind += ds[e];
      m = e;
    }
  }
}

void Canvas::strokePolyline(const Pt* p, int n, float width, const Paint& paint, bool closed) {
  float hw = width / 2;
  int segs = closed ? n : n - 1;
  for (int i = 0; i < segs; i++) {
    Pt a = p[i], b = p[(i + 1) % n];
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) continue;
    float nx = -dy / len * hw, ny = dx / len * hw;
    Pt quad[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
    fillPolygon(quad, 4, paint);
  }
  for (int i = 0; i < n; i++) fillCircle(p[i].x, p[i].y, hw, paint);   // jonctions et bouts ronds
}

void Canvas::strokeQuad(Pt a, Pt c, Pt b, float width, const Paint& paint, int segments) {
  constexpr int MAX_N = 20;
  const int N = segments < 2 ? 2 : segments > MAX_N ? MAX_N : segments;
  Pt pts[MAX_N + 1];
  for (int i = 0; i <= N; i++) {
    float t = float(i) / N, u = 1 - t;
    pts[i] = {u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y};
  }
  strokePolyline(pts, N + 1, width, paint);
}

void Canvas::strokeEllipse(float cx, float cy, float rx, float ry, float width, const Paint& paint) {
  constexpr int N = 48;
  Pt pts[N];
  for (int i = 0; i < N; i++) {
    float a = float(i) / N * 6.2831853f;
    pts[i] = {cx + cosf(a) * rx, cy + sinf(a) * ry};
  }
  strokePolyline(pts, N, width, paint, true);
}

void Canvas::blitSquash(uint16_t* dst, const uint16_t* src, int w, int h, float sx, uint16_t fill) {
  if (sx < 0.01f) sx = 0.01f;
  if (sx > 1) sx = 1;
  constexpr int MAX_W = 480;
  int16_t map[MAX_W];   // colonne source de chaque colonne destination, −1 = hors image
  if (w > MAX_W) w = MAX_W;
  const float c = w / 2.0f;
  for (int x = 0; x < w; x++) {
    float u = (x + 0.5f - c) / sx + c;
    map[x] = (u < 0 || u >= w) ? -1 : int16_t(u);
  }
  for (int y = 0; y < h; y++) {
    const uint16_t* s = src + size_t(y) * w;
    uint16_t* d = dst + size_t(y) * w;
    for (int x = 0; x < w; x++) d[x] = map[x] < 0 ? fill : s[map[x]];
  }
}

void Canvas::softEllipse(float cx, float cy, float rx, float ry, uint16_t color, float alpha) {
  if (alpha <= 0.004f) return;
  cx = cx * ts_ + tx_; cy = cy * ts_ + ty_; rx *= ts_; ry *= ts_;
  int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
  int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx);
  if (y0 < 0) y0 = 0;
  if (x0 < 0) x0 = 0;
  if (y1 >= h_) y1 = h_ - 1;
  if (x1 >= w_) x1 = w_ - 1;
  if (x0 > x1) return;
  for (int y = y0; y <= y1; y++) {
    float dy = (y + 0.5f - cy) / ry;
    mark(y, x0, x1);
    uint16_t* row = fb_ + size_t(y) * w_;
    for (int x = x0; x <= x1; x++) {
      float dx = (x + 0.5f - cx) / rx;
      float d = dx * dx + dy * dy;
      if (d >= 1) continue;
      float a = alpha * (1 - d);
      row[x] = blend565(row[x], color, uint8_t(a * 255));
    }
  }
}

void Canvas::fillStroke(const Pt* p, int n, float width, const Paint& paint, bool roundCaps) {
  constexpr int MAX_N = 24, CAP = 6;   // 2 · 24 + 2 · 6 points ≤ MAX_POLY ; le reste sert aux angles vifs
  if (n > MAX_N) n = MAX_N;
  const float hw = width / 2;
  if (n < 2) {
    if (n == 1) fillCircle(p[0].x, p[0].y, hw, paint);
    return;
  }
  Pt dir[MAX_N];                       // direction de chaque segment
  for (int i = 0; i + 1 < n; i++) {
    float dx = p[i + 1].x - p[i].x, dy = p[i + 1].y - p[i].y, len = sqrtf(dx * dx + dy * dy);
    dir[i] = len < 1e-4f ? (i ? dir[i - 1] : Pt{1, 0}) : Pt{dx / len, dy / len};
  }
  const bool closed = fabsf(p[0].x - p[n - 1].x) < 0.01f && fabsf(p[0].y - p[n - 1].y) < 0.01f && n > 2;
  const int caps = roundCaps && !closed ? CAP : 0;
  // Bords gauche et droit. Angle doux : onglet ; angle vif : biseau côté extérieur (un onglet y ferait
  // une pointe), onglet borné côté intérieur.
  Pt left[MAX_POLY], right[MAX_POLY];
  int nl = 0, nr = 0, budget = MAX_POLY - 2 * n - 2 * caps;
  for (int i = 0; i < n; i++) {
    Pt a = dir[i > 0 ? i - 1 : 0], b = dir[i < n - 1 ? i : n - 2];
    if (closed && (i == 0 || i == n - 1)) { a = dir[n - 2]; b = dir[0]; }   // boucle : pas de couture
    float nx = -(a.y + b.y), ny = a.x + b.x, len = sqrtf(nx * nx + ny * ny);
    if (len < 1e-4f) { nx = -b.y; ny = b.x; len = 1; }
    nx /= len; ny /= len;
    const float k = nx * -b.y + ny * b.x;   // cosinus du demi-angle
    const Pt v = p[i];
    if (k < 0.6f && budget > 0 && !(closed && (i == 0 || i == n - 1))) {
      budget--;
      const float m = hw / (k < 0.25f ? 0.25f : k);
      const Pt in = {nx * m, ny * m}, na = {-a.y * hw, a.x * hw}, nb = {-b.y * hw, b.x * hw};
      if (a.x * b.y - a.y * b.x > 0) {     // tourne vers la gauche : le bord droit est à l'extérieur
        left[nl++] = {v.x + in.x, v.y + in.y};
        right[nr++] = {v.x - na.x, v.y - na.y};
        right[nr++] = {v.x - nb.x, v.y - nb.y};
      } else {
        left[nl++] = {v.x + na.x, v.y + na.y};
        left[nl++] = {v.x + nb.x, v.y + nb.y};
        right[nr++] = {v.x - in.x, v.y - in.y};
      }
    } else {
      const float m = hw / (k < 0.4f ? 0.4f : k);
      left[nl++] = {v.x + nx * m, v.y + ny * m};
      right[nr++] = {v.x - nx * m, v.y - ny * m};
    }
  }
  Pt out[MAX_POLY];
  int k = 0;
  for (int i = 0; i < nl; i++) out[k++] = left[i];
  const Pt e = p[n - 1], de = dir[n - 2], s = p[0], ds = dir[0];
  for (int j = 1; j <= caps; j++) {    // bout rond d'arrivée : de la gauche à la droite par l'avant
    float t = 3.1415927f * j / (CAP + 1), c = cosf(t) * hw, sn = sinf(t) * hw;
    out[k++] = {e.x - de.y * c + de.x * sn, e.y + de.x * c + de.y * sn};
  }
  for (int i = nr - 1; i >= 0; i--) out[k++] = right[i];
  for (int j = 1; j <= caps; j++) {    // bout rond de départ : de la droite à la gauche par l'arrière
    float t = 3.1415927f * j / (CAP + 1), c = cosf(t) * hw, sn = sinf(t) * hw;
    out[k++] = {s.x + ds.y * c - ds.x * sn, s.y - ds.x * c - ds.y * sn};
  }
  fillPolygonNonZero(out, k, paint);
}

void Canvas::glowStroke(const Pt* p, int n, float width, uint16_t color, float alpha, bool roundCaps) {
  fillStroke(p, n, width + 16, Paint::blend(color, 0.10f * alpha), roundCaps);
  fillStroke(p, n, width + 8, Paint::blend(color, 0.20f * alpha), roundCaps);
  fillStroke(p, n, width, alpha >= 0.99f ? Paint::solid(color) : Paint::blend(color, alpha), roundCaps);
}

void Canvas::strokeRing(float cx, float cy, float rx, float ry, float width, const Paint& paint) {
  cx = cx * ts_ + tx_; cy = cy * ts_ + ty_;
  const float hw = width / 2 * ts_;
  const float orx = rx * ts_ + hw, ory = ry * ts_ + hw, irx = rx * ts_ - hw, iry = ry * ts_ - hw;
  if (orx <= 0.1f || ory <= 0.1f) return;
  for (int y = (int)floorf(cy - ory); y <= (int)ceilf(cy + ory); y++) {
    float dy = y + 0.5f - cy;
    if (fabsf(dy) >= ory) continue;
    float ho = orx * sqrtf(1 - (dy / ory) * (dy / ory));
    if (irx > 0 && fabsf(dy) < iry) {
      float hi = irx * sqrtf(1 - (dy / iry) * (dy / iry));
      span(y, cx - ho, cx - hi, paint);
      span(y, cx + hi, cx + ho, paint);
    } else {
      span(y, cx - ho, cx + ho, paint);
    }
  }
}

void Canvas::glowEllipse(float cx, float cy, float rx, float ry, float inner, float outer, uint16_t color, float alpha,
                         bool overBlack) {
  if (alpha <= 0.004f || rx <= 0.5f || ry <= 0.5f) return;
  cx = cx * ts_ + tx_; cy = cy * ts_ + ty_; rx *= ts_; ry *= ts_; inner *= ts_; outer *= ts_;
  const float orx = rx + outer, ory = ry + outer, irx = rx - inner, iry = ry - inner, rm = (rx + ry) / 2;
  // Distance au bord ≈ (q² − 1) · rm / 2, q² = (dx/rx)² + (dy/ry)² : sans racine, assez juste sur la
  // largeur d'un halo. Un pas de pixel ajoute 1/rx à dx/rx.
  const float irx1 = 1 / rx, kOut = rm / 2 / outer, kIn = inner > 0 ? rm / 2 / inner : 0, a255 = alpha * 255;
  auto run = [&](int y, float x0, float x1, float dy) {
    if (clip_ != ClipNone && !clipSpan(y, x0, x1)) return;
    int ix0 = (int)ceilf(x0 - 0.5f), ix1 = (int)ceilf(x1 - 0.5f) - 1;
    if (ix0 < 0) ix0 = 0;
    if (ix1 >= w_) ix1 = w_ - 1;
    if (ix0 > ix1) return;
    mark(y, ix0, ix1);
    uint16_t* row = fb_ + size_t(y) * w_;
    const float qy = (dy / ry) * (dy / ry) - 1;
    float u = (ix0 + 0.5f - cx) * irx1;
    for (int x = ix0; x <= ix1; x++, u += irx1) {
      float e = u * u + qy;              // q² − 1 : > 0 dehors
      float f = e >= 0 ? 1 - e * kOut : 1 + e * kIn;
      if (f <= 0 || (e < 0 && kIn == 0)) continue;
      uint8_t t = uint8_t(a255 * f * f);
      row[x] = overBlack ? blend565(0, color, t) : blend565(row[x], color, t);
    }
  };
  for (int y = (int)floorf(cy - ory); y <= (int)ceilf(cy + ory); y++) {
    if (y < 0 || y >= h_) continue;
    float dy = y + 0.5f - cy;
    if (fabsf(dy) >= ory) continue;
    float ho = orx * sqrtf(1 - (dy / ory) * (dy / ory));
    if (irx > 0 && fabsf(dy) < iry) {   // l'intérieur, au-delà du fondu, n'est pas touché
      float hi = irx * sqrtf(1 - (dy / iry) * (dy / iry));
      run(y, cx - ho, cx - hi, dy);
      run(y, cx + hi, cx + ho, dy);
    } else {
      run(y, cx - ho, cx + ho, dy);
    }
  }
}

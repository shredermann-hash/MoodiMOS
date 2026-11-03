// Motion tracking on grayscale images—no library, testable on a computer.
// 
// The image is reduced to a 40×30 grid of cells. Each cell is compared to a slowly learned background; cells that deviate form the target, whose center is tracked. A global lighting change (>60% of cells) re-learns the background without signaling anything.
// 
// An immobile person eventually blends into the background; the torso’s radar retains presence, and the head resumes random gazes. A cell deviating from the background but fixed between frames (e.g., an empty spot a person left, a moved object) merges into the background as *ghostMs*—these "ghosts" retained gaze on a vacant corner, preventing curiosity-driven returns (requiring 2s without a target).
#pragma once
#include <math.h>
#include <stdint.h>
#include "NeckProtocol.h"

class MotionTracker {
 public:
  static constexpr int GW = 40, GH = 30;

  struct Config {
    uint8_t threshold = 18;     // light intensity offset per cell to count as movement
    int minCells = 5;           // minimum target size
    float bgAlpha = 0.04f;      // learning by image, quiet zones
    float smooth = 0.5f;        // smoothing of position, 0 = fixed, 1 = raw
    uint32_t holdMs = 1500;     // the target remains "visible" for this duration after the last movement
    uint32_t ghostMs = 3000;    // Fixed figure beyond the background: absorbed by the background.
    bool mirrorX = true;        // Camera image viewed from the back: right side for her = left side for us.
    bool flipY = false;         // sensor mounted bottom-up on head
    // View: Only a portion of the image is covered, resulting in a central gain.
    // Center = mirrored position (−1 … 1) for a straight-forward gaze.
    float gainX = 1, gainY = 1;
    float centerX = 0, centerY = 0;
  };

  MotionTracker() : MotionTracker(Config()) {}
  explicit MotionTracker(const Config& c) : cfg_(c) {}

  Config& config() { return cfg_; }   // real-time head adjustment (command: "regard")
  void reset() { init_ = false; has_ = false; }
  int cells() const { return cells_; }   // Moving cases in the last image (test bench)

  // `gray: image width × height, 1 byte per pixel (width multiple of 40, height multiple of 30). Fills out; returns `false` if image format is invalid.`
  bool process(const uint8_t* gray, int w, int h, uint32_t now, NeckFace& out) {
    out = NeckFace{0, 0, 0, 0, 0};
    if (!gray || w < GW || h < GH || w % GW || h % GH) return false;
    const int cw = w / GW, ch = h / GH;
    float cur[GW * GH];
    for (int gy = 0; gy < GH; gy++) {
      for (int gx = 0; gx < GW; gx++) {
        uint32_t sum = 0;
        for (int y = 0; y < ch; y++) {
          const uint8_t* row = gray + size_t(gy * ch + y) * w + gx * cw;
          for (int x = 0; x < cw; x++) sum += row[x];
        }
        cur[gy * GW + gx] = float(sum) / (cw * ch);
      }
    }
    if (!init_) {
      for (int i = 0; i < GW * GH; i++) { bg_[i] = prev_[i] = cur[i]; still_[i] = 0; }
      init_ = true;
      return true;
    }

    float weight = 0, wx = 0, wy = 0;
    int cells = 0, minX = GW, maxX = -1;
    bool moving[GW * GH];
    for (int i = 0; i < GW * GH; i++) {
      float d = fabsf(cur[i] - bg_[i]);
      moving[i] = d > cfg_.threshold;
      bool frozen = moving[i] && fabsf(cur[i] - prev_[i]) < cfg_.threshold * 0.5f;
      prev_[i] = cur[i];
      if (!frozen) still_[i] = 0;
      else if (!still_[i]) still_[i] = now | 1;
      else if (uint32_t(now - still_[i]) >= cfg_.ghostMs) {   // ghost: joins the base (foundation)
        bg_[i] = cur[i];
        still_[i] = 0;
        moving[i] = false;
      }
      if (!moving[i]) continue;
      float wgt = d - cfg_.threshold;
      int gx = i % GW, gy = i / GW;
      weight += wgt;
      wx += wgt * (gx + 0.5f);
      wy += wgt * (gy + 0.5f);
      cells++;
      if (gx < minX) minX = gx;
      if (gx > maxX) maxX = gx;
    }

    cells_ = cells;
    if (cells > GW * GH * 6 / 10) {            // Light on or off
      for (int i = 0; i < GW * GH; i++) bg_[i] = cur[i];
      return true;
    }
    for (int i = 0; i < GW * GH; i++) {
      float a = moving[i] ? cfg_.bgAlpha * 0.1f : cfg_.bgAlpha;
      bg_[i] += a * (cur[i] - bg_[i]);
    }

    if (cells >= cfg_.minCells && weight > 0) {
      float tx = wx / weight / GW, ty = wy / weight / GH;   // 0 … 1
      if (has_) {
        sx_ += cfg_.smooth * (tx - sx_);
        sy_ += cfg_.smooth * (ty - sy_);
      } else {
        sx_ = tx;
        sy_ = ty;
      }
      size_ = uint8_t(fminf(255.0f, (maxX - minX + 1) * 255.0f / GW));
      has_ = true;
      lastMotion_ = now;
    }

    if (has_ && uint32_t(now - lastMotion_) < cfg_.holdMs) {
      float x = (sx_ - 0.5f) * 2, y = (sy_ - 0.5f) * 2;
      if (cfg_.mirrorX) x = -x;
      if (cfg_.flipY) y = -y;
      x = (x - cfg_.centerX) * cfg_.gainX;
      y = (y - cfg_.centerY) * cfg_.gainY;
      out.seen = 1;
      out.x = toI8(x);
      out.y = toI8(y);
      out.size = size_;
    } else {
      has_ = false;
    }
    return true;
  }

 private:
  static int8_t toI8(float v) {
    if (v > 1) v = 1;
    if (v < -1) v = -1;
    return int8_t(lroundf(v * 127));
  }

  Config cfg_;
  int cells_ = 0;
  float bg_[GW * GH] = {};
  float prev_[GW * GH] = {};
  uint32_t still_[GW * GH] = {};   // Fixed figure outside the background (0 = off)
  bool init_ = false, has_ = false;
  float sx_ = 0.5f, sy_ = 0.5f;
  uint8_t size_ = 0;
  uint32_t lastMotion_ = 0;
};

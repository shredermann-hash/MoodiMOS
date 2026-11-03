// Suivi de mouvement sur images en niveaux de gris : pas de bibliothèque, testable sur ordinateur.
//
// L'image est réduite en une grille de 40 × 30 cases. Chaque case est comparée à un fond
// appris lentement ; les cases qui s'en écartent forment la cible, dont on suit le centre.
// Un changement d'éclairage global (plus de 60 % des cases) réapprend le fond sans rien signaler.
// Une personne immobile finit par se fondre dans le fond : le radar du torse garde alors la
// présence, et la tête reprend ses regards au hasard. Une case écartée du fond mais figée d'une image
// à l'autre (la place qu'une personne vient de quitter, un objet déplacé) rejoint le fond en ghostMs :
// ces « fantômes » retenaient le regard sur un coin vide de la pièce et empêchaient la curiosité du
// retour (il faut 2 s sans cible).
#pragma once
#include <math.h>
#include <stdint.h>
#include "NeckProtocol.h"

class MotionTracker {
 public:
  static constexpr int GW = 40, GH = 30;

  struct Config {
    uint8_t threshold = 18;     // écart de luminosité d'une case pour compter comme mouvement
    int minCells = 5;           // taille minimale de la cible
    float bgAlpha = 0.04f;      // apprentissage du fond par image, cases calmes
    float smooth = 0.5f;        // lissage de la position, 0 = figé, 1 = brut
    uint32_t holdMs = 1500;     // la cible reste « vue » ce temps après le dernier mouvement
    uint32_t ghostMs = 3000;    // case figée hors du fond au-delà : absorbée par le fond
    bool mirrorX = true;        // l'image caméra est vue de dos : à droite pour elle = à gauche pour nous
    bool flipY = false;         // capteur monté tête en bas
    // Regard : une personne ne couvre qu'une partie de l'image, d'où le gain autour du centre ;
    // centre = position (−1 … 1, après miroir) qui doit donner un regard droit devant.
    float gainX = 1, gainY = 1;
    float centerX = 0, centerY = 0;
  };

  MotionTracker() : MotionTracker(Config()) {}
  explicit MotionTracker(const Config& c) : cfg_(c) {}

  Config& config() { return cfg_; }   // réglage du regard en direct (commande « regard »)
  void reset() { init_ = false; has_ = false; }
  int cells() const { return cells_; }   // cases en mouvement à la dernière image (banc)

  // gray : image w × h, 1 octet par pixel (w multiple de 40, h multiple de 30).
  // Remplit out ; renvoie faux si le format d'image ne convient pas.
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
      else if (uint32_t(now - still_[i]) >= cfg_.ghostMs) {   // fantôme : rejoint le fond
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
    if (cells > GW * GH * 6 / 10) {            // lumière allumée ou éteinte
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
  uint32_t still_[GW * GH] = {};   // case figée hors du fond depuis (0 = non)
  bool init_ = false, has_ = false;
  float sx_ = 0.5f, sy_ = 0.5f;
  uint8_t size_ = 0;
  uint32_t lastMotion_ = 0;
};

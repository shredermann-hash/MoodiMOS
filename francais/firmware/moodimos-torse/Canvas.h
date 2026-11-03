// Petit moteur de dessin RGB565 par balayage de lignes, sans bibliothèque.
// Chaque forme se remplit ligne par ligne ; une découpe (ellipse ou polygone)
// peut restreindre chaque ligne, et la « peinture fond » recopie l'image de fond
// (utile pour les paupières, qui doivent avoir la couleur exacte de la peau).
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
  uint8_t alpha;   // Blend uniquement, 0 … 255
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
  bool begin();                         // alloue le fond en PSRAM
  uint16_t* background() { return bg_; }
  void setBackground(uint16_t* bg) { bg_ = bg; }   // fond d'une autre scène (cadran)
  // Fond uni (néon : noir) : effacer et peindre « fond » deviennent un simple remplissage, sans relire
  // le fond en PSRAM. Le tampon du fond doit alors être de cette couleur.
  void setFlatBackground(bool flat, uint16_t color = 0) { flat_ = flat; flatColor_ = color; }
  void setTarget(uint16_t* fb);
  int width() const { return w_; }
  int height() const { return h_; }

  // Restaure le fond. Seules les zones dessinées depuis le dernier effacement de cette cible sont
  // recopiées (recopier les 460 Ko prend ≈ 39 ms en PSRAM) ; tout l'écran la première fois, ou
  // après invalidate() ou un changement de fond.
  void clearToBackground();
  void invalidate();                    // le fond a été repeint, ou une cible écrite hors du canevas

  // Échelle et décalage appliqués à tout ce qui est dessiné ensuite (visage réduit dans le disque
  // du personnage en pied) : x' = x · s + tx. Les épaisseurs de trait suivent l'échelle.
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
  // Une ligne horizontale, en pixels de l'écran (sans échelle) : pour les formes tracées ligne à ligne.
  void hline(int y, float x0, float x1, const Paint& p) { if (x1 > x0) span(y, x0, x1, p); }
  void strokeEllipse(float cx, float cy, float rx, float ry, float width, const Paint& paint);
  void softEllipse(float cx, float cy, float rx, float ry, uint16_t color, float alpha);

  // Traits du néon (visage de l'édition complète, tableau de bord du torse) : chaque forme est remplie en une
  // seule passe, un fondu ne se superpose donc pas aux jonctions comme avec strokePolyline.
  // Trait épais d'un seul tenant, bouts ronds (ou coupés droit) ; 24 points au plus. Il peut se recouper
  // ou se refermer (dernier point = premier : boucle sans couture).
  void fillStroke(const Pt* p, int n, float width, const Paint& paint, bool roundCaps = true);
  // Le même trait avec son halo : deux fondus de plus en plus serrés, puis le trait.
  void glowStroke(const Pt* p, int n, float width, uint16_t color, float alpha = 1, bool roundCaps = true);
  // Anneau d'ellipse (rx, ry au milieu du trait).
  void strokeRing(float cx, float cy, float rx, float ry, float width, const Paint& paint);
  // Halo : fondu de alpha sur le bord de l'ellipse jusqu'à 0, outer px dehors et inner px dedans.
  // Respecte la découpe en cours (œil en partie fermé). overBlack : le dessous est noir, la couleur du
  // halo s'écrit sans relire l'image (bien plus rapide en PSRAM).
  void glowEllipse(float cx, float cy, float rx, float ry, float inner, float outer, uint16_t color, float alpha,
                   bool overBlack = false);

  // Écrase horizontalement src vers dst autour de l'axe vertical central (retournement
  // de pièce) : sx = 1 image entière, sx → 0 tranche. Les bords libérés prennent fill.
  static void blitSquash(uint16_t* dst, const uint16_t* src, int w, int h, float sx, uint16_t fill);

 private:
  enum ClipMode : uint8_t { ClipNone, ClipEllipse, ClipPolygon };

  void span(int y, float x0, float x1, const Paint& p);
  bool clipSpan(int y, float& x0, float& x1) const;
  static int crossings(const Pt* p, int n, float sy, float* xs, int8_t* dirs = nullptr);
  void fillPolygonNonZero(const Pt* p, int n, const Paint& paint);   // les recouvrements restent pleins

  // Zones dessinées, par cible (les deux tampons de l'écran et le tampon du retournement)
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

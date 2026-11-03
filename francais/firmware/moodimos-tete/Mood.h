// Humeurs de MoodiMOS : lois émotionnelles tirées de l'univers NeoCité (portage de
// firmware/tete/preview/neocite_preview.html, mode robot). Sur le robot, on ne garde de NeoCité
// que les couleurs : ni noms de personnages, ni emblèmes, ni textes du lore.
// Logique pure, testée sur ordinateur (tests/mood_tests.cpp).
//
//   colère (e)         : chaque provocation fait (A + a) · e^0,7 — elle s'auto-alimente
//   charge (ln(√3))    : I = ln(1 + A) / ln(1 + 8), l'intensité affichée ne sature jamais
//   retour au calme (φ): A × 0,618 par cycle de 1,5 s ; fin de crise → apaisé
//   tenir (√3)         : secousse absorbée, visage qui tient bon
//   étourdi            : 1,5 s après la dernière secousse qui fâche, la tête tourne 5 s (sa colère
//                        pâlit en fond), puis il reprend ses esprits : colère effacée, apaisé
//   curiosité (i), perplexité (Ω), bouderie (−π), absence (∅), inquiétude (tête détachée),
//   joie extrême (tête reposée sur le torse, annonce du Mac, couleurs d'or de φ), fatigue (batterie
//   faible ou pénombre vue par le torse, acier). Un bruit fait sursauter puis regarder : seules les
//   secousses fâchent.
#pragma once
#include <math.h>
#include <stdint.h>
#include "NeckProtocol.h"
#include "MoodiMOSEdition.h"
#include "Design.h"

struct Rgb { float r, g, b; };
struct Palette { Rgb skin[3]; Rgb ink; Rgb accent; };   // peau : clair, moyen, sombre

// Couleurs par humeur (palettes NeoCité, sans leurs noms)
enum class Tone : uint8_t { Attentif, Colere, SeCalme, Apaise, Tenir, Curieux, Perplexe, Boude, Vide, Dodo, Count };

inline const Palette& tonePalette(Tone t) {
  static const Palette P[] = {
    {{{0x6d, 0x93, 0xcf}, {0x40, 0x67, 0x9f}, {0x1d, 0x37, 0x64}}, {0x0f, 0x1d, 0x38}, {0x38, 0xe1, 0xff}},  // bleu
    {{{0xf2, 0x3a, 0x2e}, {0xc0, 0x16, 0x16}, {0x52, 0x05, 0x08}}, {0x2a, 0x03, 0x03}, {0xff, 0x2a, 0x1e}},  // rouge colère (saturé : le rouge NeoCité d'origine tirait sur le saumon à l'écran)
    {{{0xeb, 0x9a, 0x55}, {0xb8, 0x60, 0x2c}, {0x4a, 0x24, 0x10}}, {0x2a, 0x12, 0x06}, {0xff, 0x9f, 0x43}},  // orange
    {{{0xd8, 0xb4, 0x5c}, {0x94, 0x70, 0x2c}, {0x3a, 0x2a, 0x10}}, {0x24, 0x17, 0x03}, {0xff, 0xd1, 0x66}},  // or
    {{{0xc2, 0xc9, 0xd0}, {0x7f, 0x89, 0x93}, {0x35, 0x3c, 0x44}}, {0x12, 0x16, 0x1b}, {0xe8, 0xed, 0xf2}},  // acier
    {{{0xa4, 0x84, 0xf0}, {0x6a, 0x48, 0xc0}, {0x2a, 0x18, 0x60}}, {0x14, 0x0a, 0x33}, {0xc3, 0xa6, 0xff}},  // violet
    {{{0x6a, 0x70, 0x7c}, {0x34, 0x39, 0x44}, {0x0e, 0x10, 0x14}}, {0xe8, 0xe8, 0xe8}, {0x7d, 0xf9, 0xff}},  // gris
    {{{0x9a, 0x74, 0x46}, {0x6a, 0x4a, 0x24}, {0x2a, 0x1a, 0x0a}}, {0x1a, 0x10, 0x06}, {0xff, 0xb3, 0x47}},  // brun
    {{{0xf6, 0xf6, 0xf3}, {0xe0, 0xe0, 0xdc}, {0xbc, 0xbc, 0xb7}}, {0x6c, 0x6c, 0x68}, {0xff, 0xff, 0xff}},  // blanc
    {{{0xe6, 0xb8, 0xff}, {0xb8, 0x7c, 0xdc}, {0x5c, 0x2c, 0x84}}, {0x38, 0x16, 0x54}, {0xff, 0xd8, 0x8a}},  // mauve : sommeil doux (violet NeoCité adouci ; le blanc du vide était trop terne)
  };
  return P[uint8_t(t) < uint8_t(Tone::Count) ? uint8_t(t) : 0];
}

// Néon (édition complète) : le fond reste noir, ce sont les traits qui prennent la couleur de l'humeur.
// peau = verre des yeux, ombre du verre, bouche ; encre = sourcils et traits ; accent = tableau de bord.
// Mêmes teintes que les palettes NeoCité, éclaircies pour briller sur le noir.
inline const Palette& neonPalette(Tone t) {
  static const Palette P[] = {
    {{{0xff, 0xc4, 0x22}, {0xff, 0x8c, 0x1a}, {0xff, 0x4a, 0x14}}, {0xff, 0x96, 0x1e}, {0xff, 0xc4, 0x2a}},  // ambre du rendu
    {{{0xff, 0x4a, 0x30}, {0xc8, 0x16, 0x10}, {0xe8, 0x14, 0x10}}, {0xff, 0x3c, 0x24}, {0xff, 0x2a, 0x1e}},  // rouge colère
    {{{0xff, 0xa4, 0x3c}, {0xe8, 0x64, 0x16}, {0xff, 0x50, 0x18}}, {0xff, 0x86, 0x2a}, {0xff, 0x9f, 0x43}},  // orange
    {{{0xff, 0xd0, 0x3a}, {0xff, 0x96, 0x1c}, {0xff, 0x52, 0x18}}, {0xff, 0xb0, 0x34}, {0xff, 0xd1, 0x66}},  // or
    {{{0xe6, 0xee, 0xf6}, {0x92, 0xa2, 0xb4}, {0xb8, 0xc4, 0xd0}}, {0xc8, 0xd4, 0xe0}, {0xe8, 0xed, 0xf2}},  // acier
    {{{0xcc, 0xae, 0xff}, {0x84, 0x5c, 0xe8}, {0xff, 0x5a, 0x8a}}, {0xb0, 0x90, 0xff}, {0xc3, 0xa6, 0xff}},  // violet
    {{{0x96, 0xf4, 0xff}, {0x34, 0xb0, 0xc4}, {0x58, 0xd8, 0xe8}}, {0x7d, 0xf0, 0xff}, {0x7d, 0xf9, 0xff}},  // cyan (accent du gris)
    {{{0xff, 0xb8, 0x50}, {0xb4, 0x6e, 0x26}, {0xe0, 0x60, 0x20}}, {0xd8, 0x90, 0x3a}, {0xff, 0xb3, 0x47}},  // brun ambré
    {{{0xff, 0xec, 0xc8}, {0xd4, 0xa8, 0x6c}, {0xf0, 0x9a, 0x70}}, {0xf0, 0xda, 0xae}, {0xff, 0xf2, 0xdc}},  // blanc chaud
    {{{0xec, 0xc4, 0xff}, {0xae, 0x76, 0xdc}, {0xff, 0x8c, 0xc0}}, {0xd8, 0xa8, 0xff}, {0xff, 0xd8, 0x8a}},  // mauve
  };
  return P[uint8_t(t) < uint8_t(Tone::Count) ? uint8_t(t) : 0];
}

// Couleur d'une humeur pour un design : le personnage horloge est orangé au repos (son cadran) ;
// toutes les autres humeurs gardent leurs couleurs. Le néon a ses propres teintes lumineuses.
inline const Palette& designPalette(Design d, Tone t) {
  static const Palette DIAL = {{{0xff, 0x9e, 0x3e}, {0xf4, 0x7a, 0x20}, {0xba, 0x4e, 0x10}}, {0x2a, 0x12, 0x06}, {0xff, 0xc8, 0x5a}};
  if (d == Design::Neon) return neonPalette(t);
  return isClock(d) && t == Tone::Attentif ? DIAL : tonePalette(t);
}

inline uint16_t toRgb565(const Rgb& c, float k = 1) {
  auto q = [k](float v) { v *= k; return uint16_t(v < 0 ? 0 : v > 255 ? 255 : v); };
  return uint16_t(((q(c.r) & 0xF8) << 8) | ((q(c.g) & 0xFC) << 3) | (q(c.b) >> 3));
}

class MoodEngine {
 public:
  static constexpr float A_MAX = 8, PHI = 1.6180340f, PHI_CYCLE = 1.5f;
  static constexpr float E_GAIN = 2.0137527f;   // e^0,7
  static constexpr float SHOW = 0.12f;          // charge à partir de laquelle la colère se voit
  static constexpr float BOIL = 0.55f;          // au-delà : colère emballée (rouge)
  static constexpr float COOL = 0.30f;          // le rouge tient jusqu'à ce seuil (inertie), puis orange : se calme
  static constexpr float WORRY_S = 3;           // inquiétude quand la tête quitte le cou
  static constexpr float JOY_S = 3;             // joie extrême quand elle retrouve le torse
  static constexpr float DIZZY_AFTER_S = 1.5f;  // secousses finies depuis 1,5 s (une toutes les 1,2 s au plus) : étourdi
  static constexpr float DIZZY_S = 5;           // puis il reprend ses esprits

  float A = 0, I = 0, peakA = 0;
  float hold = 0, calm = 0, curious = 0, omega = 0, sulk = 0, worried = 0, joy = 0, tired = 0;
  float settle = 0, dizzy = 0;   // attente après la dernière secousse, puis étourdissement (FaceEngine)
  bool boiling = false;      // en colère rouge (hystérésis BOIL / COOL)
  bool lowBattery = false;   // tête détachée, batterie sous 15 % : fatiguée
  bool talking = false;      // il parle (« ne me secoue pas… ») : l'étourdissement attend la fin de la phrase
  Design design = Design::Classique;
  MoodPhase phase = MOOD_DORT;
  Tone tone = Tone::Vide;
  Palette pal = tonePalette(Tone::Vide);

  void provoke(float a) {
    A = fminf(40, (A + a) * E_GAIN);
    peakA = fmaxf(peakA, A);
    calm = 0; hold = 0;
  }
  void shakeSoft() { hold = 2.4f; }                    // secousse absorbée : tient bon
  // Colère qui monte : orange à la première secousse (I ≈ 0,32), rouge à la deuxième ou troisième
  // si elles se suivent (une secousse comptée toutes les 1,2 s sur la tête) ; le rouge tient ensuite
  // jusqu'à COOL (inertie), puis orange. Une secousse pendant l'étourdissement relance la colère.
  void shakeHard() { provoke(0.5f); settle = DIZZY_AFTER_S; dizzy = 0; }
  void noise() { curious = 2.8f; }                     // après le sursaut (FaceEngine), il regarde : pas de colère
  void tap() { if (A > 0.3f) A *= 1 / PHI; else calm = 2.5f; }   // le geste apaise
  void newFace() { curious = 2.8f; }
  void unanswerable() { omega = 3.2f; }
  void ignored() { sulk = 4.5f; }
  void detached() { worried = WORRY_S; joy = 0; }
  void docked() { joy = JOY_S; worried = 0; }
  void cheer() { joy = fmaxf(joy, 1.0f); worried = 0; }   // joie entretenue par le torse (annonce du Mac)
  void tire() { tired = 6; }                              // pénombre vue par le torse : fatigué 6 s

  // awake : éveillé (ni endormi, ni couché, ni dans le noir) ; presence : quelqu'un devant
  void update(float dt, bool awake, bool presence) {
    if (settle > 0 && !talking && (settle -= dt) <= 0) { settle = 0; dizzy = DIZZY_S; }
    else if (dizzy > 0 && (dizzy -= dt) <= 0) { dizzy = 0; A = peakA = 0; boiling = false; calm = 2.5f; }   // reprend ses esprits
    A *= powf(1 / PHI, dt / PHI_CYCLE);
    if (A < 0.02f) A = 0;
    I = fminf(1, logf(1 + A) / logf(1 + A_MAX));
    if (peakA > 1 && I <= SHOW) { calm = 3; peakA = 0; }   // fin de crise : retour au calme
    if (I > BOIL) boiling = true;
    else if (I < COOL) boiling = false;
    float* timers[] = {&hold, &calm, &curious, &omega, &sulk, &worried, &joy, &tired};
    for (float* t : timers) *t = fmaxf(0, *t - dt);

    if (worried > 0)      { phase = MOOD_INQUIET; tone = Tone::Vide; }
    else if (joy > 0)     { phase = MOOD_JOIE; tone = Tone::Apaise; }
    else if (!awake)      { phase = MOOD_DORT; tone = Edition::SOFT_SLEEP ? Tone::Dodo : Tone::Vide; }
    else if (lowBattery || tired > 0) { phase = MOOD_FATIGUE; tone = Tone::Tenir; }
    else if (omega > 0)   { phase = MOOD_PERPLEXE; tone = Tone::Perplexe; }
    else if (I > SHOW)    { phase = boiling ? MOOD_COLERE : MOOD_SE_CALME; tone = boiling ? Tone::Colere : Tone::SeCalme; }
    else if (hold > 0)    { phase = MOOD_TENIR; tone = Tone::Tenir; }
    else if (curious > 0) { phase = MOOD_CURIEUX; tone = Tone::Curieux; }
    else if (calm > 0)    { phase = MOOD_APAISE; tone = Tone::Apaise; }
    else if (sulk > 0)    { phase = MOOD_BOUDE; tone = Tone::Boude; }
    else if (!presence)   { phase = MOOD_ABSENCE; tone = Tone::Vide; }
    else                  { phase = MOOD_ATTENTIF; tone = Tone::Attentif; }

    // la couleur glisse vers celle de l'humeur (≈ 0,2 s : réactif sur l'écran réel)
    const Palette& target = designPalette(design, tone);
    float f = 1 - expf(-dt / 0.2f);
    auto mix = [f](Rgb& c, const Rgb& t) { c.r += (t.r - c.r) * f; c.g += (t.g - c.g) * f; c.b += (t.b - c.b) * f; };
    for (int i = 0; i < 3; i++) mix(pal.skin[i], target.skin[i]);
    mix(pal.ink, target.ink);
    mix(pal.accent, target.accent);
  }

  // Couleurs du tableau de bord du torse : fond et panneaux sombres tirés de la peau, accent.
  // Néon : fond presque noir et panneaux teintés de l'accent, comme le visage.
  NeckMood toNeck() const {
    if (design == Design::Neon)
      return {uint8_t(phase), toRgb565(pal.accent, 0.06f), toRgb565(pal.accent, 0.22f), toRgb565(pal.accent)};
    Rgb panel = {pal.skin[2].r + (pal.skin[1].r - pal.skin[2].r) * 0.4f,
                 pal.skin[2].g + (pal.skin[1].g - pal.skin[2].g) * 0.4f,
                 pal.skin[2].b + (pal.skin[1].b - pal.skin[2].b) * 0.4f};
    return {uint8_t(phase), toRgb565(pal.skin[2], 0.45f), toRgb565(panel, 0.7f), toRgb565(pal.accent)};
  }
};

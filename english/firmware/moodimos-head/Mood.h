// MoodiMOS moods: emotional rules adapted from the NeoCité prototype
// (firmware/tete/preview/neocite_preview.html, robot mode). The robot keeps only its colours,
// not character names, emblems or story text. Pure logic, tested on a computer in tests/mood_tests.cpp.
// 
// Anger (e): each provocation applies (A + a) · e^0.7, reinforcing the existing charge.
// Visual intensity: I = ln(1 + A) / ln(1 + 8), capped at 1 in the current engine.
// Return to calm (φ): A × 0.618 per 1.5 s cycle; the outburst ends in a calm state.
// Holding steady (√3): absorbs a soft jolt and keeps its composure.
// Dizziness: starts 1.5 s after the last anger-triggering shake and lasts 5 s, with anger fading
// in the background; recovery clears the anger and requests calm.
// Other states include curiosity (i), puzzlement (Ω), sulking (−π), absence (∅), worry on detachment,
// joy on redocking or a Mac announcement with golden φ colours, and steel-coloured fatigue
// from low battery in other configurations or dim light reported by the torso.
// A noise triggers a startle followed by looking around; only shakes add anger.
#pragma once
#include <math.h>
#include <stdint.h>
#include "NeckProtocol.h"
#include "MoodiMOSEdition.h"
#include "Design.h"

struct Rgb { float r, g, b; };
struct Palette { Rgb skin[3]; Rgb ink; Rgb accent; };   // skin: light, medium, dark

// Color Palettes by Mood (NeoCité, without names)
enum class Tone : uint8_t { Attentif, Colere, SeCalme, Apaise, Tenir, Curieux, Perplexe, Boude, Vide, Dodo, Count };

inline const Palette& tonePalette(Tone t) {
  static const Palette P[] = {
    {{{0x6d, 0x93, 0xcf}, {0x40, 0x67, 0x9f}, {0x1d, 0x37, 0x64}}, {0x0f, 0x1d, 0x38}, {0x38, 0xe1, 0xff}},  // blue
    {{{0xf2, 0x3a, 0x2e}, {0xc0, 0x16, 0x16}, {0x52, 0x05, 0x08}}, {0x2a, 0x03, 0x03}, {0xff, 0x2a, 0x1e}},  // red anger (saturated: the original NeoCité red appeared salmon-colored on screen)
    {{{0xeb, 0x9a, 0x55}, {0xb8, 0x60, 0x2c}, {0x4a, 0x24, 0x10}}, {0x2a, 0x12, 0x06}, {0xff, 0x9f, 0x43}},  // orange
    {{{0xd8, 0xb4, 0x5c}, {0x94, 0x70, 0x2c}, {0x3a, 0x2a, 0x10}}, {0x24, 0x17, 0x03}, {0xff, 0xd1, 0x66}},  // or
    {{{0xc2, 0xc9, 0xd0}, {0x7f, 0x89, 0x93}, {0x35, 0x3c, 0x44}}, {0x12, 0x16, 0x1b}, {0xe8, 0xed, 0xf2}},  // steel
    {{{0xa4, 0x84, 0xf0}, {0x6a, 0x48, 0xc0}, {0x2a, 0x18, 0x60}}, {0x14, 0x0a, 0x33}, {0xc3, 0xa6, 0xff}},  // violet
    {{{0x6a, 0x70, 0x7c}, {0x34, 0x39, 0x44}, {0x0e, 0x10, 0x14}}, {0xe8, 0xe8, 0xe8}, {0x7d, 0xf9, 0xff}},  // grey
    {{{0x9a, 0x74, 0x46}, {0x6a, 0x4a, 0x24}, {0x2a, 0x1a, 0x0a}}, {0x1a, 0x10, 0x06}, {0xff, 0xb3, 0x47}},  // brown
    {{{0xf6, 0xf6, 0xf3}, {0xe0, 0xe0, 0xdc}, {0xbc, 0xbc, 0xb7}}, {0x6c, 0x6c, 0x68}, {0xff, 0xff, 0xff}},  // white
    {{{0xe6, 0xb8, 0xff}, {0xb8, 0x7c, 0xdc}, {0x5c, 0x2c, 0x84}}, {0x38, 0x16, 0x54}, {0xff, 0xd8, 0x8a}},  // mauve: soft sleep (NeoCité violet softened; the void’s white was too dull)
  };
  return P[uint8_t(t) < uint8_t(Tone::Count) ? uint8_t(t) : 0];
}

// Neon (complete edition): the background remains black; it’s the outlines that reflect the mood.
// skin = eye glass, shadow of the glass, mouth; ink = eyebrows and features; accent = dashboard.
// Same hues as NeoCité palettes, brightened to glow against the black.
inline const Palette& neonPalette(Tone t) {
  static const Palette P[] = {
    {{{0xff, 0xc4, 0x22}, {0xff, 0x8c, 0x1a}, {0xff, 0x4a, 0x14}}, {0xff, 0x96, 0x1e}, {0xff, 0xc4, 0x2a}},  // amber render output
    {{{0xff, 0x4a, 0x30}, {0xc8, 0x16, 0x10}, {0xe8, 0x14, 0x10}}, {0xff, 0x3c, 0x24}, {0xff, 0x2a, 0x1e}},  // red anger
    {{{0xff, 0xa4, 0x3c}, {0xe8, 0x64, 0x16}, {0xff, 0x50, 0x18}}, {0xff, 0x86, 0x2a}, {0xff, 0x9f, 0x43}},  // orange
    {{{0xff, 0xd0, 0x3a}, {0xff, 0x96, 0x1c}, {0xff, 0x52, 0x18}}, {0xff, 0xb0, 0x34}, {0xff, 0xd1, 0x66}},  // or
    {{{0xe6, 0xee, 0xf6}, {0x92, 0xa2, 0xb4}, {0xb8, 0xc4, 0xd0}}, {0xc8, 0xd4, 0xe0}, {0xe8, 0xed, 0xf2}},  // steel
    {{{0xcc, 0xae, 0xff}, {0x84, 0x5c, 0xe8}, {0xff, 0x5a, 0x8a}}, {0xb0, 0x90, 0xff}, {0xc3, 0xa6, 0xff}},  // violet
    {{{0x96, 0xf4, 0xff}, {0x34, 0xb0, 0xc4}, {0x58, 0xd8, 0xe8}}, {0x7d, 0xf0, 0xff}, {0x7d, 0xf9, 0xff}},  // cyan (grey accent)
    {{{0xff, 0xb8, 0x50}, {0xb4, 0x6e, 0x26}, {0xe0, 0x60, 0x20}}, {0xd8, 0x90, 0x3a}, {0xff, 0xb3, 0x47}},  // brown amber
    {{{0xff, 0xec, 0xc8}, {0xd4, 0xa8, 0x6c}, {0xf0, 0x9a, 0x70}}, {0xf0, 0xda, 0xae}, {0xff, 0xf2, 0xdc}},  // hot white
    {{{0xec, 0xc4, 0xff}, {0xae, 0x76, 0xdc}, {0xff, 0x8c, 0xc0}}, {0xd8, 0xa8, 0xff}, {0xff, 0xd8, 0x8a}},  // Mauve
  };
  return P[uint8_t(t) < uint8_t(Tone::Count) ? uint8_t(t) : 0];
}

// Color scheme for mood design:
// - The clock-like character is orange at rest (its dial).
// - All other moods retain their original colors.
// - Neon retains its own luminous tones.
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
  static constexpr float SHOW = 0.12f;          // Charge threshold at which anger is observable
  static constexpr float BOIL = 0.55f;          // beyond: enraged (red)
  static constexpr float COOL = 0.30f;          // The red state holds until the threshold (inertia), then transitions to orange—settling.
  static constexpr float WORRY_S = 3;           // concern when the head detaches from the cou
  static constexpr float JOY_S = 3;             // extreme joy when it recovers the torso
  static constexpr float DIZZY_AFTER_S = 1.5f;  // Shock finished after 1.5 s (one every max. 1.2 s): disoriented
  static constexpr float DIZZY_S = 5;           // He regains consciousness.

  float A = 0, I = 0, peakA = 0;
  float hold = 0, calm = 0, curious = 0, omega = 0, sulk = 0, worried = 0, joy = 0, tired = 0;
  float settle = 0, dizzy = 0;   // Waiting after the last jolt, then dizziness (FaceEngine)
  bool boiling = false;      // red rage (hysteresis BOIL/COOL)
  bool lowBattery = false;   // Detached head, battery at 15%: fatigued
  bool talking = false;      // He says (« don’t shake me… »): the dizziness awaits the end of the sentence.
  Design design = Design::Classique;
  MoodPhase phase = MOOD_DORT;
  Tone tone = Tone::Vide;
  Palette pal = tonePalette(Tone::Vide);

  void provoke(float a) {
    A = fminf(40, (A + a) * E_GAIN);
    peakA = fmaxf(peakA, A);
    calm = 0; hold = 0;
  }
  void shakeSoft() { hold = 2.4f; }                    // Shock absorbed: holds firm
  // Rising anger: orange on first jolt (I ≈ 0.32), red on second or third if consecutive (one jolt every 1.2 s on head); red holds until COOL (inertia), then orange. A jolt during dizziness restarts the anger.
  void shakeHard() { provoke(0.5f); settle = DIZZY_AFTER_S; dizzy = 0; }
  void noise() { curious = 2.8f; }                     // After the shock (FaceEngine), it observes: no anger
  void tap() { if (A > 0.3f) A *= 1 / PHI; else calm = 2.5f; }   // The gesture calms.
  void newFace() { curious = 2.8f; }
  void unanswerable() { omega = 3.2f; }
  void ignored() { sulk = 4.5f; }
  void detached() { worried = WORRY_S; joy = 0; }
  void docked() { joy = JOY_S; worried = 0; }
  void cheer() { joy = fmaxf(joy, 1.0f); worried = 0; }   // joy sustained by the torso (Mac announcement)
  void tire() { tired = 6; }                              // viewable dim light from the torso: 6 sec fatigue

  // awake: awake (not asleep, not lying down, not in darkness); presence: someone in front.
  void update(float dt, bool awake, bool presence) {
    if (settle > 0 && !talking && (settle -= dt) <= 0) { settle = 0; dizzy = DIZZY_S; }
    else if (dizzy > 0 && (dizzy -= dt) <= 0) { dizzy = 0; A = peakA = 0; boiling = false; calm = 2.5f; }   // recover its mind
    A *= powf(1 / PHI, dt / PHI_CYCLE);
    if (A < 0.02f) A = 0;
    I = fminf(1, logf(1 + A) / logf(1 + A_MAX));
    if (peakA > 1 && I <= SHOW) { calm = 3; peakA = 0; }   // end of crisis: return to stability
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

    // Color shifts to the mood tone (~0.2 s: reactive on real screen)
    const Palette& target = designPalette(design, tone);
    float f = 1 - expf(-dt / 0.2f);
    auto mix = [f](Rgb& c, const Rgb& t) { c.r += (t.r - c.r) * f; c.g += (t.g - c.g) * f; c.b += (t.b - c.b) * f; };
    for (int i = 0; i < 3; i++) mix(pal.skin[i], target.skin[i]);
    mix(pal.ink, target.ink);
    mix(pal.accent, target.accent);
  }

  // Dashboard torso colors: dark skin-toned background and panels, accented.
  // Neon: nearly black background with accent-colored panels, like the face.
  NeckMood toNeck() const {
    if (design == Design::Neon)
      return {uint8_t(phase), toRgb565(pal.accent, 0.06f), toRgb565(pal.accent, 0.22f), toRgb565(pal.accent)};
    Rgb panel = {pal.skin[2].r + (pal.skin[1].r - pal.skin[2].r) * 0.4f,
                 pal.skin[2].g + (pal.skin[1].g - pal.skin[2].g) * 0.4f,
                 pal.skin[2].b + (pal.skin[1].b - pal.skin[2].b) * 0.4f};
    return {uint8_t(phase), toRgb565(pal.skin[2], 0.45f), toRgb565(panel, 0.7f), toRgb565(pal.accent)};
  }
};

// Identité sonore de MoodiMOS : petits cris de voix synthétisés par le torse, sans fichier (ni carte TF ni
// flash), un par humeur marquante, trois variantes chacun. Logique pure, testée sur ordinateur
// (tests/mood_sounds_tests.cpp).
//
//   joie        « ouiii ! » qui monte                curiosité   « hmm ? » bouche fermée
//   surprise    « hé ! », « ah ! », « ouah ! »      colère      grognement rauque
//   apaisement  soupir (inspiration, long « haaa »)  sommeil     bâillement, puis silence
//   chatouilles éclat de rire « ha-ha-ha »           bisou       « mmm-smack »
//
// Une voix humaine en miniature (synthèse à formants, à la Klatt) : la glotte donne une impulsion par période,
// jamais tout à fait régulière (hauteur et force tremblent un peu), et du souffle ; le conduit vocal (quatre
// résonateurs) en fait des voyelles. Une note glisse d'une hauteur à l'autre et d'une voyelle à l'autre.
//
// MoodVoice décide quand crier : changement d'humeur de la tête (trame MOOD), surprise vue par le torse
// (bruit fort, soulevé), chatouilles et bisous (écrans du torse et de la tête). Un cri au plus toutes les 3 s,
// sauf le rire et le smack qui répondent tout de suite au geste ; chacun espacé de son propre délai, jamais deux
// fois la même variante. Le torse ne crie pas pendant une phrase, et une phrase coupe un cri (Audio.cpp).
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "NeckProtocol.h"

namespace MoodSounds {

enum Sound : uint8_t { NONE, JOIE, CURIEUX, SURPRIS, GRONDE, SOUPIR, BAILLE, RIRE, BISOU, COUNT };
constexpr int VARIANTS = 3;
constexpr uint32_t RATE = 16000;

inline const char* name(Sound s) {
  static const char* const N[] = {"aucun", "joie", "curieux", "surpris", "grogne", "soupir", "baille", "rire", "bisou"};
  return s < COUNT ? N[s] : "?";
}
inline Sound fromName(const char* n) {
  for (uint8_t s = JOIE; s < COUNT; s++)
    if (n && !strcmp(n, name(Sound(s)))) return Sound(s);
  return NONE;
}

// Voyelles (formants d'une petite voix) : a, é, i, o, ou, eu, et m (bouche fermée, le son passe par le nez).
enum Vowel : uint8_t { AA, EE, II, OO, OU, EU, MM };
// VOIX : glotte et souffle dans le conduit vocal ; CLAQUE : les lèvres qui se décollent (bisou).
enum Kind : uint8_t { VOIX, CLAQUE };
// Une note : durée (ms), hauteur de départ et d'arrivée (Hz, glissé), force (%), voyelle de départ et d'arrivée,
// souffle (% : 0 voix claire, 100 chuchoté), raucité (% : voix éraillée du grognement), genre, lie (la suivante
// enchaîne sans retomber : « h » puis « a »). Une note de force nulle est un silence.
struct Note { uint16_t ms, f0, f1; uint8_t amp, from, to, breath, rough, kind, lie; };
constexpr Note pause(uint16_t ms) { return {ms, 0, 0, 0, 0, 0, 0, 0, 0, 0}; }

// Trois variantes par cri, terminées par une note de durée nulle.
inline const Note* notes(Sound s, int variant) {
  static const Note T[COUNT][VARIANTS][24] = {
    {},
    {   // joie : « ouiii ! », « ya-ya-yiii ! », « ouhou-iii ! »
      {{120, 330, 380, 80, OU, OU, 10, 0, VOIX, 1}, {300, 390, 640, 90, OU, II, 10, 0, VOIX, 0}},
      {{90, 380, 420, 85, AA, AA, 15, 0, VOIX, 0}, pause(40), {90, 430, 470, 85, AA, AA, 15, 0, VOIX, 0}, pause(40),
       {300, 480, 700, 90, AA, II, 10, 0, VOIX, 0}},
      {{110, 340, 300, 80, OU, OO, 15, 0, VOIX, 0}, pause(50), {360, 320, 660, 90, OU, II, 10, 0, VOIX, 0}},
    },
    {   // curiosité : « hmm ? » bouche fermée, qui remonte en question
      {{260, 230, 220, 80, MM, MM, 5, 0, VOIX, 1}, {240, 220, 360, 85, MM, MM, 5, 0, VOIX, 0}},
      {{150, 250, 240, 80, MM, MM, 5, 0, VOIX, 0}, pause(70), {250, 240, 380, 85, MM, MM, 5, 0, VOIX, 0}},
      {{140, 240, 240, 75, MM, MM, 5, 0, VOIX, 1}, {260, 250, 380, 85, MM, EU, 10, 0, VOIX, 0}},
    },
    {   // surprise : petite inspiration, puis « hé ! », « ah ! », « ouah ! » qui saute vers l'aigu et retombe
      {{40, 520, 520, 100, EE, EE, 100, 0, VOIX, 1}, {70, 540, 760, 95, EE, EE, 15, 0, VOIX, 1},
       {150, 760, 560, 95, EE, EE, 15, 0, VOIX, 0}},
      {{40, 480, 480, 100, AA, AA, 100, 0, VOIX, 1}, {70, 500, 720, 95, AA, AA, 15, 0, VOIX, 1},
       {150, 720, 520, 95, AA, AA, 15, 0, VOIX, 0}},
      {{40, 480, 480, 100, OU, OU, 100, 0, VOIX, 1}, {80, 520, 740, 95, OU, AA, 15, 0, VOIX, 1},
       {150, 740, 540, 95, AA, AA, 15, 0, VOIX, 0}},
    },
    {   // colère : grognement rauque, gorge serrée
      {{460, 125, 100, 90, EU, EU, 25, 90, VOIX, 0}},
      {{220, 140, 115, 85, MM, MM, 15, 80, VOIX, 1}, {130, 120, 110, 100, EU, EU, 100, 0, VOIX, 0}},
      {{200, 130, 115, 90, EU, EU, 25, 90, VOIX, 0}, pause(70), {280, 125, 100, 90, EU, EU, 25, 90, VOIX, 0}},
    },
    {   // apaisement : petite inspiration, puis long « haaa » qui retombe
      {{280, 300, 300, 50, EU, AA, 100, 0, VOIX, 0}, pause(80), {800, 260, 170, 75, AA, EU, 75, 0, VOIX, 0}},
      {{750, 280, 180, 75, AA, OO, 60, 0, VOIX, 0}},
      {{200, 300, 300, 50, AA, AA, 100, 0, VOIX, 0}, pause(60), {650, 250, 170, 75, EU, EU, 85, 0, VOIX, 0}},
    },
    {   // sommeil : bâillement, la bouche s'ouvre puis se referme
      {{450, 260, 420, 80, AA, AA, 35, 0, VOIX, 1}, {700, 420, 180, 85, AA, OO, 40, 0, VOIX, 0}},
      {{350, 280, 440, 80, AA, AA, 35, 0, VOIX, 1}, {250, 440, 440, 85, AA, AA, 35, 0, VOIX, 1},
       {600, 440, 200, 85, AA, OU, 45, 0, VOIX, 0}},
      {{500, 250, 400, 80, EU, AA, 35, 0, VOIX, 1}, {800, 400, 170, 85, AA, OO, 40, 0, VOIX, 0}},
    },
    {   // chatouilles : éclat de rire, chaque « ha » (souffle puis voyelle) un peu plus bas
      {{30, 460, 460, 100, AA, AA, 100, 0, VOIX, 1}, {75, 460, 425, 90, AA, AA, 25, 0, VOIX, 0}, pause(45),
       {30, 440, 440, 100, AA, AA, 100, 0, VOIX, 1}, {75, 440, 405, 90, AA, AA, 25, 0, VOIX, 0}, pause(45),
       {30, 420, 420, 100, AA, AA, 100, 0, VOIX, 1}, {75, 420, 385, 90, AA, AA, 25, 0, VOIX, 0}, pause(45),
       {30, 400, 400, 100, AA, AA, 100, 0, VOIX, 1}, {75, 400, 365, 90, AA, AA, 25, 0, VOIX, 0}, pause(45),
       {35, 380, 380, 100, AA, AA, 100, 0, VOIX, 1}, {190, 370, 290, 85, AA, AA, 30, 0, VOIX, 0}},
      {{25, 560, 560, 100, II, II, 100, 0, VOIX, 1}, {60, 560, 530, 85, II, II, 25, 0, VOIX, 0}, pause(40),
       {25, 550, 550, 100, II, II, 100, 0, VOIX, 1}, {60, 550, 520, 85, II, II, 25, 0, VOIX, 0}, pause(40),
       {25, 540, 540, 100, II, II, 100, 0, VOIX, 1}, {60, 540, 510, 85, II, II, 25, 0, VOIX, 0}, pause(40),
       {25, 530, 530, 100, II, II, 100, 0, VOIX, 1}, {60, 530, 500, 85, II, II, 25, 0, VOIX, 0}, pause(60),
       {35, 480, 480, 100, AA, AA, 100, 0, VOIX, 1}, {220, 480, 350, 85, AA, AA, 30, 0, VOIX, 0}},
      {{25, 450, 450, 100, AA, AA, 100, 0, VOIX, 1}, {55, 450, 420, 90, AA, AA, 25, 0, VOIX, 0}, pause(35),
       {25, 440, 440, 100, AA, AA, 100, 0, VOIX, 1}, {55, 440, 410, 90, AA, AA, 25, 0, VOIX, 0}, pause(35),
       {25, 430, 430, 100, AA, AA, 100, 0, VOIX, 1}, {55, 430, 400, 90, AA, AA, 25, 0, VOIX, 0}, pause(35),
       {25, 415, 415, 100, AA, AA, 100, 0, VOIX, 1}, {55, 415, 385, 90, AA, AA, 25, 0, VOIX, 0}, pause(90),
       {220, 600, 600, 80, II, II, 100, 0, VOIX, 0}, pause(60),
       {30, 420, 420, 100, AA, AA, 100, 0, VOIX, 1}, {170, 410, 320, 85, AA, AA, 30, 0, VOIX, 0}},
    },
    {   // bisou : « mmm », les lèvres se décollent d'un coup, petit « ah » content
      {{110, 260, 280, 90, MM, MM, 5, 0, VOIX, 0}, {30, 0, 0, 100, MM, MM, 0, 0, CLAQUE, 0},
       {130, 330, 260, 90, AA, AA, 40, 0, VOIX, 0}},
      {{60, 280, 290, 90, MM, MM, 5, 0, VOIX, 0}, {30, 0, 0, 100, MM, MM, 0, 0, CLAQUE, 0},
       {90, 330, 300, 90, AA, AA, 40, 0, VOIX, 0}, pause(80), {60, 290, 300, 90, MM, MM, 5, 0, VOIX, 0},
       {30, 0, 0, 100, MM, MM, 0, 0, CLAQUE, 0}, {120, 340, 270, 90, AA, AA, 40, 0, VOIX, 0}},
      {{90, 270, 290, 90, MM, MM, 5, 0, VOIX, 0}, {30, 0, 0, 100, MM, MM, 0, 0, CLAQUE, 0}, pause(50),
       {200, 260, 230, 90, MM, MM, 10, 0, VOIX, 0}},
    },
  };
  return s > NONE && s < COUNT ? T[s][variant % VARIANTS] : nullptr;
}

// Synthèse par blocs, mono 16 kHz : la tâche voix du torse la lit comme une phrase.
class Synth {
 public:
  // gain 1 : aussi fort que sa voix (−15 dBFS efficaces pendant le cri, comme ses syllabes fortes ; les phrases
  // Piper font −17,5 dBFS en moyenne) ; au-delà, limité en douceur. seed : petites différences d'un cri à l'autre.
  void start(Sound s, int variant, float gain, uint32_t seed) {
    // niveau propre à chaque cri, mesuré : le souffle porte moins que la voix ; grognement et smack, tout en
    // crêtes, restent un peu en dessous pour ne pas toucher le limiteur ; la surprise, un peu au-dessus
    static const float LOUD[COUNT] = {0, 1.41f, 2.3f, 1.4f, 5.2f, 5.1f, 2.1f, 1.66f, 1.8f};
    notes_ = notes(s, variant);
    idx_ = 0; t_ = 0; gain_ = gain * (s < COUNT ? LOUD[s] : 0); rng_ = seed | 1;
    phase_ = am_ = vib_ = flow_ = noise_ = 0;
    jitter_ = 1; pulse_ = 1; odd_ = false;
    glottis_.reset(); lips_.reset();
    for (Resonator& r : tract_) r.reset();
    glottis_.set(0, 100);     // pente de la glotte : −12 dB par octave
    tract_[3].set(3700, 300);
    pitch_ = 0.96f + 0.08f * rand01();
  }
  bool active() const { return notes_ && notes_[idx_].ms; }

  // Écrit jusqu'à n échantillons ; renvoie le nombre écrit (0 : cri fini).
  int render(int16_t* out, int n) {
    int k = 0;
    while (k < n && active()) {
      const Note& note = notes_[idx_];
      const Note* prev = idx_ ? &notes_[idx_ - 1] : nullptr;
      const int len = note.ms * int(RATE) / 1000;
      float s = note.amp ? sample(note, prev, len) : 0;
      out[k++] = int16_t(lroundf(limit(s * gain_) * 32767));
      if (++t_ >= len) { t_ = 0; idx_++; }
    }
    return k;
  }

 private:
  static constexpr float TAU = 6.2831853f, PI_F = 3.1415927f;
  static constexpr float VOICE_GAIN = 2.5f, BREATH_GAIN = 0.04f, CLAQUE_GAIN = 0.35f;

  // Résonateur à deux pôles de Klatt : gain 1 aux basses fréquences, bosse à F de largeur B.
  struct Resonator {
    float a = 1, b = 0, c = 0, y1 = 0, y2 = 0;
    void set(float f, float bw) {
      float r = expf(-PI_F * bw / RATE);
      c = -r * r;
      b = 2 * r * cosf(TAU * f / RATE);
      a = 1 - b - c;
    }
    float run(float x) { float y = a * x + b * y1 + c * y2; y2 = y1; y1 = y; return y; }
    void reset() { y1 = y2 = 0; }
  };
  struct Formants { float f[3], bw[3]; };
  static const Formants& formants(uint8_t v) {
    static const Formants F[] = {
      {{850, 1300, 2700}, {90, 110, 160}},    // a
      {{450, 2100, 2900}, {70, 120, 170}},    // é
      {{330, 2500, 3200}, {60, 130, 180}},    // i
      {{500, 900, 2600}, {80, 100, 160}},     // o
      {{350, 800, 2500}, {70, 100, 160}},     // ou
      {{520, 1500, 2600}, {80, 110, 160}},    // eu
      {{270, 1100, 2300}, {60, 400, 500}},    // m : le nez étouffe les formants du haut
    };
    return F[v < 7 ? v : 0];
  }
  // Attaque et fin d'une note (échantillons) : la voix claire part vite, le souffle arrive doucement.
  static int attack(const Note& n) { return n.kind == CLAQUE ? 8 : 160 + n.breath * 6; }
  static int release(const Note& n) { return n.kind == CLAQUE ? 160 : 480 + n.breath * 10; }

  float sample(const Note& note, const Note* prev, int len) {
    const float x = float(t_) / len, sec = float(t_) / RATE;
    // force : enchaînée depuis la note liée, sinon attaque ; fin arrondie sauf si la suivante est liée
    const bool tied = prev && prev->lie && prev->amp;
    float a = note.amp / 100.0f, env = 1;
    if (tied && t_ < 480) a = (prev->amp + (note.amp - prev->amp) * t_ / 480.0f) / 100.0f;
    const int A = attack(note) < len / 2 ? attack(note) : len / 2, R = release(note) < len / 3 ? release(note) : len / 3;
    if (!tied && t_ < A) env = 0.5f - 0.5f * cosf(PI_F * t_ / A);
    if (!note.lie && len - t_ < R) env *= 0.5f - 0.5f * cosf(PI_F * (len - t_) / R);

    if (note.kind == CLAQUE) {   // lèvres : un bruit aigu qui gonfle en 5 ms et s'éteint aussitôt
      if (t_ == 0) lips_.set(2200, 1800);
      const float u = 6.0f * t_ / len;
      return lips_.run(rand01() * 2 - 1) * u * expf(1 - u) * CLAQUE_GAIN * a * env;
    }

    // hauteur glissée, vibrato qui s'installe sur les notes tenues
    float f = note.f0 * powf(float(note.f1) / note.f0, x) * pitch_;
    if (note.ms >= 200 && note.breath < 50) {
      vib_ += 5.5f / RATE;
      f *= 1 + 0.015f * fminf(1, sec / 0.12f) * sinf(TAU * vib_);
    }
    // conduit vocal : formants de la voyelle, glissés de l'une à l'autre (mis à jour toutes les 2 ms) ; dans
    // l'aigu, le premier suit la hauteur, comme une voix d'enfant qui chante (sinon le « i » s'éteint)
    if (t_ % 32 == 0) {
      const Formants &p = formants(note.from), &q = formants(note.to);
      const float m = x * x * (3 - 2 * x);
      for (int i = 0; i < 3; i++) {
        const float fi = p.f[i] + (q.f[i] - p.f[i]) * m;
        tract_[i].set(i ? fi : fmaxf(fi, 1.1f * f), p.bw[i] + (q.bw[i] - p.bw[i]) * m);
      }
    }
    // glotte : une impulsion par période ; hauteur et force tremblent un peu, beaucoup quand c'est rauque
    const float rough = note.rough / 100.0f, breath = note.breath / 100.0f;
    float pulse = 0;
    phase_ += f * jitter_ / RATE;
    if (phase_ >= 1) {
      phase_ -= floorf(phase_);
      jitter_ = 1 + (rand01() - 0.5f) * (0.02f + 0.2f * rough);
      odd_ = !odd_;   // voix éraillée : une impulsion sur deux plus faible
      pulse_ = (1 + (rand01() - 0.5f) * (0.1f + 0.5f * rough)) * (odd_ ? 1 - 0.5f * rough : 1);
      pulse = pulse_;
    }
    // rayonnement des lèvres (dérivée), pris sur la source : la voix peut s'allumer d'un coup après un souffle
    const float flow = glottis_.run(pulse * 140), voiced = (flow - flow_) * (1 - breath) * VOICE_GAIN;
    flow_ = flow;
    // souffle : bruit adouci vers l'aigu, plus fort glotte ouverte
    const float noise = (rand01() * 2 - 1) + 0.75f * noise_, puff = (noise - noise_) * breath * BREATH_GAIN;
    noise_ = noise;
    float v = voiced + puff * (phase_ < 0.5f ? 1.0f : 0.6f);
    for (Resonator& r : tract_) v = r.run(v);
    if (rough > 0) {   // gorge qui roule : « rrr »
      am_ += 26.0f / RATE;
      am_ -= floorf(am_);
      v *= 1 - 0.5f * rough * (0.5f + 0.5f * sinf(TAU * am_));
    }
    return v * a * env;
  }
  // Au-delà de 0,8 de la pleine échelle, la crête s'arrondit au lieu d'être coupée net (pas de grésillement).
  static float limit(float x) {
    float a = fabsf(x);
    if (a <= 0.8f) return x;
    a = 0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f);
    return x < 0 ? -a : a;
  }
  float rand01() {   // xorshift32
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ >> 8) / 16777216.0f;
  }
  const Note* notes_ = nullptr;
  int idx_ = 0, t_ = 0;
  float gain_ = 0, pitch_ = 1, phase_ = 0, am_ = 0, vib_ = 0, flow_ = 0, noise_ = 0, jitter_ = 1, pulse_ = 1;
  bool odd_ = false;
  Resonator glottis_, lips_, tract_[4];
  uint32_t rng_ = 1;
};

// Quand crier. Les trames MOOD arrivent à 2 Hz : un cri suit le changement d'humeur d'une demi-seconde au plus.
class MoodVoice {
 public:
  static constexpr uint32_t GAP_MS = 3000;     // entre deux cris
  static constexpr uint32_t FRESH_MS = 3000;   // humeur précédente encore valable (sinon tête qui revient)
  // Délai propre à chaque cri : curiosité et bâillement se font rares (un visage va et vient souvent au bureau).
  // Le rire et le smack répondent au geste de l'enfant : pas d'attente derrière un autre cri.
  static bool answersGesture(Sound s) { return s == RIRE || s == BISOU; }
  static uint32_t ownGap(Sound s) {
    static const uint32_t G[COUNT] = {0, 10000, 60000, 8000, 10000, 20000, 60000, 2500, 1000};
    return s < COUNT ? G[s] : 0;
  }

  // Trame MOOD reçue : le cri que mérite ce changement d'humeur, NONE sinon.
  // Humeur précédente inconnue (démarrage, tête qui revient) : seule la joie crie.
  Sound onPhase(uint8_t phase, uint32_t now) {
    const bool known = havePhase_ && now - phaseAt_ < FRESH_MS;
    const uint8_t prev = phase_;
    phase_ = phase; phaseAt_ = now; havePhase_ = true;
    if (known && prev == phase) return NONE;
    if (phase == MOOD_JOIE) return JOIE;
    if (!known) return NONE;
    switch (phase) {
      case MOOD_CURIEUX: return CURIEUX;
      case MOOD_COLERE: case MOOD_SE_CALME: return prev == MOOD_COLERE || prev == MOOD_SE_CALME ? NONE : GRONDE;
      case MOOD_APAISE: return SOUPIR;
      case MOOD_DORT: return BAILLE;
      default: return NONE;
    }
  }

  // Le cri peut-il partir maintenant ? Sa variante (jamais la même que la dernière fois), -1 sinon.
  int take(Sound s, uint32_t now) {
    if (s == NONE || s >= COUNT) return -1;
    if (cried_ && now - lastCry_ < GAP_MS && !answersGesture(s)) return -1;
    if (played_[s] && now - lastAt_[s] < ownGap(s)) return -1;
    cried_ = played_[s] = true;
    lastCry_ = lastAt_[s] = now;
    rng_ = rng_ * 1664525u + 1013904223u + now;
    variant_[s] = uint8_t((variant_[s] + 1 + (rng_ >> 16) % (VARIANTS - 1)) % VARIANTS);
    return variant_[s];
  }

 private:
  bool havePhase_ = false, cried_ = false;
  uint8_t phase_ = MOOD_COUNT;
  uint32_t phaseAt_ = 0, lastCry_ = 0, rng_ = 12345;
  bool played_[COUNT] = {};
  uint32_t lastAt_[COUNT] = {};
  uint8_t variant_[COUNT] = {};
};

}  // namespace MoodSounds

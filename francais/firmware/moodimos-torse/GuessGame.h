// « Devine l'animal » (édition complète) : la petite pense à un animal, le robot le devine en posant des questions ; elle
// répond avec les boutons de l'écran du torse (oui, non, je ne sais pas). Logique pure, testée sur ordinateur
// (tests/guess_tests.cpp) : la boucle du torse dit les phrases, joue les expressions de la tête et dessine
// l'écran d'après ce qui en sort.
//   GuessEngine : le moteur du prototype sur le Mac (extras/devine_animal/devine_animal.py), mêmes
//                 probabilités : chaque réponse renforce les animaux qui collent sans éliminer les autres (une
//                 réponse de travers ne fait pas perdre la partie), la question suivante est celle qui départage
//                 le mieux les animaux encore probables.
//   GuessGame   : la partie. Phrases enregistrées dans la flash du torse (devine_intro, devine_q07, devine_a12…,
//                 écrites par extras/devine_animal/vers_robot.py).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "GuessData.h"
#include "HeadExpressions.h"

class GuessEngine {
 public:
  enum Answer : uint8_t { YES, NO, DONT_KNOW };
  struct Move { bool propose; uint8_t index; };   // une question, ou l'animal à proposer
  static constexpr double RELIABLE = 0.9;      // un enfant répond comme le tableau 9 fois sur 10
  static constexpr double SURE = 0.8, AHEAD = 8.0;   // il propose un animal sûr, ou loin devant le deuxième
  static constexpr int MIN_QUESTIONS = 3, MAX_QUESTIONS = 14;

  void start() {
    for (double& p : p_) p = 1.0 / GuessData::ANIMALS;
    asked_ = rejected_ = 0;
    count_ = 0;
  }

  Move next() const {
    int fav = 0;
    for (int i = 1; i < GuessData::ANIMALS; i++)
      if (p_[i] > p_[fav]) fav = i;
    double second = 0;   // deuxième plus forte probabilité (égale à la première en cas d'égalité)
    for (int i = 0; i < GuessData::ANIMALS; i++)
      if (i != fav && p_[i] > second) second = p_[i];
    const int q = bestQuestion();
    const bool ahead = p_[fav] >= SURE || p_[fav] >= AHEAD * second;   // une réponse de travers ne le fait plus tourner en rond
    if (q < 0 || count_ >= MAX_QUESTIONS || (ahead && count_ >= MIN_QUESTIONS)) return {true, uint8_t(fav)};
    return {false, uint8_t(q)};
  }

  void answer(uint8_t q, Answer a) {
    if (q >= GuessData::QUESTIONS) return;
    asked_ |= 1ULL << q;
    count_++;
    if (a == DONT_KNOW) return;
    for (int i = 0; i < GuessData::ANIMALS; i++) {
      const double y = yesProb(i, q);
      p_[i] *= a == YES ? y : 1 - y;
    }
    normalize();
  }

  // Le robot a proposé cet animal et ce n'était pas lui.
  void reject(uint8_t animal) {
    if (animal >= GuessData::ANIMALS) return;
    rejected_ |= 1ULL << animal;
    p_[animal] = 0;
    normalize();
  }

  int asked() const { return count_; }
  double probability(uint8_t animal) const { return animal < GuessData::ANIMALS ? p_[animal] : 0; }

 private:
  // Probabilité que l'enfant réponde oui à la question q en pensant à l'animal a.
  double yesProb(int a, int q) const {
    const GuessData::Animal& an = GuessData::ANIMAL[a];
    double v = an.yes >> q & 1 ? 1.0 : an.maybe >> q & 1 ? 0.5 : 0.0;
    if (GuessData::QUESTION[q].inverse) v = 1 - v;
    return RELIABLE * v + (1 - RELIABLE) * (1 - v);
  }

  static double entropy(const double* p, int n, double scale) {
    double h = 0;
    for (int i = 0; i < n; i++) {
      const double x = p[i] * scale;
      if (x > 0) h -= x * log2(x);
    }
    return h;
  }

  // Question au plus grand gain d'information ; −1 s'il ne reste rien d'utile à demander.
  int bestQuestion() const {
    const int n = GuessData::ANIMALS;
    const double base = entropy(p_, n, 1);
    double gainMax = 0.02;
    int choice = -1;
    double yes[GuessData::ANIMALS], no[GuessData::ANIMALS];
    for (int q = 0; q < GuessData::QUESTIONS; q++) {
      if (asked_ >> q & 1) continue;
      double pYes = 0;
      for (int i = 0; i < n; i++) {
        yes[i] = p_[i] * yesProb(i, q);
        no[i] = p_[i] - yes[i];
        pYes += yes[i];
      }
      if (!(pYes > 0 && pYes < 1)) continue;
      const double gain = base - pYes * entropy(yes, n, 1 / pYes) - (1 - pYes) * entropy(no, n, 1 / (1 - pYes));
      if (gain > gainMax) { choice = q; gainMax = gain; }
    }
    return choice;
  }

  void normalize() {
    double s = 0;
    for (double p : p_) s += p;
    if (s <= 0) {   // plus aucun animal probable : tous ceux qui n'ont pas été écartés redeviennent possibles
      for (int i = 0; i < GuessData::ANIMALS; i++) s += p_[i] = rejected_ >> i & 1 ? 0 : 1;
      if (s <= 0) { start(); return; }
    }
    for (double& p : p_) p /= s;
  }

  double p_[GuessData::ANIMALS] = {};
  uint64_t asked_ = 0, rejected_ = 0;
  int count_ = 0;
};

class GuessGame {
 public:
  using Answer = GuessEngine::Answer;
  static constexpr int TRIES = 3;                  // trois animaux proposés au plus
  static constexpr uint32_t WAIT_MS = 15000;       // sans réponse : la question est redite
  static constexpr int REPEATS = 2;                // puis il abandonne et boude : elle a laissé la partie en plan
  static constexpr uint32_t THINK_MS = 700;        // un temps pour « réfléchir » entre deux questions
  static constexpr uint32_t SILENT_MS = 800;       // phrase jamais entendue (fichier absent) : on continue
  static constexpr uint32_t PRESSED_MS = 400;      // bouton allumé après l'appui

  bool active() const { return st_ != St::Off; }
  // Boutons de réponse actifs (sinon montrés éteints)
  bool buttons() const { return st_ == St::Ask || st_ == St::Propose || st_ == St::Replay; }
  int8_t pressed(uint32_t now) const { return pressedAt_ && since(now, pressedAt_) < PRESSED_MS ? pressed_ : -1; }
  uint8_t round() const { return st_ == St::Ask || st_ == St::Pause ? uint8_t(engine_.asked() + (st_ == St::Ask)) : 0; }
  const char* text() const { return text_; }
  uint32_t revision() const { return revision_; }   // change à chaque changement visible de l'écran

  void start(uint32_t now) {
    engine_.start();
    tries_ = 0;
    pressedAt_ = doneAt_ = 0;   // rien de la partie d'avant (un bouton resté « appuyé » des jours plus tard)
    say("devine_intro", St::Intro, now);
    show("PENSE TRÈS FORT À UN ANIMAL !");
  }

  void quit() {
    if (!active()) return;
    st_ = St::Off;
    stop_ = true;
    phrase_[0] = 0;
    revision_++;
  }

  void press(Answer a, uint32_t now) {
    if (!buttons()) return;
    pressed_ = int8_t(a);
    pressedAt_ = now | 1;
    revision_++;
    switch (st_) {
      case St::Ask:
        engine_.answer(question_, a);
        expression(HeadExpressions::PERPLEXE, 10);   // il réfléchit
        stop_ = true;                                // la question en cours s'arrête
        st_ = St::Pause;
        pauseUntil_ = now + THINK_MS;
        break;
      case St::Propose:
        if (a == Answer::YES) {
          expression(HeadExpressions::JOIE, 40);
          say("devine_youpi", St::Won, now);
          show("YOUPI ! J'AI DEVINÉ !");
        } else if (a == Answer::NO) {
          engine_.reject(animal_);
          if (tries_ < TRIES) {
            expression(HeadExpressions::SURPRIS, 15);
            say("devine_encore", St::Again, now);
            show("OH ! JE RÉFLÉCHIS ENCORE...");
          } else {
            expression(HeadExpressions::RIEUR, 40);
            say("devine_bravo", St::Lost, now);
            show("BRAVO, TU M'AS BATTU !");
          }
        } else {
          sayAgain(now);   // « je ne sais pas » à « c'est un chat ? » : il redemande
        }
        break;
      case St::Replay:
        if (a == Answer::YES) {
          engine_.start();
          tries_ = 0;
          say("devine_go", St::Go, now);
          show("ON Y VA !");
        } else {
          bye(now);
        }
        break;
      default:
        break;
    }
  }

  void update(uint32_t now, bool speaking) {
    if (!active()) return;
    if (pressedAt_ && since(now, pressedAt_) >= PRESSED_MS) { pressedAt_ = 0; revision_++; }
    if (speaking) heard_ = true;
    const bool done = !phrase_[0] && ((heard_ && !speaking) || (!heard_ && since(now, saidAt_) >= SILENT_MS));
    if (done && !doneAt_) doneAt_ = now | 1;
    switch (st_) {
      case St::Intro:
        if (done) { say("devine_go", St::Go, now); show("ON Y VA !"); }
        break;
      case St::Go:
      case St::Again:
        if (done) move(now);
        break;
      case St::Pause:
        if (int32_t(now - pauseUntil_) >= 0) move(now);
        break;
      case St::Won:
      case St::Lost:
        if (done) { say("devine_rejoue", St::Replay, now); show("ON REJOUE ?"); }
        break;
      case St::Ask:
      case St::Propose:
      case St::Replay:
        if (doneAt_ && since(now, doneAt_) >= WAIT_MS) {
          if (st_ == St::Replay) bye(now);                 // partie finie, pas de réponse : au revoir
          else if (repeats_ >= REPEATS) abandon();         // partie laissée en plan : il boude, sans un mot
          else { repeats_++; sayAgain(now); }
        }
        break;
      case St::Bye:
        if (done) { st_ = St::Off; revision_++; }
        break;
      case St::Off:
        break;
    }
  }

  // Ce que la boucle doit faire maintenant (chaque demande n'est rendue qu'une fois).
  const char* takePhrase(char* out, size_t size) {
    if (!phrase_[0]) return nullptr;
    snprintf(out, size, "%s", phrase_);
    phrase_[0] = 0;
    return out;
  }
  bool takeStop() { bool s = stop_; stop_ = false; return s; }
  bool takeSulk() { bool s = sulk_; sulk_ = false; return s; }   // partie abandonnée : la tête boude
  bool takeExpression(uint8_t& expr, uint8_t& tenths) {
    if (!exprPending_) return false;
    exprPending_ = false;
    expr = expr_;
    tenths = tenths_;
    return true;
  }

  const GuessEngine& engine() const { return engine_; }
  int tries() const { return tries_; }
  uint8_t question() const { return question_; }
  uint8_t animal() const { return animal_; }

 private:
  enum class St : uint8_t { Off, Intro, Go, Ask, Pause, Propose, Again, Won, Lost, Replay, Bye };

  // Temps écoulé depuis t ; 0 si t est un instant marqué « | 1 » tout juste dans le futur.
  static uint32_t since(uint32_t now, uint32_t t) { return int32_t(now - t) > 0 ? now - t : 0; }

  void say(const char* name, St next, uint32_t now) {
    snprintf(phrase_, sizeof(phrase_), "%s", name);
    st_ = next;
    saidAt_ = now;
    heard_ = false;
    doneAt_ = 0;
    revision_++;
  }

  void sayAgain(uint32_t now) {
    char name[sizeof(phrase_)];
    if (st_ == St::Ask) snprintf(name, sizeof(name), "devine_q%02d", question_);
    else if (st_ == St::Propose) snprintf(name, sizeof(name), "devine_a%02d", animal_);
    else snprintf(name, sizeof(name), "devine_rejoue");
    say(name, st_, now);
  }

  void move(uint32_t now) {
    const GuessEngine::Move m = engine_.next();
    char name[sizeof(phrase_)];
    repeats_ = 0;
    if (m.propose) {
      tries_++;
      animal_ = m.index;
      snprintf(name, sizeof(name), "devine_a%02d", animal_);
      expression(HeadExpressions::CONTENT, 20);   // pas EMBALLE : sur la tête, c'est la colère rouge
      say(name, St::Propose, now);
      snprintf(text_, sizeof(text_), "C'EST %s ?", GuessData::ANIMAL[animal_].name);
    } else {
      question_ = m.index;
      snprintf(name, sizeof(name), "devine_q%02d", question_);
      expression(HeadExpressions::CURIEUX, 30);
      say(name, St::Ask, now);
      show(GuessData::QUESTION[question_].text);
    }
  }

  void bye(uint32_t now) {
    expression(HeadExpressions::CLIN, 20);
    say("devine_bientot", St::Bye, now);
    show("À BIENTÔT !");
  }

  void abandon() {
    st_ = St::Off;
    sulk_ = true;
    revision_++;
  }

  void show(const char* s) { snprintf(text_, sizeof(text_), "%s", s); }
  void expression(uint8_t e, uint8_t tenths) { expr_ = e; tenths_ = tenths; exprPending_ = true; }

  GuessEngine engine_;
  St st_ = St::Off;
  char phrase_[16] = {};
  char text_[96] = {};
  uint32_t saidAt_ = 0, doneAt_ = 0, pauseUntil_ = 0, pressedAt_ = 0, revision_ = 0;
  bool heard_ = false, stop_ = false, exprPending_ = false, sulk_ = false;
  int8_t pressed_ = -1;
  uint8_t question_ = 0, animal_ = 0, expr_ = 0, tenths_ = 0;
  int tries_ = 0, repeats_ = 0;
};

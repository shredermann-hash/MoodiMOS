// Cadran de gousset 480×480 : émail ivoire, chemin de fer, chiffres romains tracés,
// petite seconde à 6 h, aiguilles Breguet bleuies. La partie fixe est dessinée une fois
// dans dial() ; chaque image ne redessine que les aiguilles.
// Néon (édition complète) : le visage dit l'heure. Ses yeux de verre regardent l'heure, qui prend la place de
// la bouche en gros chiffres de verre jaune (police du tableau de bord du torse) ; les deux points
// battent la seconde, la lunette se remplit de points au fil de la minute, la date est dessous
// (« JEUDI 29 », puis le mois).
#pragma once
#include <stdint.h>
#include "Canvas.h"

class Watch {
 public:
  bool begin(int size, bool neon = false);   // alloue le cadran et le tampon de retournement
  uint16_t* dial() { return dial_; }
  uint16_t* scratch() { return scratch_; }
  // h : 0-24 fractionnaires (le gousset tourne modulo 12), m : 0-60, s : 0-60 fractionnaires (la trotteuse
  // bat à 5 Hz). weekday : 0 = dimanche … 6 = samedi, −1 si la date est inconnue ; day : jour du mois ;
  // month : 1 … 12.
  void render(Canvas& c, float h, float m, float s, int weekday = -1, int day = 0, int month = 0);

 private:
  void renderDial(Canvas& c);
  void renderHands(Canvas& c, float h, float m, float s);
  void renderNeonDial(Canvas& c);
  void renderNeon(Canvas& c, float h, float m, float s, int weekday, int day, int month);
  bool neon_ = false;
  uint16_t* dial_ = nullptr;
  uint16_t* scratch_ = nullptr;
  int size_ = 0;
};

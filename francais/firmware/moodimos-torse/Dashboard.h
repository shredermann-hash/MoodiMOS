// Tableau de bord du torse : écran ST7796 320×480 en portrait, dessiné dans un tampon
// en PSRAM puis envoyé d'un bloc (pas de scintillement).
#pragma once
#include <stdint.h>

struct DashData {
  bool climateValid, humidityValid, lightValid, soundValid;   // BME280, BH1750, micro
  float tempC, humPct, pressHpa;
  uint16_t lux;
  uint8_t soundDb;
  bool cameraOn;              // capture OV5640 réellement active
  bool powerValid, usb, charging, battery;   // battery : LiPo détectée (V2 ; absente en V1)
  uint8_t batteryPct;
  bool headDocked, headOnline, headTalking;
  int headBatteryPct;         // −1 si inconnu (V1 : pas de batterie)
  bool headCharging;          // V2 : LiPo de la tête en charge sur le cou
  bool radarPresent, radarOnline;
  uint16_t radarCm;
  // Tête détachée (pendentif)
  bool awayStatus;            // statut Bluetooth reçu
  uint8_t awayBatteryPct;     // MOODI_UNKNOWN si inconnu
  uint32_t awaySeenS;         // il y a … secondes
  uint8_t ringState;          // HeadFinder::Ring
  bool gameReady;             // jeu « Devine l'animal » prêt (édition complète, phrases en mémoire) : bouton patte
  bool criesOn;               // cris d'humeur audibles : bouton haut-parleur, barré quand ils sont muets
  uint8_t brain;              // conversation (bouton bulle) : 0 sans cerveau (grisé), 1 prêt, 2 en cours
  uint32_t timerS;            // minuteur (édition complète) : secondes restantes, à la place du titre ; 0 sans minuteur
  uint8_t accessMin;          // accès ouvert par l'appui long : minutes restantes, à la place de l'état de la tête ; 0 fermé
  uint8_t wifi;               // TimeSource::wifi() : pas d'icône sans Wi-Fi, éteinte s'il n'a pas donné l'heure, allumée sinon
};

// Écran du jeu « Devine l'animal » (édition complète, GuessGame.h)
struct GameView {
  const char* text;           // question, proposition ou phrase du moment, en capitales
  uint8_t round;              // numéro de la question (0 : aucun)
  bool buttons;               // boutons de réponse actifs (sinon éteints)
  int8_t pressed;             // bouton qui vient d'être touché (0 oui, 1 non, 2 je ne sais pas) ou −1
};

// Couleurs de l'humeur de la tête (trame MOOD) : le tableau de bord prend les mêmes.
struct DashTheme {
  bool valid;                 // faux : palette par défaut de l'édition (prune ou bleu nuit)
  uint16_t bg, panel, accent; // RGB565
};

namespace Dashboard {
bool begin();
void draw(const DashData& d, const DashTheme& theme);
void drawWorried(const DashTheme& theme, float t);   // tête retirée : visage inquiet (t en s)
void drawJoy(float t);                               // tête reposée : joie extrême, or de φ (t en s)
void drawSleep(const DashTheme& theme, float t);     // tête endormie : le torse dort avec elle (t en s)
const uint16_t* framebuffer();              // dernier dessin 320 × 480 RGB565 (banc)
bool ringButtonHit(int16_t x, int16_t y);   // bouton « Où est ma tête ? » (tête détachée)
bool criesButtonHit(int16_t x, int16_t y);  // bouton haut-parleur à droite du niveau sonore : cris muets ou non
void drawGame(const GameView& v);
bool playButtonHit(int16_t x, int16_t y);   // bouton patte du tableau de bord : lance le jeu
bool talkButtonHit(int16_t x, int16_t y);   // bouton bulle : parler à MoodiMOS (conversation par l'ordinateur)
int gameButtonHit(int16_t x, int16_t y);    // 0 oui, 1 non, 2 je ne sais pas, 3 fin, −1 rien
void drawWho();                             // « Qui parle ? » après le bouton bulle (édition complète, Household.h)
int whoButtonHit(int16_t x, int16_t y);     // personne, Household::COUNT fermer, −1 rien
void drawTimerDone(float t);
void drawAccess(const char* cmd, float held);   // accès demandé par le port série ; held : part de l'appui long faite                // minuteur fini (édition complète) ; t en secondes
}

// Tableau de bord de l'édition complète : le même tableau de bord que les autres éditions (mesures, état de la
// tête, radar, bouton « Où est ma tête ? »), tracé en néon ambre sur fond noir comme le visage de la tête.
// Les moments tête retirée / tête reposée reprennent le visage néon de la tête, comme les autres éditions
// reprennent le leur. Dessiné dans le tampon 320 × 480 de l'écran avec le canevas de la tête (Canvas.h,
// copie identique) ; rien ne dépend de l'écran, le rendu se vérifie aussi sur ordinateur.
#pragma once
#include <stdint.h>
#include "Dashboard.h"

namespace DashboardNeon {

void draw(uint16_t* fb, const DashData& d, const DashTheme& th);
void drawWorried(uint16_t* fb, const DashTheme& th, float t);   // tête retirée (t en s)
void drawJoy(uint16_t* fb, float t);                            // tête reposée
void drawSleep(uint16_t* fb, float t);                          // tête endormie
bool ringButtonHit(int16_t x, int16_t y);
bool criesButtonHit(int16_t x, int16_t y);   // bouton haut-parleur des cris d'humeur
bool talkButtonHit(int16_t x, int16_t y);    // bouton bulle : parler à MoodiMOS (conversation par le Mac)
// Jeu « Devine l'animal » : bouton patte du tableau de bord, puis écran de jeu (question et trois boutons).
void drawGame(uint16_t* fb, const GameView& v);
bool playButtonHit(int16_t x, int16_t y);
int gameButtonHit(int16_t x, int16_t y);    // 0 oui, 1 non, 2 je ne sais pas, 3 fin, −1 rien
// « Qui parle ? » après le bouton bulle : un rond par personne de la maison (Household.h).
void drawWho(uint16_t* fb);
int whoButtonHit(int16_t x, int16_t y);     // personne (Household::PEOPLE), Household::COUNT fermer, −1 rien
void drawTimerDone(uint16_t* fb, float t);  // minuteur fini : tout l'écran, t en secondes
void drawAccess(uint16_t* fb, const char* cmd, float held);  // accès demandé par le port série

}  // namespace DashboardNeon

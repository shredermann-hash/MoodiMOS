// Caméra OV5640 du torse (kit 3.5-C, nappe 24 broches) : suivi de mouvement (MotionTracker.h),
// résultat au format de la trame FACE du cou. Remplace la XIAO de V0.2.
//
// Une tâche sur le cœur 0 allume la caméra à la demande, capture en niveaux de gris 160 × 120
// (cases de 4 × 4 pixels) et garde le dernier résultat. La boucle principale le lit et l'envoie
// à la tête à 25 Hz au plus.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "NeckProtocol.h"

namespace TorsoCamera {

void begin();                              // crée la tâche ; caméra arrêtée
void setActive(bool on);                   // quelqu'un devant et tête en place
bool running();                            // capteur initialisé
bool latest(NeckFace& face, uint32_t& at); // vrai une fois par nouveau résultat
const char* status();                      // pour la commande « camera »
uint32_t frames();                         // images reçues depuis l'allumage (banc)
uint8_t meanLuma();
int movingCells();                         // cases en mouvement à la dernière image (banc)                        // luminosité moyenne 0-255 de la dernière image (banc)

struct Gaze { float gainX, gainY, centerX, centerY; };   // voir MotionTracker::Config
void relearn();                            // le robot a bougé : fond à réapprendre une fois immobile
Gaze gaze();                               // réglage du regard (commande « regard »)
void setGaze(const Gaze& g);

}  // namespace TorsoCamera

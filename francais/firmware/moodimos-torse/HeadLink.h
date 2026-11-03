// Tête détachée, côté torse : lit son statut dans ses annonces et lui envoie une sonnerie.
// Tourne dans sa propre tâche ; la logique d'attente est dans HeadFinder.h (testée).
#pragma once
#include <stdint.h>
#include "HeadFinder.h"

namespace HeadLink {

void begin();                       // initialise le Bluetooth du torse
void setActive(bool headAway);      // ne cherche la tête que lorsqu'elle n'est pas sur le cou
bool status(MoodiHeadStatus& s, uint32_t& ageMs);
void ring(uint8_t tenths);          // « où est ma tête ? » : durée × 100 ms
HeadFinder::Ring ringState();

}  // namespace HeadLink

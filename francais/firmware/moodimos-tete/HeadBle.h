// Tête détachée → Bluetooth : périphérique MoodiMOS-TETE, service MOODI_HEAD (MoodiMOSHead.h).
// Le statut est dans l'annonce (lu par le torse sans connexion) et dans HEAD_STATUS ;
// RING reçoit une demande de sonnerie.
#pragma once
#include <stdint.h>
#include "MoodiMOSHead.h"

namespace HeadBle {

void begin();                              // une seule fois ; le Bluetooth reste initialisé
void setAdvertising(bool on);
void setStatus(const MoodiHeadStatus& s);
bool takeRing(uint8_t& tenths);            // vrai une fois par demande reçue (0 = arrêt)
bool connected();                          // un central (le torse) est connecté

}  // namespace HeadBle

// Heure du torse : RTC PCF85063 au démarrage, recalage NTP par Wi-Fi (une fois par jour),
// réglage manuel au moniteur. Le Wi-Fi ne reste allumé que le temps d'une synchronisation.
// Les identifiants Wi-Fi sont enregistrés en mémoire flash (NVS), jamais affichés.
#pragma once
#include <stdint.h>

namespace TimeSource {

void begin();
void update();
bool now(uint32_t& unixUtc);      // faux tant que l'heure n'est pas connue
uint32_t revision();              // change à chaque recalage : la tête est prévenue
bool setManual(uint32_t unixUtc);
bool setWifi(const char* ssid, const char* password);
void forgetWifi();
const char* status();
uint8_t wifi();                   // icône du tableau de bord : 0 Wi-Fi non enregistré, 1 enregistré, 2 heure venue du réseau
uint32_t timerEnd();              // fin du minuteur (édition complète) en heure Unix, gardée dans la flash ; 0 : aucun
void setTimerEnd(uint32_t unixUtc);

}  // namespace TimeSource

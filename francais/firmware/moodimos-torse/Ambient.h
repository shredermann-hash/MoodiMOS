// Capteurs d'ambiance du torse, sur le bus I2C interne de la carte (connecteur 2×16 :
// broches 26 SCL / 28 SDA, alimentés en 3V3) :
//   BME280 (0x76 ou 0x77) : température, humidité, pression ; un BMP280 est signalé.
//   BH1750 (0x23, ADDR libre) : lumière.
// Une mesure par seconde, sans attente bloquante : la boucle du torse reste fluide.
#pragma once
#include <stdint.h>

namespace Ambient {

struct Reading {
  bool climateOk;     // température et pression récentes
  bool humidityOk;    // humidité récente (BME280 seulement)
  bool lightOk;       // lumière récente
  int16_t tempC100;   // °C × 100, correction de chaleur interne comprise
  uint16_t humPct100; // % × 100
  uint16_t pressHpa10;// hPa × 10
  uint16_t lux;       // plafonné à 65535
};

void begin();                 // Board::begin() doit avoir initialisé l'I2C
void update();                // à appeler dans loop()
bool latest(Reading& out);    // faux si aucune mesure récente (moins de 5 s)
const char* status();         // « BME280 0x76 · BH1750 », pour la commande « capteurs »

}  // namespace Ambient

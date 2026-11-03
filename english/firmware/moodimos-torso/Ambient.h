// Torso ambient sensors on the board's internal I2C bus, on its 2×16 connector:
// pin 26 SCL, pin 28 SDA, powered from 3V3.
// BME280 (0x76 or 0x77): temperature, humidity and pressure; a BMP280 is identified separately.
// BH1750 (0x23, ADDR unconnected): light.
// One reading per second without a blocking wait keeps the torso loop responsive.
#pragma once
#include <stdint.h>

namespace Ambient {

struct Reading {
  bool climateOk;     // recent temperature and pressure readings
  bool humidityOk;    // recent humidity (BME280 only)
  bool lightOk;       // recent light
  int16_t tempC100;   // °C × 100, internal heat correction included
  uint16_t humPct100; // % × 100
  uint16_t pressHpa10;// hPa × 10
  uint16_t lux;       // coated to 65535 (likely *max value* or *color depth* in hexadecimal)
};

void begin();                 // Board::begin() must have initialized I2C.
void update();                // Call in loop()
bool latest(Reading& out);    // False if no recent measurement (less than 5 s)
const char* status();         // BME280 (0x76) · BH1750 – for the command "capteurs" (sensors)

}  // <source> namespace Ambient </source>

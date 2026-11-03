#include "Ambient.h"
#include "Bme280.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>

namespace Ambient {

namespace {
constexpr uint8_t BH1750_ADDR = 0x23;
constexpr uint32_t PERIOD_MS = 1000;
constexpr uint32_t MAX_AGE_MS = 5000;
// Chaleur des cartes dans le torse : à étalonner une fois monté, contre un thermomètre.
constexpr int16_t TEMP_OFFSET_C100 = 0;

uint8_t bmeAddr = 0, bmeChip = 0;
bool bh = false;
Bme280::Calib calib;
Reading last = {};
uint32_t climateAt = 0, lightAt = 0, lastRun = 0;
bool triggered = false;
char statusText[48];

bool readRegs(uint8_t addr, uint8_t reg, uint8_t* out, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool writeCmd(uint8_t addr, uint8_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

bool bmeBegin() {
  for (uint8_t addr : {uint8_t(0x76), uint8_t(0x77)}) {
    uint8_t id;
    if (!readRegs(addr, Bme280::ID_REG, &id, 1)) continue;
    if (id != Bme280::CHIP_BME280 && id != Bme280::CHIP_BMP280) continue;
    uint8_t a[26], b[7] = {};
    if (!readRegs(addr, 0x88, a, 26)) continue;
    if (id == Bme280::CHIP_BME280 && !readRegs(addr, 0xE1, b, 7)) continue;
    calib = Bme280::parseCalib(a, b);
    bmeAddr = addr;
    bmeChip = id;
    writeReg(addr, 0xF5, 0x00);   // pas de filtre, veille entre mesures
    return true;
  }
  return false;
}

// Mesure « forcée » : lancée à une seconde, lue à la suivante (≈ 10 ms suffisent).
void bmeTrigger() {
  if (bmeChip == Bme280::CHIP_BME280) writeReg(bmeAddr, 0xF2, 0x01);   // humidité ×1
  triggered = writeReg(bmeAddr, 0xF4, 0x25);                            // T ×1, P ×1, forcé
}

void bmeRead(uint32_t now) {
  uint8_t r[8];
  if (!triggered || !readRegs(bmeAddr, 0xF7, r, 8)) return;
  int32_t adcP, adcT, adcH, tFine;
  Bme280::unpackRaw(r, adcP, adcT, adcH);
  if (adcT == 0x80000) return;   // mesure pas encore faite
  last.tempC100 = int16_t(Bme280::temperature(calib, adcT, tFine) + TEMP_OFFSET_C100);
  uint32_t pq8 = Bme280::pressureQ8(calib, adcP, tFine);
  last.pressHpa10 = uint16_t(pq8 / 256 / 10);           // Pa → hPa × 10
  last.humPct100 = 0;
  if (bmeChip == Bme280::CHIP_BME280) last.humPct100 = uint16_t(Bme280::humidityQ10(calib, adcH, tFine) * 100 / 1024);
  climateAt = now;
}

void bhRead(uint32_t now) {
  uint8_t r[2];
  Wire.requestFrom(BH1750_ADDR, uint8_t(2));
  if (Wire.available() != 2) { while (Wire.available()) Wire.read(); return; }
  r[0] = Wire.read(); r[1] = Wire.read();
  uint32_t lux = ((uint32_t(r[0]) << 8) | r[1]) * 10 / 12;   // mode haute résolution : ÷ 1,2
  last.lux = lux > 65535 ? 65535 : uint16_t(lux);
  lightAt = now;
}
}  // namespace

void begin() {
  bool bme = bmeBegin();
  bh = writeCmd(BH1750_ADDR, 0x01) && writeCmd(BH1750_ADDR, 0x10);   // allumé, continu haute résolution
  snprintf(statusText, sizeof(statusText), "%s%s · %s",
           !bme ? "BME280 absent" : bmeChip == Bme280::CHIP_BME280 ? "BME280 0x" : "BMP280 (sans humidité) 0x",
           bme ? (bmeAddr == 0x76 ? "76" : "77") : "", bh ? "BH1750" : "BH1750 absent");
  if (bme) bmeTrigger();
}

void update() {
  uint32_t now = millis();
  if (now - lastRun < PERIOD_MS) return;
  lastRun = now;
  if (bmeAddr) { bmeRead(now); bmeTrigger(); }
  if (bh) bhRead(now);
}

bool latest(Reading& out) {
  uint32_t now = millis();
  out = last;
  out.climateOk = bmeAddr && climateAt && now - climateAt <= MAX_AGE_MS;
  out.humidityOk = out.climateOk && bmeChip == Bme280::CHIP_BME280;
  out.lightOk = bh && lightAt && now - lightAt <= MAX_AGE_MS;
  return out.climateOk || out.lightOk;
}

const char* status() { return statusText; }

}  // namespace Ambient

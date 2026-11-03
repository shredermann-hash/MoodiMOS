#include "Board.h"
#include "Qmi8658.h"
#include <Wire.h>

namespace Board {

namespace {
constexpr uint8_t TCA9554_ADDR = 0x20;
constexpr uint8_t AXP2101_ADDR = 0x34;
constexpr uint8_t EXIO_LCD_RST_BIT = 1;   // P1 de l'extenseur (exemples Waveshare)
uint8_t exioOut = 0xFF;
// PCF8574 : entrées à 1 (tirage faible), SW1/SW2 coupés tant que le firmware n'a pas décidé.
uint8_t pcfOut = 0xFF & ~((1 << EXP_SW1) | (1 << EXP_SW2));
Qmi8658 imu;

void writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

void writePcf() {
  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(pcfOut);
  Wire.endTransmission();
}

void setPcfBit(uint8_t bit, bool high) {
  pcfOut = high ? (pcfOut | (1 << bit)) : (pcfOut & ~(1 << bit));
  writePcf();
}

void setExioBit(uint8_t bit, bool high) {
  exioOut = high ? (exioOut | (1 << bit)) : (exioOut & ~(1 << bit));
  writeReg(TCA9554_ADDR, 0x01, exioOut);
}
}  // namespace

void begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
  writePcf();   // coupures 5 V fermées tant que le firmware n'a pas décidé
  powerRails();
  writeReg(TCA9554_ADDR, 0x01, exioOut);
  uint8_t cfg = 0xFF;
  readReg(TCA9554_ADDR, 0x03, cfg);
  writeReg(TCA9554_ADDR, 0x03, cfg & ~(1 << EXIO_LCD_RST_BIT));   // P1 en sortie

  pinMode(PIN_BACKLIGHT, OUTPUT);
  setBacklight(false);
}

void lcdReset() {
  setExioBit(EXIO_LCD_RST_BIT, true);
  delay(10);
  setExioBit(EXIO_LCD_RST_BIT, false);
  delay(10);
  setExioBit(EXIO_LCD_RST_BIT, true);
  delay(200);
}

void setBacklight(bool on) { digitalWrite(PIN_BACKLIGHT, on ? HIGH : LOW); }

// V2 : CTRL maintenue haute allume, basse éteint (sorties du PCF8574 + 10 kΩ vers 3V3)
void setMainPower(bool on) { setPcfBit(EXP_SW1, on); }
void setNeckPower(bool on) { setPcfBit(EXP_SW2, on); }
void refreshExpander() { writePcf(); }

bool readExpander(uint8_t& bits) {
  if (Wire.requestFrom(PCF8574_ADDR, (uint8_t)1) != 1) return false;
  uint8_t value = Wire.read();
  // P2…P7 sont des sorties connues ou des broches libres laissées à 1. Une autre valeur
  // signale une lecture corrompue ou un reset du PCF ; ne pas la traiter comme radar/DOCK.
  if (((value ^ pcfOut) & 0xFC) != 0) {
    writePcf();
    return false;
  }
  bits = value;
  return true;
}

// Régulateurs de l'AXP2101 réglés comme tous les exemples Waveshare (XPowersLib) : ALDO1-4 3,3 V,
// BLDO1 1,5 V et BLDO2 2,8 V (caméra), DLDO1-2 3,3 V. Tension = (mV − 500) / 100 sur les bits 4:0
// de 0x92-0x97, 0x99, 0x9A ; marche : 0x90 bits 0-5 et 7, 0x91 bit 0. On ne les coupe jamais :
// ce que chacun alimente n'est pas documenté. La capture est mise en pause sans couper les rails.
void powerRails() {
  struct { uint8_t reg; uint16_t mv; } const LDO[] = {
    {0x92, 3300}, {0x93, 3300}, {0x94, 3300}, {0x95, 3300}, {0x96, 1500}, {0x97, 2800}, {0x99, 3300}, {0x9A, 3300}};
  uint8_t v;
  for (auto& l : LDO)
    if (readReg(AXP2101_ADDR, l.reg, v)) writeReg(AXP2101_ADDR, l.reg, (v & 0xE0) | ((l.mv - 500) / 100));
  if (readReg(AXP2101_ADDR, 0x90, v)) writeReg(AXP2101_ADDR, 0x90, v | 0xBF);
  if (readReg(AXP2101_ADDR, 0x91, v)) writeReg(AXP2101_ADDR, 0x91, v | 0x01);
}

// Registres AXP2101 (à valider sur la carte) :
// 0x00 bit5 = VBUS bon, bit3 = batterie présente ; 0x01 bits 6:5 = 01 en charge ; 0xA4 = jauge %
Power readPower() {
  Power p = {};
  uint8_t s1, s2, pct;
  if (!readReg(AXP2101_ADDR, 0x00, s1) || !readReg(AXP2101_ADDR, 0x01, s2) || !readReg(AXP2101_ADDR, 0xA4, pct)) {
    return p;
  }
  p.valid = true;
  p.usb = s1 & (1 << 5);
  p.battery = s1 & (1 << 3);
  p.charging = ((s2 >> 5) & 0x03) == 0x01;
  p.percent = pct > 100 ? 100 : pct;
  return p;
}

bool readTouch(int16_t& x, int16_t& y) {
  uint8_t b[5];
  Wire.beginTransmission(0x38);
  Wire.write(0x02);                                  // TD_STATUS puis X et Y du premier point
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x38, (uint8_t)5) != 5) return false;
  for (auto& v : b) v = Wire.read();
  if ((b[0] & 0x0F) == 0) return false;
  x = int16_t(((b[1] & 0x0F) << 8) | b[2]);
  y = int16_t(((b[3] & 0x0F) << 8) | b[4]);
  return true;
}

bool imuBegin() { return imu.begin(Wire); }
bool imuRead(float a[3], float g[3]) { return imu.read(a, g); }

}  // namespace Board

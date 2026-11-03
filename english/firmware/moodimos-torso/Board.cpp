#include "Board.h"
#include "Qmi8658.h"
#include <Wire.h>

namespace Board {

namespace {
constexpr uint8_t TCA9554_ADDR = 0x20;
constexpr uint8_t AXP2101_ADDR = 0x34;
constexpr uint8_t EXIO_LCD_RST_BIT = 1;   // P1 of the actuator (Waveshare examples)
uint8_t exioOut = 0xFF;
// PCF8574: 1-bit inputs (low pull-up), SW1/SW2 disabled until firmware makes a decision.
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
}  // <source> namespace </source>

void begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
  writePcf();   // power supply cut at 5 V until the firmware makes a decision
  powerRails();
  writeReg(TCA9554_ADDR, 0x01, exioOut);
  uint8_t cfg = 0xFF;
  readReg(TCA9554_ADDR, 0x03, cfg);
  writeReg(TCA9554_ADDR, 0x03, cfg & ~(1 << EXIO_LCD_RST_BIT));   // P1 output

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

// V2: CTRL held high turns power on; low turns it off (PCF8574 outputs plus 10 kΩ to 3V3)
void setMainPower(bool on) { setPcfBit(EXP_SW1, on); }
void setNeckPower(bool on) { setPcfBit(EXP_SW2, on); }
void refreshExpander() { writePcf(); }

bool readExpander(uint8_t& bits) {
  if (Wire.requestFrom(PCF8574_ADDR, (uint8_t)1) != 1) return false;
  uint8_t value = Wire.read();
  // P2…P7 are known outputs or unused pins left at 1. Another value indicates corrupted reading or reset of the PCF; do not treat as RADAR/DOCK.
  if (((value ^ pcfOut) & 0xFC) != 0) {
    writePcf();
    return false;
  }
  bits = value;
  return true;
}

// AXP2101 regulators set like all Waveshare examples (XPowersLib):
// ALDO1-4 = 3.3 V, BLDO1 = 1.5 V, BLDO2 = 2.8 V (camera), DLDO1-2 = 3.3 V.
// Voltage = (mV − 500) / 100 in bits 4:0 (0x92–0x97, 0x99, 0x9A).
// Enable bits: 0x90 (bits 0–5, 7), 0x91 (bit 0).
// Never disable: documented power paths are unknown.
// Pauses rails without cutting power.
void powerRails() {
  struct { uint8_t reg; uint16_t mv; } const LDO[] = {
    {0x92, 3300}, {0x93, 3300}, {0x94, 3300}, {0x95, 3300}, {0x96, 1500}, {0x97, 2800}, {0x99, 3300}, {0x9A, 3300}};
  uint8_t v;
  for (auto& l : LDO)
    if (readReg(AXP2101_ADDR, l.reg, v)) writeReg(AXP2101_ADDR, l.reg, (v & 0xE0) | ((l.mv - 500) / 100));
  if (readReg(AXP2101_ADDR, 0x90, v)) writeReg(AXP2101_ADDR, 0x90, v | 0xBF);
  if (readReg(AXP2101_ADDR, 0x91, v)) writeReg(AXP2101_ADDR, 0x91, v | 0x01);
}

// AXP2101 registers, still to validate on the board:
// 0x00 bit5 = VBUS good, bit3 = battery present; 0x01 bits 6:5 = 01 while charging; 0xA4 = fuel gauge %.
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
  Wire.write(0x02);                                  // TD_STATUS, then X and Y of the first point
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

#include "Board.h"
#include "Qmi8658.h"
#include <Wire.h>

namespace Board {

namespace {
constexpr uint8_t TCA9554_ADDR = 0x20;
constexpr uint8_t TCA9554_OUTPUT = 0x01;
constexpr uint8_t TCA9554_CONFIG = 0x03;
constexpr uint8_t CST820_ADDR = 0x15;

uint8_t exioOut = 0;
Qmi8658 imu;
bool imuOk = false;
constexpr uint8_t RTC_ADDR = 0x51;

void writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}
}  // namespace

void begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  // EXIO5-7 (interruptions IMU et RTC) en entrées, le reste en sorties.
  // Sorties au repos : RST et CS hauts, buzzer coupé.
  exioOut = 0b00001111;
  writeReg(TCA9554_ADDR, TCA9554_OUTPUT, exioOut);
  writeReg(TCA9554_ADDR, TCA9554_CONFIG, 0b01110000);

  analogReadResolution(12);
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

  // Tactile : reset puis désactivation de la mise en veille automatique
  setExio(EXIO_TP_RST, false);
  delay(10);
  setExio(EXIO_TP_RST, true);
  delay(50);
  writeReg(CST820_ADDR, 0xFE, 0xFF);

  ledcAttach(PIN_BACKLIGHT, 20000, 10);
  setBacklight(0);
}

void setExio(Exio pin, bool high) {
  uint8_t mask = 1 << (pin - 1);
  exioOut = high ? (exioOut | mask) : (exioOut & ~mask);
  writeReg(TCA9554_ADDR, TCA9554_OUTPUT, exioOut);
}

void setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(PIN_BACKLIGHT, percent * 1023 / 100);
}

void setBuzzer(bool on) { setExio(EXIO_BUZZER, on); }

float batteryVolts() {
  // Pont diviseur par 3 sur la carte, correction relevée par Waveshare
  return analogReadMilliVolts(PIN_BAT_ADC) * 3.0f / 1000.0f / 0.992857f;
}

bool readTouch(Touch& t) {
  uint8_t buf[6] = {0};
  Wire.beginTransmission(CST820_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(CST820_ADDR, (uint8_t)6) != 6) return false;
  for (auto& b : buf) b = Wire.read();
  t.gesture = buf[0];
  t.touched = buf[1] != 0;
  t.x = ((buf[2] & 0x0F) << 8) | buf[3];
  t.y = ((buf[4] & 0x0F) << 8) | buf[5];
  return true;
}

bool imuBegin() { return imuOk = imu.begin(Wire); }
bool imuRead(float a[3], float g[3]) { return imu.read(a, g); }

void prepareDeepSleep() {
  setBacklight(0);
  if (imuOk) imu.powerDown();
  writeReg(CST820_ADDR, 0xFE, 0x00);   // veille automatique du tactile autorisée
}

bool rtcRead(uint8_t regs[7]) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return false;
  for (int i = 0; i < 7; i++) regs[i] = Wire.read();
  return true;
}

bool rtcWrite(const uint8_t regs[7]) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write(regs, 7);
  return Wire.endTransmission() == 0;
}

}  // namespace Board

// MoodiMOS tête — carte Waveshare ESP32-S3-Touch-LCD-2.1
// Brochage relevé dans le pack d'exemples officiel Waveshare (Arduino, 2025).
#pragma once
#include <Arduino.h>

namespace Board {

// I2C interne : tactile CST820 (0x15), extenseur TCA9554 (0x20), RTC PCF85063 (0x51), IMU QMI8658 (0x6B)
constexpr int PIN_I2C_SDA = 15;
constexpr int PIN_I2C_SCL = 7;
constexpr int PIN_BACKLIGHT = 6;
constexpr int PIN_BAT_ADC = 4;
// V1 (false) : tête sans batterie, qui reste sur le cou ; l'ADC y lit la sortie du chargeur, pas une charge.
// V2 (true, version personnelle, pendentif détachable) : LiPo 403040 (≈ 500 mAh) sur la prise MX1.25
// de la carte, rechargée par le 5 V du cou (VBus).
constexpr bool HAS_BATTERY = false;
constexpr int PIN_TOUCH_INT = 16;   // GPIO direct : peut réveiller de la veille profonde

// Connecteur 12 broches (réservés MoodiMOS, voir dossier d'étude §4)
constexpr int PIN_NECK_TX = 43;     // → torse
constexpr int PIN_NECK_RX = 44;     // ← torse
// GPIO19/20 (USB natif) et IO0 restent libres : la caméra OV5640 est sur le torse.

// Extenseur TCA9554 : numéros EXIO 1 à 8
enum Exio : uint8_t {
  EXIO_LCD_RST = 1, EXIO_TP_RST = 2, EXIO_LCD_CS = 3, EXIO_SD_CS = 4,
  EXIO_IMU_INT2 = 5, EXIO_IMU_INT1 = 6, EXIO_RTC_INT = 7, EXIO_BUZZER = 8,
};

void begin();
void setExio(Exio pin, bool high);
void setBacklight(uint8_t percent);   // 0 … 100
void setBuzzer(bool on);
float batteryVolts();

struct Touch {
  bool touched;
  uint16_t x, y;
  uint8_t gesture;   // 0x05 simple tap, 0x0B double, 0x0C appui long
};
bool readTouch(Touch& t);

// IMU QMI8658 : a en g, g en °/s, repère de la puce
bool imuBegin();
bool imuRead(float a[3], float g[3]);

// RTC PCF85063 : registres 0x04 à 0x0A (voir TimeUtil.h)
bool rtcRead(uint8_t regs[7]);
bool rtcWrite(const uint8_t regs[7]);

// Avant la veille profonde : rétroéclairage et IMU coupés, tactile en veille
// automatique (il garde la détection du toucher et réveille la carte par PIN_TOUCH_INT).
void prepareDeepSleep();

}  // namespace Board

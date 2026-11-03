// MoodiMOS head — Waveshare ESP32-S3-Touch-LCD-2.1 board
// Pinout extracted from the official Waveshare example pack (Arduino, 2025).
#pragma once
#include <Arduino.h>

namespace Board {

// Internal I²C:
// - Tactile sensor: CST820 (0x15)
// - Extender: TCA9554 (0x20)
// - RTC: PCF85063 (0x51)
// - IMU: QMI8658 (0x6B)
constexpr int PIN_I2C_SDA = 15;
constexpr int PIN_I2C_SCL = 7;
constexpr int PIN_BACKLIGHT = 6;
constexpr int PIN_BAT_ADC = 4;
// V1 (false): head without battery, remains on neck; ADC reads charger output—not charge.
// V2 (true, personal version, detachable pendant): LiPo 403040 (~500 mAh) on MX1.25 connector of the board, charged via 5 V from neck (VBus).
constexpr bool HAS_BATTERY = false;
constexpr int PIN_TOUCH_INT = 16;   // Direct GPIO: can wake the chip from deep sleep

// 12-pin connector (reserved for MoodiMOS, see study dossier §4)
constexpr int PIN_NECK_TX = 43;     // torso
constexpr int PIN_NECK_RX = 44;     // ← torso
// GPIO19/20 (native USB) and IO0 remain free: the OV5640 camera is on the torso.

// TCA9554 Extender: EXIO pins 1–8
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
  uint8_t gesture;   // `0x05` simple tap, `0x0B` double tap, `0x0C` long press
};
bool readTouch(Touch& t);

// IMU QMI8658: acceleration (a) in g, angular velocity (g) in °/s, sensor reference frame.
bool imuBegin();
bool imuRead(float a[3], float g[3]);

// RTC PCF85063: registers 0x04 to 0x0A (see TimeUtil.h)
bool rtcRead(uint8_t regs[7]);
bool rtcWrite(const uint8_t regs[7]);

// Before deep sleep: backlight and IMU disabled, touch in standby mode (keeps touch detection and wakes the board via PIN_TOUCH_INT).
void prepareDeepSleep();

}  // namespace Board

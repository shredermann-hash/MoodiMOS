// IMU QMI8658 (tête et torse, adresse 0x6B) : pilote minimal par registres.
// Registres et séquence de démarrage repris des exemples Waveshare (Gyro_QMI8658).
// Copie identique dans moodimos-torse/ et moodimos-tete/, vérifiée par les tests.
#pragma once
#include <Wire.h>

class Qmi8658 {
 public:
  static constexpr uint8_t ADDR = 0x6B;
  static constexpr float ACC_SCALE = 8.0f / 32768.0f;     // ±8 g
  static constexpr float GYRO_SCALE = 512.0f / 32768.0f;  // ±512 °/s

  bool begin(TwoWire& wire) {
    wire_ = &wire;
    uint8_t who = 0;
    if (!read(0x00, &who, 1) || who != 0x05) return false;   // WHO_AM_I
    uint8_t ctrl1 = 0;
    if (!read(0x02, &ctrl1, 1)) return false;
    write(0x02, uint8_t((ctrl1 & 0xFE) | 0x40));   // oscillateur actif, incrément d'adresse
    write(0x03, 0x26);   // accéléromètre ±8 g, 120 Hz
    write(0x04, 0x56);   // gyroscope ±512 °/s, 120 Hz
    write(0x08, 0x43);   // horloge rapide, accéléromètre et gyroscope actifs
    return true;
  }

  // Capteurs coupés avant la veille profonde (redémarrés par begin() au réveil)
  void powerDown() {
    write(0x08, 0x00);   // accéléromètre et gyroscope arrêtés
    uint8_t ctrl1 = 0;
    if (read(0x02, &ctrl1, 1)) write(0x02, uint8_t(ctrl1 | 0x01));   // oscillateur arrêté
  }

  // a en g, g en °/s, repère de la puce
  bool read(float a[3], float g[3]) {
    uint8_t b[12];
    if (!read(0x35, b, sizeof(b))) return false;
    for (int i = 0; i < 3; i++) {
      a[i] = int16_t(b[2 * i] | (b[2 * i + 1] << 8)) * ACC_SCALE;
      g[i] = int16_t(b[6 + 2 * i] | (b[6 + 2 * i + 1] << 8)) * GYRO_SCALE;
    }
    return true;
  }

 private:
  bool read(uint8_t reg, uint8_t* out, uint8_t n) {
    wire_->beginTransmission(ADDR);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) return false;
    if (wire_->requestFrom(ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) out[i] = wire_->read();
    return true;
  }
  void write(uint8_t reg, uint8_t v) {
    wire_->beginTransmission(ADDR);
    wire_->write(reg);
    wire_->write(v);
    wire_->endTransmission();
  }
  TwoWire* wire_ = nullptr;
};

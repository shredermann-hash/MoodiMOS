// MoodiMOS torse — kit Waveshare ESP32-S3-Touch-LCD-3.5-C (boîtier + caméra OV5640)
// Brochage : exemples officiels Waveshare (GitHub waveshareteam/ESP32-S3-Touch-LCD-3.5,
// 03_camera_web_server), connecteur 2×16 et dossier d'étude V0.3 §4.
#pragma once
#include <Arduino.h>

namespace Board {

// Carte
constexpr int PIN_I2C_SDA = 8, PIN_I2C_SCL = 7;   // AXP2101, TCA9554, FT6336, ES8311, QMI8658, PCF85063,
                                                   // SCCB de la caméra + BME280, BH1750, PCF8574 (broches 28 / 26)
constexpr uint32_t I2C_HZ = 100000;              // PCF8574T : mode standard, 100 kHz maximum
constexpr int PIN_LCD_SCK = 5, PIN_LCD_MOSI = 1, PIN_LCD_MISO = 2, PIN_LCD_DC = 3;
constexpr int PIN_BACKLIGHT = 6;
// Réservés par la carte : I2S audio 12-16, lecteur TF 9 (D0), 10 (CMD), 11 (CLK), USB 19/20

// Caméra OV5640 sur la nappe 24 broches. Ces GPIO sortent aussi sur le 2×16 : n'y rien brancher.
namespace Cam {
constexpr int XCLK = 38, PCLK = 41, VSYNC = 17, HREF = 18;
constexpr int D0 = 45, D1 = 47, D2 = 48, D3 = 46, D4 = 42, D5 = 40, D6 = 39, D7 = 21;
// SCCB : bus I2C principal (7/8), déjà ouvert par Wire. PWDN et RESET non câblés :
// la caméra est éteinte par ses régulateurs BLDO1 / BLDO2 de l'AXP2101.
}

// Cou : UART0 sur le 2×16 (la console passe par l'USB natif)
constexpr int PIN_NECK_TX = 43;    // broche 27 → tête, contact 2 du cou
constexpr int PIN_NECK_RX = 44;    // broche 25 ← tête, contact 3

// Extenseur PCF8574T (cavalier A1 côté VCC, A0 et A2 côté GND → 0x22). Pas 0x21 : c'est l'adresse que le pilote
// caméra sonde (OV7670/OV7725), il réécrit alors le port du PCF8574 (mesuré au banc le 2026-10-03). Pas 0x20
// (TCA9554 de la carte) ni 0x23 (BH1750).
// Alimenté en 3V3 (broche 31/32), jamais en 5 V. Entrées : écrire 1, lire (tirage haut faible interne).
constexpr uint8_t PCF8574_ADDR = 0x22;
constexpr uint8_t EXP_RADAR_OUT = 0;   // P0 ← OUT du LD2410C (haut = présence)
constexpr uint8_t EXP_DOCK = 1;        // P1 ← DOCK, contact 4 du cou (bas = tête en place)
constexpr uint8_t EXP_SW1 = 2;         // P2 → CTRL de SW1 (V2 : 5 V général), 10 kΩ vers 3V3
constexpr uint8_t EXP_SW2 = 3;         // P3 → CTRL de SW2 (V2 : 5 V du cou), 10 kΩ vers 3V3
                                       // P4 … P7 : libres

void begin();
void lcdReset();
void setBacklight(bool on);
void setMainPower(bool on);   // SW1 (V2)
void refreshExpander();      // réécrit l'état voulu du PCF8574 si un périphérique a modifié le port
void setNeckPower(bool on);   // SW2 (V2)
void powerRails();             // régulateurs de l'AXP2101 comme les exemples Waveshare (dont la caméra)

// PCF8574 : faux si l'extenseur ne répond pas (absent, mauvaise adresse)
bool readExpander(uint8_t& bits);

struct Power {
  bool valid;         // AXP2101 a répondu
  bool usb;           // alimentation USB présente
  bool battery;       // batterie détectée
  bool charging;
  uint8_t percent;    // jauge de l'AXP2101
};
Power readPower();

// IMU QMI8658 : a en g, g en °/s, repère de la puce
bool imuBegin();
bool imuRead(float a[3], float g[3]);

// Tactile FT6336 (0x38) : vrai si un doigt est posé, coordonnées écran portrait 320 × 480
bool readTouch(int16_t& x, int16_t& y);

}  // namespace Board

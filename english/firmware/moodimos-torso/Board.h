// MoodiMOS torso — Waveshare ESP32-S3-Touch-LCD-3.5-C kit, with case and OV5640 camera.
// Pin mapping follows the official Waveshare examples (GitHub waveshareteam/ESP32-S3-Touch-LCD-3.5,
// 03_camera_web_server), the 2×16 connector and the V0.3 design dossier, section 4.
#pragma once
#include <Arduino.h>

namespace Board {

// Circuit Board
constexpr int PIN_I2C_SDA = 8, PIN_I2C_SCL = 7;   // AXP2101, TCA9554, FT6336, ES8311, QMI8658, PCF85063
                                                   // Camera SCCB + BME280, BH1750, PCF8574 (pins 28 / 26)
constexpr uint32_t I2C_HZ = 100000;              // PCF8574T: standard mode, max 100 kHz
constexpr int PIN_LCD_SCK = 5, PIN_LCD_MOSI = 1, PIN_LCD_MISO = 2, PIN_LCD_DC = 3;
constexpr int PIN_BACKLIGHT = 6;
// Reserved by the board: I2S audio 12-16, TF reader 9 (D0), 10 (CMD), 11 (CLK), USB 19/20

// Camera OV5640 on the 24-pin ribbon cable. These GPIO pins also connect to the 2×16 header—do not connect anything there.
namespace Cam {
constexpr int XCLK = 38, PCLK = 41, VSYNC = 17, HREF = 18;
constexpr int D0 = 45, D1 = 47, D2 = 48, D3 = 46, D4 = 42, D5 = 40, D6 = 39, D7 = 21;
// SCCB: Main I²C bus (7/8), already opened by Wire. PWDN and RESET not wired:
// camera is powered off by its BLDO1/BLDO2 regulators of the AXP2101.
}

// Port: UART0 on the 2×16 (console routed via native USB)
constexpr int PIN_NECK_TX = 43;    // Peg 27 → head, contact 2 of neck
constexpr int PIN_NECK_RX = 44;    // peg 25 ← head, contact 3

// PCF8574T extender (cavalier A1 connected to VCC, A0 and A2 to GND → 0x22). Pitch: 0x21 is the address detected by the camera driver (OV7670/OV7725), rewriting the PCF8574 port (measured on bench on 2026-10-03). Pitches 0x20 (TCA9554 on the board) and 0x23 (BH1750) are excluded.
// 
// Powered at 3V3 (pin 31/32), never at 5V. Inputs: write `1`, read (high-to-low internal pull-up).
constexpr uint8_t PCF8574_ADDR = 0x22;
constexpr uint8_t EXP_RADAR_OUT = 0;   // P0 ← OUT from LD2410C (high = presence)
constexpr uint8_t EXP_DOCK = 1;        // P1 ← DOCK, contact 4 of neck (bottom = head in place)
constexpr uint8_t EXP_SW1 = 2;         // P2 → SW1 control (V2: 5 V general), 10 kΩ to 3V3
constexpr uint8_t EXP_SW2 = 3;         // P3 → SW2 control (V2: 5V at neck), 10 kΩ → 3V3
                                       // P4 … P7: free

void begin();
void lcdReset();
void setBacklight(bool on);
void setMainPower(bool on);   // SW1 (V2)
void refreshExpander();      // Rewrite the desired state of the PCF8574 if a peripheral has modified the port.
void setNeckPower(bool on);   // SW2 (V2)
void powerRails();             // AXP2101 power regulators (e.g., Waveshare examples, including the camera)

// PCF8574: false if the actuator does not respond (missing, wrong address)
bool readExpander(uint8_t& bits);

struct Power {
  bool valid;         // AXP2101 responded
  bool usb;           // USB power supply present
  bool battery;       // Battery detected
  bool charging;
  uint8_t percent;    // AXP2101 power/voltage gauge
};
Power readPower();

// IMU QMI8658: acceleration (a) in g, angular velocity (g) in °/s, sensor reference frame.
bool imuBegin();
bool imuRead(float a[3], float g[3]);

// Tactile FT6336 (0x38): true if a finger is pressed, screen coordinates portrait mode 320 × 480
bool readTouch(int16_t& x, int16_t& y);

}  // namespace Board

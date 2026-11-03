// BME280 (Bosch) – Integer calibration and compensation, per datasheet
// *(BST-BME280-DS002, §4.2.3 & §8.2). Pure logic, tested on computer.**
#pragma once
#include <stdint.h>

namespace Bme280 {

constexpr uint8_t ID_REG = 0xD0;
constexpr uint8_t CHIP_BME280 = 0x60;   // A BMP280 (0x58) does not measure humidity.
constexpr uint8_t CHIP_BMP280 = 0x58;

struct Calib {
  uint16_t T1; int16_t T2, T3;
  uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
  uint8_t H1; int16_t H2; uint8_t H3; int16_t H4, H5; int8_t H6;
};

// a: registers 0x88…0xA1 (26 bytes); b: registers 0xE1…0xE7 (7 bytes)
inline Calib parseCalib(const uint8_t a[26], const uint8_t b[7]) {
  auto u16 = [](const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); };
  auto s16 = [&](const uint8_t* p) { return int16_t(u16(p)); };
  Calib c;
  c.T1 = u16(a + 0); c.T2 = s16(a + 2); c.T3 = s16(a + 4);
  c.P1 = u16(a + 6); c.P2 = s16(a + 8); c.P3 = s16(a + 10); c.P4 = s16(a + 12);
  c.P5 = s16(a + 14); c.P6 = s16(a + 16); c.P7 = s16(a + 18); c.P8 = s16(a + 20); c.P9 = s16(a + 22);
  c.H1 = a[25];
  c.H2 = s16(b + 0);
  c.H3 = b[2];
  c.H4 = int16_t((int8_t(b[3]) * 16) | (b[4] & 0x0F));
  c.H5 = int16_t((int8_t(b[5]) * 16) | (b[4] >> 4));
  c.H6 = int8_t(b[6]);
  return c;
}

// Temperature in hundredths of °C; tFine is then used for pressure and humidity.
inline int32_t temperature(const Calib& c, int32_t adcT, int32_t& tFine) {
  int32_t v1 = ((((adcT >> 3) - (int32_t(c.T1) << 1))) * int32_t(c.T2)) >> 11;
  int32_t v2 = (((((adcT >> 4) - int32_t(c.T1)) * ((adcT >> 4) - int32_t(c.T1))) >> 12) * int32_t(c.T3)) >> 14;
  tFine = v1 + v2;
  return (tFine * 5 + 128) >> 8;
}

// Pressure in Pa × 256 (Q24.8 format); 0 if calculation is impossible.
inline uint32_t pressureQ8(const Calib& c, int32_t adcP, int32_t tFine) {
  int64_t v1 = int64_t(tFine) - 128000;
  int64_t v2 = v1 * v1 * c.P6;
  v2 = v2 + v1 * c.P5 * 131072;               // offsets to the left of signed values:
  v2 = v2 + int64_t(c.P4) * 34359738368LL;    // written in multiplication shifts (`<< 17`, `<< 35`, `<< 12`)
  v1 = ((v1 * v1 * c.P3) >> 8) + v1 * c.P2 * 4096;
  v1 = (((int64_t(1) << 47) + v1) * c.P1) >> 33;
  if (v1 == 0) return 0;
  int64_t p = 1048576 - adcP;
  p = (((p << 31) - v2) * 3125) / v1;
  v1 = (int64_t(c.P9) * (p >> 13) * (p >> 13)) >> 25;
  v2 = (int64_t(c.P8) * p) >> 19;
  p = ((p + v1 + v2) >> 8) + int64_t(c.P7) * 16;
  return uint32_t(p);
}

// Humidity in % × 1024 (Q22.10 format).
inline uint32_t humidityQ10(const Calib& c, int32_t adcH, int32_t tFine) {
  int32_t v = tFine - 76800;
  v = (((((adcH << 14) - int32_t(c.H4) * 1048576 - (int32_t(c.H5) * v)) + 16384) >> 15) *
       (((((((v * int32_t(c.H6)) >> 10) * (((v * int32_t(c.H3)) >> 11) + 32768)) >> 10) + 2097152) *
             int32_t(c.H2) + 8192) >> 14));
  v = v - (((((v >> 15) * (v >> 15)) >> 7) * int32_t(c.H1)) >> 4);
  v = v < 0 ? 0 : v;
  v = v > 419430400 ? 419430400 : v;
  return uint32_t(v >> 12);
}

// Raw registers 0xF7…0xFE (raw pressure, temperature, humidity data)
inline void unpackRaw(const uint8_t r[8], int32_t& adcP, int32_t& adcT, int32_t& adcH) {
  adcP = int32_t((uint32_t(r[0]) << 12) | (uint32_t(r[1]) << 4) | (r[2] >> 4));
  adcT = int32_t((uint32_t(r[3]) << 12) | (uint32_t(r[4]) << 4) | (r[5] >> 4));
  adcH = int32_t((uint32_t(r[6]) << 8) | r[7]);
}

}  // namespace BME280

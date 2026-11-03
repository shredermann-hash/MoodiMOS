// Lecture d'en-tête WAV (RIFF) : format, voies, fréquence, position des données.
// Logique pure, testée. Les voix sont attendues en PCM 16 bits, 16 kHz, mono ou stéréo
// (Piper : --output-raw non, sortie .wav rééchantillonnée à 16 kHz).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct WavInfo {
  uint16_t format = 0;       // 1 = PCM
  uint16_t channels = 0;
  uint32_t rate = 0;
  uint16_t bits = 0;
  uint32_t dataOffset = 0;   // octet de début des échantillons dans le fichier
  uint32_t dataSize = 0;     // octets d'échantillons
};

namespace WavFile {

inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

// buf = début du fichier (assez d'octets pour atteindre le bloc « data »).
inline bool parse(const uint8_t* buf, size_t n, WavInfo& w) {
  w = WavInfo();
  if (!buf || n < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;
  bool haveFmt = false;
  uint32_t pos = 12;   // 32 bits comme size_t sur l'ESP32 : les tests voient le même débordement
  while (size_t(pos) + 8 <= n) {
    const uint8_t* ck = buf + pos;
    uint32_t size = le32(ck + 4);
    if (memcmp(ck, "fmt ", 4) == 0) {
      if (size < 16 || pos + 8 + 16 > n) return false;
      w.format = le16(ck + 8);
      w.channels = le16(ck + 10);
      w.rate = le32(ck + 12);
      w.bits = le16(ck + 22);
      haveFmt = true;
    } else if (memcmp(ck, "data", 4) == 0) {
      if (!haveFmt) return false;
      w.dataOffset = uint32_t(pos + 8);
      w.dataSize = size;
      return true;
    }
    // Un bloc qui dépasse le tampon ne peut pas mener à « data » ; sans ce contrôle, une taille
    // délirante fait déborder pos, qui revient en arrière : boucle infinie sur un en-tête corrompu.
    if (size > n - (size_t(pos) + 8)) return false;
    pos += 8 + size + (size & 1);   // les blocs sont alignés sur 2 octets
  }
  return false;
}

// Format jouable tel quel par le torse (I2S 16 kHz, 16 bits).
inline bool playable(const WavInfo& w) {
  return w.format == 1 && w.bits == 16 && w.rate == 16000 && (w.channels == 1 || w.channels == 2);
}

}  // namespace WavFile

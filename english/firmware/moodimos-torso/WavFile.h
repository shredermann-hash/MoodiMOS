// WAV RIFF header reading: format, channels, sample rate, data position.
// Pure logic, tested. Expected PCM 16-bit, 16 kHz, mono/stereo audio.
// *(Piper: `--output-raw` disabled, resampled `.wav` output at 16 kHz.)*
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct WavInfo {
  uint16_t format = 0;       // 1 = PCM
  uint16_t channels = 0;
  uint32_t rate = 0;
  uint16_t bits = 0;
  uint32_t dataOffset = 0;   // Start sample octet in the file
  uint32_t dataSize = 0;     // sample bytes
};

namespace WavFile {

inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

// `buf = start of file (sufficient bytes to reach the "data" block).`
inline bool parse(const uint8_t* buf, size_t n, WavInfo& w) {
  w = WavInfo();
  if (!buf || n < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;
  bool haveFmt = false;
  uint32_t pos = 12;   // 32-bit `size_t` on ESP32: tests show identical overflow behavior
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
    // An oversized block cannot lead to "data" without this check; otherwise, an absurd size causes `pos` to overflow, triggering an infinite loop on a corrupted header.
    if (size > n - (size_t(pos) + 8)) return false;
    pos += 8 + size + (size & 1);   // The blocks are aligned on 2-byte increments.
  }
  return false;
}

// Playable as-is by the torso (I²S, 16 kHz, 16-bit).
inline bool playable(const WavInfo& w) {
  return w.format == 1 && w.bits == 16 && w.rate == 16000 && (w.channels == 1 || w.channels == 2);
}

}  // namespace WavFile

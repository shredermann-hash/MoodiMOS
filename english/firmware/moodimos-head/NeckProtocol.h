// MoodiMOS neck linkage (torso ↔ head): UART 460800 baud, 8N1.
// Frame: `[0xA5][0x5A][type][len][payload…][crc8]`, CRC-8 (poly `0x07`) over `type`, `len`, and `payload`.
// Reference: `README.md`. Identical copy in `moodimos-torso/` and `moodimos-head/`, verified by tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr uint32_t NECK_BAUD = 460800;
constexpr uint8_t NECK_SYNC0 = 0xA5, NECK_SYNC1 = 0x5A;
constexpr uint8_t NECK_MAX_PAYLOAD = 32;
constexpr uint16_t NECK_VERSION = 2;   // v2: HEAD calculated by the TORSO, no CAMERA commands.
constexpr uint8_t NECK_UNKNOWN = 255;  // Unmeasurable battery / charge
constexpr uint32_t NECK_TIMEOUT_MS = 3000;

enum NeckType : uint8_t {
  // head
  NECK_PRESENCE = 0x01,    // NeckPresence
  NECK_EVENT = 0x02,       // NeckEvent
  NECK_ENV = 0x03,         // NeckEnv
  NECK_TIME = 0x04,        // NeckTime
  NECK_SPEAK = 0x05,       // `u8 amplitude; 0 = end`
  NECK_EXPRESSION = 0x06,  // `u8 id, u8 duration × 100 ms`
  // 0x07 (CAMERA, v1) removed: the torso handles OV5640 capture directly.
  NECK_UPDATE = 0x08,      // NECK_UPDATE_MAGIC: Head reboots into its ROM download mode.
  // head → torso
  NECK_HELLO = 0x81,       // NeckHello
  NECK_TOUCH = 0x82,       // `u8 gesture`
  NECK_STATE = 0x84,       // <u8 expression>
  NECK_MOOD = 0x85,        // NeckMood: Mood and colors, relayed via the torso dashboard.
  // torso → head: target calculated from OV5640 camera
  NECK_FACE = 0x83,        // NeckFace
};

// Head NECK_TOUCH gestures (controller tactile CST816 codes), excluding self-triggered tickles. The torso produces laughter and smack (mood cries).
enum NeckGesture : uint8_t { GESTURE_TAP = 0x05, GESTURE_LONG = 0x0C, GESTURE_TICKLE = 0x20 };

// EV_LYING: value 0/1
// EV_TILT: value = inclination × 1000 (−1000 … 1000, > 0 = tilted right when viewed front-on)
// EV_FACE: value = head’s facial event (enum from `FaceEngine.h`), determined by the torso (its touchscreen, moods); numbers in `HeadExpressions.h` (torso side), validated by tests.
enum NeckEventCode : uint8_t { EV_LIFT = 1, EV_SHAKE, EV_LYING, EV_NOISE, EV_DARK, EV_LIGHT, EV_TILT, EV_FACE };

// Programming the head through the neck: in download mode, the ESP32-S3 ROM listens on UART0, connected to GPIO43/44 through the neck wires. The torso relays esptool traffic from its USB port (the 'pont' command, firmware/flash_head_neck.sh). The magic word prevents a corrupted frame from resetting the head.
constexpr uint8_t NECK_UPDATE_MAGIC[4] = {'M', 'A', 'J', '!'};
// The ROM adjusts its speed based on the first received bytes: the torso’s requests at the neck’s speed. The bridge holds it (measured on 2026-10-28: no response at 115200 bauds, sync at 460800).
constexpr uint32_t NECK_UPDATE_BAUD = NECK_BAUD;
inline bool neckIsUpdate(uint8_t type, const uint8_t* payload, uint8_t len) {
  if (type != NECK_UPDATE || len != sizeof(NECK_UPDATE_MAGIC) || !payload) return false;
  for (uint8_t i = 0; i < len; i++)
    if (payload[i] != NECK_UPDATE_MAGIC[i]) return false;
  return true;
}

struct __attribute__((packed)) NeckPresence { uint8_t present; uint16_t distanceCm; };
struct __attribute__((packed)) NeckEvent { uint8_t code; int16_t value; };
struct __attribute__((packed)) NeckEnv {
  int16_t tempC100; uint16_t humPct100; uint16_t pressHpa10; uint16_t lux; uint8_t soundDb;
};
struct __attribute__((packed)) NeckTime { uint32_t unix; int16_t tzMinutes; };
struct __attribute__((packed)) NeckHello { uint16_t version; uint8_t batteryPct; uint8_t charging; };
// FACE: calculated by the torso from OV5640, then sent to the head.
// 
// x > 0 = person to the right when viewing the robot face-on; y > 0 = lower.
struct __attribute__((packed)) NeckFace {
  uint8_t seen;   // 0 ou 1
  int8_t x, y;    // −127 … 127
  uint8_t size;   // target width, 0 … 255 image width
  uint8_t id;     // Facial recognition result: 0 = unknown (motion detection: always 0)
};
// Moods (NeoCité laws, robot: only colors, never character names)
enum MoodPhase : uint8_t {
  MOOD_DORT, MOOD_ABSENCE, MOOD_ATTENTIF, MOOD_COLERE, MOOD_SE_CALME, MOOD_TENIR, MOOD_CURIEUX,
  MOOD_APAISE, MOOD_BOUDE, MOOD_PERPLEXE, MOOD_INQUIET, MOOD_JOIE, MOOD_FATIGUE, MOOD_COUNT
};
// MOOD: Head → Torso, 2 Hz on transitions.
// RGB565 dashboard colors:
// - Background, panels, and accent from common facial palette.
struct __attribute__((packed)) NeckMood { uint8_t phase; uint16_t bg; uint16_t panel; uint16_t accent; };
static_assert(sizeof(NeckPresence) == 3 && sizeof(NeckEvent) == 3 && sizeof(NeckEnv) == 9 &&
              sizeof(NeckHello) == 4 && sizeof(NeckFace) == 5 && sizeof(NeckMood) == 7, "Tailles du protocole cou");

inline uint8_t neckCrc8(const uint8_t* d, size_t n, uint8_t crc = 0) {
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
  }
  return crc;
}

// Byte-by-byte decoder; `feed()` returns `true` when a valid frame is complete.
class NeckParser {
 public:
  uint8_t type = 0, len = 0;
  uint8_t payload[NECK_MAX_PAYLOAD];

  void reset() { st_ = 0; idx_ = 0; }

  // A broken circuit must not allow the next connection to absorb after disconnection.
  bool feed(uint8_t b, uint32_t now) {
    if (st_ && uint32_t(now - lastByteAt_) >= 100) reset();
    lastByteAt_ = now;
    return feed(b);
  }

  bool feed(uint8_t b) {
    switch (st_) {
      case 0: st_ = (b == NECK_SYNC0) ? 1 : 0; break;
      case 1: st_ = (b == NECK_SYNC1) ? 2 : (b == NECK_SYNC0 ? 1 : 0); break;
      case 2: type = b; st_ = 3; break;
      case 3:
        len = b; idx_ = 0;
        st_ = (len > NECK_MAX_PAYLOAD) ? 0 : (len ? 4 : 5);
        break;
      case 4:
        payload[idx_++] = b;
        if (idx_ >= len) st_ = 5;
        break;
      case 5: {
        st_ = 0;
        uint8_t hdr[2] = {type, len};
        return b == neckCrc8(payload, len, neckCrc8(hdr, 2));
      }
    }
    return false;
  }

 private:
  uint8_t st_ = 0, idx_ = 0;
  uint32_t lastByteAt_ = 0;
};

// Encode a frame into `out` (length `len + 5`) and return its length.
inline size_t neckEncode(uint8_t type, const void* payload, uint8_t len, uint8_t* out) {
  if (!out || len > NECK_MAX_PAYLOAD || (len && !payload)) return 0;
  out[0] = NECK_SYNC0; out[1] = NECK_SYNC1; out[2] = type; out[3] = len;
  const uint8_t* p = (const uint8_t*)payload;
  for (uint8_t i = 0; i < len; i++) out[4 + i] = p[i];
  out[4 + len] = neckCrc8(out + 4, len, neckCrc8(out + 2, 2));
  return len + 5;
}

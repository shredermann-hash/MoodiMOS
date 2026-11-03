// Liaison du cou MoodiMOS (torse ↔ tête) : UART 460800 bauds, 8N1.
// Trame : [0xA5][0x5A][type][len][payload…][crc8], CRC-8 (poly 0x07) sur type, len et payload.
// Référence : README.md. Copie identique dans moodimos-torse/ et moodimos-tete/,
// vérifiée par les tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr uint32_t NECK_BAUD = 460800;
constexpr uint8_t NECK_SYNC0 = 0xA5, NECK_SYNC1 = 0x5A;
constexpr uint8_t NECK_MAX_PAYLOAD = 32;
constexpr uint16_t NECK_VERSION = 2;   // v2 : FACE calculée par le torse, plus de commande CAMERA
constexpr uint8_t NECK_UNKNOWN = 255;  // batterie / charge non mesurables
constexpr uint32_t NECK_TIMEOUT_MS = 3000;

enum NeckType : uint8_t {
  // torse → tête
  NECK_PRESENCE = 0x01,    // NeckPresence
  NECK_EVENT = 0x02,       // NeckEvent
  NECK_ENV = 0x03,         // NeckEnv
  NECK_TIME = 0x04,        // NeckTime
  NECK_SPEAK = 0x05,       // u8 amplitude, 0 = fin
  NECK_EXPRESSION = 0x06,  // u8 id, u8 durée × 100 ms
  // 0x07 (CAMERA, v1) retiré : le torse gère directement la capture OV5640
  NECK_UPDATE = 0x08,      // NECK_UPDATE_MAGIC : la tête redémarre dans le mode téléchargement de sa ROM
  // tête → torse
  NECK_HELLO = 0x81,       // NeckHello
  NECK_TOUCH = 0x82,       // u8 geste
  NECK_STATE = 0x84,       // u8 expression
  NECK_MOOD = 0x85,        // NeckMood : humeur et couleurs, reprises par le tableau de bord du torse
  // torse → tête : cible calculée depuis la caméra OV5640
  NECK_FACE = 0x83,        // NeckFace
};

// Gestes NECK_TOUCH de la tête : codes du contrôleur tactile CST816, sauf les chatouilles que la tête reconnaît
// elle-même. Le torse en fait le rire et le smack (cris d'humeur).
enum NeckGesture : uint8_t { GESTURE_TAP = 0x05, GESTURE_LONG = 0x0C, GESTURE_TICKLE = 0x20 };

// EV_LYING : value 0/1 ; EV_TILT : value = inclinaison × 1000 (−1000 … 1000, > 0 = penché vers la droite vu de face)
// EV_FACE : value = événement du visage de la tête (enum Event de FaceEngine.h), décidé par le torse
// (son écran tactile, ses humeurs) ; numéros dans HeadExpressions.h côté torse, vérifiés par les tests.
enum NeckEventCode : uint8_t { EV_LIFT = 1, EV_SHAKE, EV_LYING, EV_NOISE, EV_DARK, EV_LIGHT, EV_TILT, EV_FACE };

// Programmation de la tête par le cou : la ROM de l'ESP32-S3, en mode téléchargement, écoute l'UART0,
// c'est-à-dire les GPIO43/44 des fils du cou. Le torse relaie alors esptool depuis son USB (commande
// « pont », firmware/flash_tete_cou.sh). Le mot magique évite qu'une trame abîmée redémarre la tête.
constexpr uint8_t NECK_UPDATE_MAGIC[4] = {'M', 'A', 'J', '!'};
// La ROM règle son débit sur les premiers octets reçus : les demandes du torse, au débit du cou. Le pont
// le garde donc (mesuré le 2026-10-28 : aucune réponse à 115200 bauds, synchronisation à 460800).
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
// FACE : calculée par le torse depuis l'OV5640 puis envoyée à la tête.
// x > 0 = personne à droite quand on regarde le robot en face ; y > 0 = plus bas.
struct __attribute__((packed)) NeckFace {
  uint8_t seen;   // 0 ou 1
  int8_t x, y;    // −127 … 127
  uint8_t size;   // largeur de la cible, 0 … 255 de la largeur d'image
  uint8_t id;     // visage reconnu, 0 = inconnu (détection de mouvement : toujours 0)
};
// Humeurs (lois NeoCité, sur le robot : couleurs seulement, jamais les noms des personnages)
enum MoodPhase : uint8_t {
  MOOD_DORT, MOOD_ABSENCE, MOOD_ATTENTIF, MOOD_COLERE, MOOD_SE_CALME, MOOD_TENIR, MOOD_CURIEUX,
  MOOD_APAISE, MOOD_BOUDE, MOOD_PERPLEXE, MOOD_INQUIET, MOOD_JOIE, MOOD_FATIGUE, MOOD_COUNT
};
// MOOD : tête → torse, 2 Hz et à chaque changement. Couleurs RGB565 du tableau de bord :
// fond, panneaux et accent, tirés de la palette courante du visage.
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

// Décodeur octet par octet ; feed() renvoie true quand une trame valide est complète.
class NeckParser {
 public:
  uint8_t type = 0, len = 0;
  uint8_t payload[NECK_MAX_PAYLOAD];

  void reset() { st_ = 0; idx_ = 0; }

  // Une trame interrompue ne doit pas absorber la suivante après un débranchement.
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

// Encode une trame dans out (taille len + 5) et renvoie sa longueur.
inline size_t neckEncode(uint8_t type, const void* payload, uint8_t len, uint8_t* out) {
  if (!out || len > NECK_MAX_PAYLOAD || (len && !payload)) return 0;
  out[0] = NECK_SYNC0; out[1] = NECK_SYNC1; out[2] = type; out[3] = len;
  const uint8_t* p = (const uint8_t*)payload;
  for (uint8_t i = 0; i < len; i++) out[4 + i] = p[i];
  out[4 + len] = neckCrc8(out + 4, len, neckCrc8(out + 2, 2));
  return len + 5;
}

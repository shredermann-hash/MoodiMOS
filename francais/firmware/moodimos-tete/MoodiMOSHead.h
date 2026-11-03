// Protocole BLE MoodiMOS — tête détachée (pendentif, périphérique) ↔ torse (central).
// Référence : handoff §7 et dossier d'étude §7.
// Copie identique dans moodimos-torse/ et moodimos-tete/, vérifiée par les tests.
// Garde classique (et non #pragma once) : les deux copies peuvent se croiser dans les tests.
#ifndef MOODIMOS_HEAD_H
#define MOODIMOS_HEAD_H
#include <stddef.h>
#include <stdint.h>

#define MOODI_HEAD_SERVICE_UUID "8afd14bb-765a-4f56-a4fa-c07785563060"
#define MOODI_HEAD_RING_UUID    "d348c64e-8819-4f4d-96e3-9aa7bb866c64"   // écriture : durée × 100 ms, 0 = arrêt
#define MOODI_HEAD_STATUS_UUID  "eae93874-57a6-493d-88a1-8fec9106c072"   // lecture + notification
#define MOODI_HEAD_NAME         "MoodiMOS-TETE"

constexpr uint8_t MOODI_UNKNOWN = 255;   // batterie ou charge non mesurée

struct __attribute__((packed)) MoodiHeadStatus {
  uint8_t batteryPct;   // estimation 0 … 100, MOODI_UNKNOWN sinon
  uint8_t charging;     // 0 / 1, MOODI_UNKNOWN sinon
  uint8_t expression;   // Expr de la tête
};
static_assert(sizeof(MoodiHeadStatus) == 3, "statut tête : 3 octets");

// Le statut est aussi glissé dans l'annonce (données constructeur) : le torse le lit en
// balayant, sans se connecter. Identifiant 0xFFFF (réservé aux essais) + « PH » + statut.
constexpr uint16_t MOODI_ADV_COMPANY = 0xFFFF;
constexpr size_t MOODI_ADV_LEN = 2 + 2 + sizeof(MoodiHeadStatus);

inline size_t moodiHeadAdvEncode(const MoodiHeadStatus& s, uint8_t out[MOODI_ADV_LEN]) {
  out[0] = MOODI_ADV_COMPANY & 0xFF;
  out[1] = MOODI_ADV_COMPANY >> 8;
  out[2] = 'P';
  out[3] = 'H';
  out[4] = s.batteryPct;
  out[5] = s.charging;
  out[6] = s.expression;
  return MOODI_ADV_LEN;
}

inline bool moodiHeadAdvDecode(const uint8_t* d, size_t n, MoodiHeadStatus& s) {
  if (!d || n != MOODI_ADV_LEN || d[0] != (MOODI_ADV_COMPANY & 0xFF) || d[1] != (MOODI_ADV_COMPANY >> 8) ||
      d[2] != 'P' || d[3] != 'H')
    return false;
  if ((d[4] > 100 && d[4] != MOODI_UNKNOWN) || (d[5] > 1 && d[5] != MOODI_UNKNOWN)) return false;
  s.batteryPct = d[4];
  s.charging = d[5];
  s.expression = d[6];
  return true;
}

#endif  // MOODIMOS_HEAD_H

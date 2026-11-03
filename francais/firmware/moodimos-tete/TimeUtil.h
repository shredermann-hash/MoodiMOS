// Calcul de dates sans bibliothèque : conversions Unix ↔ calendrier, heure de Paris
// (règle européenne d'heure d'été) et registres de la RTC PCF85063. Testé sur ordinateur.
#pragma once
#include <stdint.h>

namespace TimeUtil {

constexpr uint32_t MIN_VALID_UNIX = 1700000000u;   // 2023 : en dessous, heure non réglée

struct Civil {
  int year;
  unsigned month, day, hour, minute, second;
};

// Jours depuis le 1er janvier 1970 (algorithme de H. Hinnant, calendrier grégorien).
constexpr int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + int64_t(doe) - 719468;
}

inline uint32_t toUnix(const Civil& c) {
  return uint32_t(daysFromCivil(c.year, c.month, c.day) * 86400 + c.hour * 3600 + c.minute * 60 + c.second);
}

inline Civil fromUnix(uint32_t t) {
  int64_t z = t / 86400 + 719468;
  const uint32_t secs = t % 86400;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = unsigned(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  Civil c;
  c.day = doy - (153 * mp + 2) / 5 + 1;
  c.month = mp < 10 ? mp + 3 : mp - 9;
  c.year = int(int64_t(yoe) + era * 400 + (c.month <= 2));
  c.hour = secs / 3600;
  c.minute = secs / 60 % 60;
  c.second = secs % 60;
  return c;
}

// 0 = dimanche … 6 = samedi (le 1er janvier 1970 était un jeudi)
inline unsigned weekday(int64_t daysSinceEpoch) { return unsigned(((daysSinceEpoch % 7) + 11) % 7); }

// Dernier dimanche d'un mois de 31 jours (mars, octobre)
inline unsigned lastSunday(int year, unsigned month) {
  return 31 - weekday(daysFromCivil(year, month, 31));
}

// Europe/Paris : UTC+1 ; UTC+2 du dernier dimanche de mars 01:00 UTC
// au dernier dimanche d'octobre 01:00 UTC.
inline int16_t parisOffsetMinutes(uint32_t unix) {
  const int y = fromUnix(unix).year;
  const uint32_t start = toUnix({y, 3, lastSunday(y, 3), 1, 0, 0});
  const uint32_t end = toUnix({y, 10, lastSunday(y, 10), 1, 0, 0});
  return (unix >= start && unix < end) ? 120 : 60;
}

// Heure locale de Paris saisie à la main → UTC
inline uint32_t parisLocalToUnix(const Civil& local) {
  const uint32_t asUtc = toUnix(local);
  return asUtc - parisOffsetMinutes(asUtc - 3600) * 60;
}

inline uint8_t toBcd(unsigned v) { return uint8_t(((v / 10) << 4) | (v % 10)); }
inline unsigned fromBcd(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }

// PCF85063, registres 0x04 à 0x0A : secondes (bit 7 = OS, horloge interrompue), minutes,
// heures (24 h), jour, jour de semaine, mois, année 00-99.
inline void civilToRtc(const Civil& c, uint8_t r[7]) {
  r[0] = toBcd(c.second);        // OS remis à 0
  r[1] = toBcd(c.minute);
  r[2] = toBcd(c.hour);
  r[3] = toBcd(c.day);
  r[4] = uint8_t(weekday(daysFromCivil(c.year, c.month, c.day)));
  r[5] = toBcd(c.month);
  r[6] = toBcd(unsigned(c.year - 2000));
}

// Faux si l'horloge s'est arrêtée (OS) ou si les valeurs sont incohérentes.
inline bool rtcToCivil(const uint8_t r[7], Civil& c) {
  if (r[0] & 0x80) return false;
  c.second = fromBcd(r[0] & 0x7F);
  c.minute = fromBcd(r[1] & 0x7F);
  c.hour = fromBcd(r[2] & 0x3F);
  c.day = fromBcd(r[3] & 0x3F);
  c.month = fromBcd(r[5] & 0x1F);
  c.year = 2000 + int(fromBcd(r[6]));
  return c.second < 60 && c.minute < 60 && c.hour < 24 && c.day >= 1 && c.day <= 31 &&
         c.month >= 1 && c.month <= 12;
}

}  // namespace TimeUtil

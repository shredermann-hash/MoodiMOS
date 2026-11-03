#include "TimeSource.h"
#include "TimeUtil.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <time.h>

namespace TimeSource {

namespace {
constexpr uint8_t RTC_ADDR = 0x51;
constexpr uint32_t WIFI_TIMEOUT_MS = 60000;
constexpr uint32_t RESYNC_MS = 24UL * 3600 * 1000;
constexpr uint32_t RETRY_MS = 3600UL * 1000;
constexpr uint32_t REBASE_MS = 24UL * 3600 * 1000;   // reste loin du rebouclage de millis()

uint32_t baseUnix = 0, baseMs = 0, rev = 0;
bool valid = false, fromNtp = false;
bool hasCredentials = false, wifiOn = false;
uint32_t wifiStartedAt = 0, nextSyncAt = 0;
String ssid, password;
volatile bool ntpSynced = false;
char statusText[64];

bool rtcRead(TimeUtil::Civil& c) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return false;
  uint8_t r[7];
  for (auto& b : r) b = Wire.read();
  return TimeUtil::rtcToCivil(r, c);
}

void rtcWrite(uint32_t unix) {
  uint8_t r[7];
  TimeUtil::civilToRtc(TimeUtil::fromUnix(unix), r);
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write(r, sizeof(r));
  Wire.endTransmission();
}

void setBase(uint32_t unix, bool writeRtc) {
  baseUnix = unix;
  baseMs = millis();
  valid = true;
  rev++;
  if (writeRtc) rtcWrite(unix);
}

void onNtp(struct timeval*) { ntpSynced = true; }

void startWifi() {
  ntpSynced = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  sntp_set_time_sync_notification_cb(onNtp);
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  wifiStartedAt = millis();
  wifiOn = true;
}

void stopWifi() {
  if (!wifiOn) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
}
}  // namespace

void begin() {
  TimeUtil::Civil c;
  if (rtcRead(c)) {
    uint32_t u = TimeUtil::toUnix(c);
    if (u >= TimeUtil::MIN_VALID_UNIX) setBase(u, false);
  }
  Preferences prefs;
  prefs.begin("moodimos", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  prefs.end();
  hasCredentials = ssid.length() > 0;
  nextSyncAt = millis();
}

void update() {
  uint32_t nowMs = millis();
  if (valid && nowMs - baseMs >= REBASE_MS) {
    uint32_t elapsed = (nowMs - baseMs) / 1000;
    baseUnix += elapsed;
    baseMs += elapsed * 1000;
  }
  if (!wifiOn) {
    if (hasCredentials && int32_t(nowMs - nextSyncAt) >= 0) startWifi();
    return;
  }
  time_t t = time(nullptr);
  if (ntpSynced && t >= time_t(TimeUtil::MIN_VALID_UNIX)) {
    setBase(uint32_t(t), true);
    fromNtp = true;
    stopWifi();
    nextSyncAt = nowMs + RESYNC_MS;
  } else if (nowMs - wifiStartedAt >= WIFI_TIMEOUT_MS) {
    stopWifi();
    nextSyncAt = nowMs + RETRY_MS;
  }
}

bool now(uint32_t& unixUtc) {
  if (!valid) return false;
  unixUtc = baseUnix + (millis() - baseMs) / 1000;
  return true;
}

uint32_t revision() { return rev; }

bool setManual(uint32_t unixUtc) {
  if (unixUtc < TimeUtil::MIN_VALID_UNIX) return false;
  setBase(unixUtc, true);
  fromNtp = false;
  return true;
}

bool setWifi(const char* newSsid, const char* newPassword) {
  if (!newSsid || !*newSsid || !newPassword) return false;
  Preferences prefs;
  prefs.begin("moodimos", false);
  prefs.putString("ssid", newSsid);
  prefs.putString("pass", newPassword);
  prefs.end();
  ssid = newSsid;
  password = newPassword;
  hasCredentials = true;
  stopWifi();
  nextSyncAt = millis();
  return true;
}

void forgetWifi() {
  Preferences prefs;
  prefs.begin("moodimos", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  ssid = "";
  password = "";
  hasCredentials = false;
  stopWifi();
}

uint32_t timerEnd() {
  Preferences prefs;
  prefs.begin("moodimos", true);
  const uint32_t end = prefs.getUInt("minuteur", 0);
  prefs.end();
  return end;
}

void setTimerEnd(uint32_t unixUtc) {
  Preferences prefs;
  prefs.begin("moodimos", false);
  prefs.putUInt("minuteur", unixUtc);
  prefs.end();
}

uint8_t wifi() { return !hasCredentials ? 0 : fromNtp ? 2 : 1; }

const char* status() {
  const char* src = !valid ? "heure inconnue" : fromNtp ? "heure NTP" : "heure RTC ou manuelle";
  const char* net = !hasCredentials ? "Wi-Fi non configuré" : wifiOn ? "synchronisation en cours" : "Wi-Fi en veille";
  snprintf(statusText, sizeof(statusText), "%s, %s", src, net);
  return statusText;
}

}  // namespace TimeSource

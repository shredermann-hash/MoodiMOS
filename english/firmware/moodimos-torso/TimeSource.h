// Torso time: RTC PCF85063 at startup, NTP sync via Wi-Fi (once daily), manual adjustment via monitor. Wi-Fi stays on only during sync. Wi-Fi credentials stored in NVS flash memory, never displayed.
#pragma once
#include <stdint.h>

namespace TimeSource {

void begin();
void update();
bool now(uint32_t& unixUtc);      // false until the time is known
uint32_t revision();              // At each realignment: the head is warned.
bool setManual(uint32_t unixUtc);
bool setWifi(const char* ssid, const char* password);
void forgetWifi();
const char* status();
uint8_t wifi();                   // dashboard icon: 0 no Wi-Fi saved, 1 saved, 2 time received from the network
uint32_t timerEnd();              // End of timer (complete edition) in Unix time, stored in flash; 0: none.
void setTimerEnd(uint32_t unixUtc);

}  // <source> namespace TimeSource </source>

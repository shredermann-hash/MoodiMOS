// Detachable head: reads its status from its announcements and sends a beep.
// Rotates in its own task; the wait logic is in HeadFinder.h (tested).
#pragma once
#include <stdint.h>
#include "HeadFinder.h"

namespace HeadLink {

void begin();                       // Initialize the torso Bluetooth module.
void setActive(bool headAway);      // Check the head only if it is not already mounted on the torso.
bool status(MoodiHeadStatus& s, uint32_t& ageMs);
void ring(uint8_t tenths);          // "Where is my head?" – duration × 100 ms
HeadFinder::Ring ringState();

}  // <source> namespace HeadLink </source>

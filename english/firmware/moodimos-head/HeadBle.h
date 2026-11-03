// Detached head → Bluetooth: MoodiMOS-TETE peripheral, MOODI_HEAD service (MoodiMOSHead.h).
// Status is in the announcement (read by the torso without connection) and HEAD_STATUS.
// RING receives a ring request.
#pragma once
#include <stdint.h>
#include "MoodiMOSHead.h"

namespace HeadBle {

void begin();                              // Once only; Bluetooth remains initialized.
void setAdvertising(bool on);
void setStatus(const MoodiHeadStatus& s);
bool takeRing(uint8_t& tenths);            // `True once per received request (0 = stop)`
bool connected();                          // A central unit (the torso) is connected.

}  // <source> namespace HeadBle </source>

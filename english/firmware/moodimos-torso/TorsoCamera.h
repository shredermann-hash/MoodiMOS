// OV5640 camera on the torso (3.5-C kit, 24-pin ribbon cable): motion tracking (MotionTracker.h),
// output formatted as the FACE** frame of the neck. Replaces the V0.2 XIAO.
// 
// A task on core 0 powers the camera on demand, captures grayscale images at 160 × 120 pixels (4×4 pixel blocks) and retains the latest result. The main loop reads it and sends it to the head at ≤25 Hz.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "NeckProtocol.h"

namespace TorsoCamera {

void begin();                              // Create task; camera stopped.
void setActive(bool on);                   // someone in front, head in place
bool running();                            // sensor initialized
bool latest(NeckFace& face, uint32_t& at); // true once for each new result
const char* status();                      // Command: `camera`
uint32_t frames();                         // Received images since the startup (test bench)
uint8_t meanLuma();
int movingCells();                         // Moving cases in the last image (bench) // average brightness 0-255 of the last image (bench)

struct Gaze { float gainX, gainY, centerX, centerY; };   // See `MotionTracker::Config`
void relearn();                            // The robot moved: base to relearn after being immobile.
Gaze gaze();                               // gaze adjustment, using the regard command
void setGaze(const Gaze& g);

}  // <source> namespace TorsoCamera </source>

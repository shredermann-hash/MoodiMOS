// 480×480 round screen: ST7701 in 16-bit RGB, double buffering via PSRAM.
// Draws in `backBuffer()`; `present()` flips on the next frame return.
#pragma once
#include <stdint.h>

namespace Display {

constexpr int WIDTH = 480;
constexpr int HEIGHT = 480;

bool begin();              // `Board::begin()` must have been called beforehand.
uint16_t* backBuffer();
void present();
void sleep();              // screen off (ST7701 in light sleep) before deep sleep.

}  // Display namespace

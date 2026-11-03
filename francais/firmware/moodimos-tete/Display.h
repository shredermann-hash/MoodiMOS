// Écran rond 480×480 : ST7701 en RGB 16 bits, double tampon en PSRAM.
// On dessine dans backBuffer() puis present() bascule au prochain retour de trame.
#pragma once
#include <stdint.h>

namespace Display {

constexpr int WIDTH = 480;
constexpr int HEIGHT = 480;

bool begin();              // Board::begin() doit avoir été appelé avant
uint16_t* backBuffer();
void present();
void sleep();              // écran éteint (ST7701 en veille) avant la veille profonde

}  // namespace Display

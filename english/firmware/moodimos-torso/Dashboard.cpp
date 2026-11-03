#include "Dashboard.h"
#include "DashboardNeon.h"
#include "Board.h"
#include "MoodiMOSEdition.h"
#include <Arduino_GFX_Library.h>
#include <math.h>

namespace Dashboard {

namespace {
constexpr int W = 320, H = 480;

// RGB565 colors (no head input):
// - Dark blue
// - Neutral (complete edition has its own background, DashboardNeon)
constexpr uint16_t BG = 0x0884;
constexpr uint16_t PANEL = 0x10E8;
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t MUTED = 0x8C71;
constexpr uint16_t OK = 0x3EA9;
constexpr uint16_t WARN = 0xFD20;
constexpr uint16_t C_TEMP = 0xE8E4, C_HUM = 0x2D1F, C_PRESS = 0x2DAA, C_LUX = 0xFE60, C_SOUND = 0x8A7F;
constexpr uint16_t NEON_BG = 0x0000;   // Dark background of the complete edition, as the head’s display (DashboardNeon.cpp).
constexpr int CRIES_X = 262, CRIES_Y = 379, CRIES_R = 24;   // button for loudness/volume control, right of the sound level indicator
constexpr int TALK_X = 262, TALK_Y = 99, TALK_R = 24;       // Bubble button, to the right of the temperature display

Arduino_DataBus* bus = new Arduino_ESP32SPI(Board::PIN_LCD_DC, GFX_NOT_DEFINED, Board::PIN_LCD_SCK,
                                            Board::PIN_LCD_MOSI, Board::PIN_LCD_MISO);
Arduino_ST7796* panel = new Arduino_ST7796(bus, GFX_NOT_DEFINED, 0, true, W, H);
Arduino_Canvas* gfx = new Arduino_Canvas(W, H, panel);

// Integrated 5×7 character set for ASCII codes 437 page: `0x82` = é, `0x85` = à, `0x88` = ê, `0xF8` = °
void text(int x, int y, const char* s, uint8_t size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

int textWidth(const char* s, uint8_t size) { return strlen(s) * 6 * size; }

enum class Icon : uint8_t { Thermo, Drop, Gauge, Sun, Speaker, Muted };

// Icons in a 40×40 cell (x, y = top-left corner), drawn using the buffer primitives—no image storage required. `bg` for cutouts; `aux` (0–1) adjusts the dial needle.
void icon(Icon kind, int x, int y, uint16_t col, uint16_t bg, float aux) {
  const int cx = x + 20, cy = y + 20;
  switch (kind) {
    case Icon::Thermo: {
      gfx->fillRoundRect(cx - 6, y + 1, 12, 28, 6, col);
      gfx->fillCircle(cx, y + 31, 9, col);
      gfx->fillRoundRect(cx - 3, y + 4, 6, 25, 3, bg);       // hollow tube
      gfx->fillRoundRect(cx - 2, y + 15, 4, 17, 2, col);      // Mercury
      gfx->fillCircle(cx, y + 31, 5, col);
      for (int i = 0; i < 3; i++) gfx->drawFastHLine(cx + 9, y + 6 + i * 6, i == 1 ? 5 : 8, col);
      break;
    }
    case Icon::Drop: {
      gfx->fillTriangle(cx, y + 1, cx - 12, y + 25, cx + 12, y + 25, col);
      gfx->fillCircle(cx, y + 26, 12, col);
      gfx->fillCircle(cx - 5, y + 27, 3, TEXT);               // reflection
      break;
    }
    case Icon::Gauge: {
      gfx->fillCircle(cx, cy, 19, col);
      gfx->fillCircle(cx, cy, 15, bg);
      for (int i = 0; i <= 6; i++) {                          // graduations over 270°
        float a = (135 + 45 * i) * 0.0174533f;
        gfx->drawLine(cx + int(cosf(a) * 12), cy + int(sinf(a) * 12), cx + int(cosf(a) * 15), cy + int(sinf(a) * 15), col);
      }
      float f = aux < 0 ? 0 : aux > 1 ? 1 : aux;
      float a = (135 + 270 * f) * 0.0174533f;
      int nx = cx + int(cosf(a) * 11), ny = cy + int(sinf(a) * 11);
      gfx->drawLine(cx, cy, nx, ny, TEXT);
      gfx->drawLine(cx + 1, cy, nx + 1, ny, TEXT);
      gfx->fillCircle(cx, cy, 3, col);
      break;
    }
    case Icon::Sun: {
      gfx->fillCircle(cx, cy, 9, col);
      for (int i = 0; i < 8; i++) {
        float a = i * 0.785398f;
        float c = cosf(a), s = sinf(a);
        for (int k = -1; k <= 1; k++)                          // radii of 3 px thickness
          gfx->drawLine(cx + int(c * 13) + k * (int)fabsf(s), cy + int(s * 13) + k * (int)fabsf(c),
                        cx + int(c * 19) + k * (int)fabsf(s), cy + int(s * 19) + k * (int)fabsf(c), col);
      }
      break;
    }
    case Icon::Speaker:
    case Icon::Muted: {
      gfx->fillRect(x + 3, y + 14, 9, 12, col);
      gfx->fillTriangle(x + 11, y + 14, x + 23, y + 5, x + 23, y + 35, col);
      gfx->fillTriangle(x + 11, y + 14, x + 11, y + 26, x + 23, y + 35, col);
      if (kind == Icon::Muted) {                               // silent: orange cross instead of waves
        for (int k = -1; k <= 1; k++) {
          gfx->drawLine(x + 28 + k, y + 14, x + 38 + k, y + 26, WARN);
          gfx->drawLine(x + 28 + k, y + 26, x + 38 + k, y + 14, WARN);
        }
        break;
      }
      for (int r = 9; r <= 17; r += 8)                         // two waves
        for (int d = -50; d <= 50; d += 4) {
          float a = d * 0.0174533f;
          gfx->fillCircle(x + 22 + int(cosf(a) * r), cy + int(sinf(a) * r), 1, col);
        }
      break;
    }
  }
}

// Measurement: centered case, icon, then value and unit—no label (icon suffices for identification).
void row(int y, uint16_t panelColor, Icon kind, uint16_t color, float aux, const char* value, const char* unit, bool valid) {
  gfx->fillRoundRect(12, y, W - 24, 62, 10, panelColor);
  icon(kind, 22, y + 11, valid ? color : MUTED, panelColor, aux);
  const char* v = valid ? value : "--";
  text(76, y + 21, v, 3, TEXT);
  text(76 + textWidth(v, 3) + 6, y + 28, unit, 2, MUTED);
}

// Emotion button (screaming):
// White speaker on accent color, or crossed with a cross when silent.
void criesButton(bool on, uint16_t acc) {
  if (on) gfx->fillCircle(CRIES_X, CRIES_Y, CRIES_R, acc);
  else for (int r = CRIES_R - 1; r <= CRIES_R; r++) gfx->drawCircle(CRIES_X, CRIES_Y, r, MUTED);
  icon(on ? Icon::Speaker : Icon::Muted, CRIES_X - 21, CRIES_Y - 20, on ? TEXT : MUTED, acc, 0);
}

// Bubble button: speak to MoodiMOS (computer conversation, brain/robot_conversation.py).
// Grey when no brain, outlined in accent color when ready, filled during exchange.
void talkButton(uint8_t brain, uint16_t acc, uint16_t panel) {
  if (brain == 2) gfx->fillCircle(TALK_X, TALK_Y, TALK_R, acc);
  else for (int r = TALK_R - 2; r <= TALK_R; r++) gfx->drawCircle(TALK_X, TALK_Y, r, brain ? acc : MUTED);
  const uint16_t col = brain ? TEXT : MUTED, hole = brain == 2 ? acc : panel;
  gfx->fillEllipse(TALK_X, TALK_Y - 3, 14, 10, col);
  gfx->fillTriangle(TALK_X - 10, TALK_Y + 2, TALK_X - 2, TALK_Y + 5, TALK_X - 13, TALK_Y + 13, col);   // queue
  for (int i = -1; i <= 1; i++) gfx->fillCircle(TALK_X + i * 6, TALK_Y - 3, 2, hole);
}

void batteryIcon(int x, int y, uint8_t pct, bool charging) {
  uint16_t c = pct > 20 ? OK : WARN;
  gfx->drawRoundRect(x, y, 34, 16, 3, TEXT);
  gfx->fillRect(x + 34, y + 5, 3, 6, TEXT);
  gfx->fillRect(x + 2, y + 2, 30 * pct / 100, 12, c);
  if (charging) text(x - 14, y + 1, "+", 2, OK);
}
}  // <source> namespace </source>

bool begin() {
  Board::lcdReset();
  // The 3.5" card waits for SPI in mode 0 (examples and factory firmware from Waveshare); the latest library defaults to mode 3, causing the screen to remain black. The canvas does not restart the display.
  if (!panel->begin(GFX_NOT_DEFINED, SPI_MODE0) || !gfx->begin(GFX_SKIP_OUTPUT_BEGIN)) return false;
  gfx->fillScreen(Edition::COMPLETE ? NEON_BG : BG);
  gfx->flush();
  Board::setBacklight(true);
  return true;
}

void draw(const DashData& d, const DashTheme& th) {
  if (Edition::COMPLETE) {   // same dashboard, neon-traced like the face of the head.
    DashboardNeon::draw(gfx->getFramebuffer(), d, th);
    gfx->flush();
    return;
  }
  char buf[24];
  const uint16_t bg = th.valid ? th.bg : BG, pn = th.valid ? th.panel : PANEL, acc = th.valid ? th.accent : C_HUM;
  gfx->fillScreen(bg);

  // Header
  text(16, 18, "MoodiMOS", 3, TEXT);
  // Wi-Fi halfway between the end of the title and the CAM light: a dot and two waves, lit when the time came
  // from the network
  if (d.wifi) {
    const int x = (16 + textWidth("MoodiMOS", 3) + 200) / 2;
    const uint16_t c = d.wifi == 2 ? TEXT : MUTED;
    gfx->fillCircle(x, 36, 2, c);
    gfx->fillArc(x, 36, 7, 5, 225, 315, c);
    gfx->fillArc(x, 36, 12, 10, 225, 315, c);
  }
  gfx->fillCircle(200, 29, 7, d.cameraOn ? acc : pn);
  text(212, 24, "CAM", 1, d.cameraOn ? TEXT : MUTED);
  // V1: No battery, AXP2101 gauge irrelevant → display source (USB)
  if (d.powerValid && d.battery) batteryIcon(W - 52, 21, d.batteryPct, d.charging || d.usb);
  else if (d.powerValid && d.usb) text(W - 16 - textWidth("USB", 2), 26, "USB", 2, TEXT);
  gfx->drawFastHLine(12, 56, W - 24, th.valid ? acc : PANEL);

  // Measurements
  int y = 68;
  snprintf(buf, sizeof(buf), "%.1f", d.tempC);
  row(y, pn, Icon::Thermo, C_TEMP, 0, buf, "\xF8" "C", d.climateValid); y += 70;
  snprintf(buf, sizeof(buf), "%.0f", d.humPct);
  row(y, pn, Icon::Drop, C_HUM, 0, buf, "%", d.humidityValid); y += 70;
  snprintf(buf, sizeof(buf), "%.0f", d.pressHpa);
  row(y, pn, Icon::Gauge, C_PRESS, (d.pressHpa - 960.0f) / 80.0f, buf, "hPa", d.climateValid); y += 70;
  snprintf(buf, sizeof(buf), "%u", d.lux);
  row(y, pn, Icon::Sun, C_LUX, 0, buf, "lx", d.lightValid); y += 70;
  snprintf(buf, sizeof(buf), "%u", d.soundDb);
  row(y, pn, Icon::Speaker, C_SOUND, 0, buf, "dB", d.soundValid); y += 70;
  criesButton(d.criesOn, acc);
  talkButton(d.brain, acc, pn);

  // Status of the robot
  y += 4;
  if (d.headDocked) {
    if (!d.headOnline) snprintf(buf, sizeof(buf), "T\x88te : sans liaison");
    else if (d.headBatteryPct >= 0 && d.headCharging) snprintf(buf, sizeof(buf), "T\x88te : en charge %d%%", d.headBatteryPct);
    else if (d.headBatteryPct >= 0) snprintf(buf, sizeof(buf), "T\x88te : en place %d%%", d.headBatteryPct);
    else snprintf(buf, sizeof(buf), "T\x88te : connect\x82" "e");
    if (d.accessMin) snprintf(buf, sizeof(buf), "Acc\x8As ouvert : %u min", d.accessMin);
    text(16, y, buf, 2, d.accessMin || !d.headOnline ? WARN : TEXT);
  } else {
    // Pendant: Last Bluetooth status and ring button replacing the radar.
    if (d.awayStatus && d.awayBatteryPct <= 100)
      snprintf(buf, sizeof(buf), "T\x88te : partie %u%%", d.awayBatteryPct);
    else
      snprintf(buf, sizeof(buf), "T\x88te : partie");
    if (d.accessMin) snprintf(buf, sizeof(buf), "Acc\x8As ouvert : %u min", d.accessMin);
    text(16, y, buf, 2, WARN);
    if (d.awayStatus) {
      if (d.awaySeenS < 60) snprintf(buf, sizeof(buf), "vue il y a %lus", (unsigned long)d.awaySeenS);
      else snprintf(buf, sizeof(buf), "vue il y a %lumin", (unsigned long)(d.awaySeenS / 60));
      text(W - 16 - textWidth(buf, 1), y + 4, buf, 1, MUTED);
    }
    static const char* const RING_TEXT[] = {"O\x97 est ma t\x88te ?", "Appel en cours...", "Elle sonne !", "Introuvable"};
    const char* label = RING_TEXT[d.ringState < 4 ? d.ringState : 0];
    gfx->fillRoundRect(12, 446, W - 24, 30, 8, d.ringState == 3 ? WARN : acc);
    text((W - textWidth(label, 2)) / 2, 454, label, 2, TEXT);
    gfx->flush();
    return;
  }
  y += 24;
  if (!d.radarOnline && !d.radarPresent) {
    text(16, y, "Radar : --", 2, MUTED);
  } else if (d.radarPresent) {
    if (d.radarOnline && d.radarCm) {   // distance: pitch with the OUT connector (V1)
      snprintf(buf, sizeof(buf), "Radar : pr\x82sence %.1f m", d.radarCm / 100.0f);
      text(16, y, buf, 2, TEXT);
    } else {
      text(16, y, "Radar : pr\x82sence", 2, TEXT);
    }
  } else {
    text(16, y, "Radar : absence", 2, MUTED);
  }

  gfx->flush();
}

// Same rendering as `drawWorried` in `firmware/preview/moodimos_preview.html`
void drawWorried(const DashTheme& th, float t) {
  if (Edition::COMPLETE) { DashboardNeon::drawWorried(gfx->getFramebuffer(), th, t); gfx->flush(); return; }
  const uint16_t acc = th.valid ? th.accent : TEXT;
  gfx->fillScreen(th.valid ? th.bg : BG);
  int look = int(sinf(t * 7) * 14);   // The head seeks the torso.
  for (int s = -1; s <= 1; s += 2) {
    int cx = 160 + s * 62, cy = 200;
    gfx->fillEllipse(cx, cy, 42, 50, 0xF7DF);
    gfx->fillCircle(cx + look, cy - 6, 17, 0x1082);
    // eyebrow: inner raised edge (quadratic Bézier curve, bold line)
    float x0 = cx - s * 34, y0 = cy - 88, xc = cx, yc = cy - 78, x1 = cx + s * 36, y1 = cy - 62;
    for (int i = 0; i <= 24; i++) {
      float u = i / 24.0f, v = 1 - u;
      gfx->fillCircle(int(v * v * x0 + 2 * u * v * xc + u * u * x1), int(v * v * y0 + 2 * u * v * yc + u * u * y1), 4, acc);
    }
  }
  for (int x = -40; x <= 40; x += 2)   // Wavy mouthpiece
    gfx->fillCircle(160 + x, int(318 + sinf(x / 9.0f + t * 10) * 5), 4, acc);
  gfx->flush();
}

// Same rendering as `drawJoy` in `firmware/preview/moodimos_preview.html`. Fixed colors: gold of φ, the one the head adopts in its joy (its MOOD pattern is not yet applied at the casting stage).
void drawJoy(float t) {
  if (Edition::COMPLETE) { DashboardNeon::drawJoy(gfx->getFramebuffer(), t); gfx->flush(); return; }
  constexpr uint16_t JOY_BG = 0x3161, GOLD = 0xFE8C, CHEEK = 0xFBD2, MOUTH_IN = 0x4862, TONGUE_C = 0xE28B;
  gfx->fillScreen(JOY_BG);
  for (int i = 0; i < 10; i++) {   // sparking, spinning, flickering lights.
    float a = i * 0.628f + t * 0.8f;
    int x = 160 + int(cosf(a) * 138), y = 230 + int(sinf(a) * 190);
    int s = int(8 + 10 * fabsf(sinf(t * 6 + i * 1.3f))), w = s / 3 + 1;
    gfx->fillTriangle(x, y - s, x - w, y, x + w, y, TEXT);
    gfx->fillTriangle(x, y + s, x - w, y, x + w, y, TEXT);
    gfx->fillTriangle(x - s, y, x, y - w, x, y + w, TEXT);
    gfx->fillTriangle(x + s, y, x, y - w, x, y + w, TEXT);
  }
  int hop = int(-fabsf(sinf(t * 8.7f)) * 22);   // ≈ 3 jumps per second
  for (int s = -1; s <= 1; s += 2) {
    int cx = 160 + s * 62, cy = 200 + hop;
    for (int i = 0; i <= 24; i++) {   // smiling eyes: curved (arc) ∩ thick
      float u = 3.14159f * i / 24;
      gfx->fillCircle(cx - int(cosf(u) * 34), cy - int(sinf(u) * 26), 6, GOLD);
    }
    gfx->fillEllipse(cx + s * 22, cy + 58, 24, 13, CHEEK);   // cheekbones
  }
  // open wide grin: semi-ellipse, tongue, gold outline
  int mx = 160, my = 282 + hop;
  gfx->fillRect(mx - 70, my - 5, 141, 6, GOLD);
  for (int x = -66; x <= 66; x++) {
    int h = int(sqrtf(1 - (x / 70.0f) * (x / 70.0f)) * 62);
    gfx->drawFastVLine(mx + x, my, h, MOUTH_IN);
  }
  gfx->fillEllipse(mx, my + 44, 34, 14, TONGUE_C);
  for (int i = 0; i <= 40; i++) {
    float u = 3.14159f * i / 40;
    gfx->fillCircle(mx + int(cosf(u) * 68), my + int(sinf(u) * 62), 4, GOLD);
  }
  gfx->flush();
}

// The head sleeps: the torso sleeps with it, in its head’s colors (eyes closed, *"z"* ascending).
void drawSleep(const DashTheme& th, float t) {
  if (Edition::COMPLETE) { DashboardNeon::drawSleep(gfx->getFramebuffer(), t); gfx->flush(); return; }
  const uint16_t bg = th.valid ? th.bg : BG, acc = th.valid ? th.accent : MUTED;
  gfx->fillScreen(bg);
  int breath = int(sinf(t * 1.2f) * 3);   // slow breathing, like the head (head)
  for (int s = -1; s <= 1; s += 2) {
    int cx = 160 + s * 62, cy = 210 + breath;
    for (int i = 0; i <= 24; i++) {   // closed eyes: thick arcs
      float u = 3.14159f * i / 24;
      gfx->fillCircle(cx - int(cosf(u) * 34), cy + int(sinf(u) * 14), 5, acc);
    }
  }
  for (int x = -16; x <= 16; x += 2)   // small mouth, barely smiling
    gfx->fillCircle(160 + x, 300 + breath + int((1 - x * x / 256.0f) * 4), 3, acc);
  for (int i = 0; i < 3; i++) {   // « Z » components that increase in size
    float k = fmodf(t * 0.35f + i / 3.0f, 1.0f);
    int sz = int(16 + 14 * k), x = 232 + int(k * 20), y = 140 - int(k * 50);
    int pts[4][2] = {{x, y - sz}, {x + sz, y - sz}, {x, y}, {x + sz, y}};
    for (int j = 0; j < 3; j++)
      for (int w = -1; w <= 1; w++) gfx->drawLine(pts[j][0], pts[j][1] + w, pts[j + 1][0], pts[j + 1][1] + w, acc);
  }
  gfx->flush();
}

const uint16_t* framebuffer() { return gfx->getFramebuffer(); }

// Game "Guess the Animal" – single-edition (dashboard lacks a button for other editions).
void drawGame(const GameView& v) {
  if (!Edition::COMPLETE) return;
  DashboardNeon::drawGame(gfx->getFramebuffer(), v);
  gfx->flush();
}

bool playButtonHit(int16_t x, int16_t y) { return Edition::COMPLETE && DashboardNeon::playButtonHit(x, y); }

bool talkButtonHit(int16_t x, int16_t y) {
  if (Edition::COMPLETE) return DashboardNeon::talkButtonHit(x, y);
  const int dx = x - TALK_X, dy = y - TALK_Y;
  return dx * dx + dy * dy <= 40 * 40;   // finger: wider than the drawn button
}

void drawWho() {
  if (!Edition::COMPLETE) return;
  DashboardNeon::drawWho(gfx->getFramebuffer());
  gfx->flush();
}

int whoButtonHit(int16_t x, int16_t y) { return Edition::COMPLETE ? DashboardNeon::whoButtonHit(x, y) : -1; }

void drawTimerDone(float t) {
  if (!Edition::COMPLETE) return;
  DashboardNeon::drawTimerDone(gfx->getFramebuffer(), t);
  gfx->flush();
}

void drawAccess(const char* cmd, float held) {
  if (Edition::COMPLETE) { DashboardNeon::drawAccess(gfx->getFramebuffer(), cmd, held); gfx->flush(); return; }
  gfx->fillScreen(BG);
  const char* title = "Autoriser ?";
  const char* how = "doigt pos\x82 2 secondes";
  const char* no = "r\x82serv\x82 \x85 un adulte";
  text(160 - textWidth(title, 4) / 2, 110, title, 4, WARN);
  text(160 - textWidth(cmd, 3) / 2, 180, cmd, 3, TEXT);
  gfx->drawRect(60, 250, 200, 24, MUTED);
  gfx->fillRect(60, 250, int(200 * held), 24, WARN);
  text(160 - textWidth(how, 2) / 2, 310, how, 2, TEXT);
  text(160 - textWidth(no, 2) / 2, 350, no, 2, WARN);
  gfx->flush();
}

int gameButtonHit(int16_t x, int16_t y) { return Edition::COMPLETE ? DashboardNeon::gameButtonHit(x, y) : -1; }

bool criesButtonHit(int16_t x, int16_t y) {
  if (Edition::COMPLETE) return DashboardNeon::criesButtonHit(x, y);
  const int dx = x - CRIES_X, dy = y - CRIES_Y;
  return dx * dx + dy * dy <= 40 * 40;   // finger: wider than the drawn button
}

bool ringButtonHit(int16_t x, int16_t y) {
  if (Edition::COMPLETE) return DashboardNeon::ringButtonHit(x, y);
  return x >= 12 && x < W - 12 && y >= 440 && y < 480;
}


}  // <source> namespace Dashboard </source>

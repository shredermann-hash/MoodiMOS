#include "TorsoCamera.h"
#include "Board.h"
#include "MotionTracker.h"
#include <Arduino.h>
#include <esp_camera.h>

namespace TorsoCamera {

namespace {
volatile bool want = false, on = false;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
NeckFace result = {0, 0, 0, 0, 0};
uint32_t resultAt = 0;
bool fresh = false;
char state[48] = "arrêtée";
volatile uint32_t frameCount = 0;
volatile uint8_t lumaMean = 0;
volatile int lastCells = 0;
volatile bool relearnPending = false;
volatile uint32_t relearnAt = 0;
constexpr uint32_t RELEARN_AFTER_MS = 300;   // fond repris sur la première image après le dernier mouvement

// Orientation vérifiée sur le robot assemblé le 2026-10-28 (caméra dans le bandeau avant) : x positif
// quand on passe à droite du robot vu de face. Le haut et le bas des yeux étaient inversés : image
// retournée, validé en direct le soir (commande « regard 2.5 -3.5 0 -0.1 », équivalente). La main ne
// balayait qu'environ ±0,3 de l'image : le gain donne toute l'amplitude aux yeux. Centre décalé vers le
// haut de l'image (on y bouge surtout). Lissage plus vif pour que les yeux suivent sans traîner.
MotionTracker::Config trackerConfig() {
  MotionTracker::Config c;
  c.mirrorX = true;
  c.flipY = true;
  c.gainX = 2.5f;
  c.gainY = 3.5f;
  c.centerY = 0.1f;   // après retournement : l'ancien −0,1
  c.smooth = 0.7f;
  return c;
}
MotionTracker tracker(trackerConfig());

bool start() {
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = Board::Cam::XCLK;
  // La caméra partage le contrôleur I²C 0 déjà ouvert par Wire. Deux contrôleurs affectés
  // aux mêmes broches 7/8 donnent des réponses fantômes au scan, même caméra en pause.
  c.pin_sccb_sda = -1;
  c.pin_sccb_scl = -1;
  c.sccb_i2c_port = 0;
  c.pin_d0 = Board::Cam::D0; c.pin_d1 = Board::Cam::D1; c.pin_d2 = Board::Cam::D2; c.pin_d3 = Board::Cam::D3;
  c.pin_d4 = Board::Cam::D4; c.pin_d5 = Board::Cam::D5; c.pin_d6 = Board::Cam::D6; c.pin_d7 = Board::Cam::D7;
  c.pin_vsync = Board::Cam::VSYNC;
  c.pin_href = Board::Cam::HREF;
  c.pin_pclk = Board::Cam::PCLK;
  c.xclk_freq_hz = 10000000;         // comme l'exemple Arduino Waveshare
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size = FRAMESIZE_QQVGA;    // 160 × 120
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;  // toujours l'image la plus récente
  return esp_camera_init(&c) == ESP_OK;
}

// Veille logicielle de l'OV5640 (registre 0x3008, bit 6) : le capteur garde ses réglages et son
// bus SCCB, mais ne produit plus d'images et ne chauffe plus. Le pilote reste initialisé.
void sleepSensor(bool sleep) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s || s->id.PID != OV5640_PID) return;
  s->set_reg(s, 0x3008, 0x40, sleep ? 0x40 : 0x00);
}

void task(void*) {
  uint32_t retryAt = 0;
  bool inited = false;   // le capteur reste initialisé : redémarrer le pilote à chaque présence coupait le bus I2C
  for (;;) {
    if (want && !on) {
      if (!inited) {
        if (millis() < retryAt) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (!start()) {
          esp_camera_deinit();
          retryAt = millis() + 5000;   // nappe débranchée, capteur absent : nouvel essai dans 5 s
          strcpy(state, "échec (nappe ? nouvel essai 5 s)");
          continue;
        }
        inited = true;
      } else {
        sleepSensor(false);             // réveil : les deux images d'avant la veille sont jetées
        vTaskDelay(pdMS_TO_TICKS(50));
        for (int k = 0; k < 2; k++) {
          camera_fb_t* old = esp_camera_fb_get();
          if (old) esp_camera_fb_return(old);
        }
      }
      {
        on = true;
        tracker.reset();
        frameCount = 0;
        strcpy(state, "allumée");
      }
    }
    if (!want && on) {
      on = false;                       // pause : capteur initialisé mais endormi
      sleepSensor(true);
      strcpy(state, "capteur endormi");
      portENTER_CRITICAL(&lock);
      result = {0, 0, 0, 0, 0};
      fresh = false;
      portEXIT_CRITICAL(&lock);
    }
    if (!on) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    uint32_t sum = 0, n = 0;
    for (size_t i = 0; i < fb->len; i += 16, n++) sum += fb->buf[i];
    lumaMean = n ? sum / n : 0;
    frameCount = frameCount + 1;
    NeckFace face;
    uint32_t now = millis();
    // Robot secoué, soulevé ou tourné : la caméra a bougé avec lui, tout le fond appris est faux et les
    // « mouvements » couvrent l'image. On le réapprend une fois le robot de nouveau immobile.
    if (relearnPending && int32_t(now - relearnAt) >= 0) {
      relearnPending = false;
      tracker.reset();
    }
    bool ok = tracker.process(fb->buf, fb->width, fb->height, now, face);
    lastCells = tracker.cells();
    esp_camera_fb_return(fb);
    if (!ok) continue;
    portENTER_CRITICAL(&lock);
    result = face;
    resultAt = now;
    fresh = true;
    portEXIT_CRITICAL(&lock);
  }
}
}  // namespace

void begin() { xTaskCreatePinnedToCore(task, "camera", 12288, nullptr, 1, nullptr, 0); }
void setActive(bool active) { want = active; }
bool running() { return on; }

bool latest(NeckFace& face, uint32_t& at) {
  portENTER_CRITICAL(&lock);
  bool f = fresh;
  face = result;
  at = resultAt;
  fresh = false;
  portEXIT_CRITICAL(&lock);
  return f;
}

const char* status() { return state; }
uint32_t frames() { return frameCount; }
uint8_t meanLuma() { return lumaMean; }
int movingCells() { return lastCells; }
void relearn() {
  relearnAt = millis() + RELEARN_AFTER_MS;
  relearnPending = true;
}
// Écrit depuis la boucle, lu par la tâche caméra : des flottants alignés, une valeur à moitié prise
// ne dure qu'une image.
Gaze gaze() {
  const MotionTracker::Config& c = tracker.config();
  return {c.gainX, c.gainY, c.centerX, c.centerY};
}
void setGaze(const Gaze& g) {
  MotionTracker::Config& c = tracker.config();
  c.gainX = g.gainX;
  c.gainY = g.gainY;
  c.centerX = g.centerX;
  c.centerY = g.centerY;
}

}  // namespace TorsoCamera

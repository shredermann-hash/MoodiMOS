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
constexpr uint32_t RELEARN_AFTER_MS = 300;   // Reused from the first image after the last movement.

// Orientation checked on the assembled robot on 2026-10-28, with the camera in the front upper band:
// x is positive when moving to the right as viewed from the front. The eyes' vertical tracking was
// inverted, so the image was flipped and tested live that evening; the equivalent command was
// “regard 2.5 -3.5 0 -0.1”. A hand covered only about ±0.3 of the image, so gain gives the eyes their
// full movement range. The centre is shifted towards the top of the image, where movement usually
// occurs. Faster smoothing lets the eyes follow without lagging.
MotionTracker::Config trackerConfig() {
  MotionTracker::Config c;
  c.mirrorX = true;
  c.flipY = true;
  c.gainX = 2.5f;
  c.gainY = 3.5f;
  c.centerY = 0.1f;   // after flipping: the previous −0.1
  c.smooth = 0.7f;
  return c;
}
MotionTracker tracker(trackerConfig());

bool start() {
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = Board::Cam::XCLK;
  // The camera shares I²C controller 0, already opened by Wire. Two controllers assigned to the same pins 7/8 produce ghost responses during scans, even when the camera is paused.
  c.pin_sccb_sda = -1;
  c.pin_sccb_scl = -1;
  c.sccb_i2c_port = 0;
  c.pin_d0 = Board::Cam::D0; c.pin_d1 = Board::Cam::D1; c.pin_d2 = Board::Cam::D2; c.pin_d3 = Board::Cam::D3;
  c.pin_d4 = Board::Cam::D4; c.pin_d5 = Board::Cam::D5; c.pin_d6 = Board::Cam::D6; c.pin_d7 = Board::Cam::D7;
  c.pin_vsync = Board::Cam::VSYNC;
  c.pin_href = Board::Cam::HREF;
  c.pin_pclk = Board::Cam::PCLK;
  c.xclk_freq_hz = 10000000;         // as the Arduino Waveshare example.
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size = FRAMESIZE_QQVGA;    // 160 × 120
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;  // always the latest image
  return esp_camera_init(&c) == ESP_OK;
}

// Software monitoring of OV5640 (register `0x3008`, bit 6): the sensor retains its settings and its SCCB bus, but no longer produces images and does not overheat. The driver remains initialized.
void sleepSensor(bool sleep) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s || s->id.PID != OV5640_PID) return;
  s->set_reg(s, 0x3008, 0x40, sleep ? 0x40 : 0x00);
}

void task(void*) {
  uint32_t retryAt = 0;
  bool inited = false;   // The sensor remains initialized: restart driver on each presence cuts the I2C bus.
  for (;;) {
    if (want && !on) {
      if (!inited) {
        if (millis() < retryAt) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (!start()) {
          esp_camera_deinit();
          retryAt = millis() + 5000;   // Disconnected nappe, no sensor: new attempt in 5 s
          strcpy(state, "échec (nappe ? nouvel essai 5 s)");
          continue;
        }
        inited = true;
      } else {
        sleepSensor(false);             // alarm: discard the two images from the previous day.
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
      on = false;                       // pause: sensor initialised but asleep
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
    // Shaken, lifted, or rotated robot: the camera moves with it, all learned background is invalid, and "movements" obscure the image. It must be relearned once the robot is stationary again.
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
}  // <source> namespace </source>

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
// Translated from loop, read by camera task: aligned floats, half-evaluated value
// lasting only one frame.
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

}  // <source> namespace TorsoCamera </source>

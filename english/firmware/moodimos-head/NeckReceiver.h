// Independent UART-free logic for computer testing.
#pragma once
#include "NeckProtocol.h"
#include "FaceEngine.h"
#include "HeadClock.h"
#include <string.h>

class NeckReceiver {
 public:
  explicit NeckReceiver(FaceEngine& face) : face_(face) {}

  bool connected() const { return connected_; }
  bool environmentValid() const { return envValid_; }
  const NeckEnv& environment() const { return env_; }
  bool tracking() const { return tracking_; }       // FACE frames received less than one second ago
  static constexpr uint32_t FACE_TIMEOUT_MS = 1000;  // No frame: lost face
  static constexpr uint32_t NEW_FACE_MS = 2000;
  static constexpr uint32_t DETACH_MS = 1000;        // Head detached (emergency) silence from neck onward.
  static constexpr uint32_t LINE_LOST_MS = 50;       // RX line at 0+: head detached (immediate)

  // RX neck line sampled at each loop iteration. At rest, the torso holds it at 1; head removed, the head pull-down causes it to drop to 0. A single byte at 460800 bauds keeps it at 0 for only µs: 50 ms at 0 is an open contact. Same reflex as the DOCK of the torso.
  void lineLevel(bool high, uint32_t now) {
    if (high) { lineLow_ = false; return; }
    if (!lineLow_) { lineLow_ = true; lowSince_ = now; return; }
    if (connected_ && !detached_ && uint32_t(now - lowSince_) >= LINE_LOST_MS) {
      detached_ = true;
      face_.onEvent(Event::Detached);
    }
  }
  bool detached() const { return detached_; }      // absence of face beyond: awakening sparks curiosity
  // The time persists despite disconnection: the head (head) detached remains a watch.
  HeadClock& clock() { return clock_; }
  const HeadClock& clock() const { return clock_; }

  bool receive(uint8_t type, const uint8_t* data, uint8_t len, uint32_t now) {
    update(now);
    // The PRESENCE heartbeat dictates the connection; other commands cannot maintain it artificially or open it independently.
    if (type == NECK_PRESENCE) {
      if (len != sizeof(NeckPresence) || !data || data[0] > 1) return false;
      NeckPresence p;
      memcpy(&p, data, sizeof(p));
      bool arrival = p.present && (!connected_ || !face_.in.presence);
      bool rejoined = !connected_ || detached_;   // Head reconnects to torso.
      detached_ = false;
      connected_ = true;
      presenceAt_ = now;
      face_.in.presence = p.present;
      // 0 = unknown distance (only OUT mode), no person pressed against the screen.
      face_.in.distance = p.distanceCm ? p.distanceCm / 100.0f : 2.0f;
      if (arrival) face_.onEvent(Event::Arrive);
      if (rejoined) face_.onEvent(Event::Docked);   // extreme joy
      return true;
    }
    if (!connected_ || !data) return false;
    switch (type) {
      case NECK_ENV:
        if (len != sizeof(NeckEnv)) return false;
        memcpy(&env_, data, sizeof(env_));
        envAt_ = now;
        envValid_ = true;
        return true;
      case NECK_EVENT: {
        if (len != sizeof(NeckEvent)) return false;
        NeckEvent e;
        memcpy(&e, data, sizeof(e));
        switch (e.code) {
          case EV_LIFT: face_.in.lying = false; face_.onEvent(Event::Lift); break;
          case EV_SHAKE: face_.onEvent(Event::Shake); break;
          case EV_LYING:
            if (e.value != 0 && e.value != 1) return false;
            if (face_.in.lying && !e.value) face_.onEvent(Event::Lift);
            face_.in.lying = e.value;
            break;
          case EV_NOISE: face_.onEvent(Event::Noise); break;
          case EV_TILT:
            if (e.value < -1000 || e.value > 1000) return false;
            face_.in.tilt = e.value / 1000.0f;
            break;
          case EV_DARK: case EV_LIGHT:
            // Without recent ENV, an old measurement should not put the head to sleep.
            if (!envValid_) return false;
            lightAt_ = now; lightValid_ = true;
            setDark(e.code == EV_DARK);
            break;
          case EV_FACE: {
            // Light and connection remain inherent states here: the torso cannot enforce them.
            if (e.value < 0 || e.value > int16_t(LAST_EVENT)) return false;
            Event ev = Event(e.value);
            if (ev == Event::Dark || ev == Event::Light || ev == Event::Detached || ev == Event::Docked) return false;
            face_.onEvent(ev);
            break;
          }
          default: return false;
        }
        return true;
      }
      case NECK_SPEAK:
        if (len != 1) return false;
        face_.in.talking = data[0] != 0;
        face_.in.talkAmplitude = data[0] / 255.0f;
        speakAt_ = now; speaking_ = true;
        return true;
      case NECK_EXPRESSION:
        if (len != 2 || data[0] >= uint8_t(Expr::Count) || data[1] == 0) return false;
        face_.play(Expr(data[0]), data[1] / 10.0f);
        return true;
      case NECK_FACE: {   // target calculated by the torso from OV5640 sensor
        if (len != sizeof(NeckFace)) return false;
        NeckFace f;
        memcpy(&f, data, sizeof(f));
        if (f.seen > 1) return false;
        // A figure reappears after >2 s without a face: curiosity.
        if (f.seen && (!seenValid_ || uint32_t(now - seenAt_) >= NEW_FACE_MS)) face_.onEvent(Event::NewFace);
        if (f.seen) { seenAt_ = now; seenValid_ = true; }
        face_.in.faceSeen = f.seen;
        face_.in.faceX = norm(f.x);   // The XIAO already mirrors its own image.
        face_.in.faceY = norm(f.y);
        faceAt_ = now;
        tracking_ = true;
        return true;
      }
      case NECK_TIME: {
        if (len != sizeof(NeckTime)) return false;
        NeckTime t;
        memcpy(&t, data, sizeof(t));
        return clock_.set(t.unix, t.tzMinutes, now);
      }
      default: return false;
    }
  }

  void update(uint32_t now) {
    if (envValid_ && uint32_t(now - envAt_) >= 5000) envValid_ = false;
    if (lightValid_ && (!envValid_ || uint32_t(now - lightAt_) >= 5000)) {
      lightValid_ = false;
      setDark(false);
    }
    if (speaking_ && uint32_t(now - speakAt_) >= 500) stopSpeaking();
    // Only ongoing tracking is erased: a simulation on the monitor (eye) remains.
    if (tracking_ && uint32_t(now - faceAt_) >= FACE_TIMEOUT_MS) dropFace();
    // 5 Hz beatings: 1 s pause, head detaches from neck → anxious (inputs drop at 3 s).
    if (connected_ && !detached_ && uint32_t(now - presenceAt_) >= DETACH_MS) {
      detached_ = true;
      face_.onEvent(Event::Detached);
    }
    if (connected_ && uint32_t(now - presenceAt_) >= NECK_TIMEOUT_MS) {
      connected_ = false; envValid_ = false; lightValid_ = false; tracking_ = false;
      face_.in.presence = false;
      face_.in.distance = 2;
      face_.in.lying = false;
      face_.in.tilt = 0;
      face_.in.faceSeen = false;
      setDark(false);
      stopSpeaking();
    }
  }

 private:
  static float norm(int8_t v) { return v < -127 ? -1.0f : v / 127.0f; }
  void dropFace() {
    tracking_ = false;
    face_.in.faceSeen = false;
  }
  void setDark(bool dark) {
    if (face_.in.dark == dark) return;
    face_.in.dark = dark;
    face_.onEvent(dark ? Event::Dark : Event::Light);
    if (!dark && face_.in.presence) face_.onEvent(Event::Arrive);
  }
  void stopSpeaking() {
    speaking_ = false;
    face_.in.talking = false;
    face_.in.talkAmplitude = -1;
  }
  FaceEngine& face_;
  HeadClock clock_;
  NeckEnv env_ = {};
  bool connected_ = false, envValid_ = false, lightValid_ = false;
  bool tracking_ = false, speaking_ = false, seenValid_ = false, detached_ = false, lineLow_ = false;
  uint32_t lowSince_ = 0, seenAt_ = 0, presenceAt_ = 0, envAt_ = 0, lightAt_ = 0, speakAt_ = 0, faceAt_ = 0;
};

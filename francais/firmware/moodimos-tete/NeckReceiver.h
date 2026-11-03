// Logique de réception indépendante de l'UART pour être testée sur ordinateur.
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
  bool tracking() const { return tracking_; }       // trames FACE reçues depuis moins d'une seconde
  static constexpr uint32_t FACE_TIMEOUT_MS = 1000;  // plus de trame : visage perdu
  static constexpr uint32_t NEW_FACE_MS = 2000;
  static constexpr uint32_t DETACH_MS = 1000;        // silence du cou au-delà : tête détachée (secours)
  static constexpr uint32_t LINE_LOST_MS = 50;       // ligne RX à 0 au-delà : tête détachée (immédiat)

  // Ligne RX du cou, échantillonnée à chaque tour de boucle. Au repos, le torse la tient à 1 ;
  // tête retirée, le pull-down de la tête la fait tomber à 0. Un octet à 460800 bauds ne la
  // garde à 0 que quelques µs : 50 ms à 0, c'est un contact ouvert. Même réflexe que le DOCK du torse.
  void lineLevel(bool high, uint32_t now) {
    if (high) { lineLow_ = false; return; }
    if (!lineLow_) { lineLow_ = true; lowSince_ = now; return; }
    if (connected_ && !detached_ && uint32_t(now - lowSince_) >= LINE_LOST_MS) {
      detached_ = true;
      face_.onEvent(Event::Detached);
    }
  }
  bool detached() const { return detached_; }      // absence de visage au-delà : le retour éveille la curiosité
  // L'heure survit à la perte de liaison : la tête détachée reste une montre.
  HeadClock& clock() { return clock_; }
  const HeadClock& clock() const { return clock_; }

  bool receive(uint8_t type, const uint8_t* data, uint8_t len, uint32_t now) {
    update(now);
    // Les battements PRESENCE font autorité sur la liaison ; les autres commandes
    // ne peuvent ni la maintenir artificiellement ni l'ouvrir à elles seules.
    if (type == NECK_PRESENCE) {
      if (len != sizeof(NeckPresence) || !data || data[0] > 1) return false;
      NeckPresence p;
      memcpy(&p, data, sizeof(p));
      bool arrival = p.present && (!connected_ || !face_.in.presence);
      bool rejoined = !connected_ || detached_;   // la tête retrouve le torse
      detached_ = false;
      connected_ = true;
      presenceAt_ = now;
      face_.in.presence = p.present;
      // 0 = distance inconnue (OUT seul), pas une personne collée à l'écran.
      face_.in.distance = p.distanceCm ? p.distanceCm / 100.0f : 2.0f;
      if (arrival) face_.onEvent(Event::Arrive);
      if (rejoined) face_.onEvent(Event::Docked);   // joie extrême
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
            // Sans ENV récent, une ancienne mesure ne doit pas endormir la tête.
            if (!envValid_) return false;
            lightAt_ = now; lightValid_ = true;
            setDark(e.code == EV_DARK);
            break;
          case EV_FACE: {
            // Lumière et liaison restent des états tenus ici : le torse ne peut pas les forcer.
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
      case NECK_FACE: {   // cible calculée par le torse depuis l'OV5640
        if (len != sizeof(NeckFace)) return false;
        NeckFace f;
        memcpy(&f, data, sizeof(f));
        if (f.seen > 1) return false;
        // Quelqu'un réapparaît après plus de 2 s sans visage : curiosité.
        if (f.seen && (!seenValid_ || uint32_t(now - seenAt_) >= NEW_FACE_MS)) face_.onEvent(Event::NewFace);
        if (f.seen) { seenAt_ = now; seenValid_ = true; }
        face_.in.faceSeen = f.seen;
        face_.in.faceX = norm(f.x);   // la XIAO fait déjà le miroir de son image
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
    // Seul un suivi en cours s'efface : une simulation au moniteur (regard) reste.
    if (tracking_ && uint32_t(now - faceAt_) >= FACE_TIMEOUT_MS) dropFace();
    // Battements à 5 Hz : 1 s de silence, la tête a quitté le cou → inquiète (les entrées tombent à 3 s).
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

// « Où est ma tête ? » côté torse : dernier statut vu dans les annonces et demande de
// sonnerie en attente. Logique pure, testée ; HeadLink s'occupe du Bluetooth.
#pragma once
#include <stdint.h>
#include "MoodiMOSHead.h"

class HeadFinder {
 public:
  enum Ring : uint8_t { Idle, Pending, Sent, Failed };
  // La tête en veille profonde n'écoute que 1,5 s toutes les 30 s : on patiente 75 s.
  static constexpr uint32_t RING_TIMEOUT_MS = 75000;
  static constexpr uint32_t RESULT_SHOW_MS = 10000;   // « envoyée » / « introuvable » affichés 10 s

  void seen(const MoodiHeadStatus& s, uint32_t now) {
    status_ = s;
    seenAt_ = now;
    hasStatus_ = true;
  }

  bool status(uint32_t now, MoodiHeadStatus& s, uint32_t& ageMs) const {
    if (!hasStatus_) return false;
    s = status_;
    ageMs = now - seenAt_;
    return true;
  }

  void forget() { hasStatus_ = false; ring_ = Idle; }   // tête revenue sur le cou

  void requestRing(uint8_t tenths, uint32_t now) {
    if (tenths == 0) return;
    tenths_ = tenths;
    ring_ = Pending;
    ringAt_ = now;
  }

  // Une sonnerie attend-elle ? Passe en échec après le délai.
  bool wantsRing(uint32_t now) {
    if (ring_ != Pending) return false;
    if (uint32_t(now - ringAt_) >= RING_TIMEOUT_MS) {
      ring_ = Failed;
      ringAt_ = now;
      return false;
    }
    return true;
  }

  void ringSent(uint32_t now) { ring_ = Sent; ringAt_ = now; }

  Ring ring(uint32_t now) {
    wantsRing(now);
    if ((ring_ == Sent || ring_ == Failed) && uint32_t(now - ringAt_) >= RESULT_SHOW_MS) ring_ = Idle;
    return ring_;
  }

  uint8_t ringTenths() const { return tenths_; }

 private:
  MoodiHeadStatus status_ = {MOODI_UNKNOWN, MOODI_UNKNOWN, 0};
  uint32_t seenAt_ = 0, ringAt_ = 0;
  bool hasStatus_ = false;
  Ring ring_ = Idle;
  uint8_t tenths_ = 0;
};

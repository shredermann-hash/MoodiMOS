// Fin d'une question dite au micro : la personne a parlé, puis s'est tue. Logique pure, testée.
//
// Un niveau par bloc de 50 ms (SoundMeter), comparé au fond sonore appris avant l'écoute : un bloc nettement
// au-dessus est de la parole. La question est finie après un silence de SILENCE_MS qui suit la parole ; sans
// parole, l'écoute s'arrête au bout de WAIT_MS ; une question trop longue est coupée à la durée maximale.
#pragma once
#include <stdint.h>

class SpeechEnd {
 public:
  static constexpr uint32_t BLOCK_MS = 50;
  static constexpr float SPEECH_DB = 10;          // au-dessus du fond : de la parole
  static constexpr float MIN_SPEECH_DBFS = -60;   // pièce très calme : la parole doit au moins atteindre ce niveau
  static constexpr int MIN_SPEECH_BLOCKS = 3;     // 150 ms : un claquement seul n'est pas une question
  static constexpr uint32_t SILENCE_MS = 1200;    // une petite pause dans la phrase ne la coupe pas
  static constexpr uint32_t WAIT_MS = 6000;       // personne ne parle

  void start(float floorDbfs, uint32_t maxMs) {
    threshold_ = floorDbfs + SPEECH_DB > MIN_SPEECH_DBFS ? floorDbfs + SPEECH_DB : MIN_SPEECH_DBFS;
    maxBlocks_ = maxMs / BLOCK_MS;
    blocks_ = speech_ = quiet_ = 0;
    done_ = false;
  }

  // Niveau d'un bloc de 50 ms ; vrai quand l'écoute est finie.
  bool feed(float levelDbfs) {
    if (done_) return true;
    blocks_++;
    if (levelDbfs > threshold_) {
      speech_++;
      quiet_ = 0;
    } else {
      quiet_++;
    }
    done_ = blocks_ >= maxBlocks_ ||
            (heard() ? quiet_ * BLOCK_MS >= SILENCE_MS : blocks_ * BLOCK_MS >= WAIT_MS);
    return done_;
  }

  bool heard() const { return speech_ >= MIN_SPEECH_BLOCKS; }   // il y a eu de la parole
  bool done() const { return done_; }

 private:
  float threshold_ = MIN_SPEECH_DBFS;
  uint32_t maxBlocks_ = 0, blocks_ = 0, speech_ = 0, quiet_ = 0;
  bool done_ = false;
};

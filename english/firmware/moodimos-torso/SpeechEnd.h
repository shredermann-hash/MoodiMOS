// End of a spoken question via microphone: the person speaks, then stops. Pure logic, tested.
// 
// A level per 50 ms block (SoundMeter), compared to the learned ambient noise baseline: a block significantly above is speech. The question ends after a silence of `SILENCE_MS` following speech; without speech, listening stops after `WAIT_MS`; a question too long is truncated at the maximum duration.
#pragma once
#include <stdint.h>

class SpeechEnd {
 public:
  static constexpr uint32_t BLOCK_MS = 50;
  static constexpr float SPEECH_DB = 10;          // above the noise floor: speech
  static constexpr float MIN_SPEECH_DBFS = -60;   // very quiet room: speech must reach at least this level
  static constexpr int MIN_SPEECH_BLOCKS = 3;     // **150 ms: a single *clack* is not a question**
  static constexpr uint32_t SILENCE_MS = 1200;    // A small pause in the sentence does not truncate it.
  static constexpr uint32_t WAIT_MS = 6000;       // no one speaks

  void start(float floorDbfs, uint32_t maxMs) {
    threshold_ = floorDbfs + SPEECH_DB > MIN_SPEECH_DBFS ? floorDbfs + SPEECH_DB : MIN_SPEECH_DBFS;
    maxBlocks_ = maxMs / BLOCK_MS;
    blocks_ = speech_ = quiet_ = 0;
    done_ = false;
  }

  // Block level of 50 ms; true when listening is complete.
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

  bool heard() const { return speech_ >= MIN_SPEECH_BLOCKS; }   // No data provided for translation.
  bool done() const { return done_; }

 private:
  float threshold_ = MIN_SPEECH_DBFS;
  uint32_t maxBlocks_ = 0, blocks_ = 0, speech_ = 0, quiet_ = 0;
  bool done_ = false;
};

// MoodiMOS — firmware for the torso (Waveshare ESP32-S3-Touch-LCD-3.5 kit: enclosure + OV5640)
// 
// Arduino IDE Settings (Tools):
// - Board: ESP32S3 Dev Module
// - PSRAM: OPI PSRAM
// - Flash Size: 16MB (128Mb)
// - Partition: 16M Flash (3MB APP/9.9MB FATFS)
// - USB CDC On Boot: Enabled
// - Library: GFX Library for Arduino (pre-installed)
// 
// V1 (USB power, wiring: study folder V0.3 §3-4):
// - Dashboard: BME280 + BH1750 (I2C), microphone sound level, head, radar, and camera status
// - Radar LD2410C: Presence via OUT pin, read via PCF8574 extender (0x22); no distance (radar settings via Bluetooth, HLKRadarTool app)
// - Camera OV5640 (kit nappe): Turns on when someone is present, head in place; motion tracking (TorsoCamera) generates FACE frames for head tracking
// - Neck (UART on GPIO43/44, DOCK on PCF8574): Head detection, frames to head (presence, measurements, gaze, time, voice)
// - V2 only: SW1/SW2 (5V general/neck) + battery guard (no USB effect)
// - IMU: Raised, shaken, lying, and tilt toward head; RTC time + NTP (TimeSource)
// - Micro ES8311: Loud noise → head jerk
// - Speaker: WAV phrases from TF card or torso flash; head mouth synchronized (SPEAK)
// - Edit mode: "Guess the Animal" game (GuessGame.h), triggered by dashboard toe button
// Upcoming: Wake word, time phrases.

#include <Wire.h>
#include "Board.h"
#include "MoodiMOSEdition.h"
#include "Dashboard.h"
#include "Ambient.h"
#include "TorsoCamera.h"
#include "NeckProtocol.h"
#include "TimeSource.h"
#include "TimeUtil.h"
#include "MotionEvents.h"
#include "Microphone.h"
#include "Speaker.h"
#include "HeadLink.h"
#include "HeadExpressions.h"
#include "MoodTriggers.h"
#include "GuessGame.h"
#include "Household.h"
#include "MoodSounds.h"
#include "TimerVoice.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

HardwareSerial& neck = Serial1;       // On GPIO43/44 (pins of UART0, console routed via USB).
NeckParser neckRx;

constexpr uint32_t DOCK_DEBOUNCE_MS = 300;
constexpr uint32_t HEAD_TIMEOUT_MS = 3000;
constexpr uint8_t LOW_BATTERY_PCT = 5;
constexpr uint32_t TIME_RESEND_MS = 3600UL * 1000;   // The time is also allocated to assembly and each realignment.
constexpr uint32_t FACE_TIMEOUT_MS = 1000;
constexpr uint32_t SHAKE_MEMORY_MS = 8000;   // collisions counted as a single event if separated by less than 8 s
constexpr uint8_t SHAKES_WORRIED = 2;        // The first two disturb the head, the third angers it.
constexpr uint32_t BUMP_GAP_MS = 400;        // single abrupt impact sequence (bounces, jolt)
constexpr uint32_t BUMP_CONFIRM_MS = 150;    // a "lift" within this timeframe: take it by hand, not pushed.
constexpr uint32_t SCOLD_GAP_MS = 20000;     // Do not shake more than once every 20 s.
constexpr uint32_t FINGER_AFTER_MS = 400;    // the torso still bounces after the finger is lifted
constexpr float GRAVITY_TAU_S = 0.3f;        // smoothed gravity: what moves away from it quickly is a shake
constexpr uint32_t SWING_MS = 600;           // back and forth: going the other way within this delay
constexpr uint32_t SERIAL_TX_TIMEOUT_MS = 2;   // USB monitor: ≤40 ms delay if no one reads.
constexpr uint32_t DUMP_TX_TIMEOUT_MS = 100;  // « display » : the Mac reads, nothing must be lost.
constexpr uint32_t TOUCH_TAP_MAX_MS = 500;    // torso display, head placed: same gestures as on the head
constexpr uint32_t TOUCH_BURST_GAP_MS = 550;
constexpr uint32_t TOUCH_LONG_MS = 800;
constexpr int TOUCH_MOVED_PX = 45;
constexpr int TOUCH_RUB_PX = 140;
constexpr size_t SERIAL_RX_BUFFER = 8192;     // received Mac announcement fragments of 4 KB each.
constexpr size_t BRIDGE_RX_BUFFER = 20480;    // « bridge »: a 16 KB ESPTool binary arrives instantaneously
constexpr uint32_t BRIDGE_ASK_MS = 2000;      // repeat the request so that a head stuck in a reboot loop can receive it too
constexpr uint32_t BRIDGE_IDLE_MS = 15000;    // No Mac support: programming complete, the torso restarts.
constexpr uint32_t BRIDGE_WAIT_MS = 60000;    // Never used ESPTool
constexpr uint32_t FACE_PERIOD_MS = 40;   // Face toward head: max 25 Hz per frame (≈ 20 fps)
constexpr uint32_t SPEECH_TAIL_MS = 300;   // The speaker vibrates the torso and saturates the micro (sensor).
// Micro: dBFS → dB SPL (approx. sensitivity, codec gain to calibrate).
constexpr float DBFS_TO_DB = 120;

bool docked = false;
uint32_t headHelloAt = 0;
int headBatteryPct = -1;
bool headCharging = false;   // V2: Head recharges its LiPo via neck connector
bool headSeen = false;
bool syncHead = false;
bool timeForHead = false;   // the head has just replied: send it the time again
bool displayReady = false;
DashTheme headTheme = {};    // Head mood colors (pattern MOOD)
uint8_t headPhase = MOOD_COUNT;   // mood of the head (pattern MOOD); MOOD_COUNT not yet received.
uint32_t moodAt = 0;
uint32_t torsoAwakeUntil = 0;     // translated: touch: the torso screen stays awake even when the head is still asleep.
bool sleepShown = false;          // The torso rests with the head (sleeping face displayed on screen).
bool dashShown = false;           // dashboard display (no clearance, no joy, no anxiety, no sleep)
bool screenDark = false;          // asleep for 5 min: screen off, like the head’s.
constexpr uint32_t TORSO_AWAKE_MS = 30000;
constexpr uint32_t SLEEP_OFF_MS = 300000;   // The head turns off its screen after 5 min of sleep (SLEEP_OFF_S).
uint32_t worriedUntil = 0;   // Head removed: inquisitive face (3 sec)
uint32_t joyUntil = 0;       // Head resting: extreme joy 3s
constexpr uint32_t MOOD_TIMEOUT_MS = 3000, WORRIED_MS = 3000, JOY_MS = 3000;

// Torso IMU alignment: Axis mapping from chip → robot; verify on test bench with command `imu`.
// Torso IMU (QMI8658) axes in assembled robot (USB-C at bottom). Tested on bench 2026-10-03, USB-C up: up = +X, front (screen exit) = −Z**.
// Assembled robot 2026-10-28 (standing): Old setup read up = −0.95 → flipped 180° around screen: up = −X, right = −Y.
constexpr MotionEvents::Axes TORSO_AXES = {-2, -1, -3};
MotionEvents torsoMotion(TORSO_AXES);
MotionEvents::Output bodyMotion;
bool imuReady = false;
bool micReady = false;
bool speakerReady = false;
bool cameraOn = false;
uint32_t touchDebugUntil = 0, trackDebugUntil = 0, shockDebugUntil = 0;   // workbench: tactile command, displays 20s hold
bool cameraForced = false;   // workstation: command "camera force" (no radar, no head)
uint32_t radarHoldMs = 25000;   // presence maintained after last radar detection (adjustable: tests set it to 0)
uint8_t expBits = 0xFF;      // I2C PCF8574 input pins (radar OUT, DOCK)
bool expanderOk = false;
bool radarPresent = false;
uint32_t cameraMotionAt = 0;   // last target detected by the camera (keeps presence via radar)
bool cameraMotionEver = false;
uint8_t faceSize = 0;          // width of the last target
bool radarMoving = false;      // Current high-frequency radar output.
bool roomDark = false;         // night for the head (BH1750, hysteresis 5 / 15 lux)
float gyroDps = 0;             // rotation speed of the torso, °/s
uint32_t bodyBumpAt = 0;       // last recorded jostle, torso sway or rotation
bool bodyBumpEver = false;
uint32_t touchDownAt = 0;      // last finger placed on the torso screen.
uint32_t touchSeenAt = 0;      // last touch reading with a finger down
bool touchDownEver = false;
bool touchForGame = false;     // this finger is playing (answer button, paw, cross): its jolts are not jostling
float shockThreshold = 0.3f;   // acceleration spike counted as jostling, in g (command: 'chocs <threshold>')
MoodTriggers moodTriggers;
MoodSounds::MoodVoice moodVoice;   // mood sounds: when to play a cry
static void cry(MoodSounds::Sound s, const char* why = nullptr);
static void pollSpeech();
GuessGame game;                // « Guess the Animal »
bool gameReady = false;        // complete edition and game phrases arranged (TF map or flash)
static void checkGame();
uint32_t faceAt = 0;
bool faceSeen = false;
uint32_t speechUntil = 0;   // Current or recent text: IMU and microphone disabled.
bool listenPending = false; // Current listening query ("écoute") sent at the end by `pollListening`.
// Mac (brain/robot_conversation.py service) conversation:
// - Says "brain yes" every 8 s.
// - Says "brain busy" during exchanges.
// - Screen bubble button (complete edition) asks who is speaking; sends "button speak <name>"—if no response from Mac, it fails.
enum Brain : uint8_t { BRAIN_NONE, BRAIN_READY, BRAIN_ASKED, BRAIN_BUSY };
// Timer (complete edition): Lives in the torso to ring even when Mac is off. Counted in millis; time can be adjusted mid-operation; its Unix time stored in flash ensures survival after a reboot (torso reprogrammed during cuisson).
constexpr long TIMER_MAX_S = 86400;
constexpr uint32_t TIMER_LATE_S = 900;         // Finished or ringing before restart: still rings if less than 15 min ago.
constexpr uint32_t RING_GAP_MS = 400;          // silence between two bell-ringing tones
bool timerOn = false, timerDone = false;
uint32_t timerEndMs = 0, ringNext = 0;
uint8_t ringStep = 0;                          // bell: two "ding" (0, 1), then the phrase (2)
static uint32_t timerLeftS();
static void timerStart(uint32_t seconds);
static void timerStop();
// Access: the serial port obeys whoever holds it (Mac, Uno), possibly remotely. Only what the Mac’s service needs—no one in front of the robot—is open (`OPEN_COMMANDS`). The mic only transmits after a real finger press on the bubble: no command simulates it, and the access below does not trigger it manually or remotely. No command outputs a camera image—it only provides numbers. The rest (camera settings, screen, deposited phrases, Wi-Fi, head bridge, tests) requires a long press on the robot’s screen—far longer than a kiss (`TOUCH_LONG_MS`), never asked while playing or talking to it.
constexpr uint32_t ACCESS_MS = 600000;         // Approved for 10 min
constexpr uint32_t ACCESS_ASK_MS = 20000;      // The display shows 20 seconds remaining...
constexpr uint32_t ACCESS_RETRY_MS = 40000;    // ... no new request for 40 s: screen remains for players
constexpr uint32_t ACCESS_HOLD_MS = 2000;      // Finger held for tuning: 2 s
constexpr uint32_t ACCESS_LIFT_MS = 200;       // lost contact for a shorter duration: a failed tactile reading, not a raised finger
constexpr uint8_t ACCESS_DROPS = 3;            // ... no more than 3 times: rapid nips (tickles) yield nothing.
constexpr uint32_t MIC_GRANT_MS = 30000;       // Manual exchange on demand: one headphone requested within 30 s
uint32_t accessUntil = 0, askUntil = 0, askAt = 0, askNext = 0, micGrantUntil = 0;
bool micGrant = false;
float askHeld = 0;                             // part of the long support already made, for the screen
char askedCmd[12] = "";
static bool accessOpen() { return int32_t(accessUntil - millis()) > 0; }
static bool asking() { return int32_t(askUntil - millis()) > 0; }
uint8_t brain = BRAIN_NONE;
uint32_t brainAt = 0;
constexpr uint32_t BRAIN_TIMEOUT_MS[4] = {0, 25000, 5000, 90000};   // Ready, no response, exchange (max 45 s)
uint32_t whoUntil = 0;                 // Display "Who speaks?" open as far as previously.
constexpr uint32_t WHO_MS = 10000;     // No selection, the dashboard returns.
static bool choosing() { return int32_t(whoUntil - millis()) > 0; }
static void pressTalk();
static void talkAs(int who);
uint8_t soundDb = 0;
bool soundValid = false;

static void neckSend(uint8_t type, const void* payload, uint8_t len) {
  uint8_t frame[NECK_MAX_PAYLOAD + 5];
  size_t n = neckEncode(type, payload, len, frame);
  if (n) neck.write(frame, n);
}

// PCF8574 (0x22) read on each cycle: radar OUT and DOCK status. Without extender, the head is assumed in place (V1: still on neck) and no radar presence detected.
static void pollExpander() {
  uint8_t bits;
  bool ok = Board::readExpander(bits);
  static uint8_t failures = 0;
  static bool seen = false;
  if (ok) seen = true;
  failures = ok ? 0 : (failures < 255 ? failures + 1 : 255);
  // A single failed isolation read (bus attachment failure) is ignored: marked as "absent" only after 5 consecutive failures.
  bool present = seen && failures < 5;
  if (present != expanderOk) Serial.println(present ? "PCF8574 : présent" : "PCF8574 : absent (0x22 ? 3V3 ?)");
  expanderOk = present;
  if (ok) expBits = bits;
  // The LD2410C releases an immobile object after a few seconds: radarHoldMs ensures presence is maintained after the last high-output event, preventing the camera and head from losing it during pauses.
  static uint32_t lastRadarHigh = 0;
  static bool everHigh = false;
  bool radarHigh = present && ((expBits >> Board::EXP_RADAR_OUT) & 1);
  radarMoving = radarHigh;
  if (radarHigh) { lastRadarHigh = millis(); everHigh = true; }
  // The camera, active as long as presence lasts, extends it too: seated before the robot, movement suffices for it even when the radar loses us. Without it, the head toggles to "absent" every 25 s.
  bool cameraRecent = cameraMotionEver && uint32_t(millis() - cameraMotionAt) < radarHoldMs;
  radarPresent = present && (radarHigh || (everHigh && uint32_t(millis() - lastRadarHigh) < radarHoldMs) ||
                             cameraRecent);
}

static void updateDock() {
  static bool raw = false;
  static uint32_t changedAt = 0;
  bool now = expanderOk ? !((expBits >> Board::EXP_DOCK) & 1) : true;
  if (now != raw) { raw = now; changedAt = millis(); }
  // Immediate cut at removal; only anti-rebound on placement.
  if (raw != docked && (!raw || millis() - changedAt >= DOCK_DEBOUNCE_MS)) {
    docked = raw;
    Board::setNeckPower(docked);   // V2: 5 V output on exposed contacts
    headBatteryPct = -1;
    headCharging = false;
    headSeen = false;
    syncHead = docked;
    neckRx.reset();
    while (neck.available()) neck.read();
    // The head fails: MoodiMOS panics; it returns: extreme joy. One erases the other.
    (docked ? joyUntil : worriedUntil) = millis() + (docked ? JOY_MS : WORRIED_MS);
    (docked ? worriedUntil : joyUntil) = millis();
    Serial.println(docked ? "Tête en place" : "Tête retirée");
  }
}

static void readNeck() {
  while (neck.available()) {
    uint8_t b = neck.read();
    if (!docked || !neckRx.feed(b, millis())) continue;
    if (neckRx.type == NECK_HELLO && neckRx.len == sizeof(NeckHello)) {
      NeckHello h;
      memcpy(&h, neckRx.payload, sizeof(h));
      if (h.version != NECK_VERSION || (h.batteryPct > 100 && h.batteryPct != NECK_UNKNOWN) ||
          (h.charging > 1 && h.charging != NECK_UNKNOWN)) continue;
      headHelloAt = millis();
      // At deployment, the head begins activation (V1: powered via the neck) and only accepts time synchronization once the liaison is open—it reverts to its initial response.
      if (!headSeen) timeForHead = true;
      headSeen = true;
      headBatteryPct = h.batteryPct <= 100 ? h.batteryPct : -1;
      headCharging = h.charging == 1;
    } else if (neckRx.type == NECK_TOUCH && neckRx.len == 1) {
      Serial.printf("Tête : geste tactile %u\n", neckRx.payload[0]);
      // Head screen: playful touches/whispers and kisses; torso reacts with laughter or smacking sound.
      if (Edition::CUDDLES && neckRx.payload[0] == GESTURE_TICKLE) cry(MoodSounds::RIRE);
      if (Edition::CUDDLES && neckRx.payload[0] == GESTURE_LONG) cry(MoodSounds::BISOU);
    } else if (neckRx.type == NECK_STATE && neckRx.len == 1) {
      Serial.printf("Tête : expression %s\n", HeadExpressions::name(neckRx.payload[0]));
    } else if (neckRx.type == NECK_MOOD && neckRx.len == sizeof(NeckMood)) {
      NeckMood m;
      memcpy(&m, neckRx.payload, sizeof(m));
      if (m.phase >= MOOD_COUNT) continue;
      headTheme = {true, m.bg, m.panel, m.accent};
      headPhase = m.phase;
      moodAt = millis();
      cry(moodVoice.onPhase(m.phase, millis()));
    }
  }
}

static void sendTimeToHead() {
  static uint32_t sentRevision = 0, lastTimeAt = 0;
  uint32_t unix;
  if (!TimeSource::now(unix)) return;
  if (!syncHead && !timeForHead && TimeSource::revision() == sentRevision &&
      millis() - lastTimeAt < TIME_RESEND_MS) return;
  timeForHead = false;
  sentRevision = TimeSource::revision();
  lastTimeAt = millis();
  NeckTime t = {unix, TimeUtil::parisOffsetMinutes(unix)};
  neckSend(NECK_TIME, &t, sizeof(t));
}

static bool speechRecent() { return Speaker::playing() || int32_t(speechUntil - millis()) > 0; }

// A past deadline remains after over 12 days: otherwise, 25 days later, `int32_t(deadline − millis())` would revert to positive, misleading it as 25 days ahead (microcontroller and IMU unresponsive, torso unable to rest).
static void keepPast(uint32_t& until, uint32_t now) {
  constexpr int32_t STALE = 1 << 30;   // ≈ 12 days
  if (int32_t(until - now) < -STALE) until = now - STALE;
}
static void keepDeadlinesPast() {
  const uint32_t now = millis();
  uint32_t* const all[] = {&speechUntil, &joyUntil, &worriedUntil, &torsoAwakeUntil, &touchDebugUntil, &trackDebugUntil,
                           &shockDebugUntil, &accessUntil, &askUntil, &askNext, &micGrantUntil};
  for (uint32_t* until : all) keepPast(*until, now);
}

// MoodSounds.h (Mood Voice): Head mounted—never during a phrase, immediately after, or when the timer rings; delays enforced by `moodVoice`. Reason: triggers from the monitor’s distress signal.
static void cry(MoodSounds::Sound s, const char* why) {
  if (s == MoodSounds::NONE || !docked || timerDone || speechRecent() || Speaker::cryLevel() == 0 ||
      Microphone::listening()) return;
  int variant = moodVoice.take(s, millis());
  if (variant >= 0 && Speaker::cry(s, uint8_t(variant)))
    Serial.printf("Cri : %s %d%s%s%s\n", MoodSounds::name(s), variant + 1, why ? " (" : "", why ? why : "", why ? ")" : "");
}

// Camera: Activated when someone is present and the head can see them.
static void updateCamera() {
  bool want = cameraForced || (docked && radarPresent);
  if (want == cameraOn) return;
  cameraOn = want;
  TorsoCamera::setActive(want);
  if (!want) faceSeen = false;
}

// Camera tracking → frame rate FACE to the head (head), ≤ 25 Hz.
static void pollCamera() {
  static uint32_t lastSend = 0;
  NeckFace face;
  uint32_t at;
  if (TorsoCamera::latest(face, at) && cameraOn) {
    if (int32_t(trackDebugUntil - millis()) > 0) {
      static uint32_t lastDbg = 0;
      if (millis() - lastDbg >= 150) {
        lastDbg = millis();
        Serial.printf("suivi vu=%d x=%4d y=%4d taille=%3d cases=%d\n", face.seen, face.x, face.y, face.size,
                      TorsoCamera::movingCells());
      }
    }
    faceAt = millis();
    faceSeen = face.seen;
    faceSize = face.seen ? face.size : 0;
    if (face.seen) { cameraMotionAt = millis(); cameraMotionEver = true; }
    if (docked && millis() - lastSend >= FACE_PERIOD_MS) {
      lastSend = millis();
      neckSend(NECK_FACE, &face, sizeof(face));
    }
  }
  if (faceSeen && millis() - faceAt >= FACE_TIMEOUT_MS) faceSeen = false;
}

static void sendToHead(const Ambient::Reading* env) {
  static uint32_t lastPresenceAt = 0, lastEnvAt = 0;
  static bool dark = false;
  if (!docked) return;

  bool present = radarPresent;
  if (syncHead || millis() - lastPresenceAt >= 200) {   // 5 Hz
    lastPresenceAt = millis();
    NeckPresence p = {uint8_t(present), 0};   // unknown distance: single OUT output only
    neckSend(NECK_PRESENCE, &p, sizeof(p));
    // Torso joy (Mac announcement, head resting): the head shares its life with him, even asleep, with the same golden hues (mood maintained for 1 second per frame).
    if (int32_t(joyUntil - millis()) > 0) {
      NeckEvent cheer = {EV_FACE, HeadEvents::CHEER};
      neckSend(NECK_EVENT, &cheer, sizeof(cheer));
    }
    if (imuReady) {   // body posture, repeated as presence.
      NeckEvent lying = {EV_LYING, int16_t(bodyMotion.lying ? 1 : 0)};
      neckSend(NECK_EVENT, &lying, sizeof(lying));
      NeckEvent tilt = {EV_TILT, int16_t(lroundf(bodyMotion.tilt * 1000))};
      neckSend(NECK_EVENT, &tilt, sizeof(tilt));
    }
  }
  sendTimeToHead();   // after PRESENCE, which establishes the link on the head side.
  if (env && (syncHead || millis() - lastEnvAt >= 1000)) {
    lastEnvAt = millis();
    NeckEnv e = {env->tempC100, env->humPct100, env->pressHpa10, env->lux, soundDb};
    neckSend(NECK_ENV, &e, sizeof(e));
    // Provisional hysteresis, calibrated after assembly. No recent light measurement (BH1750 missing or silent), no false night mode.
    if (env->lightOk) {
      if (env->lux <= 5) dark = true;
      else if (env->lux >= 15) dark = false;
      roomDark = dark;
      // During a match, the head does not fall asleep in the dimness (the game is also played at night).
      NeckEvent light = {uint8_t(dark && !game.active() ? EV_DARK : EV_LIGHT), 0};
      neckSend(NECK_EVENT, &light, sizeof(light));
    }
  }
  syncHead = false;
}

// Serial monitor (115200 baud, newline "New line"):
// - `wifi <ssid> <password>` – stores Wi-Fi credentials (no spaces) and syncs time
// - `wifi forget` – deletes stored credentials
// - `time AAAA-MM-JJ HH:MM[:SS]` – manually sets Paris time
// - `status` – displays time, Wi-Fi, phrases, and clearance
// - `imu` – IMU values in robot coordinate frame (axis alignment)
// - `micro` – microphone volume level
// - `sensors` – detected BME280/BH1750 readings
// - `camera [force|auto]` – OV5640 camera state and target; `force` = always on (bench test)
// - `touch` – shows touch coordinates for 20s (bench test)
// - `screen` – hexadecimal display drawing (bench test)
// - `radar` – PCF8574 extender, radar OUT and DOCK output
// - `say <phrase> / silence` – plays `<phrase>.wav` (TF card/flash) or stops
// - `dump <name> <bytes> + wav` – uploads `<voix/<name>.wav` to torso flash in 4KB chunks (confirm each)
//   *(edit/q/guess_animal/to_robot.py → upload)*
// - `game [yes|no|jsp|end]` – starts "Guess the Animal"; responses trigger screen buttons
// - `track` – movement tracking output (25s, bench test)
// - `look [gx gy cx cy]` – camera focus gain/center (live); standalone: raw values
// - `scan` – lists I2C addresses responding (bench test)
// - `greet` – firmware-stored intro phrase (also spoken on boot)
// - `announce <bytes> + sound` – announces Mac ID (task end), 16-bit mono PCM 16kHz
// - `reply <bytes> + sound` – same as `announce` without celebration (robot_conversation.py)
// - `reply+ <bytes> + sound` – plays next response phrase without pause (max 30s total)
// - `listen [s | stop]` – captures input until silence (max 8s), then outputs `sound <bytes> <sum>` + PCM voice (16-bit mono, 16kHz); `stop` = abort immediately
// - `brain [yes|busy|no]` – Mac-driven conversation: ready, ongoing, stopped (screen bubble button); standalone: status
// - `timer [seconds | stop]` – complete edition: starts/cancels timer (max 24h), stops alarm; standalone: remaining time
// - `volume <0-255>` – codec volume (bench test; default: 178)
// - `test` – loud sine wave, volume via codec steps (bench test: where it squeaks)
// - `beep` – soft two-note test (bench test)
// - `sounds [0-10 | name [1-3]]` – sets emotion cry volume (0 = silent); stored in flash; names trigger cries:
//   joy, curious, surprised, growl, sigh, yawn, laugh, kiss; standalone: volume levels
// - `ring` – rings detached head ("Where’s my head?")
// - `face <expression> [s]` – forces head pose expression (default: 3s, max 25s); standalone: expression list
// - `shocks [threshold]` – records impact forces (g) and collisions over 60s; threshold = direct jolt g-value
// - `event <name>` – sends event to head (e.g., *lifted, shaken, noise, tap, pat, kiss, confused, sulked*)—emotion tests without shaking
// - `bridge [baud] [direct]` – programs head via neck (flash_head_neck.sh), then restarts; `direct` = head already in upload mode (bench test)
// 
// ---
// Closed-head programming: Head restarts into its ROM upload mode, listening to neck wires at initial baud rate. Torso acts as an ESPTool relay between USB and neck. Mac stops sending data for 15s—upload ends; torso restarts, resuming.
// 
// ---
// Last bridge relayed bytes persist across reboots and display on next boot.
RTC_NOINIT_ATTR uint32_t bridgeMark, bridgeToHead, bridgeFromHead;
constexpr uint32_t BRIDGE_MARK = 0x504F4E54;   // « PONT »

static void bridgeHeadUpdate(uint32_t baud, bool ask) {
  if (!docked) { Serial.println("pont : tête absente"); return; }
  if (Serial.setRxBufferSize(BRIDGE_RX_BUFFER) != BRIDGE_RX_BUFFER) {
    Serial.setRxBufferSize(SERIAL_RX_BUFFER);
    Serial.println("pont : mémoire insuffisante");
    return;
  }
  Serial.setTxTimeoutMs(DUMP_TX_TIMEOUT_MS);   // ESPTool command: *No data should be lost*
  TorsoCamera::setActive(false);
  for (uint32_t t0 = millis(); ask && millis() - t0 < BRIDGE_ASK_MS;) {
    neckSend(NECK_UPDATE, NECK_UPDATE_MAGIC, sizeof(NECK_UPDATE_MAGIC));
    delay(20);
  }
  neck.flush();
  neck.updateBaudRate(baud);
  delay(200);
  while (neck.available()) neck.read();       // log of the ROM, read at an incorrect speed
  while (Serial.available() > 0) Serial.read();
  Serial.printf("pont prêt : tête en mode téléchargement, cou à %u bauds (esptool sans reset du port)\n", unsigned(baud));
  static uint8_t buf[512];
  uint32_t lastHost = millis();
  bool started = false;
  bridgeMark = BRIDGE_MARK;
  bridgeToHead = bridgeFromHead = 0;
  for (;;) {
    int n = Serial.available(), m = neck.available();
    if (n > 0) {
      n = Serial.readBytes(buf, size_t(n) < sizeof(buf) ? size_t(n) : sizeof(buf));
      neck.write(buf, n);
      bridgeToHead += n;
      lastHost = millis();
      started = true;
    }
    if (m > 0) {
      m = neck.readBytes(buf, size_t(m) < sizeof(buf) ? size_t(m) : sizeof(buf));
      Serial.write(buf, m);
      bridgeFromHead += m;
    }
    if (millis() - lastHost >= (started ? BRIDGE_IDLE_MS : BRIDGE_WAIT_MS)) break;
    if (n <= 0 && m <= 0) delay(1);
  }
  esp_restart();
}

// Received after "announce" (`<octets>`), "dis" (`<octets>`), or "dis+" (`<octets>`) (16-bit mono PCM, 16 kHz, max 30 s),
// played immediately on the first track while the rest streams; invalid if refused or incomplete (voice stops where reception halted).
// "dis+": the next response phrase, appended after others and played sequentially (or immediately if voice ended earlier); Mac sends it while the first is spoken.
// Chunked reception (4 KB per piece): sender waits for "+" before each new chunk (otherwise, it oversends faster than the card reads, causing overflow).
static bool receivePcm(const char* cmd, long octets) {
  static int16_t* son = nullptr;
  static volatile int received = 0;                     // Samples arrived from `begin`, read by voice.
  static long debut = 0, rempli = 0;                    // octets: start of played voice, end of organized sound
  static bool suiteOk = false;                          // Only execute "dis+" after receiving a complete "dis" command.
  const long maxi = 16000L * 2 * 30;                    // Long responses in the conversation take 14 s.
  bool suite = !strcmp(cmd, "dis+");
  if (!son) son = (int16_t*)ps_malloc(maxi);
  if (!son || octets <= 0 || octets > maxi - (suite ? rempli : 0) || (octets & 1) || (suite && !suiteOk)) {
    Serial.printf("%s refusée : %ld octets (30 s au plus%s)\n", cmd, octets, suite ? " en tout, après un « dis »" : "");
    return false;
  }
  bool playing;
  if (!suite) {
    Speaker::stop();                                    // do not read the pad until rewritten.
    for (uint32_t t = millis(); Speaker::playing() && millis() - t < 500;) delay(5);
    debut = rempli = 0;
    received = 0;
    playing = Speaker::playPcm(son, int(octets / 2), &received);
  } else if (Speaker::extendPcm(int((rempli - debut + octets) / 2))) {
    playing = true;                                     // append to the current speech
  } else {
    debut = rempli;                                     // The voice had ended: the sequence restarts here.
    received = 0;
    playing = Speaker::playPcm(son + debut / 2, int(octets / 2), &received);
  }
  suiteOk = false;
  uint8_t* dest = (uint8_t*)son + rempli;
  Serial.println("pret");
  size_t lu = 0;
  while (long(lu) < octets) {
    size_t part = size_t(octets) - lu < 4096 ? size_t(octets) - lu : 4096;
    size_t got = Serial.readBytes(dest + lu, part);
    lu += got;
    received = int((rempli - debut + long(lu)) / 2);
    pollSpeech();                                       // The head's mouth follows the voice from the start.
    sendToHead(nullptr);                                // and presence at 5 Hz: the head feels detached.
    if (got != part) break;
    Serial.write('+');
  }
  rempli += long(lu);
  uint32_t somme = 0;
  for (size_t i = 0; i < lu; i++) somme += dest[i];
  if (long(lu) != octets || !playing) {
    Serial.printf("%s incomplète ou son indisponible : %u / %ld octets\n", cmd, unsigned(lu), octets);
    return false;
  }
  suiteOk = cmd[0] == 'd';
  Serial.printf("%s : %.1f s, somme %lu\n", cmd, octets / 32000.0f, (unsigned long)somme);
  return true;
}

static void handleCommand(char* line) {
  char* cmd = strtok(line, " ");
  if (!cmd) return;
  static const char* const OPEN_COMMANDS[] = {"cerveau", "annonce", "dis", "dis+", "tete", "capteurs",
                                              "etat", "minuteur", "silence", "ecoute", "acces"};
  bool open = accessOpen();
  for (const char* c : OPEN_COMMANDS) open = open || !strcmp(cmd, c);
  if (!open) {
    Serial.printf("refusé : « %s » demande un appui long sur l'écran du robot\n", cmd);
    bool word = strlen(cmd) < sizeof(askedCmd);          // a word, not lost bytes across the line
    for (const char* c = cmd; *c; c++) word = word && islower((unsigned char)*c);
    if (word && !asking() && int32_t(askNext - millis()) <= 0 && !game.active() && !choosing() && brain < BRAIN_ASKED) {
      strcpy(askedCmd, cmd);
      askAt = millis();
      askUntil = askAt + ACCESS_ASK_MS;
      askNext = askUntil + ACCESS_RETRY_MS;
    }
    return;
  }
  if (!strcmp(cmd, "wifi")) {
    char* ssid = strtok(nullptr, " ");
    char* pass = strtok(nullptr, "");            // rest of the line, spaces included
    if (ssid && !strcmp(ssid, "oubli") && !pass) {
      TimeSource::forgetWifi();
      Serial.println("Wi-Fi oublié");
    } else if (ssid && pass && TimeSource::setWifi(ssid, pass)) {
      Serial.printf("Wi-Fi « %s » enregistré, synchronisation lancée\n", ssid);
    } else {
      Serial.println("format : wifi <ssid> <mot de passe>  ou  wifi oubli");
    }
  } else if (!strcmp(cmd, "heure")) {
    char* date = strtok(nullptr, " ");
    char* clock = strtok(nullptr, " ");
    TimeUtil::Civil c = {0, 0, 0, 0, 0, 0};
    int y = 0, mo = 0, d = 0, h = -1, mi = -1, s = 0;
    bool ok = date && clock && sscanf(date, "%d-%d-%d", &y, &mo, &d) == 3 &&
              sscanf(clock, "%d:%d:%d", &h, &mi, &s) >= 2 && y >= 2023 && y <= 2099 &&
              mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h < 24 && mi >= 0 && mi < 60 &&
              s >= 0 && s < 60;
    if (ok) {
      c = {y, unsigned(mo), unsigned(d), unsigned(h), unsigned(mi), unsigned(s)};
      ok = TimeSource::setManual(TimeUtil::parisLocalToUnix(c));
    }
    Serial.println(ok ? "Heure réglée" : "format : heure AAAA-MM-JJ HH:MM[:SS]");
  } else if (!strcmp(cmd, "etat")) {
    Serial.printf("édition %s · %s\n", Edition::NAME, TimeSource::status());
    Serial.printf("phrases : %s · jeu %s\n", Speaker::storage(),
                  gameReady ? "prêt" : Edition::GAMES ? "sans ses phrases" : "absent de cette édition");
  } else if (!strcmp(cmd, "deposer")) {
    // Mac phrase (extras/guess_animal/to_robot.py): *"deposit <name> <bytes>"*, then the WAV file in 4 KB chunks, each confirmed by a ‘+’ (the Mac waits for the next, like for "announcement").
    char* name = strtok(nullptr, " ");
    char* n = strtok(nullptr, " ");
    long octets = n ? atol(n) : 0;
    if (!name || octets <= 0 || octets > 4L * 1024 * 1024 || !Speaker::storeOpen(name)) {
      Serial.printf("deposer refusé : %s, %ld octets\n", name ? name : "?", octets);
      return;
    }
    Serial.println("pret");
    static uint8_t part[4096];
    long lu = 0;
    uint32_t somme = 0;
    bool ok = true;
    while (ok && lu < octets) {
      size_t want = octets - lu < long(sizeof(part)) ? size_t(octets - lu) : sizeof(part);
      size_t got = Serial.readBytes(part, want);
      for (size_t i = 0; i < got; i++) somme += part[i];
      lu += long(got);
      ok = got == want && Speaker::storeWrite(part, got);
      if (ok) Serial.write('+');
    }
    Speaker::storeClose(ok);
    if (ok) Serial.printf("deposer %s : %ld octets, somme %lu\n", name, octets, (unsigned long)somme);
    else Serial.printf("deposer %s incomplet : %ld / %ld octets\n", name, lu, octets);
    checkGame();
  } else if (!strcmp(cmd, "jeu")) {
    char* a = strtok(nullptr, " ");
    if (!Edition::GAMES) Serial.println("jeu : pas dans cette édition");
    else if (!a) {
      if (!gameReady) Serial.println("jeu : phrases absentes (extras/guess_animal/to_robot.py envoyer)");
      else if (!docked) Serial.println("jeu : la tête doit être posée");
      else { game.start(millis()); Serial.println("jeu : Devine l'animal"); }
    } else if (!game.active()) Serial.println("jeu : pas de partie en cours");
    else if (!strcmp(a, "fin")) game.quit();
    else if (!strcmp(a, "oui")) game.press(GuessEngine::YES, millis());
    else if (!strcmp(a, "non")) game.press(GuessEngine::NO, millis());
    else if (!strcmp(a, "jsp")) game.press(GuessEngine::DONT_KNOW, millis());
    else Serial.println("format : jeu [oui|non|jsp|fin]");
  } else if (!strcmp(cmd, "dire")) {
    char* name = strtok(nullptr, " ");
    if (!name) Serial.println("format : dire <phrase>   (fichier /voix/<phrase>.wav sur la carte TF)");
    else if (!Speaker::play(name)) Serial.printf("« %s » : fichier absent ou format refusé (PCM 16 bits, 16 kHz)\n", name);
  } else if (!strcmp(cmd, "regard")) {
    // Visual adjustment, lost on reboot: correct values go into `trackerConfig()`.
    char* v[4];
    int n = 0;
    while (n < 4 && (v[n] = strtok(nullptr, " "))) n++;
    if (n == 4) TorsoCamera::setGaze({float(atof(v[0])), float(atof(v[1])), float(atof(v[2])), float(atof(v[3]))});
    else if (n) Serial.println("format : regard <gain x> <gain y> <centre x> <centre y>   (centre de −1 à 1)");
    TorsoCamera::Gaze g = TorsoCamera::gaze();
    Serial.printf("regard : gain x %.2f, gain y %.2f, centre x %.2f, centre y %.2f\n", g.gainX, g.gainY, g.centerX,
                  g.centerY);
  } else if (!strcmp(cmd, "suivi")) {
    trackDebugUntil = millis() + 25000;
    Serial.println("suivi : vu, x (−127 gauche … +127 droite), y, taille, cases en mouvement, pendant 25 s");
  } else if (!strcmp(cmd, "scan")) {
    Serial.print("I2C :");
    for (uint8_t a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    Serial.println();
  } else if (!strcmp(cmd, "annonce") || !strcmp(cmd, "dis") || !strcmp(cmd, "dis+")) {
    // Mac announcement (`brain/announce.py`, end of Claude/Codex task): "announce <octets>" followed by sound, 16-bit mono PCM @ 16 kHz. The robot speaks it via its built-in speaker (synchronized mouth of the head), while the torso reacts during playback.
    // "dis <octets>": same without reaction (conversation response, `cereveau/robot_conversation.py`).
    // "dis+ <octets>": next phrase.
    char* n = strtok(nullptr, " ");
    long octets = n ? atol(n) : 0;
    if (receivePcm(cmd, octets) && cmd[0] == 'a') joyUntil = millis() + uint32_t(octets / 32) + 1000;  // 32 octets per ms
  } else if (!strcmp(cmd, "cerveau")) {
    char* a = strtok(nullptr, " ");
    if (!a) {
      static const char* const names[4] = {"absent", "prêt", "appelé", "occupé"};
      Serial.printf("cerveau : %s\n", names[brain]);
      return;
    }
    if (!strcmp(a, "oui") && brain == BRAIN_ASKED) return;   // The support is being sent to the Mac.
    brain = !strcmp(a, "oui") ? BRAIN_READY : !strcmp(a, "occupe") ? BRAIN_BUSY : BRAIN_NONE;
    brainAt = millis();
  } else if (!strcmp(cmd, "acces")) {
    // Voice access state (brain/robot_conversation.py); "acces fin" closes it immediately.
    char* a = strtok(nullptr, " ");
    if (a && !strcmp(a, "fin")) accessUntil = millis();
    if (accessOpen()) Serial.printf("acces : ouvert %lu s\n", (unsigned long)((accessUntil - millis()) / 1000));
    else Serial.println("acces : ferme");
  } else if (!strcmp(cmd, "minuteur")) {
    char* a = strtok(nullptr, " ");
    if (!Edition::COMPLETE) {
      Serial.println("minuteur : absent de cette édition");
      return;
    }
    if (a && !strcmp(a, "fin")) {
      timerStop();
    } else if (a) {
      const long s = atol(a);
      if (s < 1 || s > TIMER_MAX_S) {
        Serial.println("minuteur : durée refusée (1 s à 24 h)");
        return;
      }
      timerStart(uint32_t(s));
    }
    if (timerDone) Serial.println("minuteur : fini");
    else if (timerOn) Serial.printf("minuteur : %lu s\n", (unsigned long)timerLeftS());
    else Serial.println("minuteur : aucun");
  } else if (!strcmp(cmd, "ecoute")) {
    // Command (torso/robot_conversation.py, UNO Q):
    // `« listen [seconds] »` – torso listens until the following silence (max 8 s default), then sends:
    // `« sound <bytes> <sum> »` + 16-bit mono PCM 16 kHz voice (via `pollListening`).
    // `« sound 0 0 »` if no speech detected.
    // `« listen end »` – immediate termination.
    char* a = strtok(nullptr, " ");
    if (a && !strcmp(a, "fin")) {
      Microphone::endListening();
      return;
    }
    if (!micGrant || int32_t(micGrantUntil - millis()) <= 0) {
      Serial.println("ecoute refusée : touche la bulle du robot");
      return;
    }
    float s = a ? atof(a) : 8;
    if (!micReady || !Microphone::listen(uint32_t(constrain(s, 1.0f, 10.0f) * 1000))) {
      Serial.println("ecoute refusée : micro absent ou déjà à l'écoute");
      return;
    }
    listenPending = true;
    micGrant = false;
    Serial.printf("ecoute : %.0f s au plus\n", constrain(s, 1.0f, 10.0f));
  } else if (!strcmp(cmd, "tete")) {
    // Fixed expression on the head via neck (Mac, bench): "head rieur 5". Retains its mood.
    char* nom = strtok(nullptr, " ");
    char* sec = strtok(nullptr, " ");
    uint8_t id;
    if (!nom || !HeadExpressions::fromName(nom, id)) {
      Serial.print("tete <expression> [secondes] :");
      for (uint8_t i = 0; i < HeadExpressions::COUNT; i++) Serial.printf(" %s", HeadExpressions::NAMES[i]);
      Serial.println();
      return;
    }
    long dixiemes = sec ? lroundf(atof(sec) * 10) : 30;   // The frame stores duration in tenths of a second.
    uint8_t payload[2] = {id, uint8_t(constrain(dixiemes, 1L, 255L))};
    neckSend(NECK_EXPRESSION, payload, sizeof(payload));
    Serial.printf("tete : %s pendant %.1f s%s\n", nom, payload[1] / 10.0f,
                  docked && headSeen ? "" : " (tête pas vue sur le cou)");
  } else if (!strcmp(cmd, "parle")) {
    Speaker::sayDemo();
    Serial.println("parle : phrase de test");
  } else if (!strcmp(cmd, "volume")) {
    char* v = strtok(nullptr, " ");
    if (!v) { Serial.println("volume <0-255> : volume du codec en direct (0xB2 = 178 au démarrage)"); }
    else {
      int n = atoi(v);
      n = n < 0 ? 0 : n > 255 ? 255 : n;
      Speaker::setVolume(uint8_t(n));
      Serial.printf("volume du codec : %d (0x%02X)\n", n, n);
    }
  } else if (!strcmp(cmd, "sons")) {
    char* v = strtok(nullptr, " ");
    MoodSounds::Sound s = MoodSounds::fromName(v);
    if (s != MoodSounds::NONE) {   // experimental vocalization, no delay or mood
      char* k = strtok(nullptr, " ");
      int variant = k ? atoi(k) - 1 : int(millis() % MoodSounds::VARIANTS);
      variant = variant < 0 ? 0 : variant % MoodSounds::VARIANTS;
      if (Speaker::cryLevel() == 0) Serial.println("sons : muets (sons <1-10> pour les entendre)");
      else if (Speaker::cry(s, uint8_t(variant))) Serial.printf("sons : %s, variante %d\n", MoodSounds::name(s), variant + 1);
      else Serial.println("sons : haut-parleur occupé ou absent");
    } else if (v && isdigit((unsigned char)v[0])) {
      int n = atoi(v);
      Speaker::setCryLevel(uint8_t(n < 0 ? 0 : n > 10 ? 10 : n));
      Serial.printf("sons : volume des cris %u/10%s (gardé)\n", Speaker::cryLevel(), Speaker::cryLevel() ? "" : ", muets");
    } else {
      Serial.printf("sons : volume des cris %u/10 ; sons <0-10>, ou sons <nom> [1-3] : joie, curieux, surpris, "
                    "grogne, soupir, baille, rire, bisou\n", Speaker::cryLevel());
    }
  } else if (!strcmp(cmd, "essai")) {
    Speaker::sweep();
    Serial.println("essai : sinus fort, volume par paliers de 2 s (0xA0 à 0xDF), stop = « silence »");
  } else if (!strcmp(cmd, "bip")) {
    Speaker::chime();
    Serial.println("bip : deux notes douces");
  } else if (!strcmp(cmd, "sonner")) {
    if (docked) Serial.println("La tête est sur le cou.");
    else { HeadLink::ring(100); Serial.println("Appel de la tête (réponse sous 30 s si elle dort)"); }
  } else if (!strcmp(cmd, "silence")) {
    Speaker::stop();
  } else if (!strcmp(cmd, "capteurs")) {
    Ambient::Reading r;
    Ambient::latest(r);
    Serial.printf("%s\n", Ambient::status());
    Serial.printf("T %.2f C (%s), HR %.1f %% (%s), P %.1f hPa, lumière %u lx (%s)\n", r.tempC100 / 100.0f,
                  r.climateOk ? "ok" : "absente", r.humPct100 / 100.0f, r.humidityOk ? "ok" : "absente",
                  r.pressHpa10 / 10.0f, r.lux, r.lightOk ? "ok" : "absente");
  } else if (!strcmp(cmd, "camera")) {
    char* arg = strtok(nullptr, " ");
    if (arg && !strcmp(arg, "force")) cameraForced = true;
    if (arg && !strcmp(arg, "auto")) cameraForced = false;
    Serial.printf("%u images, luminosité moyenne %u/255 · ", (unsigned)TorsoCamera::frames(), TorsoCamera::meanLuma());
    Serial.printf("caméra %s (%s), cible %s\n", cameraOn ? "demandée" : "en veille", TorsoCamera::status(),
                  faceSeen ? "vue" : "aucune");
  } else if (!strcmp(cmd, "tactile")) {
    touchDebugUntil = millis() + 20000;
    Serial.println("tactile : touche l'écran, coordonnées affichées pendant 20 s (portrait 320 × 480)");
  } else if (!strcmp(cmd, "ecran")) {
    // Board: Screen drawing (320 × 480 RGB565) in hexadecimal between `ÉCRAN` and `FIN`
    const uint16_t* fb = Dashboard::framebuffer();
    Serial.setTxTimeoutMs(DUMP_TX_TIMEOUT_MS);
    Serial.println("ECRAN");
    char line[257];
    for (size_t i = 0; i < 320u * 480u; i += 64) {
      for (int k = 0; k < 64; k++) snprintf(line + k * 4, 5, "%04x", fb[i + k]);
      Serial.println(line);
    }
    Serial.println("FIN");
    Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
  } else if (!strcmp(cmd, "radar")) {
    Serial.printf("PCF8574 %s (entrées 0x%02X) : radar %s, DOCK %s\n", expanderOk ? "présent" : "absent", expBits,
                  radarPresent ? "présence" : "absence", docked ? "tête en place" : "tête retirée");
  } else if (!strcmp(cmd, "micro")) {
    Serial.printf("micro %s : niveau %.1f dBFS\n", micReady ? "actif" : "absent", Microphone::levelDbfs());
  } else if (!strcmp(cmd, "imu")) {
    // On test bench: standing, "haut" ≈ +1; screen facing sky, "avant" ≈ +1.
    Serial.printf("IMU %s : droite %.2f  haut %.2f  avant %.2f  inclinaison %.2f  couché %d\n",
                  imuReady ? "active" : "absente", bodyMotion.right, bodyMotion.up, bodyMotion.out,
                  bodyMotion.tilt, bodyMotion.lying);
  } else if (!strcmp(cmd, "chocs")) {
    char* a = strtok(nullptr, " ");
    if (a && atof(a) >= 0.1 && atof(a) <= 2) {
      shockThreshold = float(atof(a));
      torsoMotion.setShakeThreshold(shockThreshold);
    }
    shockDebugUntil = millis() + 60000;
    Serial.printf("chocs : seuil %.2f g ; force des chocs et bousculades comptées pendant 60 s\n", shockThreshold);
  } else if (!strcmp(cmd, "evenement")) {
    struct Named { const char* name; uint8_t code; int16_t value; };
    static const Named EVENTS[] = {
        {"souleve", EV_LIFT, 0}, {"secoue", EV_SHAKE, 0}, {"bruit", EV_NOISE, 0},
        {"tap", EV_FACE, HeadEvents::TAP}, {"chatouilles", EV_FACE, HeadEvents::TICKLE},
        {"bisou", EV_FACE, HeadEvents::KISS}, {"perplexe", EV_FACE, HeadEvents::UNANSWERABLE},
        {"boude", EV_FACE, HeadEvents::IGNORED}};
    char* name = strtok(nullptr, " ");
    const Named* found = nullptr;
    for (const Named& e : EVENTS)
      if (name && !strcmp(name, e.name)) found = &e;
    if (!found) {
      Serial.println("evenement souleve|secoue|bruit|tap|chatouilles|bisou|perplexe|boude");
    } else if (!docked) {
      Serial.println("evenement : tête absente");
    } else {
      NeckEvent e = {found->code, found->value};
      neckSend(NECK_EVENT, &e, sizeof(e));
      Serial.printf("evenement %s envoyé à la tête\n", found->name);
    }
  } else if (!strcmp(cmd, "pont")) {
    // pont [bauds] [direct]: bench mode; direct means the head is already in download mode, so no request is sent
    char* a = strtok(nullptr, " ");
    char* b = strtok(nullptr, " ");
    uint32_t baud = a && atol(a) >= 9600 ? uint32_t(atol(a)) : NECK_UPDATE_BAUD;
    bool direct = (a && !strcmp(a, "direct")) || (b && !strcmp(b, "direct"));
    bridgeHeadUpdate(baud, !direct);
  } else {
    Serial.println("commandes : wifi, heure, etat, imu, micro, capteurs, camera, regard, radar, dire, silence, sonner, tete, evenement, chocs, pont");
  }
}

static void pollSerial() {
  static char line[128];
  static uint8_t len = 0;
  while (Serial.available()) {
    char ch = char(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[len] = 0;
      handleCommand(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    }
  }
}

// IMU at 50 Hz: raised and shaken inputs immediately directed to the head.
static void pollImu() {
  static uint32_t lastRead = 0;
  if (!imuReady || millis() - lastRead < 20) return;
  lastRead = millis();
  float a[3], g[3];
  if (!Board::imuRead(a, g)) return;
  bodyMotion = torsoMotion.update(a, g, millis());
  gyroDps = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  bool spin = gyroDps > 30;   // °/s: sudden tilt, rapid rotation
  if (bodyMotion.jolt || bodyMotion.lift || bodyMotion.shake || spin) TorsoCamera::relearn();
  // The sound causes the torso to resonate—it’s not the head being pushed (touch remains accounted for).
  if (!speechRecent() && (bodyMotion.lift || bodyMotion.shake || spin)) { bodyBumpAt = millis(); bodyBumpEver = true; }
  // Shaken sideways, standing on the table, the robot hardly lengthens its acceleration (0.36 g sideways: 0.06 g
  // away from 1 g): it had to be shaken very hard. The sideways deviation from the smoothed gravity sees the whole
  // shake; it counts when it reverses (back and forth), not when the robot is tilted. Up and down stay measured as
  // the deviation from 1 g (bodyMotion.jolt): lifted then set down is not a shake.
  static float grav[3] = {}, swingDir[3] = {};
  static uint32_t gravAt = 0, swingAt = 0;
  static bool gravOk = false, swinging = false;
  float h[3], up = 0, side = 0;
  const float gn = sqrtf(grav[0] * grav[0] + grav[1] * grav[1] + grav[2] * grav[2]);
  for (int i = 0; i < 3; i++) {
    h[i] = gravOk ? a[i] - grav[i] : 0;
    if (gn > 0.5f) up += h[i] * grav[i] / gn;
  }
  for (int i = 0; i < 3; i++) {
    if (gn > 0.5f) h[i] -= up * grav[i] / gn;   // sideways part only
    side += h[i] * h[i];
  }
  side = sqrtf(side);
  const float k = gravOk ? 1 - expf(-float(millis() - gravAt) / 1000.0f / GRAVITY_TAU_S) : 1;
  for (int i = 0; i < 3; i++) grav[i] += k * (a[i] - grav[i]);
  gravOk = true;
  gravAt = millis();
  bool swing = false;
  if (!swinging && side > shockThreshold) {
    swinging = true;
    swing = swingAt && millis() - swingAt <= SWING_MS && h[0] * swingDir[0] + h[1] * swingDir[1] + h[2] * swingDir[2] < 0;
    for (int i = 0; i < 3; i++) swingDir[i] = h[i];
    swingAt = millis() | 1;
  } else if (swinging && side < 0.6f * shockThreshold) {
    swinging = false;
  }
  if (!docked) return;
  // Test bench ('chocs' command): shock magnitude used to tune the jostling threshold.
  static float peakG = 0, peakSide = 0;
  static uint32_t peakAt = 0;
  float dev = fabsf(sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) - 1);
  if (dev > peakG) { peakG = dev; peakAt = millis(); }
  if (side > peakSide) { peakSide = side; peakAt = millis(); }
  if ((peakG > 0 || peakSide > 0) && millis() - peakAt >= 300) {
    if ((peakG >= 0.15f || peakSide >= 0.15f) && int32_t(shockDebugUntil - millis()) > 0)
      Serial.printf("choc %.2f g (écart à la pesanteur %.2f g)\n", peakG, peakSide);
    peakG = peakSide = 0;
  }
  // During the speech, the speaker (loudspeaker) vibrates the torso—neither "raised" nor "shaken".
  bool quiet = speechRecent();
  // Shoved (one or two light pushes suffice) → disturbed. Third push → angry; each additional push escalates (frown: orange → red). Demanding a true jerk (3 rapid impacts in 1 s) rarely worked. A single impact followed by a "lifted" action does not count.
  static uint8_t shakes = 0;
  static uint32_t lastShake = 0, bumpAt = 0;
  static bool bumpPending = false;
  static float bumpPeak = 0;
  if (!quiet && (bodyMotion.jolt || swing) && !bumpPending && millis() - lastShake >= BUMP_GAP_MS) {
    bumpPending = true;
    bumpAt = millis();
    bumpPeak = 0;
  }
  if (bumpPending && dev > bumpPeak) bumpPeak = dev;
  if (bumpPending && (bodyMotion.lift || quiet)) {
    bumpPending = false;
    if (int32_t(shockDebugUntil - millis()) > 0) Serial.println("à-coup non compté (soulevé ou parole)");
  }
  // A finger on the screen also causes a jolt, and the torso bounces when it is lifted: during the contact and up
  // to FINGER_AFTER_MS after, it is a tap, unless the shock is strong (a hand pushing). During a game, even a strong
  // one: a child presses hard and long, and each answer counted as jostling (angry at the third).
  bool finger = touchDownEver && millis() - touchSeenAt <= millis() - bumpAt + FINGER_AFTER_MS &&
                (touchForGame || bumpPeak < 2 * shockThreshold);
  if (bumpPending && finger) {
    bumpPending = false;
    if (int32_t(shockDebugUntil - millis()) > 0) Serial.printf("à-coup du doigt (%.2f g), pas une bousculade\n", bumpPeak);
  }
  if (bumpPending && millis() - bumpAt >= BUMP_CONFIRM_MS) {
    bumpPending = false;
    if (millis() - lastShake > SHAKE_MEMORY_MS) shakes = 0;
    lastShake = millis();
    if (shakes < 255) shakes++;
    bodyBumpAt = millis();
    bodyBumpEver = true;
    if (int32_t(shockDebugUntil - millis()) > 0) Serial.printf("bousculade %u\n", shakes);
    if (shakes <= SHAKES_WORRIED) {
      uint8_t worried[2] = {HeadExpressions::INQUIET, 25};   // 2,5 s
      neckSend(NECK_EXPRESSION, worried, sizeof(worried));
    } else {
      NeckEvent e = {EV_SHAKE, 0};
      neckSend(NECK_EVENT, &e, sizeof(e));
      // Anger rises: he says it, irritated (phrase *"shake"* stored in flash, complete edition), once per brawl and no more than once every 20 s; otherwise he growls (anger cry). Never during gameplay.
      static uint32_t scoldAt = 0;
      static bool scolded = false;
      if (shakes == SHAKES_WORRIED + 1 && !game.active() && (!scolded || millis() - scoldAt >= SCOLD_GAP_MS) &&
          Speaker::has("secoue") && Speaker::play("secoue")) {
        scolded = true;
        scoldAt = millis();
      }
    }
  }
  if (!quiet && bodyMotion.lift) {
    NeckEvent e = {EV_LIFT, 0};
    neckSend(NECK_EVENT, &e, sizeof(e));
    cry(MoodSounds::SURPRIS, "soulevé");
  }
  if (bodyMotion.lyingChanged) {
    NeckEvent e = {EV_LYING, int16_t(bodyMotion.lying ? 1 : 0)};
    neckSend(NECK_EVENT, &e, sizeof(e));
  }
}

// Micro: loud noise triggers head jolt (except its own voice); level for dashboard.
static void pollMicrophone() {
  if (!micReady) return;
  bool noise = Microphone::takeNoise();
  if (!speechRecent()) {
    float db = Microphone::levelDbfs() + DBFS_TO_DB;
    soundDb = uint8_t(db < 0 ? 0 : db > 130 ? 130 : lroundf(db));
    soundValid = true;
  }
  if (!noise || speechRecent() || !docked || Microphone::listening()) return;   // The speaker does not make it jump (reflexively).
  NeckEvent e = {EV_NOISE, 0};
  neckSend(NECK_EVENT, &e, sizeof(e));
  cry(MoodSounds::SURPRIS, "bruit");
}

// End of listening to a question: "sound <bytes> <sum>"—then the voice (the UNO Q reads aloud), nothing lost.
static void pollListening() {
  if (!listenPending || Microphone::listening()) return;
  listenPending = false;
  int n = 0;
  const uint8_t* voice = (const uint8_t*)Microphone::heard(n);
  uint32_t somme = 0;
  for (long i = 0; i < 2L * n; i++) somme += voice[i];
  Serial.setTxTimeoutMs(DUMP_TX_TIMEOUT_MS);
  Serial.printf("son %ld %lu\n", 2L * n, (unsigned long)somme);
  for (long i = 0; i < 2L * n; i += 4096) {             // piecewise, the presence of the head between two
    Serial.write(voice + i, size_t(2L * n - i < 4096 ? 2L * n - i : 4096));
    sendToHead(nullptr);
  }
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
}

static void sendHeadEvent(int16_t event) {
  NeckEvent e = {EV_FACE, event};
  neckSend(NECK_EVENT, &e, sizeof(e));
}

static void playOnHead(uint8_t expression, uint8_t tenths) {
  uint8_t payload[2] = {expression, tenths};
  neckSend(NECK_EXPRESSION, payload, sizeof(payload));
}

static uint32_t timerLeftS() {   // rounded at the top: only 0 at the end
  const int32_t left = int32_t(timerEndMs - millis());
  return timerOn && left > 0 ? (uint32_t(left) + 999) / 1000 : 0;
}

static void timerStart(uint32_t seconds) {
  timerDone = false;
  timerOn = true;
  timerEndMs = millis() + seconds * 1000;
  uint32_t now;
  TimeSource::setTimerEnd(TimeSource::now(now) ? now + seconds : 0);   // unknown time: no restart on reboot
}

// Cancelled, or alarm stopped (touch, "timer end")
static void timerStop() {
  if (timerOn || timerDone) TimeSource::setTimerEnd(0);
  if (timerDone) {
    Speaker::stop();
    joyUntil = millis();
  }
  timerOn = timerDone = false;
}

// Timer running before restart: resume or state immediately if finished during pause.
static void timerResume() {
  uint32_t end, now;
  if (!Edition::COMPLETE || !(end = TimeSource::timerEnd()) || !TimeSource::now(now)) return;
  if (end > now + TIMER_MAX_S || now > end + TIMER_LATE_S) {
    TimeSource::setTimerEnd(0);
    return;
  }
  timerOn = true;
  timerEndMs = millis() + (end > now ? (end - now) * 1000 : 0);
}

// Timer end: it activates before the segment and triggers "Who speaks?" and continues indefinitely until screen touch or "timer end" is detected; the end time remains unchanged until then. Not triggered over a phrase or while listening—interruptions are cut (Speaker::talking does not count), and interrupt adjustments do not affect the ringtone.
static void pollTimer() {
  const uint32_t now = millis();
  if (timerOn && int32_t(timerEndMs - now) <= 0) {
    timerOn = false;
    timerDone = true;
    ringStep = 0;
    ringNext = now + 300;   // `pollGame` statement ends here.
    game.quit();
    whoUntil = now;
  }
  if (!timerDone) return;
  joyUntil = now + 1000;    // The head celebrates even when dormant.
  if (int32_t(ringNext - now) <= 0 && !Speaker::talking() && !Microphone::listening()) {
    const bool phrase = ringStep == 2;
    const int samples = phrase ? TimerVoice::SAMPLES : TimerVoice::RING_SAMPLES;
    Speaker::playPcm(phrase ? TimerVoice::DATA : TimerVoice::RING, samples);
    ringStep = (ringStep + 1) % 3;
    ringNext = now + uint32_t(samples) / 16 + RING_GAP_MS;   // 16 samples per millisecond
  }
}

// Bubble button: If the Mac is ready, the screen asks who speaks (complete edition, household module in `Household_local.h`, out of game).
// Otherwise, the exchange happens immediately; without a brain, the head doesn’t respond.
static void pressTalk() {
  if (brain == BRAIN_NONE) playOnHead(HeadExpressions::PERPLEXE, 15);
  else if (brain == BRAIN_READY && Edition::COMPLETE && Household::COUNT && !game.active()) whoUntil = millis() + WHO_MS;
  else talkAs(-1);
}

// "Talk button `<nom>` ready on Mac, adjusts responses to user (−1: unnamed). Confirms with "brain occupied", otherwise grey-outs bubble after 5 s.
static void talkAs(int who) {
  whoUntil = millis();
  if (brain != BRAIN_READY) return;
  char line[40];
  const int n = who >= 0 ? snprintf(line, sizeof(line), "bouton parler %s\n", Household::PEOPLE[who].id)
                         : snprintf(line, sizeof(line), "bouton parler\n");
  Serial.write((const uint8_t*)line, n);
  brain = BRAIN_ASKED;
  brainAt = millis();
}

static void keepBrain() {
  if (brain != BRAIN_NONE && millis() - brainAt >= BRAIN_TIMEOUT_MS[brain]) brain = BRAIN_NONE;
  if (brain != BRAIN_READY && choosing()) whoUntil = millis();   // The Mac has stopped functioning or already speaks: no options left.
}

// Clearance ready: complete edition, phrases aligned (first and last sent to `_robot.py`).
static void checkGame() {
  char last[16];
  snprintf(last, sizeof(last), "devine_a%02d", GuessData::ANIMALS - 1);
  gameReady = Edition::GAMES && Speaker::has("devine_intro") && Speaker::has(last);
}

// Part of *"Guess the Animal"* in progress: head phrases, expressions; remove head (stop).
// 
// ---
// Documentation:
// - head = head
// - arrête = stop (verb form)
static void pollGame() {
  if (game.takeSulk()) sendHeadEvent(HeadEvents::IGNORED);   // unfinished part: left idle *(or: "left incomplete")*
  if (!game.active()) return;
  if (!docked) game.quit();
  game.update(millis(), Speaker::playing());
  if (game.takeStop()) Speaker::stop();
  char name[16];
  if (const char* p = game.takePhrase(name, sizeof(name))) Speaker::play(p);
  uint8_t e, t;
  if (game.takeExpression(e, t)) playOnHead(e, t);
}

// Suspicious, puzzled, dizzy and tired: see MoodTriggers.h.
static void pollMoodTriggers() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 50) return;
  float dt = (now - last) / 1000.0f;
  last = now;
  Ambient::Reading env;
  bool envOk = Ambient::latest(env);
  MoodTriggers::In in;
  in.docked = docked && !game.active();   // During clearance, the torso no longer decides mood.
  in.radarMoving = radarMoving;
  in.present = radarPresent;
  in.cameraOn = cameraOn;
  in.seen = faceSeen;
  in.size = faceSize;
  in.lightOk = envOk && env.lightOk;
  in.dark = roomDark;
  in.lux = envOk ? env.lux : 0;
  in.gyroDps = gyroDps;
  in.moving = bodyBumpEver && now - bodyBumpAt < 2000;
  MoodTriggers::Action a = moodTriggers.update(in, now, dt);
  switch (a) {
    case MoodTriggers::MEFIANT: playOnHead(HeadExpressions::MEFIANT, 25); break;
    case MoodTriggers::ETOURDI: playOnHead(HeadExpressions::ETOURDI, 30); break;
    case MoodTriggers::FATIGUE: sendHeadEvent(HeadEvents::TIRED); break;   // mood/appearance: stainless steel colors, head=head, torso=torso
    case MoodTriggers::PERPLEXE: sendHeadEvent(HeadEvents::UNANSWERABLE); break;
    case MoodTriggers::NONE: return;
  }
  Serial.printf("Torse : humeur %s\n", MoodTriggers::name(a));
}

// Head detached: tapping 'Where is my head?' makes it ring for 10 seconds.
// Dashboard speaker button: mutes or restores mood sounds; spoken phrases remain enabled. Pressing a button is not a caress, so nothing is sent to the head.
// 'Guess the animal': the paw button starts the game. During play, the screen accepts only answer-button input and sends nothing to the head.
// Head attached: touching the torso is touching MoodiMOS; the head responds as it does to its own screen. A tap calms it; three quick taps or rubbing tickle it; a long press with a still finger gives a kiss. With cuddles disabled (Instructable edition), only the tap is enabled.
// Holding the torso to jostle, lift or turn it is not a caress: contact during or immediately after these movements is ignored. Otherwise it could cancel freshly triggered anger or give a kiss while being jostled.
constexpr uint32_t TOUCH_AFTER_BUMP_MS = 1500;
// The finger initiated an exchange: an earbud for it. Pressing "Qui parle ?" or touching the gray bubble yields no result.
static void grantMic(uint32_t now) {
  micGrant = brain == BRAIN_ASKED;
  micGrantUntil = now + MIC_GRANT_MS;
}

static void pollTouchScreen() {
  static uint32_t lastPoll = 0, downAt = 0, lastTapAt = 0;
  static bool wasDown = false, longFired = false, rubFired = false, spoiled = false;
  static int16_t lastX = 0, lastY = 0;
  static int moved = 0;
  static uint8_t taps = 0;
  uint32_t now = millis();
  if (now - lastPoll < 50) return;
  lastPoll = now;
  int16_t x, y;
  bool down = Board::readTouch(x, y);
  bool waking = false;
  const bool picking = choosing();   // Before activation: the touch that opens the screen selects no one.
  if (down && int32_t(touchDebugUntil - now) > 0) Serial.printf("tactile x=%d y=%d\n", x, y);
  if (down && !wasDown && !docked && !picking && !timerDone && !asking() && Dashboard::ringButtonHit(x, y)) HeadLink::ring(100);
  if (down) touchSeenAt = now;
  if (down && !wasDown) {
    downAt = now; moved = 0; longFired = rubFired = false;
    touchDownAt = now;
    touchDownEver = true;
    touchForGame = false;
    spoiled = bodyBumpEver && now - bodyBumpAt < TOUCH_AFTER_BUMP_MS;
    waking = sleepShown;                      // The display was dormant: touch awakened it, without targeting a button.
    torsoAwakeUntil = now + TORSO_AWAKE_MS;
    if (!waking && dashShown && Dashboard::criesButtonHit(x, y)) {
      Speaker::toggleCries();
      Serial.printf("Cris : %s (bouton de l'écran)\n", Speaker::cryLevel() ? "entendus" : "muets");
      spoiled = true;
    }
    if (!waking && dashShown && Dashboard::talkButtonHit(x, y)) {
      pressTalk();
      grantMic(now);
      spoiled = true;
    }
  }
  if (timerDone) {   // finished timer: a touch silences it, nothing else.
    if (down && !wasDown) {
      spoiled = true;   // the raised finger, screen off, is not a tap.
      timerStop();
    }
    taps = 0;
    wasDown = down;
    return;
  }
  if (asking()) {   // Access request: - Finger placed for 2 sec → granted - Lifted before placement → denied - Finger already placed → no action
    static bool holding = false;
    static uint32_t holdAt = 0, seenAt = 0;
    static uint8_t drops = 0;
    if (holding && int32_t(holdAt - askAt) < 0) holding = false;   // request submission feedback
    if (down && !wasDown) {
      spoiled = true;
      if (holding) drops++;
    }
    if (down && !holding && int32_t(downAt - askAt) >= 0) {
      holding = true;
      holdAt = now;
      drops = 0;
    }
    if (down) seenAt = now;
    askHeld = holding ? constrain((now - holdAt) / float(ACCESS_HOLD_MS), 0.0f, 1.0f) : 0;
    if (holding && down && now - holdAt >= ACCESS_HOLD_MS) {
      accessUntil = now + ACCESS_MS;
      askUntil = now;
      holding = false;
      Serial.printf("accès : autorisé pour 10 min (contact perdu %u fois)\n", drops);
    } else if (holding && ((!down && now - seenAt >= ACCESS_LIFT_MS) || drops > ACCESS_DROPS)) {
      askUntil = now;
      holding = false;
      Serial.printf("accès : refusé (contact perdu %u fois)\n", drops);
    }
    taps = 0;
    wasDown = down;
    return;
  }
  if (picking) {   // Who speaks? → A round initiates the exchange; the cross closes the screen.
    if (down && !wasDown) {
      spoiled = true;   // The raised finger after selection, screen off, is not a tap.
      const int who = Dashboard::whoButtonHit(x, y);
      if (who == Household::COUNT) whoUntil = now;
      else if (who >= 0) {
        talkAs(who);
        grantMic(now);
      }
    }
    taps = 0;
    wasDown = down;
    return;
  }
  if (!game.active() && brain < BRAIN_ASKED && down && !wasDown && !waking && docked && gameReady &&
      Dashboard::playButtonHit(x, y))   // No moving parts during conversation.
    game.start(now);
  if (game.active()) {
    if (down && !wasDown) {
      touchForGame = true;
      int b = Dashboard::gameButtonHit(x, y);
      if (b == 3) game.quit();
      else if (b >= 0) game.press(GuessEngine::Answer(b), now);
    }
    taps = 0;
    wasDown = down;
    return;
  }
  if (down && bodyBumpEver && int32_t(bodyBumpAt - downAt) >= 0) spoiled = true;   // bumped during contact
  if (spoiled) { taps = 0; wasDown = down; return; }
  if (down && wasDown) {
    int d = abs(x - lastX) + abs(y - lastY);
    if (d < 200) moved += d;   // An abnormal jump in the controller is not a friction issue.
  }
  if (down) { lastX = x; lastY = y; }
  if (docked && Edition::CUDDLES && down) {
    if (!rubFired && moved >= TOUCH_RUB_PX) {
      rubFired = true;
      sendHeadEvent(HeadEvents::TICKLE);
      cry(MoodSounds::RIRE);
    }
    if (!longFired && moved < TOUCH_MOVED_PX && now - downAt >= TOUCH_LONG_MS) {
      longFired = true;
      sendHeadEvent(HeadEvents::KISS);
      cry(MoodSounds::BISOU);
    }
  }
  if (docked && !down && wasDown && !longFired && !rubFired && moved < TOUCH_MOVED_PX &&
      now - downAt < TOUCH_TAP_MAX_MS) {
    taps = now - lastTapAt < TOUCH_BURST_GAP_MS && taps < 255 ? taps + 1 : 1;
    lastTapAt = now;
    if (taps >= 3 && Edition::CUDDLES) {   // Each additional tap extends it further.
      sendHeadEvent(HeadEvents::TICKLE);
      cry(MoodSounds::RIRE);
    }
    else if (taps <= 2) sendHeadEvent(HeadEvents::TAP);
  }
  wasDown = down;
}

// Voice: The head’s mouth follows the sound envelope at 30 Hz, then closes.
// A hysterical scream does not activate speech (the expression retains its mouth), but silences the microphone and IMU.
static void pollSpeech() {
  static uint32_t lastSpeak = 0;
  static bool wasSpeaking = false;
  if (Speaker::playing()) speechUntil = millis() + SPEECH_TAIL_MS;
  bool speaking = Speaker::talking();
  if (!docked) { wasSpeaking = false; return; }
  if (speaking && millis() - lastSpeak >= 33) {
    lastSpeak = millis();
    uint8_t amp = Speaker::amplitude();
    if (amp == 0) amp = 1;                 // `0` = halt / end of transmission
    neckSend(NECK_SPEAK, &amp, 1);
  } else if (!speaking && wasSpeaking) {
    uint8_t end = 0;
    neckSend(NECK_SPEAK, &end, 1);
  }
  wasSpeaking = speaking;
}

// At startup, MoodiMOS displays: `/voix/bonjour.wav` from the TF card if it exists, otherwise the firmware-stored phrase:
// *« Bonjour, je suis MoodiMOS, ton compagnon de bureau… »*. After 4 s: the head starts and its mouth follows the voice.
static void pollGreeting() {
  constexpr uint32_t GREETING_MS = 4000;
  static bool done = false;
  if (done || millis() < GREETING_MS) return;
  done = true;
  if (micReady && !Speaker::play("bonjour")) Speaker::sayDemo();
}

// The head sleeps (frame MOOD recent): the torso sleeps with it, unless it has just been touched.
static bool torsoSleeps() {
  return docked && headPhase == MOOD_DORT && millis() - moodAt < MOOD_TIMEOUT_MS &&
         int32_t(torsoAwakeUntil - millis()) <= 0;
}

static void guardBattery(const Board::Power& p) {
  static bool cut = false;
  bool low = p.valid && p.battery && !p.usb && p.percent <= LOW_BATTERY_PCT;
  if (low != cut) {
    cut = low;
    Board::setMainPower(!cut);
    Serial.println(cut ? "Batterie vide : 5 V coupé" : "5 V rétabli");
  }
}

void setup() {
  Serial.setRxBufferSize(SERIAL_RX_BUFFER);
  Serial.begin(115200);
  // USB port connected but closed on Mac: each message blocked for up to 2 s (20 × 100 ms), no head (head) beats, appearing detached (white, then joy). Lost messages instead of expected ones.
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
  Board::begin();
  Board::setMainPower(true);

  displayReady = Dashboard::begin();
  if (!displayReady) Serial.println("Écran : échec de l'initialisation (PSRAM = OPI ?)");
  neck.begin(NECK_BAUD, SERIAL_8N1, Board::PIN_NECK_RX, Board::PIN_NECK_TX);
  TorsoCamera::begin();
  Ambient::begin();
  Serial.printf("Capteurs : %s\n", Ambient::status());
  HeadLink::begin();
  TimeSource::begin();
  timerResume();
  imuReady = Board::imuBegin();
  if (!imuReady) Serial.println("IMU : absente");
  // Whole robot on legs, heavier than just the head: sudden impact at 0.3 g (1 g rarely left it; 0.4 g required strong shaking); adjustable via "chocs <threshold>" in real-time.
  // 
  // 2–3 close shakes each trigger anger, like with the head, escalating to red.
  torsoMotion.setShakeThreshold(shockThreshold);
  torsoMotion.setShakeCooldown(1200);
  micReady = Microphone::begin();
  if (!micReady) Serial.println("Micro : codec ES8311 absent");
  speakerReady = micReady && Speaker::begin();
  if (micReady && !speakerReady) Serial.println("Voix : ni carte TF ni phrase en flash");
  checkGame();
  Serial.printf("MoodiMOS torse prêt (édition %s).\n", Edition::NAME);
  if (bridgeMark == BRIDGE_MARK) {
    Serial.printf("Dernier pont : %u octets vers la tête, %u reçus d'elle\n", unsigned(bridgeToHead), unsigned(bridgeFromHead));
    bridgeMark = 0;
  }
}

void loop() {
  static uint32_t lastDraw = 0;
  static bool drawnCriesOn = false;   // status of the button for the last scream drawing
  static uint8_t drawnBrain = 0;      // status of the bubble button in the latest drawing

  keepDeadlinesPast();
  pollSerial();
  TimeSource::update();
  pollExpander();
  updateDock();
  pollImu();
  pollMicrophone();
  pollListening();
  pollTimer();
  keepBrain();
  pollSpeech();
  pollGreeting();
  pollGame();
  HeadLink::setActive(!docked);
  pollTouchScreen();
  readNeck();
  updateCamera();
  pollCamera();
  Ambient::update();
  pollMoodTriggers();

  Ambient::Reading env;
  bool envOk = Ambient::latest(env);
  sendToHead(envOk ? &env : nullptr);

  // Monitored battery regardless of display (joy, clearance, sleep)
  static Board::Power pw = {};
  static uint32_t lastPower = 0;
  if (!lastPower || millis() - lastPower >= 500) {
    lastPower = millis();
    pw = Board::readPower();
    guardBattery(pw);
  }

  bool worried = int32_t(worriedUntil - millis()) > 0, joyful = int32_t(joyUntil - millis()) > 0;
  const bool picking = choosing();
  const bool askShown = asking() && !timerDone;
  // During the timer, the torso does not fall asleep with the head; the remaining time is displayed on the screen.
  bool sleeping = !worried && !joyful && !game.active() && !picking && !timerOn && !timerDone && !askShown && torsoSleeps();
  sleepShown = displayReady && sleeping;
  dashShown = displayReady && !worried && !joyful && !game.active() && !picking && !sleeping && !timerDone && !askShown;
  // Sleep mode (5 min): screen turns off simultaneously with the head display; auto-reboots on wake or touch.
  static uint32_t asleepAt = 0;
  static bool wasSleeping = false;
  if (sleeping && !wasSleeping) asleepAt = millis();
  wasSleeping = sleeping;
  bool dark = displayReady && sleeping && millis() - asleepAt >= SLEEP_OFF_MS;
  if (dark != screenDark) {
    screenDark = dark;
    Board::setBacklight(!dark);
  }
  if (timerDone) {   // Finished timer, above all else (~12 images/sec)
    static uint32_t lastDone = 0;
    if (displayReady && millis() - lastDone >= 80) {
      lastDone = millis();
      Dashboard::drawTimerDone(millis() / 1000.0f);
    }
    lastDraw = 0;
  } else if (askShown) {   // access granted: visible, even asleep
    static uint32_t lastAsk = 0;
    if (displayReady && millis() - lastAsk >= 80) {
      lastAsk = millis();
      Dashboard::drawAccess(askedCmd, askHeld);
    }
    lastDraw = 0;
  } else if (displayReady && (worried || joyful)) {   // ≈ 25 images/sec during distress or joy
    static uint32_t lastFace = 0;
    if (millis() - lastFace >= 40) {
      lastFace = millis();
      if (worried) Dashboard::drawWorried(headTheme, millis() / 1000.0f);
      else Dashboard::drawJoy(millis() / 1000.0f);
    }
    lastDraw = 0;   // dashboard returns at the end.
  } else if (game.active()) {   // game screen, redrawn on every change (question, touched button)
    static uint32_t drawnRev = 0;
    if (displayReady && (lastDraw || game.revision() != drawnRev)) {
      drawnRev = game.revision();
      GameView v = {game.text(), game.round(), game.buttons(), game.pressed(millis())};
      Dashboard::drawGame(v);
    }
    lastDraw = 0;
  } else if (picking) {   // "Who speaks?" immediately, then every 500 ms (after the joy of an announcement).
    static uint32_t lastWho = 0;
    if (displayReady && (lastDraw || millis() - lastWho >= 500)) {
      lastWho = millis();
      Dashboard::drawWho();
    }
    lastDraw = 0;
  } else if (sleeping) {   // The head sleeps: the torso sleeps with it (the "z" elements rise, ≈ 12 images/s).
    static uint32_t lastSleep = 0;
    if (displayReady && !screenDark && millis() - lastSleep >= 80) {
      lastSleep = millis();
      Dashboard::drawSleep(headTheme, millis() / 1000.0f);
    }
    lastDraw = 0;
  } else if (millis() - lastDraw >= 500 || drawnCriesOn != (Speaker::cryLevel() > 0) ||
             drawnBrain != (brain >= BRAIN_ASKED ? 2 : brain)) {   // touch button: immediately
    lastDraw = millis();

    DashData d = {};
    d.climateValid = env.climateOk;
    d.humidityValid = env.humidityOk;
    d.lightValid = env.lightOk;
    d.soundValid = soundValid;
    d.tempC = env.tempC100 / 100.0f;
    d.humPct = env.humPct100 / 100.0f;
    d.pressHpa = env.pressHpa10 / 10.0f;
    d.lux = env.lux;
    d.soundDb = soundDb;
    d.cameraOn = TorsoCamera::running();  // CAM indicator: an actual capture, not merely a request
    d.wifi = TimeSource::wifi();
    d.powerValid = pw.valid;
    d.usb = pw.usb;
    d.charging = pw.charging;
    d.battery = pw.battery;
    d.batteryPct = pw.percent;
    d.headDocked = docked;
    d.headOnline = docked && headSeen && millis() - headHelloAt < HEAD_TIMEOUT_MS;
    d.headBatteryPct = d.headOnline ? headBatteryPct : -1;
    d.headCharging = d.headOnline && headCharging;
    d.radarPresent = radarPresent;
    d.radarOnline = expanderOk;
    d.radarCm = 0;   // Output OUT only: no pitch
    MoodiHeadStatus away;
    uint32_t awayAge = 0;
    d.awayStatus = !docked && HeadLink::status(away, awayAge);
    d.awayBatteryPct = d.awayStatus ? away.batteryPct : MOODI_UNKNOWN;
    d.awaySeenS = awayAge / 1000;
    d.ringState = uint8_t(HeadLink::ringState());
    d.gameReady = gameReady;
    d.criesOn = drawnCriesOn = Speaker::cryLevel() > 0;
    d.brain = drawnBrain = brain >= BRAIN_ASKED ? 2 : brain;
    d.timerS = timerLeftS();
    d.accessMin = accessOpen() ? uint8_t((accessUntil - millis() + 59999) / 60000) : 0;
    // Emotions' colors while the head is placed and speaking; otherwise, original colors.
    // *(Uncertainty of removal retains the last received colors.)*
    DashTheme theme = headTheme;
    theme.valid = docked && headTheme.valid && millis() - moodAt < MOOD_TIMEOUT_MS;
    if (displayReady) Dashboard::draw(d, theme);
  }
  delay(2);
}

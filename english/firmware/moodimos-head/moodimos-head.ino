// MoodiMOS — firmware for the head (Waveshare ESP32-S3-Touch-LCD-2.1)
// 
// Arduino IDE Settings (Tools):
// - Board: ESP32S3 Dev Module
// - PSRAM: OPI PSRAM
// - Flash Size: 16MB (128Mb)
// - Partition: 16M Flash (3MB APP/9.9MB FATFS)
// - USB CDC On Boot: Enabled (USB serial monitor active)
// 
// Head + UART Couple:
// - Detects presence, environment, events, expression, speech, and gaze
// - FACE frames calculated by the torso from its OV5640 camera.
// 
// Programming:
// - Head detached from the neck (GPIO43/44 shared with USB-serial bridge).
// - Head closed on robot: programmed via the neck, using USB from the torso (`firmware/flash_head_neck.sh`).
// 
// Serial Monitor Simulation:
// - Baud rate: 115200, line ending: "Newline" (`\n`).

#include "Board.h"
#include "MoodiMOSEdition.h"
#include "Display.h"
#include "Canvas.h"
#include "Face.h"
#include "FaceEngine.h"
#include "NeckReceiver.h"
#include "Watch.h"
#include "WatchMode.h"
#include "TimeUtil.h"
#include "MotionEvents.h"
#include "PowerPolicy.h"
#include "HeadBle.h"
#include "BatteryEstimate.h"
#include "RingPattern.h"
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/rtc_cntl_reg.h>

Canvas canvas(Display::WIDTH, Display::HEIGHT);
FaceRenderer renderer;
FaceEngine face;
HardwareSerial& neck = Serial1;
NeckParser neckParser;
NeckReceiver torso(face);

Watch watch;
WatchMode watchMode;
bool watchReady = false;
uint16_t* faceBackground = nullptr;
uint8_t* faceGradient = nullptr;   // grayscale gradient (0–255 per pixel), recolored based on mood

// Robot face design: selected via menu (hold for 3 sec) or via command "design", retained in memory.
Preferences prefs;
Design design = defaultDesign();
bool repaintFace = false;          // Redesigned: repaint base.
bool menuOpen = false;
Design menuPick = defaultDesign();
uint32_t menuTouchedAt = 0;
constexpr uint32_t MENU_IDLE_MS = 10000;   // do not touch: the shown design is retained.

static void applyDesign(Design d) {
  renderer.setDesign(d);
  face.mood.design = d;
  repaintFace = true;
}

// Proprietary key for complete edition: a selection retained by another edition (classic or old watch) does not replace the neon face.
constexpr const char* DESIGN_KEY = Edition::COMPLETE ? "design_neon" : "design";

static void saveDesign(Design d) {
  if (d != design) prefs.putUChar(DESIGN_KEY, uint8_t(d));
  design = d;
  applyDesign(d);
}

static void openMenu() {
  if (designCount() < 2) return;   // Instructable edition: single design, no menu
  menuOpen = true;
  menuPick = design;
  menuTouchedAt = millis();
}

static void closeMenu() {   // Keep the shown design.
  menuOpen = false;
  saveDesign(menuPick);
  face.play(Expr::Content, 1.5f);
  Serial.printf("design : %s\n", designName(design));
}

// Head IMU, used only when detached; while on the body, the torso makes the decisions.
// Chip-to-robot axis mapping was bench-checked on 01/10/2026 with imu:
// lying flat with the screen facing upwards, the chip Z axis reads −1, so forward = −Z.
constexpr MotionEvents::Axes HEAD_AXES = {1, 2, -3};
MotionEvents headMotion(HEAD_AXES);
MotionEvents::Output headPose;
bool imuReady = false;
uint32_t rtcSavedRevision = 0;

PowerPolicy power;   // Pendant (necklace): - Head (visage) → Screen off → Deep sleep mode
bool dozing = false;
// Sleep mode display:
// Onset of dimming after 30 s of sleep → off after 5 min;
// Auto-reboot on wake-up.
constexpr float SLEEP_DIM_S = 30, SLEEP_OFF_S = 300;
constexpr uint8_t NIGHT_LEVEL = 10;   // Backlight standby power percentage
uint8_t dayLevel = 80;                // awake (command "lum")
bool screenOff = false;
uint8_t backlightNow = 80;            // for the 'etat' command
bool interaction = false;             // When touched or moved: the screen turns back on.
// Timing of an image (command "perf"): sliding averages in µs
float perfLogic = 0, perfClear = 0, perfDraw = 0, perfPresent = 0;

// Bluetooth pendant: "Where’s my head?" and estimated battery life.
HeadBattery headBattery;
RingPattern ringer;
bool bleStarted = false;
uint8_t bootRing = 0;   // Alarm received during 30-second wake-up

static void neckSend(uint8_t type, const void* payload, uint8_t len) {
  uint8_t frame[NECK_MAX_PAYLOAD + 5];
  size_t n = neckEncode(type, payload, len, frame);
  if (n) neck.write(frame, n);
}

// Neural programming via UART0 (GPIO43/44, neck wiring): ROM in download mode listens to UART0 without firmware dependency. Mode entry is exclusive; esptool triggers a watchdog-reset to restart the head.
static void rebootForUpdate() {
  Serial.println(F("Programmation par le cou : redémarrage en mode téléchargement"));
  Serial.flush();
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

// At startup, before anything else: a firmware that fails further can still be reprogrammed via the neck (torso bridge repeats its request for 2 s).
static void catchUpdateRequest(uint32_t ms) {
  NeckParser p;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (neck.available())
      if (p.feed(neck.read(), millis()) && neckIsUpdate(p.type, p.payload, p.len)) rebootForUpdate();
    delay(2);
  }
}

static void pollNeck() {
  torso.update(millis());
  torso.lineLevel(digitalRead(Board::PIN_NECK_RX), millis());   // Head removed: line at 0
  // Budget constrained: even a parasitic flow must not block rendering.
  for (unsigned n = 0; n < 256 && neck.available(); ++n) {
    if (!neckParser.feed(neck.read(), millis())) continue;
    if (neckIsUpdate(neckParser.type, neckParser.payload, neckParser.len)) rebootForUpdate();
    torso.receive(neckParser.type, neckParser.payload, neckParser.len, millis());
  }
}

// Battery (V2): Measured every 2 s, detached or charging on the neck. Low detachment → fatigued.
static void pollBattery() {
  static uint32_t last = 0;
  if (!Board::HAS_BATTERY || (last && millis() - last < 2000)) return;
  last = millis();
  headBattery.sample(Board::batteryVolts(), torso.connected() && !torso.detached());
  face.in.lowBattery = headBattery.low();
}

// Detached: Bluetooth announcement with battery and voice output, torso-triggered ringtone.
static void pollPendantBle() {
  uint32_t now = millis();
  bool away = !torso.connected();
  if (away && !bleStarted) {
    HeadBle::begin();
    bleStarted = true;
  }
  if (!bleStarted) return;
  HeadBle::setAdvertising(away);

  static uint32_t lastStatus = 0;
  if (away && now - lastStatus >= 2000) {
    lastStatus = now;
    MoodiHeadStatus st = {headBattery.percent(), uint8_t(headBattery.charging()), uint8_t(face.state())};
    HeadBle::setStatus(st);
  }

  uint8_t tenths;
  if (HeadBle::takeRing(tenths)) {
    if (tenths == 0) {
      ringer.stop();
    } else {
      ringer.start(now, uint32_t(tenths) * 100);
      power.activity(now);
      face.play(Expr::Surpris, 1.5f);
    }
  }
  static bool buzzing = false;
  bool on = ringer.buzzerOn(now);
  if (on != buzzing) {
    buzzing = on;
    Board::setBuzzer(on);
  }
  if (ringer.active(now)) power.activity(now);
}

static void showTime() {
  if (watchReady) watchMode.show(millis());
}

// Pendant (pendant):
// Elevated, shaken, laid, tilted, and *"watching the time"* come from the head.
// 
// The IMU is read at 50 Hz by a separate task: drawing an image takes ≈ 60 ms—too slow to capture acceleration spikes from a secousse. Detected gestures are retained until the next loop.
portMUX_TYPE imuLock = portMUX_INITIALIZER_UNLOCKED;
MotionEvents::Output imuLatest;
bool imuShake = false, imuLift = false, imuLook = false, imuLyingChanged = false, imuMoved = false, imuFresh = false;
bool imuJolt = false;
volatile uint32_t inputTicks = 0, touchMicros = 0, imuMicros = 0;   // diagnostic (command « perf »)

static void scanTouch(uint32_t now);

static void imuTask(void*) {   // fast inputs: tactile and IMU, every 20 ms
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
    uint32_t u0 = micros();
    scanTouch(millis());
    uint32_t u1 = micros();
    inputTicks++;
    touchMicros = u1 - u0;
    float a[3], g[3];
    if (!imuReady || !Board::imuRead(a, g)) continue;
    imuMicros = micros() - u1;
    MotionEvents::Output o = headMotion.update(a, g, millis());
    bool spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) > 30;
    portENTER_CRITICAL(&imuLock);
    imuLatest = o;
    imuShake |= o.shake; imuLift |= o.lift; imuLook |= o.lookUp; imuLyingChanged |= o.lyingChanged;
    imuJolt |= o.jolt;
    imuMoved |= spin;
    imuFresh = true;
    portEXIT_CRITICAL(&imuLock);
  }
}

static void pollImu() {
  if (!imuReady) return;
  portENTER_CRITICAL(&imuLock);
  bool fresh = imuFresh, spin = imuMoved;
  headPose = imuLatest;
  headPose.shake = imuShake; headPose.lift = imuLift; headPose.lookUp = imuLook; headPose.lyingChanged = imuLyingChanged;
  headPose.jolt = imuJolt;
  imuShake = imuLift = imuLook = imuLyingChanged = imuMoved = imuFresh = imuJolt = false;
  portEXIT_CRITICAL(&imuLock);
  if (!fresh) return;
  bool moved = headPose.lift || headPose.shake || headPose.lookUp || headPose.lyingChanged || spin;
  if (moved) { power.activity(millis()); interaction = true; }
  if (torso.connected()) return;
  // Lay flat and left undisturbed for 2.5 seconds. As soon as it is picked up (movement, abrupt), it awakens immediately without waiting to be upright: the anger of a jolt is immediately visible.
  static uint32_t handledAt = 0;
  if (moved || headPose.jolt) handledAt = millis();
  bool lying = headPose.lying && millis() - handledAt >= 2500;
  if (lying != face.in.lying) {
    face.in.lying = lying;
    if (!lying && !headPose.shake) face.onEvent(Event::Lift);   // retrieved or manually inspected
  }
  if (headPose.shake) face.onEvent(Event::Shake);
  // Immediate reaction to the first impact only if calm: once anger is unleashed, subsequent impacts must not obscure the angry face by "surprised."
  else if (headPose.jolt && face.mood.I <= MoodEngine::SHOW) face.play(Expr::Surpris, 0.35f);
  else if (headPose.lift) face.onEvent(Event::Lift);
  face.in.tilt = headPose.tilt;
  // Gesture "look at time" (only in pendant V2): On a head without battery, tilting the screen upward should not replace the face with the dial (double tap remains).
  if (headPose.lookUp && Board::HAS_BATTERY) showTime();
}

// Head RTC (Real-Time Clock):
// Rechecked on startup, rewritten on each dated reset (TIME frame).
static void restoreClock() {
  uint8_t r[7];
  TimeUtil::Civil c;
  if (Board::rtcRead(r) && TimeUtil::rtcToCivil(r, c)) torso.clock().setLocal(TimeUtil::toUnix(c), millis());
  rtcSavedRevision = torso.clock().revision();
}

static void saveClock() {
  if (torso.clock().revision() == rtcSavedRevision) return;
  rtcSavedRevision = torso.clock().revision();
  uint32_t local;
  if (!torso.clock().localEpoch(millis(), local)) return;
  uint8_t r[7];
  TimeUtil::civilToRtc(TimeUtil::fromUnix(local), r);
  Board::rtcWrite(r);
}

static void reportToTorso() {
  static uint32_t lastHello = 0;
  static bool wasConnected = false;
  static Expr lastState = Expr::Count;
  bool linked = torso.connected();
  if (!linked) { wasConnected = false; return; }
  bool first = !wasConnected;
  wasConnected = true;
  if (first || millis() - lastHello >= 1000) {
    lastHello = millis();
    // Estimated by voltage (±15%); no battery (V1): undetermined.
    NeckHello hello = {NECK_VERSION, NECK_UNKNOWN, NECK_UNKNOWN};
    if (Board::HAS_BATTERY && headBattery.known()) {
      hello.batteryPct = headBattery.percent();
      hello.charging = headBattery.charging();
    }
    neckSend(NECK_HELLO, &hello, sizeof(hello));
  }
  if (first || face.state() != lastState) {
    lastState = face.state();
    uint8_t state = uint8_t(lastState);
    neckSend(NECK_STATE, &state, 1);
  }
  // Mood and color display: the torso’s dashboard mirrors the same.
  static uint32_t lastMood = 0;
  static uint8_t lastPhase = 0xFF;
  if (first || face.mood.phase != lastPhase || millis() - lastMood >= 500) {
    lastMood = millis();
    lastPhase = face.mood.phase;
    NeckMood m = face.mood.toNeck();
    neckSend(NECK_MOOD, &m, sizeof(m));
  }
}

// Adjust face background color dynamically when mood changes (≤ 10 times per second).
static void paintMood() {
  static uint32_t at = 0;
  static uint16_t painted = 0xFFFF;
  if (!faceGradient || millis() - at < 100) return;
  const Palette& pal = face.mood.pal;
  uint16_t key = toRgb565(pal.skin[1]);
  renderer.setInk(toRgb565(pal.ink));
  renderer.setSkin(toRgb565(pal.skin[0]), key, toRgb565(pal.skin[2]));
  // Below, the golden base remains unchanged: it’s the character’s disk that adopts the hue of the mood.
  // Neon: the base stays black; it’s the features that absorb the color.
  const bool fixedBackground = renderer.fullBody() || renderer.neon();
  if (fixedBackground ? !repaintFace : key == painted && !repaintFace) return;
  at = millis();
  painted = fixedBackground ? 0xFFFF : key;
  repaintFace = false;
  if (renderer.fullBody()) FaceRenderer::paintBackdrop(faceBackground, Display::WIDTH);
  else if (renderer.neon()) FaceRenderer::paintNeon(faceBackground, Display::WIDTH * Display::HEIGHT);
  else FaceRenderer::paintBackground(faceBackground, faceGradient, Display::WIDTH * Display::HEIGHT, pal);
  canvas.setTarget(faceBackground);   // Design embellishment (graduations), drawn once in the background.
  renderer.drawDecor(canvas);
  canvas.invalidate();   // re-painted parts: both screen pads must be fully replaced
}

static void printHelp() {
  Serial.println(F(
    "Commandes :\n"
    "  e <nom>            expression 3 s : content surpris rieur fache apaise mefiant dort baille etourdi clin\n"
    "                     emballe tenir curieux perplexe boude absent inquiet colere joie fatigue bisou\n"
    "  ev <nom>           événement : lift shake noise tap dark light arrive\n"
    "                     visage detache pose question ignore chatouille bisou\n"
    "  humeur             humeur courante (charge A, intensité I)\n"
    "  presence 0|1       présence radar      distance <m>   distance radar\n"
    "  regard <x> <y>     visage vu par la caméra (−1 … 1)   regard off\n"
    "  penche <t>         inclinaison (−1 … 1)\n"
    "  couche 0|1         nuit 0|1         parle 0|1\n"
    "  lum <0-100>        rétroéclairage   buzz               bip court\n"
    "  heure [HH:MM[:SS]] cadran de l'heure (règle l'heure si donnée)\n"
    "  design [nom]       design : classique horloge enpied neon (selon l'édition, gardé en mémoire)\n"
    "  menu               ouvre le menu des designs\n"
    "  imu                IMU dans le repère du robot (réglage des axes)\n"
    "  etat               état courant, batterie, ligne du cou, images/s\n"
    "Écran : tap = apaise, double tap = cadran de l'heure (tap = retour),\n"
    "        3 taps rapides ou plus, ou frotter = chatouilles, appui long = bisou.\n"
    "        Appui de 3 s = menu des designs : flèches gauche / droite, tap au centre = valider."));
}

static bool parseFlag(const char* s) { return s && (s[0] == '1' || s[0] == 'o'); }

static void handleCommand(char* line) {
  char* cmd = strtok(line, " ");
  if (!cmd) return;
  char* a = strtok(nullptr, " ");
  char* b = strtok(nullptr, " ");

  if (!strcmp(cmd, "aide") || !strcmp(cmd, "help")) {
    printHelp();
  } else if (!strcmp(cmd, "e") && a) {
    Expr e;
    if (exprFromName(a, e)) face.play(e, 3);
    else Serial.println(F("expression inconnue"));
  } else if (!strcmp(cmd, "ev") && a) {
    struct { const char* n; Event e; } evs[] = {{"lift", Event::Lift}, {"shake", Event::Shake},
      {"noise", Event::Noise}, {"tap", Event::Tap}, {"dark", Event::Dark}, {"light", Event::Light},
      {"arrive", Event::Arrive}, {"visage", Event::NewFace}, {"detache", Event::Detached}, {"pose", Event::Docked},
      {"question", Event::Unanswerable}, {"ignore", Event::Ignored},
      {"chatouille", Event::Tickle}, {"bisou", Event::Kiss}};
    bool ok = false;
    for (auto& ev : evs) if (!strcmp(a, ev.n)) { face.onEvent(ev.e); ok = true; }
    if (!ok) Serial.println(F("événement inconnu"));
  } else if (!strcmp(cmd, "perf")) {
    Serial.printf("par image : logique %.1f ms, fond %.1f ms, visage %.1f ms, affichage %.1f ms\n",
                  perfLogic / 1000, perfClear / 1000, perfDraw / 1000, perfPresent / 1000);
    static uint32_t lastTicks = 0, lastAt = 0;
    Serial.printf("entrées : %.0f lectures/s, tactile %.1f ms, IMU %.1f ms\n",
                  (inputTicks - lastTicks) * 1000.0f / (millis() - lastAt + 1), touchMicros / 1000.0f, imuMicros / 1000.0f);
    lastTicks = inputTicks; lastAt = millis();
  } else if (!strcmp(cmd, "humeur")) {
    static const char* const PHASES[] = {"dort", "absence", "attentif", "en colère", "se calme", "tient bon",
                                         "curieux", "apaisé", "boude", "perplexe", "inquiet", "joie", "fatigué"};
    Serial.printf("humeur : %s  A=%.2f  I=%.2f\n", face.mood.phase < MOOD_COUNT ? PHASES[face.mood.phase] : "?",
                  face.mood.A, face.mood.I);
  } else if (!strcmp(cmd, "presence")) {
    face.in.presence = parseFlag(a);
    if (face.in.presence) face.onEvent(Event::Arrive);
  } else if (!strcmp(cmd, "distance") && a) {
    face.in.distance = atof(a);
  } else if (!strcmp(cmd, "regard") && a) {
    if (!strcmp(a, "off")) {
      face.in.faceSeen = false;
    } else {
      face.in.faceSeen = true;
      face.in.faceX = atof(a);
      face.in.faceY = b ? atof(b) : 0;
      face.onEvent(Event::Arrive);
    }
  } else if (!strcmp(cmd, "penche") && a) {
    face.in.tilt = atof(a);
  } else if (!strcmp(cmd, "couche")) {
    face.in.lying = parseFlag(a);
    if (!face.in.lying) face.onEvent(Event::Lift);
  } else if (!strcmp(cmd, "nuit")) {
    face.in.dark = parseFlag(a);
    face.onEvent(face.in.dark ? Event::Dark : Event::Light);
  } else if (!strcmp(cmd, "parle")) {
    face.in.talking = parseFlag(a);
    face.in.talkAmplitude = -1;
  } else if (!strcmp(cmd, "lum") && a) {
    dayLevel = uint8_t(constrain(atoi(a), 0, 100));
  } else if (!strcmp(cmd, "buzz")) {
    Board::setBuzzer(true);
    delay(80);
    Board::setBuzzer(false);
  } else if (!strcmp(cmd, "heure")) {
    if (a) {
      int hh = -1, mm = -1, ss = 0;
      if (sscanf(a, "%d:%d:%d", &hh, &mm, &ss) >= 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60 &&
          ss >= 0 && ss < 60) {
        torso.clock().setTimeOfDay(uint32_t(hh) * 3600 + mm * 60 + ss, millis());
      } else {
        Serial.println(F("format : heure HH:MM[:SS]"));
        return;
      }
    }
    showTime();
  } else if (!strcmp(cmd, "design")) {
    Design d;
    if (a && designFromName(a, d)) { menuOpen = false; saveDesign(d); }
    else if (a) Serial.printf("design inconnu : %s\n", Edition::CLOCK_DESIGNS ? "neon, classique, horloge, enpied"
                              : "cette édition n'a que « classique »");
    Serial.printf("design : %s\n", designName(design));
  } else if (!strcmp(cmd, "menu")) {
    openMenu();
  } else if (!strcmp(cmd, "imu")) {
    // On test bench: head upright, "top" ≈ +1; screen facing up, "front" ≈ +1.
    Serial.printf("IMU %s : droite %.2f  haut %.2f  avant %.2f  inclinaison %.2f  couché %d\n",
                  imuReady ? "active" : "absente", headPose.right, headPose.up, headPose.out,
                  headPose.tilt, headPose.lying);
  } else if (!strcmp(cmd, "etat")) {
    Serial.printf("édition %s · design %s · état %s, éveillé %d, écran %u %%, batterie %.2f V\n", Edition::NAME,
                  designName(design), exprName(face.state()), face.awake(), backlightNow, Board::batteryVolts());
    Serial.printf("cou %s (ligne RX à %d%s), regard caméra %s, environnement %s\n",
                  torso.connected() ? "connecté" : "absent", digitalRead(Board::PIN_NECK_RX),
                  torso.detached() ? ", détachée" : "", torso.tracking() ? "suivi" : "aucun",
                  torso.environmentValid() ? "récent" : "absent");
    if (Board::HAS_BATTERY)
      Serial.printf("pendentif : Bluetooth %s, batterie estimée %u %% (%.2f V)%s%s\n", bleStarted ? "actif" : "éteint",
                    headBattery.percent(), headBattery.volts(), headBattery.charging() ? ", en charge" : "",
                    headBattery.low() ? ", faible" : "");
    else
      Serial.printf("Bluetooth %s, pas de batterie (V1 : ni veille ni pendentif)\n", bleStarted ? "actif" : "éteint");
    Serial.printf("heure %s\n", torso.clock().valid() ? "réglée" : "non réglée");
    if (torso.environmentValid()) {
      const NeckEnv& e = torso.environment();
      Serial.printf("T %.2f C, HR %.2f %%, P %.1f hPa, lumière %u lx, son %u dB\n",
                    e.tempC100 / 100.0f, e.humPct100 / 100.0f, e.pressHpa10 / 10.0f,
                    e.lux, e.soundDb);
    }
  } else {
    Serial.println(F("commande inconnue, tapez « aide »"));
  }
}

static void pollSerial() {
  static char line[64];
  static uint8_t len = 0;
  if (Serial.available()) power.activity(millis());
  while (Serial.available()) {
    char ch = Serial.read();
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

// Tap screen: short press (< 400 ms). Two close presses = time display dial.
// Pressing the dial triggers a face movement with a blink.
// Tactile input sampled at 50 Hz by the entry task (a light tap lasts < one frame). The task detects taps, and the loop applies their effects.
bool touchAny = false, touchRelease = false, touchSingle = false, touchDouble = false;
bool touchTickle = false, touchLong = false;   // Quick taps (3+) / Long press (kiss)
bool touchMenu = false;                        // 3-second press, finger stationary: design menu
uint16_t touchReleaseX = 240;                  // where the finger is raised (menu arrows)

static void scanTouch(uint32_t now) {   // called by the task, every 20 ms.
  static uint32_t downAt = 0, lastTapAt = 0;
  static bool wasDown = false, longFired = false, menuFired = false;
  static uint8_t burst = 0;             // fast successive taps
  // Adjusted for a child’s hand: a single tap lasts 0.5 s; two taps from the same series are spaced 0.55 s apart.
  constexpr uint32_t TAP_MAX_MS = 500, BURST_GAP_MS = 550, LONG_PRESS_MS = 800, MENU_PRESS_MS = 3000;
  // Touch the screen = nudge: traveled path by the finger (pixels, screen 480px width).
  constexpr float RUB_STEP_PX = 140, MOVED_PX = 45;
  static float path = 0, moved = 0;     // since the last touch / since the contact
  static uint16_t lastX = 0, lastY = 0;
  Board::Touch t;
  if (!Board::readTouch(t)) return;
  bool any = t.touched, release = false, single = false, dbl = false, tickle = false, longPress = false, menu = false;
  if (t.touched && !wasDown) { downAt = now; longFired = menuFired = false; path = moved = 0; }
  if (t.touched && wasDown) {
    float d = sqrtf(float(int(t.x) - lastX) * (int(t.x) - lastX) + float(int(t.y) - lastY) * (int(t.y) - lastY));
    if (d < 200) { path += d; moved += d; }      // an erroneous jump is not friction
    if (path >= RUB_STEP_PX) { tickle = true; path = 0; burst = 0; }
  }
  if (t.touched) { lastX = t.x; lastY = t.y; }
  // Long press, finger stationary: kiss
  if (t.touched && !longFired && moved < MOVED_PX && now - downAt >= LONG_PRESS_MS) {
    longFired = true;
    longPress = true;
    burst = 0;
  }
  if (t.touched && !menuFired && moved < MOVED_PX && now - downAt >= MENU_PRESS_MS) {
    menuFired = true;
    menu = true;
  }
  if (!t.touched && wasDown && moved >= MOVED_PX) {
    release = true;                     // end of a swipe, not a tap
    if (now - downAt < TAP_MAX_MS) tickle = true;   // A sharp glide (sleeve) scratches even a brief one.
  } else if (!t.touched && wasDown && now - downAt < TAP_MAX_MS) {
    release = true;
    if (burst < 255) burst++;
    lastTapAt = now;
    if (burst >= 3) tickle = true;      // each additional tap prolongs the tickles
  }
  wasDown = t.touched;
  // Confirm one or two taps only after the gap (a third tap triggers tickling).
  if (burst && now - lastTapAt >= BURST_GAP_MS) {
    if (burst == 1) single = true;
    else if (burst == 2 || !Edition::CUDDLES) dbl = true;   // with cuddles disabled, three taps count as a double tap
    burst = 0;
  }
  if (!Edition::CUDDLES) {              // Instructable edition: no pinches, no kisses
    tickle = false;
    longPress = false;
  }
  if (!(any || release || single || dbl || tickle || longPress || menu)) return;
  portENTER_CRITICAL(&imuLock);
  touchAny |= any; touchRelease |= release; touchSingle |= single; touchDouble |= dbl;
  touchTickle |= tickle; touchLong |= longPress; touchMenu |= menu;
  if (release) touchReleaseX = lastX;
  portEXIT_CRITICAL(&imuLock);
}

static void pollTouch() {
  static uint32_t ignoreTapsUntil = 0;
  uint32_t now = millis();
  // Deadline passed, maintained at ≤12 days → becomes "upcoming" after 25 days (ignored taps).
  if (int32_t(ignoreTapsUntil - now) < -(1 << 30)) ignoreTapsUntil = now - (1u << 30);
  portENTER_CRITICAL(&imuLock);
  bool any = touchAny, release = touchRelease, single = touchSingle, dbl = touchDouble;
  bool tickle = touchTickle, longPress = touchLong, menu = touchMenu;
  uint16_t releaseX = touchReleaseX;
  touchAny = touchRelease = touchSingle = touchDouble = touchTickle = touchLong = touchMenu = false;
  portEXIT_CRITICAL(&imuLock);
  if (any) { power.activity(now); interaction = true; }
  if (any && ringer.active(now)) ringer.stop();   // found: it is silenced
  // Menu of designs: tap left / right = previous / next (shown immediately),
  // tap center or 10s no touch = keep displayed design.
  if (menuOpen) {
    power.activity(now);
    interaction = true;
    if (any) menuTouchedAt = now;
    if (release && (releaseX < 170 || releaseX > 310)) {
      menuPick = nextDesign(menuPick, releaseX < 170 ? -1 : 1);
      applyDesign(menuPick);
    } else if (release || now - menuTouchedAt >= MENU_IDLE_MS) {
      closeMenu();
      ignoreTapsUntil = now + 700;
    }
    return;
  }
  if (menu && designCount() > 1 && watchMode.scene() == WatchMode::Face && !watchMode.flipping()) {
    openMenu();
    face.play(Expr::Attentif, 0.2f);   // The long appui had just given a kiss.
    return;
  }
  if (release && watchMode.scene() == WatchMode::Watch) {
    watchMode.dismiss(now, true);                  // A tap on the dial repositions the face.
    ignoreTapsUntil = now + 600;
    return;
  }
  if (int32_t(ignoreTapsUntil - now) > 0) return;
  if (tickle) {
    face.onEvent(Event::Tickle);
    if (torso.connected()) {   // The torso bursts into laughter.
      uint8_t gesture = GESTURE_TICKLE;
      neckSend(NECK_TOUCH, &gesture, 1);
    }
  } else if (longPress) {
    face.onEvent(Event::Kiss);
    if (torso.connected()) {   // long press: torso executes *smack* action.
      uint8_t gesture = GESTURE_LONG;
      neckSend(NECK_TOUCH, &gesture, 1);
    }
  } else if (dbl) {
    showTime();
  } else if (single) {
    face.onEvent(Event::Tap);
    if (torso.connected()) {
      uint8_t gesture = GESTURE_TAP;
      neckSend(NECK_TOUCH, &gesture, 1);
    }
  }
}

// Deep monitoring: wake on touch (GPIO16) or every 30 s to check if the head returns to the cou.
// All useful state (time) is stored in the RTC; the visage resets to zero.
static void enterDeepSleep() {
  Display::sleep();
  Board::prepareDeepSleep();
  const gpio_num_t touch = gpio_num_t(Board::PIN_TOUCH_INT);
  rtc_gpio_pullup_en(touch);
  rtc_gpio_pulldown_dis(touch);
  // High resting tactile line, low on touch. If already low, touch wake-up would loop—use only the timer.
  if (digitalRead(Board::PIN_TOUCH_INT) == HIGH) esp_sleep_enable_ext0_wakeup(touch, 0);
  esp_sleep_enable_timer_wakeup(uint64_t(PowerPolicy::DOCK_CHECK_S) * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

// Wake-up via timer, no screen: Bluetooth announces time for the torso to request an alarm.
static bool ringRequestedWithin(uint32_t ms) {
  HeadBle::begin();
  bleStarted = true;
  headBattery.sample(Board::batteryVolts(), false);
  HeadBle::setStatus({headBattery.percent(), 0, uint8_t(Expr::Dort)});
  HeadBle::setAdvertising(true);
  uint32_t t0 = millis();
  uint8_t tenths;
  // The connected torso has 3 seconds left to submit its request.
  while (millis() - t0 < ms || (HeadBle::connected() && millis() - t0 < ms + 3000)) {
    if (HeadBle::takeRing(tenths) && tenths) {
      bootRing = tenths;
      return true;
    }
    delay(10);
  }
  return false;
}

// Wake via timer: does the torso send its `PRESENCE` frames without screen powering on?
static bool torsoAnswers(uint32_t ms) {
  NeckParser p;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (neck.available()) {
      if (p.feed(neck.read(), millis()) && p.type == NECK_PRESENCE) return true;
    }
    delay(5);
  }
  return false;
}

// Wake-up lighting: fully awake; asleep, it dims gently into standby mode then turns off; the alarm (touch, motion, light, presence) quickly reactivates it.
static void updateBacklight(float dt) {
  static float level = dayLevel, asleepFor = 0;
  static int applied = -1;
  backlightNow = uint8_t(level + 0.5f);
  asleepFor = face.state() == Expr::Dort && !interaction ? asleepFor + dt : 0;
  interaction = false;
  float target = dozing || asleepFor >= SLEEP_OFF_S ? 0 : asleepFor >= SLEEP_DIM_S ? NIGHT_LEVEL : dayLevel;
  float step = (target > level ? 250.0f : 20.0f) * dt;   // per second: sharp wake, gentle shutdown
  level = fabsf(target - level) <= step ? target : level + (target > level ? step : -step);
  int percent = int(level + 0.5f);
  if (percent != applied) {
    applied = percent;
    Board::setBacklight(uint8_t(percent));
  }
  screenOff = applied == 0 && target == 0;
}

void setup() {
  Serial.begin(115200);
  Board::begin();
  neck.begin(NECK_BAUD, SERIAL_8N1, Board::PIN_NECK_RX, Board::PIN_NECK_TX);
  // Removed head: the RX line must not float—it drops to 0, indicating the head is detached.
  gpio_set_pull_mode(gpio_num_t(Board::PIN_NECK_RX), GPIO_PULLDOWN_ONLY);
  if (digitalRead(Board::PIN_NECK_RX)) catchUpdateRequest(150);   // Line 1: placed on the torso
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && !torsoAnswers(400) &&
      !ringRequestedWithin(1500))
    enterDeepSleep();
  if (!Display::begin()) {
    Serial.println(F("Écran : échec de l'initialisation"));
    while (true) delay(1000);
  }
  if (!canvas.begin()) {
    Serial.println(F("PSRAM : allocation impossible (réglage PSRAM = OPI ?)"));
    while (true) delay(1000);
  }
  renderer.renderBackground(canvas.background(), Display::WIDTH);
  faceBackground = canvas.background();
  faceGradient = (uint8_t*)ps_malloc(Display::WIDTH * Display::HEIGHT);
  if (faceGradient) FaceRenderer::buildGradient(faceGradient, Display::WIDTH);
  else Serial.println(F("PSRAM : couleurs d'humeur indisponibles"));
  watchReady = watch.begin(Display::WIDTH, Edition::NEON);   // Complete edition: neon dial matching the face
  if (!watchReady) Serial.println(F("PSRAM : cadran de l'heure indisponible"));
  face.begin();
  prefs.begin("moodimos", false);
  design = Design(prefs.getUChar(DESIGN_KEY, uint8_t(defaultDesign())));
  if (!designAvailable(design)) design = defaultDesign();
  applyDesign(design);
  restoreClock();
  imuReady = Board::imuBegin();
  if (!imuReady) Serial.println(F("IMU : absente"));
  headMotion.setShakeThreshold(0.4f);   // Head held firmly: a sharp shake suffices.
  headMotion.setShakeCooldown(1200);    // 2–3 rapid shocks each (rising anger)
  xTaskCreatePinnedToCore(imuTask, "entrees", 4096, nullptr, 2, nullptr, 0);
  Board::setBacklight(dayLevel);
  face.onEvent(Event::Arrive);   // wakes up on startup
  if (bootRing) {   // wake to ring
    ringer.start(millis(), uint32_t(bootRing) * 100);
    face.play(Expr::Surpris, 1.5f);
    power.activity(millis());
  }
  Serial.printf("MoodiMOS tête prête (édition %s, design %s). Tapez « aide ».\n", Edition::NAME, designName(design));
}

void loop() {
  static uint32_t last = micros(), fpsAt = millis();
  static uint16_t frames = 0;
  uint32_t now = micros();
  float dt = (now - last) / 1e6f;
  if (dt > 0.05f) dt = 0.05f;
  last = now;

  uint32_t t0 = micros();
  pollNeck();
  saveClock();
  pollImu();
  pollBattery();
  pollPendantBle();
  pollSerial();
  pollTouch();
  uint32_t nowMs = millis();
  if (watchMode.update(nowMs)) face.play(Expr::Clin, 1.4f);   // wink after the clock animation
  face.update(dt);
  paintMood();
  reportToTorso();

  // Pendant (pendant):
  // Display turns off → deep standby when no activity detected.
  if (watchMode.scene() == WatchMode::Watch || watchMode.flipping()) power.activity(nowMs);
  // Without battery (V1), the head operates solely on the neck or USB—no screen off or sleep mode.
  PowerPolicy::Mode mode = Board::HAS_BATTERY ? power.update(nowMs, torso.connected(), face.in.lying)
                                              : PowerPolicy::Active;
  if (mode == PowerPolicy::Sleep) enterDeepSleep();
  dozing = mode == PowerPolicy::Doze;
  updateBacklight(dt);
  if (screenOff) {   // screen off: nothing to draw, inputs continue being monitored.
    delay(20);
    return;
  }

  // During a flip, the scene is rendered separately and then flattened onto the screen.
  bool flipping = watchReady && watchMode.flipping();
  canvas.setTarget(flipping ? watch.scratch() : Display::backBuffer());
  if (watchReady && watchMode.scene() == WatchMode::Watch) {
    canvas.setBackground(watch.dial());
    canvas.setFlatBackground(false);
    canvas.clearToBackground();
    float h = 10 + 10 / 60.0f, m = 10, s = fmodf(face.clock(), 60);   // 10:10 AM (no time set)
    torso.clock().hands(nowMs, h, m, s);
    int weekday = -1, day = 0, month = 0;   // Extract time only if time is timestamped (frame TIME, RTC)
    uint32_t local;
    if (torso.clock().localEpoch(nowMs, local)) {
      const TimeUtil::Civil date = TimeUtil::fromUnix(local);
      weekday = int(TimeUtil::weekday(local / 86400));
      day = int(date.day);
      month = int(date.month);
    }
    watch.render(canvas, h, m, s, weekday, day, month);
  } else {
    uint32_t t1 = micros();
    canvas.setBackground(faceBackground);
    canvas.setFlatBackground(renderer.neon());   // Neon: black background, erased by simple fill.
    canvas.clearToBackground();
    uint32_t t2 = micros();
    renderer.render(canvas, face.params(), face.blink(), face.bounce(), face.clock(), face.state());
    if (menuOpen) renderer.drawMenu(canvas, menuPick);
    uint32_t t3 = micros();
    perfLogic += 0.1f * (float(t1 - t0) - perfLogic);
    perfClear += 0.1f * (float(t2 - t1) - perfClear);
    perfDraw += 0.1f * (float(t3 - t2) - perfDraw);
  }
  if (flipping) {
    Canvas::blitSquash(Display::backBuffer(), watch.scratch(), Display::WIDTH, Display::HEIGHT,
                       watchMode.squash(nowMs), 0x0000);
    canvas.invalidate();   // screen tampon (sticker) written outside the canvas.
  }
  uint32_t t4 = micros();
  Display::present();
  perfPresent += 0.1f * (float(micros() - t4) - perfPresent);

  frames++;
  if (millis() - fpsAt >= 10000) {
    Serial.printf("%.1f images/s\n", frames / ((millis() - fpsAt) / 1000.0f));
    frames = 0;
    fpsAt = millis();
  }
}

// MoodiMOS — firmware du torse (kit Waveshare ESP32-S3-Touch-LCD-3.5-C : boîtier + OV5640), le hub du robot
//
// Réglages Arduino IDE (Outils) :
//   Carte          : ESP32S3 Dev Module
//   PSRAM          : OPI PSRAM
//   Flash Size     : 16MB (128Mb)
//   Partition      : 16M Flash (3MB APP/9.9MB FATFS)
//   USB CDC On Boot: Enabled
// Bibliothèque : GFX Library for Arduino (déjà installée)
//
// V1 (alimentation USB, câblage : dossier d'étude V0.3 §3-4) :
//   - tableau de bord : BME280 + BH1750 (I2C), niveau sonore du micro, état de la tête, du radar
//     et de la caméra
//   - radar LD2410C : présence par sa sortie OUT, lue sur l'extenseur PCF8574 (0x22) ; pas de
//     distance (réglages du radar par Bluetooth, appli HLKRadarTool)
//   - caméra OV5640 (nappe du kit) : allumée quand quelqu'un est là, tête en place ; le suivi de
//     mouvement (TorsoCamera) produit les trames FACE pour que la tête suive du regard
//   - cou (UART sur GPIO43/44, DOCK sur le PCF8574) : détection de la tête, trames vers la tête
//     (présence, mesures, regard, heure, voix)
//   - V2 seulement : SW1/SW2 (5 V général et du cou) et garde batterie, sans effet sur USB
//   - IMU : soulevé, secoué, couché et inclinaison vers la tête ; heure RTC + NTP (TimeSource)
//   - micro ES8311 : bruit fort → sursaut de la tête
//   - haut-parleur : phrases WAV de la carte TF ou de la flash du torse, bouche de la tête synchronisée (SPEAK)
//   - édition complète : jeu « Devine l'animal » (GuessGame.h), lancé par le bouton patte du tableau de bord
// À venir : mot de réveil, phrases de l'heure.

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

HardwareSerial& neck = Serial1;       // sur GPIO43/44 (broches de l'UART0, la console passe par l'USB)
NeckParser neckRx;

constexpr uint32_t DOCK_DEBOUNCE_MS = 300;
constexpr uint32_t HEAD_TIMEOUT_MS = 3000;
constexpr uint8_t LOW_BATTERY_PCT = 5;
constexpr uint32_t TIME_RESEND_MS = 3600UL * 1000;   // l'heure part aussi à la pose et à chaque recalage
constexpr uint32_t FACE_TIMEOUT_MS = 1000;
constexpr uint32_t SHAKE_MEMORY_MS = 8000;   // bousculades comptées ensemble si moins de 8 s les séparent
constexpr uint8_t SHAKES_WORRIED = 2;        // les deux premières inquiètent la tête, la troisième la fâche
constexpr uint32_t BUMP_GAP_MS = 400;        // une seule bousculade comptée par à-coup (rebonds, secousse)
constexpr uint32_t BUMP_CONFIRM_MS = 150;    // un « soulevé » dans ce délai : on le prend en main, pas bousculé
constexpr uint32_t SCOLD_GAP_MS = 20000;     // « ne me secoue pas… » : pas plus d'une fois toutes les 20 s
constexpr uint32_t FINGER_AFTER_MS = 400;    // le torse rebondit encore après le doigt levé
constexpr float GRAVITY_TAU_S = 0.3f;        // pesanteur lissée : ce qui s'en écarte vite est une secousse
constexpr uint32_t SWING_MS = 600;           // va-et-vient : repartir dans l'autre sens dans ce délai
constexpr uint32_t SERIAL_TX_TIMEOUT_MS = 2;   // moniteur USB : 40 ms d'attente au plus si personne ne lit
constexpr uint32_t DUMP_TX_TIMEOUT_MS = 100;  // « ecran » : le Mac lit, rien ne doit se perdre
constexpr uint32_t TOUCH_TAP_MAX_MS = 500;    // écran du torse, tête posée : mêmes gestes que sur la tête
constexpr uint32_t TOUCH_BURST_GAP_MS = 550;
constexpr uint32_t TOUCH_LONG_MS = 800;
constexpr int TOUCH_MOVED_PX = 45;
constexpr int TOUCH_RUB_PX = 140;
constexpr size_t SERIAL_RX_BUFFER = 8192;     // annonces du Mac reçues par morceaux de 4 Ko
constexpr size_t BRIDGE_RX_BUFFER = 20480;    // « pont » : un bloc d'esptool (16 Ko) arrive d'un coup
constexpr uint32_t BRIDGE_ASK_MS = 2000;      // demande répétée : une tête qui redémarre en boucle l'entend aussi
constexpr uint32_t BRIDGE_IDLE_MS = 15000;    // plus rien du Mac : programmation finie, le torse redémarre
constexpr uint32_t BRIDGE_WAIT_MS = 60000;    // esptool jamais venu
constexpr uint32_t FACE_PERIOD_MS = 40;   // FACE vers la tête : 25 Hz au plus, chaque image (≈ 20 i/s)
constexpr uint32_t SPEECH_TAIL_MS = 300;   // le haut-parleur fait vibrer le torse et sature le micro
// Micro : dBFS → dB SPL approximatifs (sensibilité du micro et gain du codec à étalonner).
constexpr float DBFS_TO_DB = 120;

bool docked = false;
uint32_t headHelloAt = 0;
int headBatteryPct = -1;
bool headCharging = false;   // V2 : la tête recharge sa LiPo sur le cou
bool headSeen = false;
bool syncHead = false;
bool timeForHead = false;   // la tête vient de répondre : lui renvoyer l'heure
bool displayReady = false;
DashTheme headTheme = {};    // couleurs de l'humeur de la tête (trame MOOD)
uint8_t headPhase = MOOD_COUNT;   // humeur de la tête (trame MOOD) ; MOOD_COUNT : pas encore reçue
uint32_t moodAt = 0;
uint32_t torsoAwakeUntil = 0;     // touché : l'écran du torse reste éveillé, même si la tête dort encore
bool sleepShown = false;          // le torse dort avec la tête (visage endormi à l'écran)
bool dashShown = false;           // tableau de bord à l'écran (ni jeu, ni joie, ni inquiétude, ni sommeil)
bool screenDark = false;          // endormi depuis 5 min : écran éteint, comme celui de la tête
constexpr uint32_t TORSO_AWAKE_MS = 30000;
constexpr uint32_t SLEEP_OFF_MS = 300000;   // la tête éteint son écran après 5 min de sommeil (SLEEP_OFF_S)
uint32_t worriedUntil = 0;   // tête retirée : visage inquiet 3 s
uint32_t joyUntil = 0;       // tête reposée : joie extrême 3 s
constexpr uint32_t MOOD_TIMEOUT_MS = 3000, WORRIED_MS = 3000, JOY_MS = 3000;

// IMU du torse : correspondance axes puce → robot, à vérifier au banc avec la commande « imu »
// Axes de l'IMU du torse (QMI8658) dans le robot monté, USB-C en bas. Au banc le 2026-10-03, carte posée
// USB-C vers le haut : haut = +X, avant (sortie de l'écran) = −Z. Robot assemblé le 2026-10-28 : debout,
// l'ancien réglage lisait haut = −0,95 → carte retournée de 180° autour de l'écran, haut = −X, droite = −Y.
constexpr MotionEvents::Axes TORSO_AXES = {-2, -1, -3};
MotionEvents torsoMotion(TORSO_AXES);
MotionEvents::Output bodyMotion;
bool imuReady = false;
bool micReady = false;
bool speakerReady = false;
bool cameraOn = false;
uint32_t touchDebugUntil = 0, trackDebugUntil = 0, shockDebugUntil = 0;   // banc : commande « tactile », affiche les appuis 20 s
bool cameraForced = false;   // banc : commande « camera force », sans radar ni tête
uint32_t radarHoldMs = 25000;   // présence gardée après la dernière détection du radar (réglable : les tests le mettent à 0)
uint8_t expBits = 0xFF;      // entrées du PCF8574 (radar OUT, DOCK)
bool expanderOk = false;
bool radarPresent = false;
uint32_t cameraMotionAt = 0;   // dernière cible vue par la caméra (garde la présence avec le radar)
bool cameraMotionEver = false;
uint8_t faceSize = 0;          // largeur de la dernière cible
bool radarMoving = false;      // sortie du radar haute en ce moment
bool roomDark = false;         // nuit pour la tête (BH1750, hystérésis 5 / 15 lux)
float gyroDps = 0;             // vitesse de rotation du torse, °/s
uint32_t bodyBumpAt = 0;       // dernière bousculade comptée, soulèvement ou rotation du torse
bool bodyBumpEver = false;
uint32_t touchDownAt = 0;      // dernier doigt posé sur l'écran du torse
uint32_t touchSeenAt = 0;      // dernière lecture du tactile avec un doigt posé
bool touchDownEver = false;
bool touchForGame = false;     // ce doigt-là joue (bouton de réponse, patte, croix) : ses à-coups ne bousculent pas
float shockThreshold = 0.3f;   // à-coup compté comme bousculade, en g (commande « chocs <seuil> »)
MoodTriggers moodTriggers;
MoodSounds::MoodVoice moodVoice;   // cris d'humeur : quand crier
static void cry(MoodSounds::Sound s, const char* why = nullptr);
static void pollSpeech();
GuessGame game;                // « Devine l'animal »
bool gameReady = false;        // édition complète et phrases du jeu rangées (carte TF ou flash)
static void checkGame();
uint32_t faceAt = 0;
bool faceSeen = false;
uint32_t speechUntil = 0;   // parole en cours ou récente : IMU et micro ignorés
bool listenPending = false; // question en cours d'écoute (« ecoute ») : envoyée à la fin par pollListening
// Conversation par le Mac (cerveau/conversation_robot.py service) : il dit « cerveau oui » toutes les 8 s et
// « cerveau occupe » pendant un échange. Le bouton bulle de l'écran demande qui parle (édition complète), puis envoie
// « bouton parler <nom> » ; sans nouvelles du Mac, il se grise.
enum Brain : uint8_t { BRAIN_NONE, BRAIN_READY, BRAIN_ASKED, BRAIN_BUSY };
// Minuteur (édition complète) : il vit dans le torse pour sonner même Mac éteint. Compté en millis, car l'heure peut être
// recalée en route ; sa fin en heure Unix, gardée dans la flash, le fait survivre à un redémarrage (torse reprogrammé
// pendant une cuisson).
constexpr long TIMER_MAX_S = 86400;
constexpr uint32_t TIMER_LATE_S = 900;         // fini ou sonnant avant un redémarrage : sonne encore si c'était il y a moins de 15 min
constexpr uint32_t RING_GAP_MS = 400;          // silence entre deux sons de la sonnerie
bool timerOn = false, timerDone = false;
uint32_t timerEndMs = 0, ringNext = 0;
uint8_t ringStep = 0;                          // sonnerie : deux « dring » (0, 1), puis la phrase (2)
static uint32_t timerLeftS();
static void timerStart(uint32_t seconds);
static void timerStop();
// Accès : le port série obéit à qui le tient (Mac, UNO Q), peut-être à distance. Seul ce dont le service du Mac a besoin,
// personne devant le robot, est ouvert (OPEN_COMMANDS). Le micro ne part qu'après un vrai doigt sur la bulle : aucune
// commande ne la simule et l'accès ci-dessous ne l'ouvre pas, à la main comme à distance. Aucune commande ne fait sortir
// une image de la caméra : elle ne donne que des chiffres. Le reste (réglages de la
// caméra, écran, phrases déposées, Wi-Fi, pont de la tête, essais) attend un appui long sur l'écran du robot, bien plus
// long que le bisou (TOUCH_LONG_MS) que connaît la petite, et jamais demandé pendant qu'on joue ou qu'on lui parle.
constexpr uint32_t ACCESS_MS = 600000;         // accordé pour 10 min
constexpr uint32_t ACCESS_ASK_MS = 20000;      // la demande reste 20 s à l'écran ...
constexpr uint32_t ACCESS_RETRY_MS = 40000;    // ... puis pas de nouvelle demande pendant 40 s : l'écran reste à ceux qui jouent
constexpr uint32_t ACCESS_HOLD_MS = 2000;      // doigt posé 2 s pour l'accorder
constexpr uint32_t ACCESS_LIFT_MS = 200;       // contact perdu moins longtemps : une lecture ratée du tactile, pas un doigt levé
constexpr uint8_t ACCESS_DROPS = 3;            // ... pas plus de 3 fois : des petits coups rapides (chatouilles) n'accordent rien
constexpr uint32_t MIC_GRANT_MS = 30000;       // échange lancé au doigt : une seule écoute, demandée dans les 30 s
uint32_t accessUntil = 0, askUntil = 0, askAt = 0, askNext = 0, micGrantUntil = 0;
bool micGrant = false;
float askHeld = 0;                             // part de l'appui long déjà faite, pour l'écran
char askedCmd[12] = "";
static bool accessOpen() { return int32_t(accessUntil - millis()) > 0; }
static bool asking() { return int32_t(askUntil - millis()) > 0; }
uint8_t brain = BRAIN_NONE;
uint32_t brainAt = 0;
constexpr uint32_t BRAIN_TIMEOUT_MS[4] = {0, 25000, 5000, 90000};   // prêt, appui sans réponse, échange (45 s au plus)
uint32_t whoUntil = 0;                 // écran « Qui parle ? » ouvert jusque-là
constexpr uint32_t WHO_MS = 10000;     // sans choix, le tableau de bord revient
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

// PCF8574 (0x22) lu à chaque tour : radar OUT et DOCK. Sans extenseur, la tête est supposée en
// place (V1 : elle reste sur le cou) et il n'y a plus de présence radar.
static void pollExpander() {
  uint8_t bits;
  bool ok = Board::readExpander(bits);
  static uint8_t failures = 0;
  static bool seen = false;
  if (ok) seen = true;
  failures = ok ? 0 : (failures < 255 ? failures + 1 : 255);
  // Une lecture ratée isolée (accrochage du bus) ne compte pas : « absent » seulement après 5 échecs de suite.
  bool present = seen && failures < 5;
  if (present != expanderOk) Serial.println(present ? "PCF8574 : présent" : "PCF8574 : absent (0x22 ? 3V3 ?)");
  expanderOk = present;
  if (ok) expBits = bits;
  // Le LD2410C lâche une personne immobile au bout de quelques secondes : la présence est gardée radarHoldMs
  // après la dernière sortie haute, pour que la caméra et la tête ne la perdent pas à chaque pause.
  static uint32_t lastRadarHigh = 0;
  static bool everHigh = false;
  bool radarHigh = present && ((expBits >> Board::EXP_RADAR_OUT) & 1);
  radarMoving = radarHigh;
  if (radarHigh) { lastRadarHigh = millis(); everHigh = true; }
  // La caméra, allumée tant que la présence dure, la prolonge aussi : assis devant le robot, on bouge assez
  // pour elle même quand le radar nous a perdus. Sans elle, la tête passait en « absent » toutes les 25 s.
  bool cameraRecent = cameraMotionEver && uint32_t(millis() - cameraMotionAt) < radarHoldMs;
  radarPresent = present && (radarHigh || (everHigh && uint32_t(millis() - lastRadarHigh) < radarHoldMs) ||
                             cameraRecent);
}

static void updateDock() {
  static bool raw = false;
  static uint32_t changedAt = 0;
  bool now = expanderOk ? !((expBits >> Board::EXP_DOCK) & 1) : true;
  if (now != raw) { raw = now; changedAt = millis(); }
  // Coupure immédiate au retrait ; anti-rebond uniquement à la pose.
  if (raw != docked && (!raw || millis() - changedAt >= DOCK_DEBOUNCE_MS)) {
    docked = raw;
    Board::setNeckPower(docked);   // V2 : pas de 5 V sur les contacts à l'air
    headBatteryPct = -1;
    headCharging = false;
    headSeen = false;
    syncHead = docked;
    neckRx.reset();
    while (neck.available()) neck.read();
    // La tête part : MoodiMOS s'inquiète ; elle revient : joie extrême. L'une efface l'autre.
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
      // À la pose, la tête démarre à peine (V1 : allumée par le cou) et n'accepte l'heure qu'une
      // fois la liaison ouverte : on la renvoie à sa première réponse.
      if (!headSeen) timeForHead = true;
      headSeen = true;
      headBatteryPct = h.batteryPct <= 100 ? h.batteryPct : -1;
      headCharging = h.charging == 1;
    } else if (neckRx.type == NECK_TOUCH && neckRx.len == 1) {
      Serial.printf("Tête : geste tactile %u\n", neckRx.payload[0]);
      // chatouilles et bisous sur l'écran de la tête : le torse rit, ou fait smack
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

// Une échéance passée depuis plus de 12 jours y est maintenue : sinon, 25 jours après coup, int32_t(échéance −
// millis()) redeviendrait positif et la ferait croire à venir pendant 25 jours (micro et IMU sourds, joie ou
// inquiétude, torse qui ne dort plus…).
static void keepPast(uint32_t& until, uint32_t now) {
  constexpr int32_t STALE = 1 << 30;   // ≈ 12 jours
  if (int32_t(until - now) < -STALE) until = now - STALE;
}
static void keepDeadlinesPast() {
  const uint32_t now = millis();
  uint32_t* const all[] = {&speechUntil, &joyUntil, &worriedUntil, &torsoAwakeUntil, &touchDebugUntil, &trackDebugUntil,
                           &shockDebugUntil, &accessUntil, &askUntil, &askNext, &micGrantUntil};
  for (uint32_t* until : all) keepPast(*until, now);
}

// Cri d'humeur (MoodSounds.h), tête posée : jamais pendant une phrase ni juste après ni quand le minuteur sonne, délais
// tenus par moodVoice. why : ce qui l'a fait crier, pour le moniteur.
static void cry(MoodSounds::Sound s, const char* why) {
  if (s == MoodSounds::NONE || !docked || timerDone || speechRecent() || Speaker::cryLevel() == 0 ||
      Microphone::listening()) return;
  int variant = moodVoice.take(s, millis());
  if (variant >= 0 && Speaker::cry(s, uint8_t(variant)))
    Serial.printf("Cri : %s %d%s%s%s\n", MoodSounds::name(s), variant + 1, why ? " (" : "", why ? why : "", why ? ")" : "");
}

// Caméra : allumée quand quelqu'un est là et que la tête peut le regarder.
static void updateCamera() {
  bool want = cameraForced || (docked && radarPresent);
  if (want == cameraOn) return;
  cameraOn = want;
  TorsoCamera::setActive(want);
  if (!want) faceSeen = false;
}

// Suivi de la caméra → trames FACE vers la tête, 25 Hz au plus.
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
    NeckPresence p = {uint8_t(present), 0};   // distance inconnue : sortie OUT seule
    neckSend(NECK_PRESENCE, &p, sizeof(p));
    // Joie du torse (annonce du Mac, tête reposée) : la tête la vit avec lui, même endormie, avec les mêmes
    // couleurs d'or (humeur entretenue 1 s à chaque trame).
    if (int32_t(joyUntil - millis()) > 0) {
      NeckEvent cheer = {EV_FACE, HeadEvents::CHEER};
      neckSend(NECK_EVENT, &cheer, sizeof(cheer));
    }
    if (imuReady) {   // posture du corps, répétée comme la présence
      NeckEvent lying = {EV_LYING, int16_t(bodyMotion.lying ? 1 : 0)};
      neckSend(NECK_EVENT, &lying, sizeof(lying));
      NeckEvent tilt = {EV_TILT, int16_t(lroundf(bodyMotion.tilt * 1000))};
      neckSend(NECK_EVENT, &tilt, sizeof(tilt));
    }
  }
  sendTimeToHead();   // après PRESENCE, qui ouvre la liaison côté tête
  if (env && (syncHead || millis() - lastEnvAt >= 1000)) {
    lastEnvAt = millis();
    NeckEnv e = {env->tempC100, env->humPct100, env->pressHpa10, env->lux, soundDb};
    neckSend(NECK_ENV, &e, sizeof(e));
    // Hystérésis provisoire, à étalonner une fois monté. Sans mesure de lumière récente
    // (BH1750 absent ou muet), pas de fausse nuit.
    if (env->lightOk) {
      if (env->lux <= 5) dark = true;
      else if (env->lux >= 15) dark = false;
      roomDark = dark;
      // Pendant une partie, la tête ne s'endort pas dans la pénombre (le jeu se joue aussi le soir).
      NeckEvent light = {uint8_t(dark && !game.active() ? EV_DARK : EV_LIGHT), 0};
      neckSend(NECK_EVENT, &light, sizeof(light));
    }
  }
  syncHead = false;
}

// Moniteur série (115200, fin de ligne « Nouvelle ligne ») :
//   wifi <ssid> <mot de passe>   enregistre le Wi-Fi (SSID sans espace) et synchronise l'heure
//   wifi oubli                   efface les identifiants
//   heure AAAA-MM-JJ HH:MM[:SS]  règle l'heure de Paris à la main
//   etat                         heure, Wi-Fi, phrases et jeu
//   imu                          valeurs de l'IMU dans le repère du robot (réglage des axes)
//   micro                        niveau sonore du micro
//   capteurs                     BME280 / BH1750 détectés et dernières mesures
//   camera [force|auto]          état de la caméra OV5640 et de la cible ; force = allumée sans radar (banc)
//   tactile                      affiche les coordonnées des appuis pendant 20 s (banc)
//   ecran                        dessin de l'écran en hexadécimal (banc)
//   radar                        extenseur PCF8574, sortie OUT du radar et DOCK
//   dire <phrase> / silence      joue /voix/<phrase>.wav (carte TF ou flash) / arrête
//   deposer <nom> <octets> + wav range /voix/<nom>.wav dans la flash du torse, par morceaux de 4 Ko confirmés
//                                (extras/devine_animal/vers_robot.py envoyer)
//   jeu [oui|non|jsp|fin]        lance « Devine l'animal » ; avec une réponse : comme les boutons de l'écran
//   suivi                        ce que calcule le suivi de mouvement, 25 s (banc)
//   regard [gx gy cx cy]         gain et centre du regard de la caméra, en direct ; seul : les valeurs
//   scan                         liste les adresses I2C qui répondent (banc)
//   parle                        phrase de présentation rangée dans le firmware, aussi dite au démarrage
//   annonce <octets> + son       annonce du Mac (fin de tâche), PCM 16 bits mono 16 kHz : dite et fêtée
//   dis <octets> + son           la même chose sans la fête : réponse de la conversation (cerveau/conversation_robot.py)
//   dis+ <octets> + son          phrase suivante de la réponse, jouée à la suite sans coupure (30 s en tout)
//   ecoute [s | fin]             écoute une question jusqu'au silence (8 s au plus), puis « son <octets> <somme> » + la
//                                voix en PCM 16 bits mono 16 kHz ; fin = arrête l'écoute tout de suite
//   cerveau [oui|occupe|non]     conversation par le Mac : prête, échange en cours, arrêtée (bouton bulle de l'écran) ;
//                                seul : l'état
//   minuteur [secondes | fin]    édition complète : lance ou annule le minuteur (24 h au plus), arrête sa sonnerie ; seul : ce
//                                qu'il reste. Un nouveau remplace celui en cours
//   volume <0-255>               volume du codec en direct (banc ; 178 = réglage de départ)
//   essai                        sinus fort, volume du codec par paliers (banc : où ça grésille)
//   bip                          deux notes douces pour essayer le haut-parleur (banc)
//   sons [0-10 | nom [1-3]]      volume des cris d'humeur (0 = muets), gardé dans la flash ; un nom fait entendre
//                                un cri : joie, curieux, surpris, grogne, soupir, baille, rire, bisou ; seul : le volume
//   sonner                       fait sonner la tête détachée (« où est ma tête ? »)
//   tete <expression> [s]        impose une expression à la tête posée (3 s par défaut, 25 s au plus) ; seul : la liste
//   chocs [seuil]                force des chocs (g) et bousculades comptées, 60 s ; seuil d'à-coup en g, en direct
//   evenement <nom>              envoie un événement à la tête posée (souleve, secoue, bruit, tap, chatouilles,
//                                bisou, perplexe, boude) : essai des humeurs sans secouer
//   pont [bauds] [direct]        programmation de la tête par le cou (firmware/flash_tete_cou.sh), puis redémarre ;
//                                direct = tête déjà en mode téléchargement (banc)
// Programmation de la tête fermée : elle redémarre dans le mode téléchargement de sa ROM, qui écoute les fils
// du cou au débit de leurs premiers octets, et le torse ne fait plus que relayer esptool entre son USB et le cou. Le Mac
// n'envoie plus rien pendant 15 s : c'est fini, le torse redémarre et tout reprend.
// Octets relayés au dernier pont, gardés à travers le redémarrage et affichés au démarrage suivant.
RTC_NOINIT_ATTR uint32_t bridgeMark, bridgeToHead, bridgeFromHead;
constexpr uint32_t BRIDGE_MARK = 0x504F4E54;   // « PONT »

static void bridgeHeadUpdate(uint32_t baud, bool ask) {
  if (!docked) { Serial.println("pont : tête absente"); return; }
  if (Serial.setRxBufferSize(BRIDGE_RX_BUFFER) != BRIDGE_RX_BUFFER) {
    Serial.setRxBufferSize(SERIAL_RX_BUFFER);
    Serial.println("pont : mémoire insuffisante");
    return;
  }
  Serial.setTxTimeoutMs(DUMP_TX_TIMEOUT_MS);   // esptool lit : rien ne doit se perdre
  TorsoCamera::setActive(false);
  for (uint32_t t0 = millis(); ask && millis() - t0 < BRIDGE_ASK_MS;) {
    neckSend(NECK_UPDATE, NECK_UPDATE_MAGIC, sizeof(NECK_UPDATE_MAGIC));
    delay(20);
  }
  neck.flush();
  neck.updateBaudRate(baud);
  delay(200);
  while (neck.available()) neck.read();       // journal de la ROM, lu au mauvais débit
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

// Son reçu après « annonce <octets> », « dis <octets> » ou « dis+ <octets> » (PCM 16 bits mono 16 kHz, 30 s au plus),
// joué dès le premier morceau, pendant que la suite arrive ; faux s'il est refusé ou incomplet (la voix s'arrête alors
// là où la réception s'est arrêtée). « dis+ » : la phrase suivante de la réponse, rangée après les autres et jouée à
// leur suite sans coupure (ou dès son arrivée, si la voix avait déjà fini) ; le Mac l'envoie pendant que la première
// est dite. Réception par morceaux de 4 Ko : l'envoyeur attend le « + » de chaque morceau avant le suivant (sans
// cela, il envoie plus vite que la carte ne lit et la réception déborde).
static bool receivePcm(const char* cmd, long octets) {
  static int16_t* son = nullptr;
  static volatile int received = 0;                     // échantillons arrivés depuis `debut`, lus par la voix
  static long debut = 0, rempli = 0;                    // octets : début de la voix jouée, fin du son rangé
  static bool suiteOk = false;                          // « dis+ » seulement après un « dis » reçu entier
  const long maxi = 16000L * 2 * 30;                    // les longues réponses de la conversation font 14 s
  bool suite = !strcmp(cmd, "dis+");
  if (!son) son = (int16_t*)ps_malloc(maxi);
  if (!son || octets <= 0 || octets > maxi - (suite ? rempli : 0) || (octets & 1) || (suite && !suiteOk)) {
    Serial.printf("%s refusée : %ld octets (30 s au plus%s)\n", cmd, octets, suite ? " en tout, après un « dis »" : "");
    return false;
  }
  bool playing;
  if (!suite) {
    Speaker::stop();                                    // le tampon ne doit plus être lu avant d'être réécrit
    for (uint32_t t = millis(); Speaker::playing() && millis() - t < 500;) delay(5);
    debut = rempli = 0;
    received = 0;
    playing = Speaker::playPcm(son, int(octets / 2), &received);
  } else if (Speaker::extendPcm(int((rempli - debut + octets) / 2))) {
    playing = true;                                     // à la suite de la voix en cours
  } else {
    debut = rempli;                                     // la voix avait fini : la suite repart d'ici
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
    pollSpeech();                                       // la bouche de la tête suit la voix dès le début
    sendToHead(nullptr);                                // et la présence à 5 Hz : la tête se croirait détachée
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
    bool word = strlen(cmd) < sizeof(askedCmd);          // un mot, pas des octets perdus sur la ligne
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
    char* pass = strtok(nullptr, "");            // le reste de la ligne, espaces compris
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
    // Phrase du Mac (extras/devine_animal/vers_robot.py) : « deposer <nom> <octets> », puis le fichier WAV
    // par morceaux de 4 Ko, chacun confirmé par un « + » (le Mac attend avant le suivant, comme pour « annonce »).
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
      if (!gameReady) Serial.println("jeu : phrases absentes (extras/devine_animal/vers_robot.py envoyer)");
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
    // Réglage à l'œil, perdu au redémarrage : les bonnes valeurs vont ensuite dans trackerConfig().
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
    // Annonce du Mac (cerveau/annonce.py, fin d'une tâche de Claude ou de Codex) : « annonce <octets> » puis le son,
    // PCM 16 bits mono 16 kHz. Le robot la dit sur son haut-parleur (bouche de la tête synchronisée) et le torse
    // fait sa joie pendant ce temps. « dis <octets> » : la même chose sans la joie (réponse de la conversation,
    // cerveau/conversation_robot.py) ; « dis+ <octets> » : sa phrase suivante.
    char* n = strtok(nullptr, " ");
    long octets = n ? atol(n) : 0;
    if (receivePcm(cmd, octets) && cmd[0] == 'a') joyUntil = millis() + uint32_t(octets / 32) + 1000;  // 32 octets par ms
  } else if (!strcmp(cmd, "cerveau")) {
    char* a = strtok(nullptr, " ");
    if (!a) {
      static const char* const names[4] = {"absent", "prêt", "appelé", "occupé"};
      Serial.printf("cerveau : %s\n", names[brain]);
      return;
    }
    if (!strcmp(a, "oui") && brain == BRAIN_ASKED) return;   // l'appui est en route vers le Mac
    brain = !strcmp(a, "oui") ? BRAIN_READY : !strcmp(a, "occupe") ? BRAIN_BUSY : BRAIN_NONE;
    brainAt = millis();
  } else if (!strcmp(cmd, "acces")) {
    // L'état de l'accès, pour la voix (cerveau/conversation_robot.py) ; « acces fin » le referme tout de suite.
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
    // Question de la conversation (cerveau/conversation_robot.py, sur la UNO Q) : « ecoute [secondes] », le torse
    // écoute jusqu'au silence qui suit la parole (8 s au plus par défaut), puis envoie « son <octets> <somme> »
    // et la voix en PCM 16 bits mono 16 kHz (pollListening) ; « son 0 0 » si personne n'a parlé.
    // « ecoute fin » arrête l'écoute tout de suite.
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
    // Expression imposée à la tête par le cou (Mac, banc) : « tete rieur 5 ». Elle reprend ensuite son humeur.
    char* nom = strtok(nullptr, " ");
    char* sec = strtok(nullptr, " ");
    uint8_t id;
    if (!nom || !HeadExpressions::fromName(nom, id)) {
      Serial.print("tete <expression> [secondes] :");
      for (uint8_t i = 0; i < HeadExpressions::COUNT; i++) Serial.printf(" %s", HeadExpressions::NAMES[i]);
      Serial.println();
      return;
    }
    long dixiemes = sec ? lroundf(atof(sec) * 10) : 30;   // la trame porte la durée en dixièmes de seconde
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
    if (s != MoodSounds::NONE) {   // essai d'un cri, sans délai ni humeur
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
    // Banc : le dessin de l'écran (320 × 480 RGB565) en hexadécimal entre ECRAN et FIN
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
    // Au banc : debout, « haut » doit valoir ≈ +1 ; écran vers le ciel, « avant » ≈ +1.
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
    // pont [bauds] [direct] : banc ; direct = tête déjà en mode téléchargement, pas de demande
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

// IMU à 50 Hz : soulevé et secoué partent tout de suite vers la tête.
static void pollImu() {
  static uint32_t lastRead = 0;
  if (!imuReady || millis() - lastRead < 20) return;
  lastRead = millis();
  float a[3], g[3];
  if (!Board::imuRead(a, g)) return;
  bodyMotion = torsoMotion.update(a, g, millis());
  gyroDps = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  bool spin = gyroDps > 30;   // °/s : tourné, penché vivement
  if (bodyMotion.jolt || bodyMotion.lift || bodyMotion.shake || spin) TorsoCamera::relearn();
  // La voix fait vibrer le torse : ce n'est pas lui qu'on bouscule (le toucher reste pris en compte).
  if (!speechRecent() && (bodyMotion.lift || bodyMotion.shake || spin)) { bodyBumpAt = millis(); bodyBumpEver = true; }
  // Secoué de côté, debout sur la table, le robot n'allonge presque pas son accélération (0,36 g de côté : 0,06 g
  // d'écart à 1 g) : il fallait le secouer très fort. L'écart de côté à la pesanteur lissée voit la secousse en entier ;
  // il compte quand il repart dans l'autre sens (va-et-vient), pas quand on penche le robot. Le haut et le bas restent
  // à l'écart à 1 g (bodyMotion.jolt) : soulevé puis reposé n'est pas une secousse.
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
    if (gn > 0.5f) h[i] -= up * grav[i] / gn;   // la part de côté seulement
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
  // Banc (commande « chocs ») : force des chocs, pour régler le seuil des bousculades.
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
  // Pendant la parole, le haut-parleur fait vibrer le torse : ni « soulevé » ni « secoué ».
  bool quiet = speechRecent();
  // Bousculé (un à-coup franc suffit) une ou deux fois : inquiet. Une troisième fois : en colère, et chaque
  // bousculade de plus la fait monter (fâché orange, puis rouge). Exiger une vraie secousse (3 à-coups en
  // 1 s) ne partait presque jamais. Un à-coup suivi d'un « soulevé » ne compte pas.
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
  // Un doigt posé sur l'écran fait aussi un à-coup, et le torse rebondit quand il se lève : pendant le contact et
  // jusqu'à FINGER_AFTER_MS après, c'est un tap, sauf si le choc est franc (main qui pousse). En partie, même franc :
  // la petite appuie fort et longtemps, et chaque réponse comptait comme une bousculade (fâché à la troisième).
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
      // La colère monte : il le dit, agacé (phrase « secoue » rangée dans la flash, édition complète), une fois par
      // bousculade et pas plus d'une fois toutes les 20 s ; sinon il grogne (cri d'humeur). Jamais en pleine partie.
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

// Micro : un bruit fort fait sursauter la tête (sauf sa propre voix) ; niveau pour le tableau de bord.
static void pollMicrophone() {
  if (!micReady) return;
  bool noise = Microphone::takeNoise();
  if (!speechRecent()) {
    float db = Microphone::levelDbfs() + DBFS_TO_DB;
    soundDb = uint8_t(db < 0 ? 0 : db > 130 ? 130 : lroundf(db));
    soundValid = true;
  }
  if (!noise || speechRecent() || !docked || Microphone::listening()) return;   // qui lui parle ne le fait pas sursauter
  NeckEvent e = {EV_NOISE, 0};
  neckSend(NECK_EVENT, &e, sizeof(e));
  cry(MoodSounds::SURPRIS, "bruit");
}

// Fin de l'écoute d'une question : « son <octets> <somme> », puis la voix (la UNO Q lit, rien ne doit se perdre).
static void pollListening() {
  if (!listenPending || Microphone::listening()) return;
  listenPending = false;
  int n = 0;
  const uint8_t* voice = (const uint8_t*)Microphone::heard(n);
  uint32_t somme = 0;
  for (long i = 0; i < 2L * n; i++) somme += voice[i];
  Serial.setTxTimeoutMs(DUMP_TX_TIMEOUT_MS);
  Serial.printf("son %ld %lu\n", 2L * n, (unsigned long)somme);
  for (long i = 0; i < 2L * n; i += 4096) {             // par morceaux, la présence de la tête entre deux
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

static uint32_t timerLeftS() {   // arrondi au-dessus : 0 seulement à la fin
  const int32_t left = int32_t(timerEndMs - millis());
  return timerOn && left > 0 ? (uint32_t(left) + 999) / 1000 : 0;
}

static void timerStart(uint32_t seconds) {
  timerDone = false;
  timerOn = true;
  timerEndMs = millis() + seconds * 1000;
  uint32_t now;
  TimeSource::setTimerEnd(TimeSource::now(now) ? now + seconds : 0);   // heure inconnue : pas de reprise au redémarrage
}

// Annulé, ou sonnerie arrêtée (toucher, « minuteur fin »)
static void timerStop() {
  if (timerOn || timerDone) TimeSource::setTimerEnd(0);
  if (timerDone) {
    Speaker::stop();
    joyUntil = millis();
  }
  timerOn = timerDone = false;
}

// Minuteur qui courait avant un redémarrage : repris, ou dit tout de suite s'il a fini entre-temps.
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

// Fin du minuteur : il passe avant la partie et « Qui parle ? » et sonne sans limite, jusqu'au toucher de l'écran ou à
// « minuteur fin » ; l'heure de fin reste gardée jusque-là. Pas par-dessus une phrase ni pendant qu'il écoute ; un cri,
// lui, est coupé (Speaker::talking ne le compte pas) et le réglage des cris ne change rien à la sonnerie.
static void pollTimer() {
  const uint32_t now = millis();
  if (timerOn && int32_t(timerEndMs - now) <= 0) {
    timerOn = false;
    timerDone = true;
    ringStep = 0;
    ringNext = now + 300;   // la phrase de la partie s'arrête d'abord (pollGame)
    game.quit();
    whoUntil = now;
  }
  if (!timerDone) return;
  joyUntil = now + 1000;    // la tête fait la fête, même endormie
  if (int32_t(ringNext - now) <= 0 && !Speaker::talking() && !Microphone::listening()) {
    const bool phrase = ringStep == 2;
    const int samples = phrase ? TimerVoice::SAMPLES : TimerVoice::RING_SAMPLES;
    Speaker::playPcm(phrase ? TimerVoice::DATA : TimerVoice::RING, samples);
    ringStep = (ringStep + 1) % 3;
    ringNext = now + uint32_t(samples) / 16 + RING_GAP_MS;   // 16 échantillons par milliseconde
  }
}

// Bouton bulle : le Mac prêt, l'écran demande qui parle (édition complète, maison dans Household_local.h, hors du jeu),
// sinon l'échange part tout de suite ; sans cerveau, la tête ne sait pas.
static void pressTalk() {
  if (brain == BRAIN_NONE) playOnHead(HeadExpressions::PERPLEXE, 15);
  else if (brain == BRAIN_READY && Edition::COMPLETE && Household::COUNT && !game.active()) whoUntil = millis() + WHO_MS;
  else talkAs(-1);
}

// « bouton parler <nom> » au Mac prêt, qui adapte ses réponses à la personne (−1 : sans nom). Il confirme par
// « cerveau occupe », sinon la bulle se grise au bout de 5 s.
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
  if (brain != BRAIN_READY && choosing()) whoUntil = millis();   // le Mac s'est tu ou parle déjà : plus de choix
}

// Jeu prêt : édition complète, et phrases rangées (la première et la dernière qu'envoie vers_robot.py).
static void checkGame() {
  char last[16];
  snprintf(last, sizeof(last), "devine_a%02d", GuessData::ANIMALS - 1);
  gameReady = Edition::GAMES && Speaker::has("devine_intro") && Speaker::has(last);
}

// Partie de « Devine l'animal » en cours : phrases, expressions de la tête ; retirer la tête l'arrête.
static void pollGame() {
  if (game.takeSulk()) sendHeadEvent(HeadEvents::IGNORED);   // partie laissée en plan : il boude
  if (!game.active()) return;
  if (!docked) game.quit();
  game.update(millis(), Speaker::playing());
  if (game.takeStop()) Speaker::stop();
  char name[16];
  if (const char* p = game.takePhrase(name, sizeof(name))) Speaker::play(p);
  uint8_t e, t;
  if (game.takeExpression(e, t)) playOnHead(e, t);
}

// Méfiant, perplexe, étourdi, fatigue : voir MoodTriggers.h.
static void pollMoodTriggers() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 50) return;
  float dt = (now - last) / 1000.0f;
  last = now;
  Ambient::Reading env;
  bool envOk = Ambient::latest(env);
  MoodTriggers::In in;
  in.docked = docked && !game.active();   // pendant le jeu, le torse ne décide plus d'humeur
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
    case MoodTriggers::FATIGUE: sendHeadEvent(HeadEvents::TIRED); break;   // humeur : couleurs acier, tête et torse
    case MoodTriggers::PERPLEXE: sendHeadEvent(HeadEvents::UNANSWERABLE); break;
    case MoodTriggers::NONE: return;
  }
  Serial.printf("Torse : humeur %s\n", MoodTriggers::name(a));
}

// Tête détachée : un doigt sur le bouton « Où est ma tête ? » la fait sonner 10 s.
// Bouton haut-parleur du tableau de bord : coupe les cris d'humeur ou les rend (les phrases restent) ; un bouton
// n'est pas une caresse, rien ne part vers la tête.
// Jeu « Devine l'animal » : le bouton patte le lance ; pendant la partie, l'écran ne sert qu'aux boutons de
// réponse et rien ne part vers la tête.
// Tête posée : toucher le torse, c'est toucher MoodiMOS, la tête réagit comme à son propre écran. Tap : apaisé ;
// trois taps rapides ou un frottement : chatouilles ; appui long, doigt immobile : bisou. Sans câlins
// (édition instructable), le tap seul. La main qui tient le torse pour le bousculer, le soulever ou le
// tourner n'est pas une caresse : un contact pendant ou juste après ces mouvements est ignoré (il apaisait
// la colère aussitôt montée, ou donnait un bisou en pleine bousculade).
constexpr uint32_t TOUCH_AFTER_BUMP_MS = 1500;
// Le doigt a lancé un échange : une écoute pour lui. Ouvrir « Qui parle ? » ou toucher la bulle grise n'ouvre rien.
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
  const bool picking = choosing();   // avant l'appui : le toucher qui ouvre l'écran n'y choisit personne
  if (down && int32_t(touchDebugUntil - now) > 0) Serial.printf("tactile x=%d y=%d\n", x, y);
  if (down && !wasDown && !docked && !picking && !timerDone && !asking() && Dashboard::ringButtonHit(x, y)) HeadLink::ring(100);
  if (down) touchSeenAt = now;
  if (down && !wasDown) {
    downAt = now; moved = 0; longFired = rubFired = false;
    touchDownAt = now;
    touchDownEver = true;
    touchForGame = false;
    spoiled = bodyBumpEver && now - bodyBumpAt < TOUCH_AFTER_BUMP_MS;
    waking = sleepShown;                      // l'écran dormait : ce toucher le réveille, sans viser de bouton
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
  if (timerDone) {   // minuteur fini : un toucher le fait taire, rien d'autre
    if (down && !wasDown) {
      spoiled = true;   // le doigt levé après, écran fermé, n'est pas un tap
      timerStop();
    }
    taps = 0;
    wasDown = down;
    return;
  }
  if (asking()) {   // accès demandé : doigt posé 2 s, accordé ; levé avant, refusé ; doigt déjà posé, rien
    static bool holding = false;
    static uint32_t holdAt = 0, seenAt = 0;
    static uint8_t drops = 0;
    if (holding && int32_t(holdAt - askAt) < 0) holding = false;   // appui de la demande d'avant
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
  if (picking) {   // « Qui parle ? » : un rond lance l'échange, la croix ferme l'écran
    if (down && !wasDown) {
      spoiled = true;   // le doigt levé après le choix, écran fermé, n'est pas un tap
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
      Dashboard::playButtonHit(x, y))   // pas de partie pendant une conversation
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
  if (down && bodyBumpEver && int32_t(bodyBumpAt - downAt) >= 0) spoiled = true;   // bousculé pendant le contact
  if (spoiled) { taps = 0; wasDown = down; return; }
  if (down && wasDown) {
    int d = abs(x - lastX) + abs(y - lastY);
    if (d < 200) moved += d;   // un saut aberrant du contrôleur n'est pas un frottement
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
    if (taps >= 3 && Edition::CUDDLES) {   // chaque tap de plus les prolonge
      sendHeadEvent(HeadEvents::TICKLE);
      cry(MoodSounds::RIRE);
    }
    else if (taps <= 2) sendHeadEvent(HeadEvents::TAP);
  }
  wasDown = down;
}

// Voix : la bouche de la tête suit l'enveloppe du son, 30 fois par seconde, puis se ferme.
// Un cri d'humeur ne la fait pas parler (l'expression garde sa bouche), mais il fait taire micro et IMU.
static void pollSpeech() {
  static uint32_t lastSpeak = 0;
  static bool wasSpeaking = false;
  if (Speaker::playing()) speechUntil = millis() + SPEECH_TAIL_MS;
  bool speaking = Speaker::talking();
  if (!docked) { wasSpeaking = false; return; }
  if (speaking && millis() - lastSpeak >= 33) {
    lastSpeak = millis();
    uint8_t amp = Speaker::amplitude();
    if (amp == 0) amp = 1;                 // 0 voudrait dire « fin de parole »
    neckSend(NECK_SPEAK, &amp, 1);
  } else if (!speaking && wasSpeaking) {
    uint8_t end = 0;
    neckSend(NECK_SPEAK, &end, 1);
  }
  wasSpeaking = speaking;
}

// Au démarrage, MoodiMOS se présente : /voix/bonjour.wav de la carte TF s'il existe, sinon la phrase
// rangée dans le firmware (« Bonjour, je suis MoodiMOS, ton compagnon de bureau… »). Après 4 s : la tête a démarré et sa bouche
// suit la voix.
static void pollGreeting() {
  constexpr uint32_t GREETING_MS = 4000;
  static bool done = false;
  if (done || millis() < GREETING_MS) return;
  done = true;
  if (micReady && !Speaker::play("bonjour")) Speaker::sayDemo();
}

// La tête dort (trame MOOD récente) : le torse dort avec elle, sauf s'il vient d'être touché.
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
  // Port USB branché mais fermé sur le Mac : chaque message bloquait jusqu'à 2 s (20 × 100 ms), plus de
  // battements vers la tête, qui se croyait détachée (blanc, puis joie). Messages perdus plutôt qu'attendus.
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
  // Robot entier sur ses pieds, plus lourd que la tête seule : à-coup dès 0,3 g (1 g ne partait presque
  // jamais, 0,4 demandait encore de secouer fort) ; réglable en direct avec « chocs <seuil> ».
  // 2-3 secousses rapprochées comptent chacune, comme sur la tête, pour que la colère monte jusqu'au rouge.
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
  static bool drawnCriesOn = false;   // état du bouton des cris au dernier dessin
  static uint8_t drawnBrain = 0;      // état du bouton bulle au dernier dessin

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

  // Batterie surveillée quel que soit l'écran (joie, jeu, sommeil)
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
  // Pendant le minuteur, le torse ne s'endort pas avec la tête : le temps restant reste à l'écran.
  bool sleeping = !worried && !joyful && !game.active() && !picking && !timerOn && !timerDone && !askShown && torsoSleeps();
  sleepShown = displayReady && sleeping;
  dashShown = displayReady && !worried && !joyful && !game.active() && !picking && !sleeping && !timerDone && !askShown;
  // Endormi 5 min : l'écran s'éteint avec celui de la tête ; il se rallume au réveil ou au toucher.
  static uint32_t asleepAt = 0;
  static bool wasSleeping = false;
  if (sleeping && !wasSleeping) asleepAt = millis();
  wasSleeping = sleeping;
  bool dark = displayReady && sleeping && millis() - asleepAt >= SLEEP_OFF_MS;
  if (dark != screenDark) {
    screenDark = dark;
    Board::setBacklight(!dark);
  }
  if (timerDone) {   // minuteur fini, par-dessus tout le reste (≈ 12 images/s)
    static uint32_t lastDone = 0;
    if (displayReady && millis() - lastDone >= 80) {
      lastDone = millis();
      Dashboard::drawTimerDone(millis() / 1000.0f);
    }
    lastDraw = 0;
  } else if (askShown) {   // accès demandé : se voit, même endormi
    static uint32_t lastAsk = 0;
    if (displayReady && millis() - lastAsk >= 80) {
      lastAsk = millis();
      Dashboard::drawAccess(askedCmd, askHeld);
    }
    lastDraw = 0;
  } else if (displayReady && (worried || joyful)) {   // ≈ 25 images/s pendant l'inquiétude ou la joie
    static uint32_t lastFace = 0;
    if (millis() - lastFace >= 40) {
      lastFace = millis();
      if (worried) Dashboard::drawWorried(headTheme, millis() / 1000.0f);
      else Dashboard::drawJoy(millis() / 1000.0f);
    }
    lastDraw = 0;   // le tableau de bord revient dès la fin
  } else if (game.active()) {   // écran du jeu, redessiné à chaque changement (question, bouton touché)
    static uint32_t drawnRev = 0;
    if (displayReady && (lastDraw || game.revision() != drawnRev)) {
      drawnRev = game.revision();
      GameView v = {game.text(), game.round(), game.buttons(), game.pressed(millis())};
      Dashboard::drawGame(v);
    }
    lastDraw = 0;
  } else if (picking) {   // « Qui parle ? » : tout de suite, puis toutes les 500 ms (après la joie d'une annonce)
    static uint32_t lastWho = 0;
    if (displayReady && (lastDraw || millis() - lastWho >= 500)) {
      lastWho = millis();
      Dashboard::drawWho();
    }
    lastDraw = 0;
  } else if (sleeping) {   // la tête dort : le torse dort avec elle (« z » qui montent, ≈ 12 images/s)
    static uint32_t lastSleep = 0;
    if (displayReady && !screenDark && millis() - lastSleep >= 80) {
      lastSleep = millis();
      Dashboard::drawSleep(headTheme, millis() / 1000.0f);
    }
    lastDraw = 0;
  } else if (millis() - lastDraw >= 500 || drawnCriesOn != (Speaker::cryLevel() > 0) ||
             drawnBrain != (brain >= BRAIN_ASKED ? 2 : brain)) {   // bouton touché : tout de suite
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
    d.cameraOn = TorsoCamera::running();  // voyant CAM : capture réelle, pas seulement demande
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
    d.radarCm = 0;   // sortie OUT seule : pas de distance
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
    // Couleurs de l'humeur tant que la tête est posée et parle ; sinon celles d'origine.
    // (L'inquiétude du retrait garde les dernières couleurs reçues.)
    DashTheme theme = headTheme;
    theme.valid = docked && headTheme.valid && millis() - moodAt < MOOD_TIMEOUT_MS;
    if (displayReady) Dashboard::draw(d, theme);
  }
  delay(2);
}

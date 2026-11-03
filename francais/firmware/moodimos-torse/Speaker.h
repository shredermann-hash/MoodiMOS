// Haut-parleur du torse : phrases WAV (/voix/<nom>.wav) lues sur la carte TF, ou à défaut dans la mémoire
// flash du torse (partition FAT de 9 Mo, remplie par le Mac : commande « deposer »), PCM 16 bits 16 kHz
// mono ou stéréo. Lecture dans sa propre tâche ; le micro est ignoré
// pendant que le robot parle.
#pragma once
#include <stdint.h>

namespace Speaker {

bool begin();                   // monte la carte TF et la flash ; Microphone::begin() doit avoir réussi
bool play(const char* name);    // « bonjour » → /voix/bonjour.wav ; faux si absent ou format refusé
bool has(const char* name);     // phrase présente (carte TF ou flash)
const char* storage();          // où sont les phrases, pour « etat »
// Phrase reçue du Mac, rangée dans la flash (formatée à la première) ; storeClose(false) l'efface.
bool storeOpen(const char* name);
bool storeWrite(const uint8_t* data, size_t n);
bool storeClose(bool keep);
bool playing();                 // phrase ou cri : le micro et l'IMU du torse les ignorent
bool talking();                 // une phrase seulement : la bouche de la tête la suit, pas les cris
// Cri d'humeur synthétisé (MoodSounds.h), jamais par-dessus une phrase ; une phrase le coupe.
bool cry(uint8_t sound, uint8_t variant);
uint8_t cryLevel();             // volume des cris, 0 (muets) à 10, gardé dans la flash du torse
void setCryLevel(uint8_t level);
void toggleCries();             // bouton de l'écran : muets, ou de nouveau au dernier volume choisi
void sayDemo();                 // phrase de test rangée dans le firmware (banc, sans carte TF)
// Son en mémoire, PCM 16 bits mono 16 kHz (annonce, réponse). `ready` : échantillons déjà reçus, si le son arrive
// encore pendant qu'il est joué ; la lecture attend chaque morceau (1 s au plus) avant de le jouer.
bool playPcm(const int16_t* pcm, int samples, const volatile int* ready = nullptr);
// Suite du son de playPcm (réponse phrase par phrase) : il dure maintenant `samples`, joué sans coupure ; faux si
// la lecture avait déjà fini (la suite est alors à jouer elle-même).
bool extendPcm(int samples);
void setVolume(uint8_t reg32);  // registre 0x32 du codec, 0 à 255 (banc ; remis à VOLUME_REG au redémarrage)
void sweep();                   // sinus fort, volume du codec par paliers (banc : trouver où ça grésille)
void chime();                   // deux notes douces pour essayer le haut-parleur (banc)
uint8_t amplitude();            // ouverture de bouche 0 … 255 pour la trame SPEAK
void stop();

}  // namespace Speaker

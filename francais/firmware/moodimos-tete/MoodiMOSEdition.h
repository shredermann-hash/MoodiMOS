// Édition du robot. Copie identique dans moodimos-tete/ et moodimos-torse/, vérifiée par les tests :
// tête et torse d'un même robot se compilent avec la même édition (le corps suit la tête).
//
//   INSTRUCTABLE : édition neutre d'origine. Couleurs adultes et neutres (palette NeoCité telle
//                  quelle), pas de câlins : tap = apaise, double tap = cadran. Un seul visage.
//   COMPLETE     : le robot de la notice publiée (cao/MoodiMOS_Robot). Il
//                  reprend tout l'ancienne édition « petite » (retirée le 2026-10-28) : la petite joue
//                  avec lui, toujours avec un adulte à côté, et il est éteint en leur absence. Visage
//                  néon ambre par défaut, personnages horloge (visage, en pied) et classique au menu
//                  des designs ; sommeil mauve qu'elle a choisi ; câlins (chatouilles, bisou) ; toutes
//                  les humeurs. Tableau de bord du torse et cadran en néon.
//
// Choix à la compilation : -DMOODIMOS_EDITION=2 (instructable) ou 3 (complete), voir firmware/build.sh et
// firmware/flash.sh, qui programme complete par défaut (1 était la petite). Sans option (IDE Arduino) : complete aussi.
#pragma once

#define MOODIMOS_INSTRUCTABLE 2
#define MOODIMOS_COMPLETE 3
#ifndef MOODIMOS_EDITION
#define MOODIMOS_EDITION MOODIMOS_COMPLETE
#endif
#if MOODIMOS_EDITION != MOODIMOS_INSTRUCTABLE && MOODIMOS_EDITION != MOODIMOS_COMPLETE
#error "MOODIMOS_EDITION : 2 (instructable) ou 3 (complete) ; l'édition petite (1) est devenue complete"
#endif

namespace Edition {
constexpr bool COMPLETE = MOODIMOS_EDITION == MOODIMOS_COMPLETE;
constexpr const char* NAME = COMPLETE ? "complete" : "instructable";
constexpr bool CUDDLES = COMPLETE;         // chatouilles (taps rapides, frottement) et bisou (appui long)
constexpr bool SOFT_SLEEP = COMPLETE;      // sommeil mauve, sourire et joues roses ; sinon blanc du vide, bouche en o
constexpr bool CLOCK_DESIGNS = COMPLETE;   // personnage horloge (visage et en pied) : l'édition neutre n'en a pas
constexpr bool NEON = COMPLETE;            // visage néon (par défaut), cadran et tableau de bord assortis
constexpr bool GAMES = COMPLETE;           // jeu « Devine l'animal » sur l'écran du torse
}  // namespace Edition

// Game "Guess the Animal" (complete edition) table, generated from `extras/guess_animal/to_robot.py` using `animals.json`—do not edit manually. For each animal, lists questions where the answer is yes (bit `n = question n`), and those where the answer depends (bit `n`). All others default to no. An "inverse" question expects no for a yes in the table. Uppercase text for screen outline (torso display).
#pragma once
#include <stdint.h>

namespace GuessData {

struct Question { const char* text; bool inverse; };
struct Animal { const char* name; uint64_t yes, maybe; };

constexpr int QUESTIONS = 45, ANIMALS = 38;

constexpr Question QUESTION[QUESTIONS] = {
  {"EST-CE QU'IL VIT DANS L'EAU ?", false},   // eau
  {"EST-CE QU'IL VIT DANS LA MER ?", false},   // mer
  {"EST-CE QU'IL SAIT VOLER ?", false},   // vol
  {"EST-CE QU'IL A DES PLUMES ?", false},   // feathers
  {"EST-CE QU'IL A DES POILS ?", false},   // hair
  {"EST-CE QU'IL VIT À LA FERME ?", false},   // closed
  {"EST-CE QU'IL PEUT VIVRE DANS UNE MAISON, AVEC NOUS ?", false},   // house
  {"EST-CE QU'ON VA LE VOIR AU ZOO ?", false},   // zoo
  {"EST-CE QU'IL EST PLUS GROS QUE TOI ?", false},   // large
  {"EST-CE QU'IL EST ÉNORME, PLUS GROS QU'UNE VOITURE ?", false},   // tres_gros → very_large
  {"EST-CE QU'IL EST TOUT PETIT, PLUS PETIT QUE TA MAIN ?", false},   // small
  {"EST-CE QU'IL MARCHE À QUATRE PATTES ?", false},   // four_legs
  {"EST-CE QU'IL A DES PATTES ?", true},   // sans_pattes
  {"EST-CE QU'IL A DES RAYURES ?", false},   // scratches
  {"EST-CE QU'IL A DES TACHES ?", false},   // dots
  {"EST-CE QU'IL EST NOIR ET BLANC ?", false},   // noir_blank
  {"EST-CE QU'IL EST JAUNE OU ORANGE ?", false},   // orange
  {"EST-CE QU'IL EST ROSE ?", false},   // rose
  {"EST-CE QU'IL A UNE CORNE, OU DES CORNES SUR LA TÊTE ?", false},   // horns
  {"EST-CE QU'IL A UN TRÈS LONG COU ?", false},   // `long_cou`
  {"EST-CE QU'IL A UNE TROMPE ?", false},   // Optical lens (for the robot’s head)
  {"EST-CE QU'IL A UNE CARAPACE, OU UNE COQUILLE ?", false},   // shell
  {"EST-CE QUE C'EST UNE PETITE BÊTE, COMME UNE FOURMI ?", false},   // insect
  {"EST-CE QU'IL FAIT DE GRANDS SAUTS ?", false},   // jump
  {"EST-CE QU'IL PEUT MORDRE OU PIQUER TRÈS FORT ?", false},   // dangerous
  {"EST-CE QU'IL NOUS DONNE DU LAIT ?", false},   // milk
  {"EST-CE QU'IL POND DES OEUFS ?", false},   // eggs
  {"EST-CE QU'IL A UNE CRINIÈRE, DES LONGS POILS SUR LE COU ?", false},   // mane
  {"EST-CE QU'IL A DE LA LAINE ?", false},   // wool
  {"EST-CE QU'IL A DE GRANDES OREILLES ?", false},   // ears
  {"EST-CE QU'IL GRIMPE AUX ARBRES ?", false},   // Arms
  {"EST-CE QU'IL EST MAGIQUE ?", false},   // magical
  {"EST-CE QUE C'EST UN ANIMAL D'IL Y A TRÈS TRÈS LONGTEMPS ?", false},   // disappeared
  {"EST-CE QU'IL FAIT MIAOU ?", false},   // meow
  {"EST-CE QU'IL FAIT OUAF OUAF ?", false},   // barks
  {"EST-CE QU'IL FAIT MEUH ?", false},   // *meow*
  {"EST-CE QU'IL FAIT BÊÊÊ ?", false},   // bee
  {"EST-CE QU'IL FAIT GROIN-GROIN ?", false},   // groin
  {"EST-CE QU'IL FAIT COT-COT ?", false},   // No meaningful translation possible—`cotcot` is a placeholder or non-textual artifact.
  {"EST-CE QU'IL FAIT COIN-COIN ?", false},   // coincoin
  {"EST-CE QU'IL FAIT HOU-HOU ?", false},   // Houhou
  {"EST-CE QU'IL FAIT BZZZ ?", false},   // bzz
  {"EST-CE QU'IL FAIT CROA-CROA ?", false},   // *croak*
  {"EST-CE QU'IL FAIT HIII, COMME UN CHEVAL ?", false},   // hennit
  {"EST-CE QU'IL RUGIT TRÈS FORT, GRRR ?", false},   // Rugit
};

constexpr Animal ANIMAL[ANIMALS] = {
  {"UN CHAT", 0x0000000240000850ULL, 0x000000000081a000ULL},
  {"UN CHIEN", 0x0000000400000850ULL, 0x000000002100c100ULL},
  {"UNE VACHE", 0x000000080204c930ULL, 0x0000000000000000ULL},
  {"UN COCHON", 0x0000002000020820ULL, 0x0000000000000110ULL},
  {"UN CHEVAL", 0x0000080008000930ULL, 0x0000000000800000ULL},
  {"UN MOUTON", 0x0000001010000830ULL, 0x0000000002040100ULL},
  {"UNE POULE", 0x0000004004000028ULL, 0x0000000000010004ULL},
  {"UN CANARD", 0x000000800400002cULL, 0x0000000000010001ULL},
  {"UN LAPIN", 0x0000000020800810ULL, 0x0000000000000060ULL},
  {"UNE SOURIS", 0x0000000000000c10ULL, 0x0000000020000040ULL},
  {"UN LION", 0x0000100009010990ULL, 0x0000000000000000ULL},
  {"UN TIGRE", 0x0000100001012990ULL, 0x0000000000000000ULL},
  {"UN ÉLÉPHANT", 0x0000000020100b80ULL, 0x0000000000000010ULL},
  {"UNE GIRAFE", 0x0000000000094990ULL, 0x0000000000040200ULL},
  {"UN ZÈBRE", 0x000000000800a990ULL, 0x0000080000000000ULL},
  {"UN SINGE", 0x0000000040000090ULL, 0x0000000000800900ULL},
  {"UN OURS", 0x0000000001000910ULL, 0x0000100040000080ULL},
  {"UN PANDA", 0x0000000000008990ULL, 0x0000000040004000ULL},
  {"UN KANGOUROU", 0x0000000000800190ULL, 0x0000000020000800ULL},
  {"UN CROCODILE", 0x0000000005000981ULL, 0x0000000000000000ULL},
  {"UN SERPENT", 0x0000000005001000ULL, 0x0000000000006180ULL},
  {"UNE TORTUE", 0x0000000004200800ULL, 0x0000000000000041ULL},
  {"UNE GRENOUILLE", 0x0000040004800801ULL, 0x0000000000000400ULL},
  {"UN POISSON", 0x0000000004001001ULL, 0x0000000000012442ULL},
  {"UN REQUIN", 0x0000000001001103ULL, 0x0000000004000280ULL},
  {"UNE BALEINE", 0x0000000000001303ULL, 0x0000000000000000ULL},
  {"UN DAUPHIN", 0x0000000000801103ULL, 0x0000000000000080ULL},
  {"UNE PIEUVRE", 0x0000000004000003ULL, 0x0000000000031100ULL},
  {"UN PAPILLON", 0x0000000004400404ULL, 0x0000000000014000ULL},
  {"UNE ABEILLE", 0x0000020005412404ULL, 0x0000000000000000ULL},
  {"UNE COCCINELLE", 0x0000000004404404ULL, 0x0000000000200000ULL},
  {"UN ESCARGOT", 0x0000000004201400ULL, 0x0000000000400000ULL},
  {"UN HIBOU", 0x000001000400000cULL, 0x0000000040000000ULL},
  {"UN PINGOUIN", 0x0000000004008089ULL, 0x0000000000000002ULL},
  {"UN DINOSAURE", 0x0000100105000300ULL, 0x00000000000c0804ULL},
  {"UNE LICORNE", 0x0000000088040910ULL, 0x0000080000000004ULL},
  {"UN RHINOCÉROS", 0x0000000000040980ULL, 0x0000000001000210ULL},
  {"UN HIPPOPOTAME", 0x0000000000000981ULL, 0x0000000001020200ULL},
};

}  // <source> namespace GuessData </source>

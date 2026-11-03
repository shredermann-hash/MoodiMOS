# MoodiMOS — le robot de la notice

Le robot de la notice publiée. Depuis le
28/10/2026 il reprend aussi tout l'ancienne édition « petite » : c'est le robot avec lequel joue une
enfant, **toujours à côté d'un adulte** (le robot est éteint en son absence). Son avis sur
l'apparence et les réactions passe avant celui de l'adulte. Sa CAO est dans
[`cao/MoodiMOS_Robot/`](../cao/MoodiMOS_Robot/LISEZMOI.md). Le firmware reprend le visage de ses rendus
(`cao/MoodiMOS_Robot/ecrans/visage.png`) : **traits lumineux ambre sur fond noir**. Le torse garde le
tableau de bord des autres éditions, tracé dans le même style (l'écran du torse des rendus n'est
qu'une illustration).

## Principe de conception : éducatif malgré l'écran

Le robot apprend à **respecter un écran et du matériel**. Il ne se commande pas à coups de
secousses ni d'appuis forts :

- le secouer le **fâche** (rouge, puis orange en retombant) : l'enfant voit tout de suite que c'est
  trop fort, et apprend à doser ;
- les gestes doux sont **récompensés** : un tap l'apaise, le frotter le fait rire, un appui long
  lui fait un bisou et fait s'envoler des petits cœurs ; l'écran du torse relaie les mêmes gestes ;
- le tactile est réglé pour une petite main et des gestes légers : aucune réaction n'exige de
  taper fort ou de secouer fort ;
- il s'endort quand on le pose à plat ou qu'il fait noir : on le laisse tranquille.

À garder en tête pour toute nouvelle réaction : **la douceur doit toujours payer plus que la
force**, et rien ne doit encourager à malmener l'objet.

## Ce qui la distingue

| | Édition complète |
|---|---|
| Visage (tête) | design « neon » : yeux en verre jaune lumineux avec pupille et reflets, sourcils en fil orangé, joues « /// », bouche orange et langue saumon, halo autour de chaque trait |
| Yeux fermés | rire, joie, apaisé, clin d'œil : le verre reste allumé, barré d'un arc sombre ∩ au-dessus d'un dôme orangé (le visage du rendu) ; sommeil : arc ∪ ; clignement : le verre s'éteint, un fil lumineux à la place |
| Couleurs d'humeur | le fond reste noir, ce sont les traits qui changent : ambre au repos, rouge en colère, orange, or, acier, violet, cyan, blanc chaud |
| Cadran (double tap) | le visage dit l'heure : ses yeux de verre regardent l'heure, qui prend la place de la bouche en gros chiffres de verre jaune (police du tableau de bord du torse, ombre orangée en bas comme ses yeux) ; les deux points battent la seconde, la lunette se remplit de points au fil de la minute, il cligne des yeux ; dessous, la date (« JEUDI 29 », puis le mois) quand l'heure est datée |
| Tableau de bord (torse) | le même que les autres éditions (en-tête, icône Wi-Fi, voyant CAM, batterie ou USB, cinq mesures, tête, radar, bouton « Où est ma tête ? »), en traits néon sur noir avec une police au trait, sans cases autour des mesures (les icônes suffisent) ; l'icône Wi-Fi, entre le titre et le voyant CAM, s'allume en jaune quand l'heure est venue du réseau, reste grise sinon et n'apparaît pas sans Wi-Fi enregistré ; « USB » s'allume en jaune quand le torse est branché ; le trait sous l'en-tête prend la couleur de l'humeur de la tête |
| Tête retirée / reposée | comme les autres éditions, le torse montre le visage de la tête : ici le visage néon, inquiet qui cherche le corps, puis rieur avec étincelles |
| Sommeil | traits **mauves** (le choix de la petite), petit sourire, joues roses, « z » au-dessus de la tête |
| Chatouilles | 3 taps rapides ou plus, ou frotter l'écran (tête ou torse) → il rit en sautillant (et sa colère retombe) |
| Bisou | appui long, doigt immobile (tête ou torse) → yeux clos, bouche ronde, cœurs roses, puis content et apaisé |
| Minuteur | un seul à la fois, lancé à la voix par le propriétaire ou depuis le Mac (`cerveau/minuteur.sh`), un nouveau remplace l'ancien. Le temps restant prend la place du titre du tableau de bord, en grand (« 12:34 », puis « 2H23 » au-delà d'une heure). À la fin, écran « FINI ! » et sonnerie sans limite, jusqu'à ce qu'on touche l'écran : deux « dring » de cloche, puis « Le minuteur est fini ! », et ainsi de suite, pendant que la tête fait la fête, même endormie. Elle est à part des bruitages d'humeur : elle s'entend même bruitages coupés, coupe un bruitage en cours et aucun ne part pendant qu'elle sonne. L'heure de fin est gardée en mémoire jusqu'au toucher : il continue après un redémarrage, et sonne encore s'il a fini depuis moins de 15 min |
| Designs | néon par défaut ; le menu (doigt immobile 3 s sur le visage, puis glisser à gauche ou à droite) propose aussi le classique, l'horloge et l'horloge en pied, le personnage de la petite |

Communs avec l'édition instructable : humeurs NeoCité, colère par secousse, humeurs décidées par
le torse, suivi du regard, voix, capteurs, liaison du cou et toutes les corrections de fond.

## Jeux

- **« Devine l'animal »**, dans le robot depuis le 28/10/2026. Le bouton patte du tableau de bord
  du torse (tête posée) lance la partie : la petite pense à un animal, le robot pose ses questions
  à voix haute (voix siwis) et elle répond avec les trois gros boutons de l'écran du torse, pouce
  vert = oui, pouce rouge = non, « ? » = je ne sais pas. La tête est curieuse à chaque question,
  réfléchit, sourit quand il propose, fait la fête quand il trouve. Trois propositions au plus ;
  sans réponse, la question est redite deux fois, puis la partie s'arrête et il boude un peu (partie
  laissée en plan). Après une partie finie, « On rejoue ? » sans réponse : il dit au revoir. La croix
  en haut à droite arrête la partie sans bouderie, retirer la tête aussi. Pendant la partie, la pénombre
  ne l'endort pas.
  - Moteur : `GuessGame.h`, le même calcul que le prototype (mêmes questions dans le même ordre pour
    les 38 animaux), sans IA : un tableau d'animaux et des probabilités. Tests : `tests/guess_tests.cpp`.
  - Table et phrases : [`devine_animal/vers_robot.py`](devine_animal/vers_robot.py) écrit
    `GuessData.h` depuis `animaux.json` (`table`), fabrique les 90 phrases du jeu et celle des humeurs (`secoue`) avec Piper (`voix`,
    5,9 Mo) et les range dans la mémoire flash du torse par l'USB (`envoyer`, commande `deposer`) :
    pas besoin de carte TF ni d'ouvrir le robot. Après un changement d'`animaux.json` : les trois,
    puis reprogrammer le torse.
  - Moniteur série : `jeu` lance une partie, `jeu oui|non|jsp|fin` répond comme les boutons.
  - Prototype d'origine, sur le Mac, où elle répond à voix haute (Whisper) :
    [`devine_animal/devine_animal.py`](devine_animal/devine_animal.py).
- [`horloge/horloge_preview.html`](horloge/horloge_preview.html) : le personnage horloge en pied,
  porté dans le firmware (`Design::HorlogeEnPied`).

## Compiler et programmer

```bash
sh firmware/flash.sh torse
sh firmware/flash_tete_cou.sh        # tête fermée, programmée par le cou depuis l'USB du torse
```

Tête et torse d'un même robot se programment dans la **même édition** (`MoodiMOSEdition.h`, copie
identique dans les deux firmwares). L'édition complète est celle des scripts par défaut ; `sh firmware/flash.sh tete` reste possible tête ouverte.

Robot branché sur le hub de la **UNO Q** (validé le 29/10/2026) : le Mac compile, la UNO Q programme par
son USB (SSH par clé, `UNO_Q=arduino@adresse`, ou celle gardée par le service du Mac). Seule l'application est écrite : les
phrases rangées dans la flash du torse restent.

```bash
sh firmware/flash_uno_q.sh tete      # tête par le cou, comme flash_tete_cou.sh
sh firmware/flash_uno_q.sh torse
```

Sur la UNO Q, une fois : `python3 -m venv ~/moodimos/venv && ~/moodimos/venv/bin/pip install esptool==5.3.1`.
Garder cette version : avec esptool 5.6.0, l'outil chargé dans la tête ne répondait plus à travers le cou.

## Couleurs du corps

À choisir avec la petite. Le mauve du sommeil est sa couleur préférée à ce jour.

## Aperçus

Images produites sur ordinateur par le code même du firmware (`Face.cpp`, `DashboardNeon.cpp`,
`Watch.cpp`, `Canvas.cpp`), sans retouche :

- [`visage_neon.png`](visage_neon.png) : attentif, content, rieur, joie, apaisé, clin d'œil,
  surpris, curieux, fâché, emballé, boude, perplexe, inquiet, fatigué, endormi, clignement.
- [`tableau_de_bord.png`](tableau_de_bord.png) : tableau de bord sans couleur de la tête, au repos,
  en colère, tête partie ; puis tête retirée et tête reposée.
- [`cadran.png`](cadran.png) : le cadran du double tap, mercredi 30 septembre à 14:35:58 (la date la plus longue).
- [`minuteur.png`](minuteur.png) : le tableau de bord avec un minuteur de 12 min 34 s, puis de 2 h 23, et l'écran de fin.
- [`qui_parle.png`](qui_parle.png) : l'écran « Qui parle ? » du bouton bulle, un rond par personne de la maison
  (`Household_local.h`, personnel).

## Code

- Tête : `Design::Neon` (`Design.h`), palettes `neonPalette` (`Mood.h`), rendu `renderNeon` et
  suivants (`Face.cpp`), cadran `Watch::begin(taille, true)`.
- Torse : `DashboardNeon.cpp`, appelé par `Dashboard.cpp` quand `Edition::COMPLETE` (mêmes données `DashData`,
  mêmes couleurs d'humeur `DashTheme`, même bouton de sonnerie).
- `Canvas.h/.cpp` est maintenant une copie identique dans les deux firmwares (traits d'un seul
  tenant `fillStroke`, halos `glowStroke` et `glowEllipse`, anneaux `strokeRing`), vérifiée par
  `tests/run.sh`.
- `StrokeFont.h`, la police au trait du tableau de bord, est aussi une copie identique dans les deux
  firmwares : le torse en écrit ses textes, la tête les chiffres et la date du cadran.
- Tests : `tests/dashboard_q_tests.cpp`, rendu néon dans `tests/mood_tests.cpp`, cadran néon dans
  `tests/head_tests.cpp` ; toutes les suites tournent pour les deux éditions.

## Mesuré sur les cartes (14/10/2026)

Tête et torse programmés en édition complète au banc (USB, sans le cou). Commande `perf` de la tête :
visage 13,5 ms (absent) à 23,7 ms (joie, étincelles) par image, effacement 5,5 à 9,4 ms : environ
**29 images/s**, comme le visage classique. Pour y arriver, le fond noir s'efface par simple
remplissage (`Canvas::setFlatBackground`) et les halos s'écrivent en teintes assombries sans relire
l'image en PSRAM (avec des fondus pixel par pixel, la joie tombait à 15 images/s). La commande
`ecran` du torse renvoie bien le tableau de bord néon.

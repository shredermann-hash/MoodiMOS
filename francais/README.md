# MoodiMOS : le dossier de fabrication

Commencez par la notice, publiée sur Instructables. Ce dossier contient ce qui sert à fabriquer le robot
alimenté par USB : les programmes des deux cartes ESP32-S3, les pièces à imprimer, les générateurs de CAO, les scripts
Fusion, les schémas de câblage et les outils de conversation facultatifs pour Mac.

Le robot parle et comprend le français. Un dossier de fabrication en anglais se trouve à côté de celui-ci, dans
`english/`, avec des noms de fichiers en anglais.

## Avant de commencer

MoodiMOS est un projet personnel partagé gratuitement. Claude, l'IA d'Anthropic, a aidé à le développer et a écrit le
code du garde-fou de la conversation à partir des règles de l'auteur ; Codex a participé à la relecture du code et de
la notice. Les fichiers décrivent le montage et les essais de l'auteur et sont fournis tels quels, sans garantie.
Vérifiez les dimensions sur vos composants et contrôlez le câblage avant la première mise sous tension.

Gardez les petites pièces et les aimants hors de portée des jeunes enfants. La conversation avec une intelligence
artificielle est facultative et expérimentale : elle peut donner des réponses fausses ou inadaptées. Si un enfant
l'utilise, activez le garde-fou en français décrit à l'étape 15 et restez à ses côtés. Il ne garantit pas chaque
réponse.

Le corps fourni, plus récent, n'a pas été monté avec la carte : le robot de l'auteur porte la façade fournie sur une
première coque. Faites l'essai de l'écran de l'étape 3 avant d'imprimer le corps entier.

## Trouver les fichiers

- La notice illustrée complète : sur Instructables
- Contacts du cou et couleurs des fils : [MoodiMOS_Cablage_pogo_cou.md](MoodiMOS_Cablage_pogo_cou.md)
- Schéma général de câblage : [MoodiMOS_Schema_cablage_torse_tete.svg](MoodiMOS_Schema_cablage_torse_tete.svg)
- Schéma du cou : [MoodiMOS_Schema_pogo_cou.svg](MoodiMOS_Schema_pogo_cou.svg)
- Pièces prêtes à imprimer : `cao/MoodiMOS_Robot/stl_impression/`
- CAO, cotes et les deux scripts Fusion : [cao/MoodiMOS_Robot/LISEZMOI.md](cao/MoodiMOS_Robot/LISEZMOI.md)
- Réglages d'impression et jauge du connecteur :
  [cao/MoodiMOS_Organique/stl_impression/REGLAGES_IMPRESSION.md](cao/MoodiMOS_Organique/stl_impression/REGLAGES_IMPRESSION.md)
- Programme de la tête : `firmware/moodimos-tete/`
- Programme du torse : `firmware/moodimos-torse/`
- Comportement, jeu, visage horloge et aperçus des écrans : [extras/LISEZMOI.md](extras/LISEZMOI.md)
- Conversation, annonces et minuteur, facultatifs : [cerveau/LISEZMOI.md](cerveau/LISEZMOI.md)
- Rendus de conception de l'étape Fusion : `illustrations/`

Lancez les commandes depuis ce dossier et gardez son arborescence : les générateurs Python et les scripts Fusion
trouvent leurs fichiers par rapport à eux-mêmes. Ne mélangez pas ses fichiers avec ceux du dossier anglais, dont les
noms sont traduits.

## Programmer les deux cartes

La chaîne de référence est ESP32 3.3.11 avec GFX Library for Arduino 1.6.7, installée comme à l'étape 5. Donnez le
port USB réel, affiché par `arduino-cli board list` :

```sh
sh firmware/flash.sh torse /dev/cu.usbmodem1101
sh firmware/flash.sh tete /dev/cu.usbmodem1101
```

Programmez chaque carte à part la première fois, la tête hors du cou, sur son propre port USB. Une fois les deux
cartes programmées et le cou en état, la tête fermée se met à jour par le torse, sur macOS :

```sh
sh firmware/flash_tete_cou.sh /dev/cu.usbmodem1101
```

Sans port, les scripts le cherchent eux-mêmes. `sh firmware/build.sh` compile les deux cartes sans rien téléverser.

## Régénérer la CAO

```sh
python3 -m venv cao/MoodiMOS_Organique/.venv
cao/MoodiMOS_Organique/.venv/bin/pip install -r cao/MoodiMOS_Organique/requirements.txt
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_pieces.py
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_membres.py
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_verifier.py
```

Attendez la fin des générateurs avant l'import dans Fusion ou un rendu. Ils écrivent dans `stl/` les pièces placées
comme dans l'assemblage ; `stl_impression/` contient les pièces posées sur le plateau. Les STL fournis s'impriment sans
Fusion. Les
`bras_*` et `basket_*` de `stl_impression/` sont allégés pour rester sous la limite de 10 Mo d'Instructables (écart
mesuré : 0,02 mm au plus) ; les générateurs redonnent les fichiers complets, jusqu'à 29 Mo. Le socle facultatif et les
pochoirs de peinture viennent de `robot_socle.py` et `robot_pochoirs.py`, dans le même dossier.

Pour Fusion, ajoutez l'un de ces dossiers par **Utilitaires > Compléments > Scripts et compléments** :

- `cao/MoodiMOS_Robot/MoodiMOS_Robot/` : construit les volumes de la tête et du torse et importe les maillages sculptés
  des membres ;
- `cao/MoodiMOS_Robot/MoodiMOS_Assemblage/` : importe l'assemblage des maillages générés.

Les deux ont besoin des fichiers générés. Lancez-les dans Fusion, pas avec le Python du terminal. Une retouche faite
dans Fusion ne se reporte pas dans les STL générés par Python.

## Conversation sur Mac (facultative)

Suivez l'étape 15 pour installer Whisper, Ollama et Piper. Les gros modèles et l'environnement Python restent hors de
ce dossier.

```sh
sh cerveau/service_mac.sh installer
sh cerveau/service_mac.sh etat
sh cerveau/service_mac.sh journal
sh cerveau/service_mac.sh retirer
```

Ouvrir ce dossier n'installe aucun service. Le robot doit être branché par un câble USB de données. Après les
téléchargements nécessaires, le Mac traite la conversation sur place.

Créez vous-même `cerveau/personnes.json` pour le garde-fou ou les profils de la maison, comme l'explique
[cerveau/LISEZMOI.md](cerveau/LISEZMOI.md). Ne publiez ni ce fichier ni `firmware/moodimos-torse/Household_local.h`.

## Les chemins à définir

Aucun chemin n'est propre à une machine. Ce que chacun adapte :

- `~/MoodiMOS-IA` : le dossier où installer Python, la voix, Whisper et Ollama ; variable `MOODIMOS_IA`.
- `arduino@adresse` : votre UNO Q, facultative ; `UNO_Q=arduino@votre_adresse` devant la commande.
- `/media/arduino/NOM_DE_VOTRE_CARTE` : la carte SD de la UNO Q, reliée une fois à `~/moodimos/ia` ; le nom de votre
  carte, ou `MOODIMOS_SD`.
- `/dev/cu.usbmodem…` : le port du robot, ici un exemple macOS ; trouvé par les scripts, ou donné en argument.

`~/moodimos`, `~/Library/LaunchAgents` et `~/Library/Logs` restent tels quels : ce sont des dossiers de votre compte,
créés au besoin. Sur Mac, le service de conversation et le minuteur passent par `launchd` et `plutil`.

## Voix du torse et commandes série utiles

Chaque commande se termine par un retour à la ligne ; le robot répond en français. Avant une séance à la main, libérez
le port série du service de conversation, comme le dit la notice.

- `capteurs` : mesures de la pièce
- `etat` : état du robot et place restante pour les phrases
- `radar` : radar et détection de la tête sur le cou
- `camera` : suivi de la caméra, sans exporter d'image
- `tete clin 2` : clin d'œil de la tête pendant deux secondes
- `silence` : arrête la lecture
- `acces` / `acces fin` : lire ou fermer l'autorisation donnée au doigt sur l'écran

`dire nom` joue une phrase enregistrée. `deposer nom taille` range un fichier WAV dans le torse : il répond `pret`,
reçoit le fichier par morceaux de 4 Ko, confirme chacun par un `+`, puis donne le nombre d'octets reçus et leur somme
de contrôle. Passez par l'outil fourni, `extras/devine_animal/vers_robot.py` (étape 12), plutôt que de taper les octets
à la main. `dis taille` et `dis+ taille` envoient au haut-parleur un son mono à 16 kHz en 16 bits signés ; le second
continue une réponse. `annonce taille` demande le transfert d'une annonce.

## Ce que ce dossier ne contient pas

Ni fichier personnel de la maison, ni historique git. Les rendus de conception montrent l'apparence voulue ; les
photos du robot assemblé et les notes de montage disent ce qui a vraiment été essayé. Un rendu ne remplace ni l'essai
d'ajustement ni le test de l'électronique.

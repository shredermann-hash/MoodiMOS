# MoodiMOS — la CAO du robot

Le robot de la notice publiée. Cette variante travaille
la silhouette et les couleurs des captures fournies le 4 octobre 2026.

## Références

Les quatre captures originales, sans retouche, sont dans `references/` : vue d'ensemble,
détail de main, jambes et chaussures, puis profil technique main/basket.

Les captures définissent l'apparence. Leurs étiquettes de cotes ne constituent pas un
plan de fabrication. Après précision de l'auteur, **le corps garde 82 × 120,5 × 48 mm**,
la caméra et le radar gardent leur implantation d'origine. Les cartes, les vitres,
le connecteur pogo garde ses dimensions source (connecteur magnétique entier, sans aimants séparés depuis le
13/10/2026).

## Changements de silhouette

- Lunette noire Ø 86 autour de la vitre Ø 75, tête orange Ø 92, bélière claire plus étendue.
- Cadre noir mince collé autour de la vitre du torse, avec ouverture de caméra.
- Torse en deux pièces depuis le 16/10/2026 : façade (paroi avant et poche de vitre, imprimée dos sur le plateau)
  vissée au corps par 4 vis M2 × 8 à tête bombée, aux places des vis décoratives. Le radar est décalé contre le
  berceau de la caméra pour laisser la place au bossage de la vis du haut à droite.
- Bras plus écartés, poignet et coude noirs, bracelets orange épais.
- Mains sculptées dans une sphère ovale pleine : lobes courts de doigts et sillons,
  pouce accolé au volume, sans doigts en boucle ; poignet peint en noir.
- Jambes légèrement écartées vers les chevilles, articulations noires aux extrémités.
- Chaussures de 73,9 mm sur leur axe, tournées de 12° vers l'extérieur :
  semelle noire épaisse, bride orange rainurée, deux ouïes latérales,
  panneau arrière noir, anneau orange et collerette ivoire.
- Visage agrandi dans la vraie zone active Ø 53,28 ; tableau de bord dans la zone
  active 48,96 × 73,44. Les marges noires réelles des écrans restent visibles.
- Orange plus vif, éclairage renforcé, fond continu sans ligne d'horizon.
- Normalisation des accents des noms de fichiers : le liseré orange de lunette
  reçoit désormais le bon matériau sur macOS.

`robot_style.py` centralise les dimensions de design : hauteur nominale des chaussures 46 mm,
jambe visible 36 mm, axe de chaussure à ±34 mm. Les logements de jambes sont à
±27,5 mm et utilisent les tenons aimantés d'origine. Hauteur totale : **315,8 mm**.
Les rendus précédant cette révision sont conservés dans `rendus/avant_2026-10-04/`.
Les essais intermédiaires sont rangés dans `rendus/iterations_2026-10-04/`.

## Socle

`robot_socle.py` dessine un socle rond et plein, dans l'esprit de la lunette de la tête : Ø 150 × 20 mm, gradin dont le
dessus est peint en orange, plateau Ø 138. Le disque est centré sur les deux semelles. Le robot s'y pose dans les empreintes de ses baskets
(2,5 mm, forme exacte des semelles, à 12° vers l'extérieur). Un aimant Ø 10 × 5 par pied, sous l'axe de la jambe,
le tient en place ; l'aimant jumeau se loge sous la basket, dans un trou percé à la main avec
`gabarit_aimant_basket_droit.stl` / `_gauche.stl`. Décor peint :
bandeau noir en bas (rainure-guide), dessus du gradin orange (de la paroi du plateau à sa rainure-guide, avant l'arrondi du bord),
corps et plateau ivoire. Quatre logements de patins Ø 12 dessous.
Rendu sur le socle : `ROBOT_SOCLE=1 blender -b -P cao/MoodiMOS_Robot/robot_rendu.py -- 34 face` (images `*_socle.png`).

## Pochoirs de la lunette

`robot_pochoirs.py` écrit deux pièces pour peindre en orange les index et le liseré de la lunette noire, d'un seul passage
de bombe. On peint la lunette seule, avant de poser la vitre.

- `pochoir_index_lunette.stl` : disque plein de 0,8 posé sur la face. Sa lèvre se centre dans la poche de vitre. Il masque
  la face noire, la rainure-guide et l'intérieur. Restent ouverts les 8 index et la bande du bord (r 41,8 → 43).
- `cache_flanc_lunette.stl` : bague qui épouse le flanc arrondi et le méplat. Elle s'enfile par l'avant et se cale d'elle-même.
  Son rebord dépasse de 0,5 devant la face et couvre le jeu au bord de la bande.

Pose : lunette face en haut. Enfiler le cache, puis poser le pochoir, lèvre dans la poche de vitre. Le motif se répète
tous les quarts de tour : tourner le pochoir jusqu'à ce que deux grandes barrettes tombent en face des encoches du cache
(12 h et 6 h, au milieu du méplat). Le pochoir se retire en le poussant par l'arrière, à travers l'ouverture de la lunette.

## Générer et vérifier

Depuis la racine du projet :

```sh
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_pieces.py
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_membres.py
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_ecrans.py
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_verifier.py
blender -b -P cao/MoodiMOS_Robot/robot_rendu.py -- face 34 profil dos tete casque lunette main chaussures chaussure_profil profil_technique
cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_finition.py
```

Attendre la fin des générateurs avant de lancer Blender : les STL sont réécrits
pendant la génération. Les aperçus isolés se produisent avec `ROBOT_DETAIL_ONLY=main`
ou `ROBOT_DETAIL_ONLY=chaussures` ; ils ajoutent `_detail` au nom de la vue. `robot_membres.py --partie bras` ou `--partie chaussures` permet
de régénérer seulement une famille. Pour un aperçu rapide, utiliser
`ROBOT_RESOLUTION=60 ROBOT_SAMPLES=24` avant la commande Blender.

| Fichier | Rôle |
|---|---|
| `robot_style.py` | Dimensions de design communes aux générateurs. |
| `robot_pieces.py` | Tête, torse, cadre noir, coordonnées `stl/cotes.json`. |
| `robot_organique.py` | Formes organiques propres à ce robot. Lit les interfaces dans le torse source. |
| `robot_membres.py` | Bandes d'articulation et orientation pour l'impression. |
| `robot_ecrans.py` | Textures d'écrans de présentation. |
| `robot_socle.py` | Socle rond et gabarits de perçage des aimants des baskets. |
| `robot_pochoirs.py` | Pochoir des index et cache du flanc pour peindre la lunette. |
| `robot_rendu.py` | Scène Blender, matériaux et caméras ; embarque les textures dans le `.blend`. |
| `robot_finition.py` | Halo des couleurs lumineuses et planche face + trois quarts. |
| `robot_verifier.py` | Fermeture, volume, nombre de volumes et pose sur plateau des STL imprimables. |

Sorties : `stl/` pour l'assemblage, `stl_impression/` pour les pièces posées,
`rendus/` pour les vues, `verification_maillages.json` pour les contrôles.
Les STL d'impression (`stl_impression/`) sont **versionnés** : ce sont les fichiers à imprimer.
Les pièces en place (`stl/`) et la scène Blender sont régénérables et ignorées par Git.

## Dans Fusion

**Robot complet en pièces Fusion : `MoodiMOS_Robot`** (recommandé pour comprendre l'assemblage).
Utilitaires > Compléments > Scripts et compléments > « + » > dossier `MoodiMOS_Robot` > Exécuter.

- **Torse** : construit par le script du torse principal avec les réglages, plus les liserés des flancs, leurs joncs et le
  cadre d'écran. Les composants électroniques sont en place.
- **Tête** : construite en vraies fonctions Fusion. Elle comprend la lunette et sa feuillure, la plaque à goutte et la
  rainure-guide de sa bélière peinte, le dôme (coque, colonnes, puits de vis, logement de LiPo, médaillon) et ses casques. Les casques sont gravés
  droits, tournés de 22°, puis joints au dôme : une seule pièce imprimée, casques peints en noir. À l'impression
  (face arrière, celle du médaillon, sur le plateau), aucun support dans le dôme ; supports arborescents touchant le
  plateau sous le dessous de chaque casque seulement.
- **Membres sculptés** : importés en maillages depuis `stl/`.
- **Rangement** : une pièce par composant, en trois groupes (Tête, Torse, Membres), avec les couleurs de `robot_rendu.py`.
- **Contrôle** : la fenêtre de fin compare le volume de chaque pièce Fusion à celui de `robot_pieces.py` (écart toléré : 1,5 %).
- **Lecture** : le script lit `fusion_reference.json` (cotes, contours de la bélière peinte, volumes attendus), écrit par
  `robot_pieces.py`. Il faut donc relancer `robot_pieces.py` après tout changement de cote.

**Import simple des maillages : `MoodiMOS_Assemblage`** (toutes les pièces telles qu'imprimées, sans fonctions Fusion).

Utilitaires > Compléments > Scripts et compléments > « + » > dossier `MoodiMOS_Assemblage`
> Exécuter. Le script importe les pièces en place de `stl/`, telles qu'on les imprime et que Blender les rend.

- Il range une pièce par composant, dans quatre groupes : Tête, Torse, Membres et « Rendu (non imprimé) ».
  Les noms sont accordés (« Épaule droite »).
- Il reprend les couleurs de `robot_rendu.py`, lues dans ce fichier et converties en sRGB.
- On l'utilise ensuite comme l'assemblage principal : atelier Animation › Éclatement automatique, puis Dessin.
- Les pièces sont des maillages, à cause des formes sculptées. On modifie les cotes dans les scripts Python,
  on régénère, puis on relance le script.
- Pour usiner une pièce simple dans Fusion : clic droit sur le maillage > Convertir le maillage.
- `stl/` doit d'abord être généré (`robot_pieces.py`, `robot_membres.py`). L'import des 62 maillages, environ 220 Mo,
  prend un moment.

## Limites de la vérification

Une pièce fermée n'est pas une validation du montage électronique, des collisions,
des jeux réels ou des supports d'impression. Les chaussures et les mains sont des
formes de design à essayer sur prototype. Le cadre noir est une nouvelle pièce de
0,8 mm, à coller ou à reproduire en peinture si une pièce aussi mince est inadaptée.

Les dimensions du corps et les supports de caméra/radar sont conservés. Les cotes
marquées « à mesurer » dans la CAO source restent à vérifier sur les composants.
Les joncs lumineux et leur éclairage restent à valider.
Les textures servent au rendu. Le firmware a son équivalent programmé, l'édition complète
(`sh firmware/flash.sh tete`, voir [extras/LISEZMOI.md](../../extras/LISEZMOI.md)).
La ressemblance avec l'image est améliorée, sans promettre une reproduction exacte
avec les dimensions physiques actuelles des écrans et du boîtier.

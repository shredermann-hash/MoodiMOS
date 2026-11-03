# Réglages d'impression MoodiMOS

Imprimante : Creality Ender 3 S1 Pro, buse 0,4 mm, plaque PEI · Trancheur : UltiMaker Cura.
Profil Cura enregistré : **« MoodiMOS PETG 0,16 »** (partir de *Dynamic Quality 0,16 mm*).
Validé le 30/09/2026 sur la basket droite : 8 h 03, 43 g.

## Réglages communs (profil)

| Section Cura | Paramètre | Valeur |
|---|---|---|
| Qualité | Hauteur de couche | 0,16 mm |
| Parois | Épaisseur de la paroi | 1,6 mm (4 lignes) |
| Parois | Expansion horizontale | 0,0 mm |
| Parois | Alignement de la jointure en Z | Coin le plus pointu |
| Haut / bas | Épaisseur du dessus/dessous | 1,0 mm (arrondi à 7 couches de 0,16 mm) |
| Remplissage | Densité / motif | 40 % Gyroïde (voir tableau par pièce) |
| Matériau | Température d'impression | 235 °C (PETG) |
| Matériau | Température du plateau | 75 °C |
| Vitesse | Vitesse d'impression | 50 mm/s |
| Vitesse | Paroi externe | 25 mm/s |
| Vitesse | Couche initiale | 20 mm/s |
| Déplacement | Rétraction | 0,8 mm à 40 mm/s |
| Déplacement | Mode de détours | Pas dans la couche extérieure |
| Déplacement | Décalage en Z lors d'une rétraction | activé |
| Refroidissement | Vitesse du ventilateur | 40 % |
| Refroidissement | Vitesse des ventilateurs initiale | 0 % |
| Supports | Générer / structure | selon la pièce ; Arborescent |
| Supports | Positionnement | Touche le plateau |
| Supports | Angle de porte-à-faux | 60° |
| Supports | Distance Z des supports | 0,32 mm (2 couches, le PETG soude aux supports) |
| Adhérence | Type / largeur | Bordure, 4 mm |

Plateau : voile de colle en bâton (anti-adhérent PETG sur PEI). PETG resté à l'air :
séchage 4 h à 65 °C. Surveiller la première couche.

## Variante PLA+ (Anycubic)

Validée le 13/10/2026 : cerclage et plaque de la tête, puis dôme de la tête (5 h 55, 12,1 m, environ 36 g).
Dupliquer le profil PETG en **« MoodiMOS PLA+ 0,16 »** et ne changer
que ce qui suit ; le reste (couche, parois, dessus/dessous, remplissage, supports arborescents, bordure) et le
tableau par pièce ci-dessous ne changent pas.

| Section Cura | Paramètre | Valeur PLA+ |
|---|---|---|
| Matériau | Température d'impression | 215 °C (à ajuster dans la plage indiquée sur la bobine) |
| Matériau | Température du plateau | 60 °C (couche initiale : 65 °C) |
| Refroidissement | Vitesse du ventilateur | 100 %, à partir de la couche 3 |
| Refroidissement | Vitesse des ventilateurs initiale | 0 % |
| Vitesse | Vitesse d'impression | 60 mm/s |
| Vitesse | Paroi externe | 30 mm/s |
| Vitesse | Couche initiale | 15 mm/s |
| Vitesse | Vitesse de déplacement de la couche initiale | 50 mm/s (à 100, la buse accrochait les tours posés) |
| Qualité | Largeur de ligne de la couche initiale | 120 % |
| Déplacement | Rétraction | 0,8 mm à 40 mm/s (extrudeur direct) |
| Déplacement | Décalage en Z lors d'une rétraction | désactivé |
| Supports | Distance Z des supports | 0,16 mm (1 couche) |

Plateau : PEI propre, dégraissé à l'alcool isopropylique, **sans colle**. PLA+ resté à l'air : séchage
4 à 6 h à 50 °C. Au premier essai, une tour de température trouve la bonne valeur de la bobine
(en général 210 à 220 °C).

Points d'attention :
- Le PLA+ est plus raide que le PETG : trous, aimants et tenons sortent souvent un peu serrés. Imprimer
  d'abord le cerclage de la tête et essayer vitre, aimants et vis ; si ça force, *Expansion horizontale des
  trous* = 0,1 mm pour tout le robot.
- Édition complète : les ergots du logement de LiPo clipsent par la souplesse de la paroi. S'ils résistent trop,
  baisser `lipoLipIn` de 1,2 à 0,8 mm dans `cao/MoodiMOS_Robot/robot_pieces.py`.
- Le PLA ramollit vers 55-60 °C : pas de robot au soleil derrière une vitre ni dans une voiture.
- Plateau : laver au liquide vaisselle et à l'eau chaude (l'alcool étale le gras des doigts), ne plus toucher
  le centre. Nettoyer la buse à chaud après un raté : une boule restée collée arrache les tours suivants.
- Édition complète, dôme `tete_dos.stl` : face du médaillon sur le plateau (tel qu'exporté), **jupe** et non bordure
  (la bordure laisserait une lèvre sur le bord arrière), supports arborescents touchant le plateau sous les
  casques seulement ; un **bloqueur de support** dans chacun des 4 puits de vis (sinon, pousser le bouchon avec
  une tige de 2 mm par le trou de vis).
- Torse (deux éditions) : **deux pièces depuis le 16/10/2026**. La façade (paroi avant, 2,5 mm) s'imprime dos sur le
  plateau : la poche de vitre s'ouvre vers le haut et l'épaulement se construit sans support (la couronne détachable
  essayée le 14/10 restait soudée). Elle se visse au corps par 4 vis M2 × 8 à tête bombée (ISO 7380), aux places des
  anciennes vis décoratives. Le corps s'imprime joint en bas, **supports désactivés dans Cura** ; son STL contient
  trois ailettes détachables sous le collier du cou (couché, 6 mm de porte-à-faux), à retirer à la pince. Les
  bossages des bras et des jambes sont en
  goutte à 45° : plus d'arbres dans la coque. Les trous des tenons restent ronds : le tenon y pivote, tiré par son
  aimant, et son frottement tient le membre dans la pose choisie. Si leur plafond s'affaisse un peu à l'impression,
  repasser un foret de 9,5 à la main. Les autres plafonds (cavité du pogo 8,5, BME280 6,2, USB-C,
  rainures, ouïes) se pontent avec les paramètres de pont activés.
- Sonic Pad : après avoir copié un fichier (clé ou mémoire locale), attendre 2 à 3 min avant de lancer. Il réécrit
  le fichier pour l'annulation d'objets ; lancé trop tôt, Klipper lit deux versions mélangées et s'arrête sur
  « Unable to parse move ».

## Par pièce

Édition complète, celle de la notice : tous ces fichiers sont dans `cao/MoodiMOS_Robot/stl_impression/`, sans sous-dossier `fusion/`, et le bras s'y recouche dans le plan de son coude (bordure 8 mm, 4 parois, 40 %).

| Pièce | Fichier | Supports | Remplissage |
|---|---|---|---|
| Baskets | `basket_droit.stl`, `basket_gauche.stl` (miroirs : pastille côté extérieur) | Arborescents, touchant le plateau (sous le bout relevé) | 40 % |
| Gants + bracelets | `gant_bracelet_droit.stl`, `gant_bracelet_gauche.stl` (bracelet en bas, doigts en haut) | Arborescents, touchant le plateau ; **bordure 6-8 mm** | 40 % |
| Bras | `bras_droit.stl`, `bras_gauche.stl` (couché sur la face intérieure, contre le flanc ; face extérieure en haut) | Arborescents, touchant le plateau (sous l'avant-bras, côté caché) ; **bordure 8-10 mm** car le contact initial est très réduit | 40 % |
| Épaules | `epaule_droit.stl`, `epaule_gauche.stl` (tenon en bas, logement d'aimant sur le plateau) | aucun ; **bordure 6 mm** | 100 % (petite pièce, tenon Ø 9 solide) |
| Jambes | `jambe_droit.stl`, `jambe_gauche.stl` (debout, sur le bout caché dans la basket, tenon en haut ; 65 mm de haut) | aucun ; **bordure 6-8 mm** (pièce haute sur Ø 14) | 40 % |
| Corps du torse | `fusion/torse_coque.stl` (joint avec la façade sur le plateau ; ailettes détachables sous le collier) | **aucun** : désactiver les supports dans Cura ; bossages des bras et des jambes en goutte, trous des tenons ronds | 25 % |
| Façade du torse | `fusion/torse_facade.stl` (dos sur le plateau, poche de vitre en haut) | aucun | 30 % |
| Dos du torse | `fusion/torse_dos.stl` (face extérieure sur le plateau) | aucun | 20-30 % |
| Boîte du haut-parleur | `fusion/torse_boite_hp.stl` (fond fermé en bas) | aucun | 20-30 % |
| Tête : cerclage | `tete_cerclage.stl` (face arrière sur le plateau, gradins et poche de vitre en haut) | aucun | 30 % |
| Tête : plaque à goutte | `tete_plaque.stl` (à plat ; logement du connecteur pogo, soudures, fils = trous horizontaux) | aucun | 30 % |
| Tête : dos bombé | `tete_dos.stl` (ouverture sur le plateau, bombé en haut) | touchant le plateau : sous le fond (Ø 74) et le berceau de LiPo, à l'intérieur, invisibles | 30 % |
| Jauge du pogo | `jauge_pogo.stl` (peau de 2 mm sur le plateau, cavités en haut ; 1 à 3 points = fente de 4,5 / 4,6 / 4,7) | aucun | 20 % |
| Socle | `cao/MoodiMOS_Robot/stl_impression/socle.stl` (dessous sur le plateau ; disque Ø 150 × 20, plein) | aucun | 10-15 % gyroïde (pièce statique, lourde = stable) |
| Gabarits de perçage | `cao/MoodiMOS_Robot/stl_impression/gabarit_aimant_basket_droit.stl` / `_gauche.stl` (plaque sur le plateau, rebord en haut ; à retourner sur la basket posée à l'envers, puis forer Ø 10 sur 5 mm à l'avant-trou) | aucun | 20 % |
| Pochoir des index de la lunette | `cao/MoodiMOS_Robot/stl_impression/pochoir_index_lunette.stl` (disque Ø 83,6 de 0,8 : face peinte sur le plateau, lèvre de centrage en haut) | aucun | 100 % |
| Cache du flanc de la lunette | `cao/MoodiMOS_Robot/stl_impression/cache_flanc_lunette.stl` (bague de 1,2 : côté large sur le plateau, encoches en haut) | aucun | 100 % |

Orientation : telle que fournie (baskets sur la semelle, gants sur le bracelet, pièces de la tête déjà orientées, 0 / 0 / 0).
Les baskets et gants exportés dans ce dossier sont centrés sur le plateau avec leur base à Z = 0.
Bras, épaules et jambes orientés depuis `../stl/` : `cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Organique/orienter_impression.py`
(après `organique.py` ; bouche aussi un petit défaut de maillage du bras).
Export automatique Fusion : lancer `MoodiMOS_Torse.py` ou `MoodiMOS_Tete.py` dans Fusion pour écrire
les pièces déjà orientées dans `stl_impression/fusion/` (`torse_*.stl`, `tete_*.stl`, qualité de maillage très haute).
Le sous-dossier `fusion/` n'est présent qu'après cet export ; les pièces du torse du tableau ne sont
donc pas encore disponibles dans ce dossier tant que l'export Fusion n'a pas été lancé.
STL de la tête générés sans Fusion : `cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Tete/tete_stl.py`
(mêmes paramètres que `MoodiMOS_Tete.py` ; aperçu : `cao/MoodiMOS_Tete/apercu_stl.html` via le serveur local).
Toujours découper puis vérifier l'onglet **APERÇU**, surtout les premières couches et les supports
qui doivent bien atteindre le dessous des bras et des gants ; zones rouges = surplombs sans support.

## Emboîtements

Trou de jambe de la basket Ø 14,5 pour jambe Ø 14 ; aimants Ø 6,2 ; tenons Ø 9 dans Ø 9,4.
Bras sur épaule : coupelle du bras sur la boule Ø 17 avec 0,15 de jeu, collée.
Tête : poche de vitre Ø 75,6 (vitre Ø 75, jeu 0,3) sur 1,0 de profondeur (vitre mesurée à peine 1 mm),
épaulement Ø 72,4 (dalle LCD Ø 71,8), dégagements pour les deux USB-C de la carte (elles débordent du Ø 72),
lamage arrière Ø 78 derrière l'épaulement (USB-C, lecteur microSD et sa carte, nappe pliée),
vis M2 Ø 2,3 et chambrages Ø 4,2, connecteur pogo monté par l'intérieur sans aimants séparés : fente du nez 21,4 × 4,6
dans une peau de 2, oreilles en appui derrière (largeur à confirmer avec `jauge_pogo.stl` : le nez passe sans forcer et
sans jeu, face au ras de la peau) : imprimer d'abord le cerclage
et essayer la carte avant le reste, **sans forcer** (une USB-C s'arrache facilement).
Si une pièce force à l'essai : *Expansion horizontale des trous* = 0,1 mm dans le profil
(vaut alors pour tout le robot).

## Couleurs

Ce qui suit vaut pour l'édition neutre ; l'édition complète est décrite dans la notice. Choix retenu : un filament clair puis peinture (gris argent non métallisé, ivoire, bleu roi,
blanc). Variante multicolore des baskets : changements de filament à **2 mm** (liseré bleu),
**9 mm** (semelle blanche → tige bleue) et **32,5 mm** (col blanc) via *Extensions →
Post-traitement → Modifier le G-code → Filament Change* ; bout, lacets et pastille peints.

# Vérification du robot — 4 octobre 2026

Les quatre captures fournies sont enregistrées sans retouche dans `references/`.
Les anciennes vues face et trois quarts sont conservées dans `rendus/avant_2026-10-04/`.

## Encombrement des composants

Le raccourcissement du corps a été annulé à la demande de l'auteur. Le torse reste à
**82 × 120,5 × 48 mm**. La caméra, son berceau et le cadre du radar sont revenus à
l'implantation d'origine. Le radar n'est pas déplacé derrière l'écran.
Les dimensions des composants et des vitres, des entretoises et des interfaces
électroniques sont relues dans les fichiers de la CAO source.

L'enveloppe du torse devient plus arrondie (rayon 8 au lieu de 6). Les fixations des
jambes se déplacent de ±23 à ±27,5 mm pour l'écartement ; diamètre et profondeur de
leurs tenons et logements restent identiques. L'implantation électronique garde ses
paramètres source. Les pièces source de l'autre édition ne sont pas modifiées.

## Écarts visuels corrigés

| Observation sur l'ancien rendu | Correction |
|---|---|
| Liseré de lunette gris au lieu d'orange | Noms de fichiers normalisés pour les accents macOS ; bon matériau lumineux. |
| Orange terne, horizon tranché | Palette et éclairage corrigés, fond cyclorama continu. |
| Mains en forme de moufles | Sphère ovale pleine, doigts sculptés dans le quart inférieur et pouce court accolé. |
| Articulation noire au milieu de la jambe | Bande déplacée à la cheville ; hanche noire conservée. |
| Pieds trop proches et chaussures basses | Jambes écartées vers les chevilles, profil de 73,9 mm, pieds tournés de 12°, semelle noire épaissie et bout orange bombé. |
| Petits lacets et pastille épaisse | Bride orange avec rainures, anneau latéral plus fin, deux ouïes noires. |
| Visage et smiley trop petits | Motifs agrandis dans les surfaces actives réelles des écrans. |
| Façade ivoire peu structurée | Cadre noir mince autour de la vitre, découpé pour la caméra. |

## Contrôles réalisés

- Les 22 STL d'impression sont des maillages fermés, de volume positif, chacun d'une
  seule pièce, avec leur point le plus bas sur le plateau à moins de 0,01 mm.
- La génération corrige quatre petits triangles manquants sur chaque bras lors de
  l'orientation pour l'impression ; les fichiers corrigés passent le contrôle.
- Les paramètres composants du torse restent identiques à ceux de la source, hors
  les deux paramètres de design explicités ci-dessus.
- Les scripts passent la vérification de syntaxe.
- Les nouvelles vues face, trois quarts, mains et chaussures ont été inspectées.

La semelle mesure **73,93 mm sur l'axe de la chaussure**, pour une cible de 73,9 mm
indiquée sur la quatrième capture. Le médaillon latéral a une face plane, avec un
anneau orange complet ; les anciens panneaux en relief ont été retirés.

Le détail machine des contrôles est dans `rendus/verification_maillages.json`.

## Limites et suite de fabrication

La hauteur totale est **315,8 mm**, contre 309,8 mm avant cette révision. Le corps
n'a pas été réduit : les chaussures ajoutent 6 mm au réglage nominal. Une copie exacte de l'illustration
n'est pas obtenue : sa façade ne montre pas la même place pour la caméra ni les marges
physiques des écrans, et ses étiquettes de cotes ne servent pas de plan d'assemblage.

La fermeture des STL ne garantit pas le montage de toutes les cartes ni les jeux
mesurés. Le déplacement des fixations de jambes, la bride et les doigts demandent
un essai sur prototype ; les supports FDM et les passages de câbles sont à vérifier.
Les joncs sont des pièces orange pour le rendu ; leur éclairage réel reste à définir.

## Reprise des baskets et des mains (même jour)

Les baskets et les mains de la révision précédente avaient cassé le rendu. Les causes étaient les suivantes :

- le médaillon latéral était un cylindre plein qui dépassait du flanc ;
- le talon était une boule de 17 mm de rayon ;
- la forme était étirée par trois facteurs d'échelle différents ;
- quatre ellipsoïdes de doigts étaient collés sous la main.

L'ancienne version est gardée dans `rendus/iterations_2026-10-04/robot_organique_avant_reprise.py`.

| Pièce | Reprise |
|---|---|
| Basket | Réécrite en mm réels, sans étirement, d'après les vues 3/4, profil et dos de la fiche. Elle se compose de : <br>• une semelle noire de 5 mm et un intercalaire ivoire jusqu'à 11,5 mm ; <br>• un bout orange bombé cerné d'un trait noir ; <br>• une empeigne ivoire avec deux ouïes noires ; <br>• un panneau arrière noir avec un anneau orange incrusté à fleur ; <br>• un talon orange ; <br>• une bride orange à deux rainures, devant la collerette ; <br>• une collerette rebondie. <br>Conservés : 73,9 × 42 mm, pieds tournés de 12°, jambes à ±27,5. |
| Mains | Retour à la boule sculptée validée : un volume ovale, un pouce en lobe et des doigts repliés fondus, avec des sillons peints en noir. **Bague noire du poignet** (`poignet_gant`) entre le bracelet et la main. |
| Rendu | Couleurs ajoutées pour `talon` et `poignet_gant`. Cyclorama lissé et sans vernis : les reflets nets des panneaux lumineux formaient des taches grises dans le fond. Panneaux lumineux invisibles à la caméra. |

`robot_verifier.py` : les 22 STL d'impression sont OK, et les paramètres des composants ne sont pas modifiés.

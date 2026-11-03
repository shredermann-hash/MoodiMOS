# Câblage du pogo du cou (torse ↔ tête)

Schéma : [`MoodiMOS_Schema_pogo_cou.svg`](MoodiMOS_Schema_pogo_cou.svg). Vue d'ensemble :
[`MoodiMOS_Schema_cablage_torse_tete.svg`](MoodiMOS_Schema_cablage_torse_tete.svg).

## Correspondance des 5 contacts

| Contact | Signal | Côté torse (2×16, ressorts) | Côté tête (12 broches) | Fil côté torse | Fil du câble livré (tête) |
|---|---|---|---|---|---|
| 1 | +5 V | broche 2 (5V) | n° 2 VBus | rouge | **rouge** (2e fil) |
| 2 | T→H | broche 27, TX GPIO43 | n° 10, RXD GPIO44 | violet | **bleu** (10e fil) |
| 3 | H→T | broche 25, RX GPIO44 | n° 9, TXD GPIO43 | bleu clair | **vert** (9e fil) |
| 4 | DOCK | PCF8574 P1 (tirage interne) | relié à GND (n° 5) | vert | **noir** (5e fil) |
| 5 | GND | broche 29 (ou 4, 30) | n° 1 (GND) | noir | **noir** (1er fil) |

### Le câble 12 fils livré avec la tête (ordre des broches 1 à 12)

noir (GND) · rouge (VBus) · vert (D−) · jaune (D+) · noir (GND) · rouge (3V3) · marron (SCL) ·
orange (SDA) · vert (TXD) · bleu (RXD) · violet (NC) · gris (IO0)

**Trois couleurs sont en double. Ne te fie pas à la couleur seule, mais à la position :**
- **2 rouges** : VBus (n° 2, les 5 V) et **3V3** (n° 6). Les confondre met 5 V sur une ligne 3,3 V.
- **2 verts** : D− (n° 3) et TXD (n° 9).
- 2 noirs : les deux GND (n° 1 et 5), sans risque.
- Les 7 fils inutilisés (D−, D+, 3V3, SCL, SDA, NC, IO0) : **isoler chacun** (gaine ou ruban), surtout le rouge du 3V3 et les verts/jaunes de l'USB.
- Avant de souder : multimètre, **un fil à la fois**, continuité entre le fil et la broche annoncée de la carte.

Les contacts 2 et 3 sont **croisés** : le TX d'un côté va au RX de l'autre. Les couleurs côté torse sont une
proposition ; côté tête, ce sont celles du câble livré. Seul l'ordre des contacts compte.

## Matériel (déjà prévu dans la liste d'achats)

- Connecteur magnétique à pogo 5 broches, pas 2,5 mm, paire à ressorts + pastilles, ≥ 1 A par contact
  (Sylen B0H22943X6, mesuré à la règle : 25 mm de long avec ses oreilles, 4 de large, 4 de haut = 2 côté aimants,
  1 d'oreilles et 1 côté soudures ; un aimant intégré à chaque bout de la face).
- Pas d'aimants séparés depuis le 13/10/2026 : les bouts du connecteur tombaient sur leurs logements, et les
  aimants intégrés tiennent la paire plaquée.
- Fil souple de silicone 26 à 28 AWG, 5 couleurs ; gaine thermorétractable ; Kapton ; étain.

## Longueurs (estimations, à couper à la mesure)

- Côté torse, du pogo du collier au connecteur 2×16 : environ **150 mm**, pour passer sans tendre.
- Côté tête, de la plaque au connecteur 12 broches : environ **60 mm**.
- Chaque moitié se monte **par l'intérieur** : son nez (environ 20 × 4 × 2 mm, côté aimants) passe dans une fente de
  21,4 × 4,6 mm percée dans une peau de 2 mm, et sa face arrive au ras. Les oreilles s'appuient derrière cette peau,
  qui encaisse la traction des aimants. Côté tête, on glisse le connecteur, fils soudés,
  par l'ouverture centrale de la plaque, dans un canal de 26,4 mm qui descend jusqu'au méplat. Côté torse, on le
  pousse depuis l'intérieur de la coque dans la cavité sous le collier (Ø 30). Cotes tirées de la jauge
  `cao/MoodiMOS_Organique/stl_impression/jauge_pogo.stl` (13/10/2026 : 4,4 × 20,6 / 25,6 un peu trop juste).

## Ordre de montage

1. **Sens du connecteur d'abord.** Ses aimants intégrés sont polarisés : les deux moitiés ne s'attirent que
   dans un sens. Vérifie qu'à l'envers elles se repoussent, puis marque le sens au feutre sur chaque moitié
   avant de coller (contact 1 en face du contact 1).
2. Souder les fils sur le pogo torse, puis sur les pastilles de la tête, **un contact à la fois**, en
   gardant les couleurs. Isoler à la gaine thermo : les contacts sont à 2,5 mm.
3. Côté tête, relier le contact 4 à GND (n° 5) avec un petit fil.
4. Côté torse, relier le contact 1 à la broche 2, les contacts 2 et 3 aux broches 27 et 25, le contact 5
   à la broche 29, et le contact 4 à P1 du PCF8574.
5. Avant de raccorder la tête : **multimètre**, toute alimentation débranchée.
   - Continuité de chaque contact (1 à 5) entre le torse et la tête.
   - Aucun bip entre deux contacts voisins, ni entre le contact 1 (+5 V) et le contact 5 (GND). Seule
     exception : les contacts 4 et 5, reliés exprès dans la tête.
6. Alimenter le torse **seul**, tête électriquement débranchée : mesurer environ 5 V entre les contacts 1 et 5
   du cou. Débrancher avant de raccorder la tête.
7. Le contact 1 est à 5 V dès que le torse est alimenté : tant que la tête n'est pas en place, éloigner outils
   et trombones du connecteur. La tête reste ensuite sur le cou ; pour une réparation, débrancher le robot
   avant de la retirer.

## Contrôles côté logiciel

- Tête en place : `radar` doit annoncer « tête en place » (DOCK à 0 sur P1) ; il faut le PCF8574 câblé.
- Le torse affiche « Tête : connectée » quand la liaison UART (460 800 bauds) tient.

## Pièges connus

- **Programmer la tête par son propre USB : la garder hors du cou.** Sa puce USB-série partage GPIO43 et GPIO44
  avec le cou. Une fois la tête fermée, elle se programme par le cou (`firmware/flash_tete_cou.sh`).
- La ROM de la tête écrit son journal de démarrage sur GPIO43 : le torse le rejette (contrôle CRC).
- La liaison cou fait 3,3 V : ne jamais relier le contact 1 (+5 V) à un contact de signal.

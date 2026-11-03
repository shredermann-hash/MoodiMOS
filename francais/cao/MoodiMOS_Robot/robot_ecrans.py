# MoodiMOS — écrans pour le rendu (fiche technique)
#
# Dessine les deux écrans de la fiche, au format exact des vitres (le rendu les plaque dessus) :
#   ecrans/visage.png : tête, vitre Ø 75 (image 1024 × 1024 ; zone active Ø 53,28 au centre) : visage orange
#                         lumineux sur fond noir, yeux fermés rieurs, sourcils, joues hachurées, bouche ouverte ;
#   ecrans/torse.png  : torse, vitre 61 × 92,44 (20 px/mm ; zone active 48,96 × 73,44 au centre) : barre d'état
#                         (wifi, Bluetooth, batterie), titre MOODIMOS, smiley cerclé avec flèches, rangée de 4 humeurs.
# Lancer : cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_ecrans.py

import math, os
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "ecrans")
ORANGE = (255, 138, 24)
AMBER = (255, 184, 46)
CORAL = (255, 96, 82)
FONT = "/System/Library/Fonts/Avenir Next Condensed.ttc"


def font(size):
    for idx in (2, 1, 0):          # Demi Bold si présent dans la collection
        try:
            return ImageFont.truetype(FONT, size, index=idx)
        except OSError:
            continue
    return ImageFont.load_default()


def glow(layer, radius, gain=1.6):
    """Ajoute un halo (flou additif) autour des dessins lumineux du calque."""
    blur = layer.filter(ImageFilter.GaussianBlur(radius))
    blur = blur.point(lambda v: min(255, int(v * gain)))
    return ImageChops.add(layer, blur)


def visage():
    S, c, ra = 1024, 512, 1024 * 53.28 / 75 / 2
    img = Image.new("RGB", (S, S), (0, 0, 0))
    lay = Image.new("RGB", (S, S), (0, 0, 0))
    d = ImageDraw.Draw(lay)
    for sx in (-1, 1):
        ex, ey = c + sx * 150, 488
        d.ellipse((ex - 112, ey - 86, ex + 112, ey + 106), fill=(242, 150, 22))            # œil
        d.ellipse((ex - 84, ey - 2, ex + 84, ey + 106), fill=(230, 112, 14))                # dégradé bas
        d.arc((ex - 64, ey - 26, ex + 64, ey + 104), 195, 345, fill=(26, 6, 1), width=30)   # paupière fermée ∩
        d.arc((ex - 64, ey - 190, ex + 64, ey - 80), 215, 325, fill=ORANGE, width=16)       # sourcil
        for k in range(3):                                                                  # joue ///
            bx = ex + sx * 62 + (k - 1) * 22
            d.line((bx - 9, 655, bx + 9, 625), fill=(230, 80, 60), width=9)
    # Bord supérieur incurvé et fond arrondi de la bouche de la référence.
    top = [(c - 105 + 210 * i / 40, 650 + 14 * math.sin(math.pi * i / 40)) for i in range(41)]
    bottom = [(c + 105 * math.cos(math.pi * i / 60), 650 + 110 * math.sin(math.pi * i / 60)) for i in range(61)]
    d.polygon(top + bottom, fill=(245, 58, 16))
    d.chord((c - 70, 670, c + 70, 760), 0, 180, fill=(255, 117, 70))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse((c - ra, c - ra, c + ra, c + ra), fill=255)
    # Agrandir les traits à l'intérieur de la vraie zone active Ø 53,28.
    size = round(S * 1.28)
    lay = lay.resize((size, size), Image.Resampling.LANCZOS)
    off = (size - S) // 2
    lay = lay.crop((off, off, off + S, off + S))
    lay = glow(lay, 12, 0.35)
    img.paste(lay, (0, 0), mask)
    return img


def torse():
    ppm = 20
    W, H = round(61 * ppm), round(92.44 * ppm)
    aw, ah = round(48.96 * ppm), round(73.44 * ppm)
    x0, y0 = (W - aw) // 2, (H - ah) // 2
    img = Image.new("RGB", (W, H), (0, 0, 0))
    lay = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(lay)
    d.rectangle((x0, y0, x0 + aw, y0 + ah), fill=(14, 10, 8))
    cx = x0 + aw // 2
    # barre d'état : wifi (gauche), Bluetooth et batterie (droite)
    wx, wy = x0 + 70, y0 + 95
    for k, r in enumerate((44, 30, 16)):
        d.arc((wx - r, wy - r, wx + r, wy + r), 225, 315, fill=AMBER, width=8)
    d.ellipse((wx - 6, wy - 6, wx + 6, wy + 6), fill=AMBER)
    bx = x0 + aw - 175
    d.line((bx, y0 + 50, bx, y0 + 112), fill=(70, 140, 255), width=6)
    d.line((bx - 16, y0 + 66, bx + 16, y0 + 96), fill=(70, 140, 255), width=6)
    d.line((bx - 16, y0 + 96, bx + 16, y0 + 66), fill=(70, 140, 255), width=6)
    d.line((bx, y0 + 50, bx + 16, y0 + 66), fill=(70, 140, 255), width=6)
    d.line((bx, y0 + 112, bx + 16, y0 + 96), fill=(70, 140, 255), width=6)
    gx = x0 + aw - 130
    d.rounded_rectangle((gx, y0 + 62, gx + 84, y0 + 104), 6, outline=AMBER, width=6)
    d.rectangle((gx + 84, y0 + 74, gx + 92, y0 + 92), fill=AMBER)
    d.rectangle((gx + 12, y0 + 72, gx + 72, y0 + 94), fill=AMBER)
    # titre
    f = font(118)
    t = "MOODIMOS"
    tw = d.textlength(t, font=f)
    d.text((cx - tw / 2, y0 + 190), t, font=f, fill=ORANGE)
    # smiley cerclé, repères et flèches
    cy, r = y0 + ah * 0.53, 290
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=AMBER, width=16)
    for a in range(4):
        ang = math.radians(90 * a + 45)
        p1 = (cx + (r + 26) * math.cos(ang), cy + (r + 26) * math.sin(ang))
        p2 = (cx + (r + 52) * math.cos(ang), cy + (r + 52) * math.sin(ang))
        d.line((p1, p2), fill=(150, 80, 20), width=5)
    for sx in (-1, 1):
        d.ellipse((cx + sx * 80 - 22, cy - 85, cx + sx * 80 + 22, cy - 35), fill=AMBER)
    d.arc((cx - 120, cy - 110, cx + 120, cy + 120), 25, 155, fill=AMBER, width=16)
    for sx in (-1, 1):
        ax = cx + sx * (r + 95)
        d.line(((ax, cy - 50), (ax + sx * 34, cy), (ax, cy + 50)), fill=ORANGE, width=14, joint="curve")
    # rangée de 4 humeurs (la 3e choisie, pleine)
    ry, rr = y0 + ah - 110, 83
    for k, mood in enumerate(("content", "triste", "rieur", "neutre")):
        mx = x0 + aw * (k + 0.5) / 4
        box = (mx - rr, ry - rr, mx + rr, ry + rr)
        if mood == "rieur":
            d.ellipse(box, fill=AMBER)
            ink = (60, 25, 4)
        else:
            d.ellipse(box, outline=ORANGE, width=8)
            ink = ORANGE
        for sx in (-1, 1):
            d.ellipse((mx + sx * 22 - 8, ry - 26, mx + sx * 22 + 8, ry - 8), fill=ink)
        if mood == "triste":
            d.arc((mx - 34, ry + 12, mx + 34, ry + 56), 200, 340, fill=ink, width=8)
        elif mood == "neutre":
            d.line((mx - 28, ry + 26, mx + 28, ry + 26), fill=ink, width=8)
        else:
            d.arc((mx - 36, ry - 20, mx + 36, ry + 40), 20, 160, fill=ink, width=8)
    lay = glow(lay, 10, 0.45)
    img.paste(lay, (0, 0))
    return img


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    visage().save(os.path.join(OUT, "visage.png"))
    torse().save(os.path.join(OUT, "torse.png"))
    print("écrans écrits dans", os.path.relpath(OUT))

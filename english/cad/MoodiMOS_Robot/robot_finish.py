# MoodiMOS – Blender render finishes and overview sheet ("STYLE" spec sheet)
# 
# Add a bloom effect to bright areas (screens, branches, index), then assemble the front view and 3/4 view side-by-side with the title: `renders/planche.png`.
# 
# Run after `robot_render.py`:
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_finish.py
# ```

import os
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "renders")
ORANGE = (240, 96, 24)


def bloom(img, threshold=220, radius=8, gain=0.55):
    'Halo: Blurry, oversaturated bright pixels (glowing orange, screens) are overlaid on the image.'
    rgb = img.convert("RGB")
    pixels = np.asarray(rgb)
    mask = (pixels[:, :, 0] > threshold) & (pixels[:, :, 1] > 75) & (pixels[:, :, 2] < 150)
    lum = Image.fromarray(mask.astype(np.uint8) * 255)
    bright = Image.composite(rgb, Image.new("RGB", rgb.size), lum)
    halo = bright.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: int(v * gain))
    halo2 = bright.filter(ImageFilter.GaussianBlur(radius * 3)).point(lambda v: int(v * gain * 0.6))
    return ImageChops.add(ImageChops.add(rgb, halo), halo2)


def font(size, bold=True):
    path = "/System/Library/Fonts/Avenir Next.ttc"
    for idx in ((8, 2) if bold else (0,)):
        try:
            return ImageFont.truetype(path, size, index=idx)
        except OSError:
            continue
    return ImageFont.load_default()


def main():
    views = {}
    for v in ("face", "34", "profil", "back", "head", "headphone", "bezel", "main", "chaussures", "chaussure_profil", "profil_technique"):
        p = os.path.join(R, v + ".png")
        if os.path.exists(p):
            views[v] = bloom(Image.open(p))
            views[v].save(os.path.join(R, v + "_fini.png"))
    if "face" in views and "34" in views:
        a, b = views["face"], views["34"]
        w, h = a.size
        crop = lambda im: im.crop((int(w * 0.12), int(h * 0.07), int(w * 0.88), int(h * 0.92)))
        a, b = crop(a), crop(b)
        cw, ch = a.size
        top = 190
        sheet = Image.new("RGB", (cw * 2, ch + top), (22, 23, 27))
        sheet.paste(a, (0, top))
        sheet.paste(b, (cw, top))
        d = ImageDraw.Draw(sheet)
        d.text((40, 20), "MoodiMOS", font=font(120), fill=ORANGE)
        d.text((46, 150), 'OVERALL VIEW', font=font(34), fill=(230, 230, 230))
        for x, label in ((cw // 2, "VUE DE FACE"), (cw + cw // 2, "VUE 3/4")):
            f = font(32)
            d.text((x - d.textlength(label, font=f) / 2, top + ch - 60), label, font=f, fill=(225, 225, 225))
        sheet.save(os.path.join(R, "planche.png"))
        print("planche :", os.path.relpath(os.path.join(R, "planche.png")))


if __name__ == "__main__":
    main()

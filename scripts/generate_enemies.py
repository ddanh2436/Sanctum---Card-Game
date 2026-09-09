"""
Generate placeholder enemy sprites (assets/characters/*.png).

Run from the project root:   python scripts/generate_enemies.py

These stand in until real monster art exists. They are deliberately simple
silhouettes rather than detailed illustration, but they are lit, layered and
sized like real sprites so they sit beside the hand-painted card art without
looking like debug shapes.
"""

import math
import os
import random
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "characters")


def glow(size, centre, radius, colour):
    """A soft radial bloom, drawn oversized then blurred."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse([centre[0] - radius, centre[1] - radius,
               centre[0] + radius, centre[1] + radius], fill=colour)
    return layer.filter(ImageFilter.GaussianBlur(radius * 0.55))


def hooded_figure(w, h, robe, trim, eye, horns=False, seed=0):
    """A cowled silhouette: robe, hood opening, eyes, and optional horns."""
    rng = random.Random(seed)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx = w // 2
    shoulder_y = int(h * 0.34)
    hem_y = int(h * 0.94)

    # Robe: a ragged bell shape, hem cut into tatters
    hem_points = []
    steps = 11
    for i in range(steps + 1):
        t = i / steps
        x = int(w * 0.10 + (w * 0.80) * t)
        jag = rng.randint(0, int(h * 0.06))
        hem_points.append((x, hem_y - jag if i % 2 else hem_y))

    body = [(cx, int(h * 0.10)),
            (int(w * 0.70), shoulder_y),
            (int(w * 0.88), int(h * 0.62))] + hem_points[::-1] + \
           [(int(w * 0.12), int(h * 0.62)),
            (int(w * 0.30), shoulder_y)]
    d.polygon(body, fill=robe)

    # Shoulder mantle, a slightly lighter layer for depth
    d.polygon([(cx, int(h * 0.14)),
               (int(w * 0.78), int(h * 0.40)),
               (int(w * 0.62), int(h * 0.50)),
               (cx, int(h * 0.44)),
               (int(w * 0.38), int(h * 0.50)),
               (int(w * 0.22), int(h * 0.40))], fill=trim)

    # Hood opening: pure void where the face should be
    hood_top = int(h * 0.16)
    hood_bot = int(h * 0.34)
    d.ellipse([cx - int(w * 0.16), hood_top, cx + int(w * 0.16), hood_bot],
              fill=(6, 4, 10, 255))

    if horns:
        for side in (-1, 1):
            base_x = cx + side * int(w * 0.15)
            d.polygon([(base_x, hood_top + int(h * 0.02)),
                       (base_x + side * int(w * 0.19), int(h * 0.02)),
                       (base_x + side * int(w * 0.07), hood_top + int(h * 0.05))],
                      fill=trim)

    img = img.filter(ImageFilter.SMOOTH)

    # Eyes last, over a bloom, so they read as the focal point
    eye_y = (hood_top + hood_bot) // 2
    for side in (-1, 1):
        ex = cx + side * int(w * 0.07)
        img.alpha_composite(glow((w, h), (ex, eye_y), int(w * 0.09), eye[:3] + (150,)))
        d2 = ImageDraw.Draw(img)
        r = max(2, int(w * 0.022))
        d2.ellipse([ex - r, eye_y - r, ex + r, eye_y + r], fill=eye)

    return img


def main():
    os.makedirs(OUT, exist_ok=True)

    # Boss: larger, horned, purple runes and a heavy bloom
    boss = hooded_figure(320, 420,
                         robe=(38, 18, 54, 245), trim=(74, 34, 96, 250),
                         eye=(255, 58, 48, 255), horns=True, seed=7)
    halo = glow((320, 420), (160, 250), 108, (150, 52, 210, 60))
    boss = Image.alpha_composite(halo, boss)
    boss.save(os.path.join(OUT, "void_apostle.png"))

    # Undead thrall: colder, bonier, pale green eyes
    minion = hooded_figure(220, 300,
                           robe=(30, 36, 44, 240), trim=(56, 68, 78, 245),
                           eye=(150, 255, 190, 255), seed=13)
    minion.save(os.path.join(OUT, "shadow_minion.png"))

    # Cultist: crimson robes, orange zeal
    fiend = hooded_figure(230, 310,
                          robe=(56, 18, 24, 242), trim=(104, 34, 34, 248),
                          eye=(255, 150, 60, 255), seed=29)
    fiend.save(os.path.join(OUT, "cultist_fiend.png"))

    for name in ("void_apostle", "shadow_minion", "cultist_fiend"):
        path = os.path.join(OUT, name + ".png")
        print("  wrote assets/characters/{}.png  ({} bytes)".format(name, os.path.getsize(path)))


if __name__ == "__main__":
    main()

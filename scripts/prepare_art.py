"""
Cut the hand-supplied key art into the UI pieces the game loads.

Run from the project root:   python scripts/prepare_art.py

Sources (drop your own paintings here, 4:3 works best):
    assets/characters/Paladin.png
    assets/characters/Thanh_Nu.png
    assets/characters/Vanguard.png

Outputs
    assets/ui/menu_bg.png          1280x720  darkened key art for the main menu
    assets/ui/victory_bg.png       1280x720  brighter variant for the win screen
    assets/portraits/paladin.png    256x256  Sanctum commander portrait
    assets/portraits/saintess.png   256x256  used by Saintess-role framing
    assets/portraits/vanguard.png   256x256  the Royal Vanguard

Card illustrations are NOT handled here - drop those into assets/cards/ and run
scripts/import_card_art.py instead.
"""

import os
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHARACTERS = os.path.join(ROOT, "assets", "characters")

PORTRAIT = (256, 256)
SCREEN = (1280, 720)

# Which slice of which painting becomes each portrait.
PORTRAITS = {
    # output name   source file        crop box (left, top, right, bottom)
    "paladin":     ("Paladin.png",  (520, 60, 820, 360)),
    "saintess":    ("Thanh_Nu.png", (600, 170, 920, 490)),
    "vanguard":    ("Vanguard.png", (330, 210, 630, 510)),
}


def crop_to(img, box, size):
    return img.crop(box).resize(size, Image.LANCZOS)


def darken(img, factor, top_alpha, bottom_alpha):
    """Dim the image and lay a vertical gradient over it so text stays readable."""
    out = ImageEnhance.Brightness(img).enhance(factor)
    out = ImageEnhance.Color(out).enhance(0.92)

    gradient = Image.new("L", (1, out.height))
    for y in range(out.height):
        t = y / max(1, out.height - 1)
        gradient.putpixel((0, y), int(top_alpha + (bottom_alpha - top_alpha) * t))
    gradient = gradient.resize(out.size)

    shade = Image.new("RGB", out.size, (6, 5, 12))
    return Image.composite(shade, out, gradient)


def vignette(img, strength=170):
    """Darken the edges so the centre of the composition carries the eye."""
    mask = Image.new("L", img.size, 0)
    draw = ImageDraw.Draw(mask)
    inset_x = img.width // 7
    inset_y = img.height // 7
    draw.ellipse([-inset_x, -inset_y, img.width + inset_x, img.height + inset_y], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(img.width // 10))

    dark = Image.new("RGB", img.size, (4, 3, 9))
    return Image.composite(img, Image.blend(img, dark, strength / 255.0), mask)


def clamp_box(box, size):
    """Keep a crop inside the image, whatever resolution the source turned out to be."""
    left, top, right, bottom = box
    w, h = size
    left, top = max(0, min(left, w - 1)), max(0, min(top, h - 1))
    right, bottom = max(left + 1, min(right, w)), max(top + 1, min(bottom, h))
    return (left, top, right, bottom)


def main():
    for sub in ("ui", "portraits"):
        os.makedirs(os.path.join(ROOT, "assets", sub), exist_ok=True)

    made, skipped = [], []

    # ---- portraits ----
    for name, (source, box) in PORTRAITS.items():
        path = os.path.join(CHARACTERS, source)
        if not os.path.exists(path):
            skipped.append((name, source))
            continue
        with Image.open(path) as img:
            rgb = img.convert("RGB")
            out = crop_to(rgb, clamp_box(box, rgb.size), PORTRAIT)
        target = os.path.join(ROOT, "assets", "portraits", name + ".png")
        out.save(target)
        made.append("portraits/" + name + ".png")

    # ---- menu and victory backdrops, taken from the paladin key art ----
    paladin_path = os.path.join(CHARACTERS, "Paladin.png")
    if os.path.exists(paladin_path):
        with Image.open(paladin_path) as img:
            paladin = img.convert("RGB")
            pw, ph = paladin.size
            box = clamp_box((0, int(ph * 0.06), pw, int(ph * 0.06) + int(pw * 9 / 16)),
                            paladin.size)

            menu = vignette(darken(crop_to(paladin, box, SCREEN), 0.62, 110, 205))
            menu.save(os.path.join(ROOT, "assets", "ui", "menu_bg.png"))
            made.append("ui/menu_bg.png")

            victory = vignette(darken(crop_to(paladin, box, SCREEN), 0.85, 60, 150),
                               strength=140)
            victory.save(os.path.join(ROOT, "assets", "ui", "victory_bg.png"))
            made.append("ui/victory_bg.png")
    else:
        skipped.append(("menu_bg / victory_bg", "Paladin.png"))

    print("Prepared:")
    for name in made:
        path = os.path.join(ROOT, "assets", name.replace("/", os.sep))
        print("  {:<32} {:>7} bytes".format(name, os.path.getsize(path)))
    if skipped:
        print("\nSkipped - source painting not found:")
        for name, source in skipped:
            print("  {:<32} needs assets/characters/{}".format(name, source))


if __name__ == "__main__":
    main()

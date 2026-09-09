"""
Build every card illustration the catalogue references.

Run from the project root:   python scripts/build_card_art.py

Sanctum cards are cropped from the two supplied key-art pieces
(assets/characters/Paladin.png and Thanh_Nu.png). Eclipse cards have no
photographic source, so they are drawn procedurally in a matching dark-gothic
register - flat, lit shapes rather than detail that would look muddy at
120 px wide.
"""

import math
import os
import random
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARDS = os.path.join(ROOT, "assets", "cards")
FRAMES = os.path.join(ROOT, "assets", "frames")
PORTRAITS = os.path.join(ROOT, "assets", "portraits")

PALADIN = os.path.join(ROOT, "assets", "characters", "Paladin.png")
SAINTESS = os.path.join(ROOT, "assets", "characters", "Thanh_Nu.png")
SIZE = (420, 320)   # GDD 3.1 artwork window

# Which slice of which painting each Sanctum card gets.
SANCTUM_CROPS = {
    # id                     source     (left, top, right, bottom)
    "aurelius":            ("p", (150, 40, 1150, 790)),
    "inquisitor_paladin":  ("p", (330, 60, 1090, 630)),
    "lightbringer":        ("p", (430, 120, 1180, 682)),
    "blade_of_judgment":   ("p", (600, 300, 1440, 930)),
    "dawn_saintess":       ("s", (330, 20, 1170, 650)),
    "novice_nun":          ("s", (500, 120, 1060, 540)),
    "chapel_healer":       ("s", (560, 330, 1160, 780)),
    "holy_communion":      ("s", (600, 380, 1140, 785)),
    "seraphina":           ("s", (0, 60, 1000, 810)),
    "sanctuary_anthem":    ("s", (150, 0, 1150, 750)),
    "sacred_mirror":       ("s", (820, 60, 1420, 510)),
}


def crop_to(img, box, size=SIZE):
    return img.crop(box).resize(size, Image.LANCZOS)


# ---------------------------------------------------------------------------
# Procedural art for the Eclipse faction
# ---------------------------------------------------------------------------

def gradient(size, top, bottom):
    img = Image.new("RGB", size)
    d = ImageDraw.Draw(img)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        d.line([(0, y), (size[0], y)],
               fill=(int(top[0] * (1 - t) + bottom[0] * t),
                     int(top[1] * (1 - t) + bottom[1] * t),
                     int(top[2] * (1 - t) + bottom[2] * t)))
    return img


def bloom(img, centre, radius, colour):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse([centre[0] - radius, centre[1] - radius,
               centre[0] + radius, centre[1] + radius], fill=colour)
    layer = layer.filter(ImageFilter.GaussianBlur(radius * 0.5))
    return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")


def eclipse_scene(seed, top, bottom, glow, motif):
    """A dark backdrop plus one bold silhouette, keyed to the card's motif."""
    rng = random.Random(seed)
    img = gradient(SIZE, top, bottom)
    w, h = SIZE

    # Broken cathedral arches receding into the dark
    d = ImageDraw.Draw(img, "RGBA")
    for i in range(5):
        x = 20 + i * 96 + rng.randint(-12, 12)
        arch_w = 54
        d.rectangle([x, 60 + rng.randint(0, 30), x + arch_w, h], fill=(0, 0, 0, 70))
        d.ellipse([x, 40, x + arch_w, 120], fill=(0, 0, 0, 70))

    img = bloom(img, (w // 2, int(h * 0.42)), 120, glow + (90,))
    d = ImageDraw.Draw(img, "RGBA")
    cx, cy = w // 2, int(h * 0.55)

    if motif == "cultist":
        d.polygon([(cx, 70), (cx + 82, h), (cx - 82, h)], fill=(28, 14, 38, 255))
        d.ellipse([cx - 30, 80, cx + 30, 150], fill=(8, 5, 12, 255))
        for side in (-1, 1):
            d.ellipse([cx + side * 16 - 7, 108, cx + side * 16 + 7, 120], fill=glow + (255,))
    elif motif == "beast":
        d.polygon([(cx - 110, h), (cx - 40, 130), (cx + 30, 120), (cx + 120, h)],
                  fill=(30, 16, 20, 255))
        for side in (-1, 1):
            d.polygon([(cx + side * 40, 130), (cx + side * 70, 60), (cx + side * 20, 118)],
                      fill=(52, 26, 30, 255))
            d.ellipse([cx + side * 30 - 8, 150, cx + side * 30 + 8, 164], fill=glow + (255,))
    elif motif == "undead":
        d.polygon([(cx, 84), (cx + 70, h), (cx - 70, h)], fill=(24, 30, 30, 255))
        d.ellipse([cx - 34, 76, cx + 34, 150], fill=(200, 200, 186, 255))   # skull
        for side in (-1, 1):
            d.ellipse([cx + side * 15 - 8, 104, cx + side * 15 + 8, 120], fill=(10, 8, 10, 255))
        d.rectangle([cx - 10, 128, cx + 10, 146], fill=(10, 8, 10, 255))
    elif motif == "demon":
        d.polygon([(cx, 40), (cx + 120, h), (cx - 120, h)], fill=(34, 12, 40, 255))
        for side in (-1, 1):
            d.polygon([(cx + side * 44, 96), (cx + side * 118, 8), (cx + side * 30, 74)],
                      fill=(74, 22, 82, 255))
        for side in (-1, 1):
            d.ellipse([cx + side * 26 - 12, 112, cx + side * 26 + 12, 134], fill=(255, 48, 44, 255))
    elif motif == "witch":
        d.polygon([(cx, 66), (cx + 76, h), (cx - 76, h)], fill=(30, 18, 44, 255))
        d.polygon([(cx - 54, 74), (cx + 54, 74), (cx, 0)], fill=(46, 24, 66, 255))
        d.ellipse([cx - 26, 84, cx + 26, 142], fill=(10, 6, 14, 255))
        for side in (-1, 1):
            d.ellipse([cx + side * 12 - 6, 106, cx + side * 12 + 6, 118], fill=glow + (255,))
    elif motif == "ritual":
        # A summoning circle for the spell cards
        for r, alpha in ((130, 150), (96, 110), (58, 80)):
            d.ellipse([cx - r, cy - r // 2, cx + r, cy + r // 2],
                      outline=glow + (alpha,), width=4)
        for i in range(8):
            a = i * math.pi / 4
            d.line([(cx, cy), (cx + int(150 * math.cos(a)), cy + int(75 * math.sin(a)))],
                   fill=glow + (60,), width=3)
    elif motif == "snare":
        # A trap: chains crossing the frame
        for i in range(6):
            y = 40 + i * 46
            d.line([(0, y), (w, y + rng.randint(-24, 24))], fill=(60, 40, 30, 200), width=6)
        d.ellipse([cx - 40, cy - 40, cx + 40, cy + 40], outline=glow + (200,), width=5)

    return img.filter(ImageFilter.SMOOTH)


# Sanctum cards with no matching subject in the key art get drawn instead, in a
# gold-and-steel register so they still read as the same faction.
SANCTUM_ART = {
    #  id                   seed  top          bottom      glow             motif
    "squire_knight":       (13, (52, 44, 30), (14, 12, 10), (236, 200, 120), "shield"),
    "tower_bastion":       (17, (40, 44, 56), (10, 12, 18), (150, 190, 245), "tower"),
    "cathedral_guard":     (19, (46, 42, 38), (12, 11, 12), (210, 190, 150), "shield"),
    "unyielding_vow":      (29, (56, 40, 24), (14, 10, 8), (250, 190, 110), "oath"),
    "crossbow_marksman":   (31, (36, 46, 38), (10, 14, 11), (168, 226, 172), "bolt"),
    "radiant_volley":      (43, (54, 48, 26), (14, 12, 8), (252, 226, 130), "volley"),
    "solar_snare":         (47, (56, 46, 20), (15, 12, 6), (255, 214, 96), "sun"),
}


def sanctum_scene(seed, top, bottom, glow, motif):
    """Stained-glass light behind a single heraldic shape."""
    rng = random.Random(seed)
    img = gradient(SIZE, top, bottom)
    w, h = SIZE
    cx, cy = w // 2, int(h * 0.52)

    # Cathedral window: light shafts fanning down from above
    d = ImageDraw.Draw(img, "RGBA")
    for i in range(9):
        x = int(w * i / 8)
        d.polygon([(cx, -40), (x - 26, h), (x + 26, h)], fill=glow + (26,))

    img = bloom(img, (cx, int(h * 0.34)), 120, glow + (80,))
    d = ImageDraw.Draw(img, "RGBA")

    steel = (206, 212, 224, 255)
    dark = (52, 58, 72, 255)

    if motif == "shield":
        pts = [(cx - 78, 62), (cx + 78, 62), (cx + 70, 200), (cx, 268), (cx - 70, 200)]
        d.polygon(pts, fill=dark, outline=glow + (255,))
        d.polygon([(cx - 58, 78), (cx + 58, 78), (cx + 52, 194), (cx, 246), (cx - 52, 194)],
                  outline=steel, width=3)
        d.rectangle([cx - 9, 96, cx + 9, 220], fill=glow + (255,))
        d.rectangle([cx - 44, 130, cx + 44, 148], fill=glow + (255,))
    elif motif == "tower":
        d.rectangle([cx - 62, 70, cx + 62, h - 20], fill=dark, outline=glow + (255,))
        for row in range(4):
            y = 96 + row * 44
            d.line([(cx - 62, y), (cx + 62, y)], fill=steel, width=2)
        for i in range(5):
            x = cx - 62 + i * 31
            d.rectangle([x, 52, x + 18, 72], fill=dark, outline=glow + (200,))
        d.ellipse([cx - 20, 150, cx + 20, 190], fill=glow + (220,))
    elif motif == "oath":
        # A sword driven into the ground, hilt up, wreathed in light
        d.rectangle([cx - 8, 60, cx + 8, 250], fill=steel)
        d.rectangle([cx - 52, 96, cx + 52, 112], fill=glow + (255,))
        d.ellipse([cx - 18, 44, cx + 18, 80], fill=glow + (255,))
        for r in (60, 92, 124):
            d.ellipse([cx - r, 250 - r // 3, cx + r, 250 + r // 3],
                      outline=glow + (110,), width=3)
    elif motif == "bolt":
        d.polygon([(cx - 96, 150), (cx + 96, 138), (cx + 96, 162), (cx - 96, 174)], fill=dark)
        d.polygon([(cx + 96, 126), (cx + 150, 150), (cx + 96, 174)], fill=steel)
        d.polygon([(cx - 96, 138), (cx - 134, 150), (cx - 96, 162)], fill=glow + (255,))
        for i in range(3):
            y = 96 + i * 54
            d.line([(cx - 150, y), (cx + 150, y)], fill=glow + (50,), width=2)
    elif motif == "volley":
        for i in range(11):
            x = 20 + i * 38 + rng.randint(-8, 8)
            d.polygon([(x, 30), (x + 7, 30), (x + 3, 150 + rng.randint(-20, 30))],
                      fill=glow + (235,))
        d.ellipse([cx - 130, 210, cx + 130, 290], fill=glow + (60,))
    elif motif == "sun":
        for i in range(16):
            a = i * math.pi / 8
            d.line([(cx, cy), (cx + int(210 * math.cos(a)), cy + int(150 * math.sin(a)))],
                   fill=glow + (90,), width=7)
        d.ellipse([cx - 62, cy - 62, cx + 62, cy + 62], fill=glow + (255,))
        d.ellipse([cx - 40, cy - 40, cx + 40, cy + 40], fill=(255, 250, 226, 255))

    return img.filter(ImageFilter.SMOOTH)


ECLIPSE_ART = {
    "void_fanatic":     (11, (34, 18, 44), (10, 6, 14), (214, 60, 52), "cultist"),
    "blight_hound":     (23, (40, 20, 22), (12, 7, 9), (250, 140, 60), "beast"),
    "cursed_husk":      (37, (26, 34, 34), (8, 12, 12), (140, 240, 180), "undead"),
    "dread_knight":     (41, (38, 14, 18), (12, 5, 8), (236, 70, 66), "cultist"),
    "coven_mistress":   (53, (34, 20, 48), (11, 7, 16), (176, 108, 246), "witch"),
    "malakar":          (67, (44, 14, 52), (12, 4, 16), (222, 74, 226), "demon"),
    "abyssal_wyrm":     (71, (30, 12, 34), (8, 4, 10), (190, 60, 210), "demon"),
    "blood_rite":       (83, (44, 12, 16), (12, 4, 6), (238, 54, 48), "ritual"),
    "corrosive_rain":   (89, (26, 34, 20), (8, 12, 6), (152, 236, 90), "ritual"),
    "bog_of_torment":   (97, (24, 28, 22), (8, 10, 8), (128, 200, 120), "snare"),
    "spiteful_revenge": (101, (40, 16, 20), (12, 5, 7), (250, 96, 60), "snare"),
}


def build_eclipse_frame():
    """A frame in the Eclipse palette, matching generate_frames.py's design."""
    w, h = 500, 700
    bright, dim = (196, 76, 206), (96, 34, 108)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    inset = 10
    d.rectangle([inset, inset, w - inset - 1, h - inset - 1], outline=bright, width=5)
    d.rectangle([inset + 9, inset + 9, w - inset - 10, h - inset - 10],
                outline=dim + (170,), width=2)

    arm, thick = 62, 12
    for cx, cy in ((inset, inset), (w - inset, inset), (inset, h - inset), (w - inset, h - inset)):
        dx = arm if cx < w // 2 else -arm
        dy = arm if cy < h // 2 else -arm
        d.line([(cx, cy), (cx + dx, cy)], fill=bright, width=thick)
        d.line([(cx, cy), (cx, cy + dy)], fill=bright, width=thick)

    # An inverted crest: a horned sigil instead of the Sanctum's cross
    notch = 46
    d.rectangle([w // 2 - notch, inset - 4, w // 2 + notch, inset + 8], fill=(0, 0, 0, 0))
    cx, cy = w // 2, inset + 34
    d.polygon([(cx, cy + 18), (cx - 24, cy - 16), (cx - 8, cy - 4),
               (cx, cy - 26), (cx + 8, cy - 4), (cx + 24, cy - 16)], fill=bright)

    y_div = int(h * 0.510)
    d.line([(inset + 26, y_div), (w - inset - 26, y_div)], fill=dim + (210,), width=4)
    d.ellipse([w // 2 - 9, y_div - 9, w // 2 + 9, y_div + 9], fill=bright)

    img.save(os.path.join(FRAMES, "frame_eclipse.png"))


def build_eclipse_portrait():
    """Commander portrait for the Eclipse side of the duel HUD."""
    size = 256
    img = gradient((size, size), (44, 14, 52), (10, 4, 14))
    img = bloom(img, (size // 2, size // 2), 90, (196, 60, 206, 110))
    d = ImageDraw.Draw(img, "RGBA")
    cx = size // 2
    d.polygon([(cx, 40), (cx + 96, size), (cx - 96, size)], fill=(30, 12, 40, 255))
    for side in (-1, 1):
        d.polygon([(cx + side * 34, 78), (cx + side * 96, 4), (cx + side * 22, 60)],
                  fill=(72, 22, 84, 255))
    d.ellipse([cx - 44, 62, cx + 44, 150], fill=(8, 4, 10, 255))
    for side in (-1, 1):
        d.ellipse([cx + side * 20 - 11, 96, cx + side * 20 + 11, 116], fill=(255, 52, 46, 255))
    img.save(os.path.join(PORTRAITS, "eclipse.png"))


def has_real_art(card_id):
    """True once a hand-made master exists for this card.

    import_card_art.py stores every supplied illustration under
    art_source/cards/. Placeholders must never overwrite one, so this is the
    guard that keeps a stray run of this script from destroying real artwork.
    """
    source = os.path.join(ROOT, "art_source", "cards")
    if not os.path.isdir(source):
        return False
    for name in os.listdir(source):
        stem = os.path.splitext(name)[0]
        if stem == card_id or stem.startswith(card_id + "_"):
            return True
    return False


def main():
    for path in (CARDS, FRAMES, PORTRAITS):
        os.makedirs(path, exist_ok=True)

    paladin = Image.open(PALADIN).convert("RGB")
    saintess = Image.open(SAINTESS).convert("RGB")

    made = 0
    protected = []

    for card_id, (source, box) in SANCTUM_CROPS.items():
        if has_real_art(card_id):
            protected.append(card_id)
            continue
        img = paladin if source == "p" else saintess
        crop_to(img, box).save(os.path.join(CARDS, card_id + ".png"))
        made += 1

    for card_id, (seed, top, bottom, glow, motif) in SANCTUM_ART.items():
        if has_real_art(card_id):
            protected.append(card_id)
            continue
        sanctum_scene(seed, top, bottom, glow, motif).save(
            os.path.join(CARDS, card_id + ".png"))
        made += 1

    for card_id, (seed, top, bottom, glow, motif) in ECLIPSE_ART.items():
        if has_real_art(card_id):
            protected.append(card_id)
            continue
        eclipse_scene(seed, top, bottom, glow, motif).save(
            os.path.join(CARDS, card_id + ".png"))
        made += 1

    build_eclipse_frame()
    build_eclipse_portrait()

    print("Generated {} placeholder illustrations".format(made))
    if protected:
        print("Left alone - real art exists in art_source/cards/:")
        for card_id in sorted(protected):
            print("  " + card_id)
    print("  + assets/frames/frame_eclipse.png")
    print("  + assets/portraits/eclipse.png")


if __name__ == "__main__":
    main()

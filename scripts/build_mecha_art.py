"""
Generate a placeholder illustration for every card in the catalogue.

Run from the project root:   python scripts/build_mecha_art.py

The Mecha-Chivalry theme arrived without art, and a card with no texture draws
as a flat magenta block - unreadable on a board of eight. These placeholders are
not concept art and are not trying to be: they are role-coloured plates with a
blocky chassis silhouette, so a player can tell a Vanguard wall from a Dragoon
interceptor at a glance while the real illustrations are commissioned.

It never overwrites real art: any card with a master in art_source/cards/ is
skipped, so dropping a finished piece in and rerunning scripts/import_card_art.py
takes over permanently.
"""

import io
import json
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARDS = os.path.join(ROOT, "assets", "cards")
SOURCE = os.path.join(ROOT, "art_source", "cards")
SIZE = (420, 320)

# One palette per doctrine: (deep ground, mid plate, hot accent). These follow
# the accent colours CardArt::accentFor uses, so a placeholder and its frame
# read as the same faction.
ROLE_COLOURS = {
    "Vanguard":   ((14, 22, 34), (38, 62, 92), (120, 160, 210)),
    "Paladin":    ((30, 22, 10), (86, 64, 22), (240, 190, 80)),
    "Valkyrie":   ((12, 28, 26), (34, 76, 68), (150, 220, 195)),
    "Dragoon":    ((32, 16, 12), (92, 44, 30), (230, 120, 90)),
    "Siege":      ((26, 21, 14), (74, 60, 40), (190, 150, 100)),
    "Inquisitor": ((24, 14, 32), (62, 40, 84), (175, 120, 225)),
}


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def backdrop(draw, ground, mid, accent):
    """A vertical gradient with a horizon band, so the chassis has somewhere to
    stand rather than floating in a void."""
    w, h = SIZE
    horizon = int(h * 0.68)
    for y in range(h):
        if y < horizon:
            t = y / float(horizon)
            draw.line([(0, y), (w, y)], fill=lerp(ground, mid, t * 0.7))
        else:
            t = (y - horizon) / float(h - horizon)
            draw.line([(0, y), (w, y)], fill=lerp(mid, ground, t))
    draw.line([(0, horizon), (w, horizon)], fill=accent, width=2)


def hangar_lights(draw, accent, rng, count=7):
    """Vertical strip lights receding into the hangar."""
    w, h = SIZE
    for _ in range(count):
        x = rng.randint(10, w - 10)
        top = rng.randint(10, int(h * 0.45))
        length = rng.randint(30, 90)
        shade = lerp((0, 0, 0), accent, rng.uniform(0.18, 0.42))
        draw.line([(x, top), (x, top + length)], fill=shade, width=rng.choice([1, 1, 2]))


def chassis(draw, role, tier, category, accent, mid, rng):
    """A blocky silhouette whose bulk tracks the tier and whose shape tracks the
    doctrine, so the board reads at a glance."""
    w, h = SIZE
    cx = w // 2
    base = int(h * 0.72)

    bulk = {1: 0.72, 2: 0.88, 3: 1.10}.get(tier, 0.8)
    body_w = int(96 * bulk)
    body_h = int(120 * bulk)

    plate = lerp(mid, (0, 0, 0), 0.35)
    edge = lerp(accent, (0, 0, 0), 0.25)

    def box(x0, y0, x1, y1, fill=None, outline=None, width=2):
        draw.rectangle([x0, y0, x1, y1], fill=fill or plate,
                       outline=outline or edge, width=width)

    if category == "Spell":
        # An operation, not a frame: a rising energy column.
        for i in range(5):
            t = i / 4.0
            half = int(70 * (1.0 - t * 0.55))
            y = base - int(t * 190)
            draw.rectangle([cx - half, y - 14, cx + half, y + 8],
                           fill=lerp(plate, accent, t * 0.8), outline=edge)
        return

    if category == "Trap":
        # A sealed counter-protocol: a locked plate with a warning chevron.
        box(cx - 84, base - 150, cx + 84, base, outline=accent, width=3)
        for i in range(3):
            y = base - 120 + i * 38
            draw.polygon([(cx - 46, y), (cx, y + 26), (cx + 46, y)],
                         outline=accent)
        return

    legs = 32 + int(14 * bulk)
    # Legs
    box(cx - body_w // 2, base - legs, cx - body_w // 2 + 26, base)
    box(cx + body_w // 2 - 26, base - legs, cx + body_w // 2, base)
    # Torso
    torso_top = base - legs - body_h
    box(cx - body_w // 2, torso_top, cx + body_w // 2, base - legs)
    # Cockpit band
    draw.rectangle([cx - body_w // 2 + 10, torso_top + 18,
                    cx + body_w // 2 - 10, torso_top + 40],
                   fill=lerp(accent, (0, 0, 0), 0.35), outline=accent)
    # Head
    box(cx - 20, torso_top - 30, cx + 20, torso_top, outline=accent)
    draw.line([(cx - 12, torso_top - 15), (cx + 12, torso_top - 15)],
              fill=accent, width=3)

    # Doctrine silhouette: the one shape that tells the roles apart at 120px.
    if role == "Vanguard":
        box(cx - body_w // 2 - 42, torso_top + 6, cx - body_w // 2 - 6,
            base - legs + 10, outline=accent, width=3)          # tower shield
    elif role == "Siege":
        draw.rectangle([cx + body_w // 2 - 4, torso_top + 8,
                        cx + body_w // 2 + 96, torso_top + 30],
                       fill=plate, outline=accent, width=2)      # barrel
        draw.ellipse([cx + body_w // 2 + 84, torso_top + 4,
                      cx + body_w // 2 + 110, torso_top + 34], outline=accent)
    elif role == "Dragoon":
        for side in (-1, 1):
            draw.polygon([(cx + side * (body_w // 2), torso_top + 20),
                          (cx + side * (body_w // 2 + 70), torso_top - 18),
                          (cx + side * (body_w // 2 + 20), torso_top + 52)],
                         outline=accent)                         # swept wings
    elif role == "Paladin":
        draw.line([(cx + body_w // 2, base - legs + 6),
                   (cx + body_w // 2 + 30, torso_top - 54)], fill=accent, width=5)
        draw.ellipse([cx - 46, torso_top - 96, cx + 46, torso_top - 22],
                     outline=accent)                             # halo + lance
    elif role == "Valkyrie":
        for side in (-1, 1):
            for i in range(3):
                draw.line([(cx + side * (body_w // 2 - 4), torso_top + 10 + i * 16),
                           (cx + side * (body_w // 2 + 54 - i * 10),
                            torso_top - 24 + i * 22)], fill=accent, width=2)
    elif role == "Inquisitor":
        draw.ellipse([cx - 58, torso_top - 74, cx + 58, torso_top - 4],
                     outline=accent)                             # scanning ring
        draw.line([(cx - 58, torso_top - 39), (cx + 58, torso_top - 39)],
                  fill=accent, width=1)


def build(card):
    role = card.get("role", "Vanguard")
    ground, mid, accent = ROLE_COLOURS.get(role, ROLE_COLOURS["Vanguard"])
    rng = random.Random(card["id"])          # stable art per card id

    img = Image.new("RGB", SIZE, ground)
    draw = ImageDraw.Draw(img)
    backdrop(draw, ground, mid, accent)
    hangar_lights(draw, accent, rng)
    chassis(draw, role, card.get("tier", 1), card.get("category", "Unit"),
            accent, mid, rng)

    img = img.filter(ImageFilter.GaussianBlur(0.6))

    # A vignette keeps the card name legible where the frame overlaps the art.
    shade = Image.new("L", SIZE, 0)
    ImageDraw.Draw(shade).ellipse([-90, -70, SIZE[0] + 90, SIZE[1] + 70], fill=255)
    img = Image.composite(img, Image.new("RGB", SIZE, ground),
                          shade.filter(ImageFilter.GaussianBlur(48)))
    return img


def main():
    path = os.path.join(ROOT, "assets", "data", "cards.json")
    cards = json.load(io.open(path, encoding="utf-8"))["cards"]
    os.makedirs(CARDS, exist_ok=True)

    built, skipped = 0, []
    for card in cards:
        card_id = card["id"]
        # Real art always wins: a master in art_source means this card is done.
        if os.path.isdir(SOURCE) and any(
                os.path.splitext(name)[0] == card_id for name in os.listdir(SOURCE)):
            skipped.append(card_id)
            continue
        # JPEG to match the shipped card art: these are opaque and
        # photographic-ish, and PNG costs several times the bytes.
        build(card).save(os.path.join(CARDS, card_id + ".jpg"),
                         format="JPEG", quality=90, optimize=True)
        built += 1

    print("placeholders built: {}".format(built))
    if skipped:
        print("kept real art for: {}".format(", ".join(skipped)))

    total = sum(os.path.getsize(os.path.join(CARDS, f))
                for f in os.listdir(CARDS)
                if os.path.isfile(os.path.join(CARDS, f)))
    print("assets/cards ships {:.2f} MB".format(total / 1024 / 1024))


if __name__ == "__main__":
    main()

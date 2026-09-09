"""
Generate the card frames (assets/frames/frame_*.png).

Run from the project root:   python scripts/generate_frames.py

Design notes
------------
A frame is drawn at 500x700 (GDD 3.1) but is displayed at 150x210 in hand,
a 0.30x reduction. Hairlines vanish at that scale and dense ornament turns to
mush, so every element here is sized to survive the downscale:

    5 px outer rule        -> 1.5 px on screen
    12 px corner accents   -> 3.6 px on screen
    ~44 px top emblem      -> ~13 px on screen

The frame carries the ONLY border the card has - the card background beneath it
is drawn without an outline, otherwise the two stack into the heavy double
border the previous version had.
"""

import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "frames")

W, H = 500, 700

THEMES = {
    "frame_paladin":  ((236, 190, 74),  (150, 112, 30)),   # gold
    "frame_saintess": ((226, 233, 246), (140, 152, 176)),  # silver-white
    "frame_knight":   ((104, 162, 232), (48, 88, 148)),    # steel blue
}


def draw_cross(draw, cx, cy, size, colour):
    """Small latin cross used as the crest at the top of the frame."""
    arm = max(2, size // 5)
    draw.rectangle([cx - arm // 2, cy - size, cx + arm // 2, cy + size], fill=colour)
    draw.rectangle([cx - size * 2 // 3, cy - size // 3,
                    cx + size * 2 // 3, cy - size // 3 + arm], fill=colour)


def build(name, bright, dim):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    inset = 10
    # Single outer rule - the card's only border
    d.rectangle([inset, inset, W - inset - 1, H - inset - 1], outline=bright, width=5)
    # Faint companion line just inside it for a bevelled, metal-leaf feel
    d.rectangle([inset + 9, inset + 9, W - inset - 10, H - inset - 10],
                outline=dim + (170,), width=2)

    # Corner accents: short thick strokes reading as filigree once scaled down
    arm, thick = 62, 12
    for cx, cy in ((inset, inset), (W - inset, inset),
                   (inset, H - inset), (W - inset, H - inset)):
        dx = arm if cx < W // 2 else -arm
        dy = arm if cy < H // 2 else -arm
        d.line([(cx, cy), (cx + dx, cy)], fill=bright, width=thick)
        d.line([(cx, cy), (cx, cy + dy)], fill=bright, width=thick)

    # Crest at the top centre, sitting on a cleared notch in the rule
    notch = 46
    d.rectangle([W // 2 - notch, inset - 4, W // 2 + notch, inset + 8], fill=(0, 0, 0, 0))
    draw_cross(d, W // 2, inset + 30, 26, bright)

    # Divider marking the top of the text panel (art window ends here)
    y_div = int(H * 0.510)
    d.line([(inset + 26, y_div), (W - inset - 26, y_div)], fill=dim + (210,), width=4)
    d.ellipse([W // 2 - 9, y_div - 9, W // 2 + 9, y_div + 9], fill=bright)

    img.save(os.path.join(OUT_DIR, name + ".png"))
    return name


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, (bright, dim) in THEMES.items():
        build(name, bright, dim)
        print("  wrote assets/frames/{}.png".format(name))


if __name__ == "__main__":
    main()

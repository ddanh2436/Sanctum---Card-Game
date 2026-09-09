"""
Fit tall source art into the 4:3 artwork window without decapitating it.

Run from the project root:   python scripts/fit_card_art.py [--commit]

The first pass cropped a 4:3 band out of each portrait, which on a 736x1308 pin
keeps 42% of the height - not enough for a standing figure, so heads went over
the top edge. Cropping cannot fix that: no 4:3 window of a tall figure contains
both the head and the body.

So this fits instead of crops. It takes the vertical span that actually holds
the subject, scales it to the full height of the window, and fills the space
either side with a blurred, darkened copy of the same image. The subject stays
whole, and the surround reads as depth of field rather than as letterbox bars.

`span` is (top, bottom) as fractions of the source height - the part worth
keeping. Every one was set by looking at the image.

Run with --commit to write; without it, only a preview sheet.
"""

import math
import os
import shutil
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARDS = os.path.join(ROOT, "assets", "cards")
SOURCE = os.path.join(ROOT, "art_source", "cards")
# The preview sheet is a working file, not an asset - keep it out of the build.
SCRATCH = os.path.join(ROOT, "art_source")

BOX = (840, 640)
RATIO = BOX[0] / float(BOX[1])

# id, master file, mode, span(top, bottom), note
#
# Every master is archived in art_source/cards/ under the id it serves, so
# the source column is just "<id>.<ext>". It is deliberately NOT the original
# Pinterest filename: those were reused across cards and pointing at them
# swapped four pairs of images.
PLAN = [
    # --- Paladin: tall pins, fitted so the whole frame stays in shot ---------
    ("pl_solaris",    "pl_solaris.jpg",    "fit", (0.00, 0.66), "winged lord, sunburst halo"),
    ("pl_judicator",  "pl_judicator.jpg",  "fit", (0.00, 0.72), "all-gold temple knight"),
    ("pl_vindicator", "pl_vindicator.jpg",     "fit", (0.02, 0.60), "white/red crusader over a street"),
    ("pl_censer",     "pl_censer.jpg", "fit", (0.08, 0.68), "dark red-trimmed frame, long blade"),
    ("pl_scout",      "pl_scout.jpg",      "fit", (0.06, 0.70), "lance wreathed in white-hot energy"),
    ("pl_squire",     "pl_squire.jpg",  "fit", (0.02, 0.62), "red/white head against sky"),
    ("pl_lumen",      "pl_lumen.jpg",       "fit", (0.00, 0.64), "golden halo behind a winged frame"),
    ("pl_conduit",    "pl_conduit.jpg", "fit", (0.06, 0.78), "head close-up, blue energy crystals"),
    ("pl_overcharge", "pl_overcharge.jpg",    "fit", (0.00, 0.60), "winged angel under a ring of light"),
    ("pl_flare",      "pl_flare.jpg",      "fit", (0.04, 0.66), "colossus looming over the field"),
    ("pl_smite",      "pl_smite.jpg",     "fit", (0.18, 0.80), "frame before a vast orbital ring"),

    # --- Inquisitor: tall pins again, so fitted rather than cropped. Each span
    # runs from the top of the halo/head down to roughly the waist, which is
    # what "the whole face and the whole upper body" means on this framing.
    ("iq_blackice",  "iq_blackice.jpg",  "fit", (0.00, 0.60), "crowned figure, blue flame, greatsword"),
    ("iq_centurion", "iq_centurion.jpg", "fit", (0.00, 0.70), "gold orrery halo over a black frame"),
    ("iq_cipher",    "iq_cipher.jpg",    "fit", (0.00, 0.62), "hooded reader under a red ring"),
    ("iq_deusex",    "iq_deusex.jpg",    "fit", (0.00, 0.62), "white-haired judge with a mace"),
    ("iq_firewall",  "iq_firewall.jpg",  "fit", (0.05, 0.70), "ivory plate, green firewall glow"),
    ("iq_jammer",    "iq_jammer.jpg",    "fit", (0.02, 0.70), "smoking operative, silver hair"),
    ("iq_overload",  "iq_overload.jpg",  "fit", (0.00, 0.72), "blue-lit figure, gold circuit veil"),
    ("iq_probe",     "iq_probe.jpg",     "fit", (0.02, 0.62), "red-helmed marine on a street"),
    ("iq_purge",     "iq_purge.jpg",     "fit", (0.03, 0.68), "masked crown, red blade"),
    ("iq_snare",     "iq_snare.jpg",     "fit", (0.02, 0.75), "violet helm, hands cupping light"),
    ("iq_surge",     "iq_surge.jpg",     "fit", (0.05, 0.72), "hooded frame inside a gold ring"),
    ("iq_warden",    "iq_warden.jpg",    "fit", (0.00, 0.72), "white-haired warden, storm sky"),

    # --- Dragoon: aerials and dragons, all taller than the window. Spans run
    # from the top of the wings or the crest down to about the waist.
    ("dg_afterburn",  "dg_afterburn.jpg",  "fit", (0.00, 0.72), "pale dragon coiled over cloud"),
    ("dg_evasion",    "dg_evasion.jpg",    "fit", (0.00, 0.70), "magenta dragon, wings spread"),
    ("dg_lancer",     "dg_lancer.jpg",     "fit", (0.02, 0.72), "horned rider against a white moon"),
    ("dg_outrider",   "dg_outrider.jpg",   "fit", (0.02, 0.78), "black and gold armour, feathered"),
    ("dg_raptor",     "dg_raptor.jpg",     "fit", (0.00, 0.78), "winged colossus on a shoreline"),
    ("dg_scoutbike",  "dg_scoutbike.jpg",  "fit", (0.02, 0.75), "gold dragon wound round a rider"),
    ("dg_stormrider", "dg_stormrider.jpg", "fit", (0.00, 0.68), "black dragon crossing the moon"),
    ("dg_veer",       "dg_veer.jpg",       "fit", (0.00, 0.72), "gold winged frame, halo behind"),
    ("dg_zephyr",     "dg_zephyr.jpg",     "fit", (0.12, 0.88), "ice crown, face low in frame"),
    ("dg_zero",       "dg_zero.jpg",       "fit", (0.00, 0.62), "white dragon over the long avenue"),

    # --- Vanguard: already 1.31, a hair off 4:3. Cover, no fill needed.
    ("vg_shieldwall", "vg_shieldwall.png", "cover", (0.0, 1.0), ""),
    ("vg_bastion",    "vg_bastion.png",    "cover", (0.0, 1.0), ""),
    ("vg_anvil",      "vg_anvil.png",      "cover", (0.0, 1.0), ""),
    ("vg_pike",       "vg_pike.png",       "cover", (0.0, 1.0), ""),
    ("vg_castellan",  "vg_castellan.png",  "cover", (0.0, 1.0), ""),
    ("vg_galahad",    "vg_galahad.png",    "cover", (0.0, 1.0), ""),
    ("vg_behemoth",   "vg_behemoth.png",   "cover", (0.0, 1.0), ""),
    ("vg_holdline",   "vg_holdline.png",   "cover", (0.0, 1.0), ""),
]


def master_path(filename):
    for folder in (SOURCE, CARDS):
        p = os.path.join(folder, filename)
        if os.path.exists(p):
            return p
    return None


def cover(img):
    """Centre-crop to 4:3, then scale to the box."""
    w, h = img.size
    if w / float(h) > RATIO:
        cw = int(round(h * RATIO))
        img = img.crop(((w - cw) // 2, 0, (w - cw) // 2 + cw, h))
    else:
        ch = int(round(w / RATIO))
        top = (h - ch) // 2
        img = img.crop((0, top, w, top + ch))
    return img.resize(BOX, Image.LANCZOS)


def fit(img, span):
    """Scale the chosen span to full height and fill the sides from itself."""
    w, h = img.size
    top = int(round(h * span[0]))
    bottom = int(round(h * span[1]))
    bottom = max(top + 40, min(h, bottom))
    subject = img.crop((0, top, w, bottom))

    # Background: the same span blown up to cover the box, blurred right down
    # and darkened so it never competes with the subject or the card text.
    bg = cover(subject.copy())
    bg = bg.filter(ImageFilter.GaussianBlur(28))
    bg = ImageEnhance.Brightness(bg).enhance(0.45)
    bg = ImageEnhance.Color(bg).enhance(0.7)

    # Foreground: the whole span, scaled to the window height.
    sw, sh = subject.size
    scale = min(BOX[0] / float(sw), BOX[1] / float(sh))
    fw, fh = max(1, int(round(sw * scale))), max(1, int(round(sh * scale)))
    front = subject.resize((fw, fh), Image.LANCZOS)

    x = (BOX[0] - fw) // 2
    y = (BOX[1] - fh) // 2

    # Feather the vertical seam so the subject melts into its own backdrop
    # instead of sitting on a hard rectangle.
    mask = Image.new("L", (fw, fh), 255)
    draw = ImageDraw.Draw(mask)
    feather = max(6, int(fw * 0.05))
    for i in range(feather):
        v = int(255 * (i / float(feather)))
        draw.line([(i, 0), (i, fh)], fill=v)
        draw.line([(fw - 1 - i, 0), (fw - 1 - i, fh)], fill=v)
    mask = mask.filter(ImageFilter.GaussianBlur(3))

    bg.paste(front, (x, y), mask)
    return bg


def build(entry):
    _, filename, mode, span, _ = entry
    path = master_path(filename)
    if path is None:
        return None
    with Image.open(path) as im:
        img = im.convert("RGB")
        return cover(img) if mode == "cover" else fit(img, span)


def preview():
    cell_w, cell_h = 330, 288
    cols = 4
    rows = math.ceil(len(PLAN) / cols)
    sheet = Image.new("RGB", (cell_w * cols, cell_h * rows), (16, 16, 20))
    draw = ImageDraw.Draw(sheet)
    for i, entry in enumerate(PLAN):
        out = build(entry)
        x0, y0 = (i % cols) * cell_w, (i // cols) * cell_h
        if out is not None:
            thumb = out.copy()
            thumb.thumbnail((cell_w - 10, cell_h - 44), Image.LANCZOS)
            sheet.paste(thumb, (x0 + 5, y0 + 30))
        draw.text((x0 + 6, y0 + 6), entry[0], fill=(245, 215, 140))
        draw.text((x0 + 6, y0 + 17), "%s  span %.2f-%.2f" % (entry[1], entry[3][0], entry[3][1]),
                  fill=(130, 140, 155))
        draw.rectangle([x0, y0, x0 + cell_w - 1, y0 + cell_h - 1], outline=(56, 56, 66))
    path = os.path.join(SCRATCH, "preview_fit.png")
    sheet.save(path)
    print(path)


def commit():
    for entry in PLAN:
        card_id, filename = entry[0], entry[1]
        out = build(entry)
        if out is None:
            print("  missing master:", filename)
            continue
        # Archive the untouched master under the id it serves - but ONLY if it
        # is not archived already.
        #
        # This used to remove whatever was in art_source and move the file from
        # assets/cards over it. After the first run assets/cards holds the
        # RENDERED card, not a master, so a second run replaced every original
        # with its own output and the masters were gone for good. The originals
        # are the one thing here that cannot be regenerated, so an existing
        # archive is now never touched.
        ext = os.path.splitext(filename)[1].lower()
        stored = os.path.join(SOURCE, card_id + ext)
        src = os.path.join(CARDS, filename)
        if os.path.exists(src) and not os.path.exists(stored):
            shutil.move(src, stored)
        out.save(os.path.join(CARDS, card_id + ".jpg"), format="JPEG",
                 quality=90, optimize=True, progressive=True)
        print("  %-14s %s" % (card_id, entry[4]))
    print("done")


if __name__ == "__main__":
    if "--commit" in sys.argv:
        commit()
    else:
        preview()

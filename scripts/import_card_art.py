"""
Normalise hand-made card art into what the game actually loads.

Run from the project root:   python scripts/import_card_art.py

Drop artwork into assets/cards/ named after the card id - any of .png .jpg
.jpeg .webp - then run this. For each card it:

  1. gathers every candidate for that id and keeps the highest-resolution one
     as the master in art_source/cards/  (nothing is ever deleted)
  2. centre-crops the master to the 4:3 artwork window
  3. writes assets/cards/<id>.jpg

Why this exists:
  * cards.json names one exact path per card. A .jpg dropped in beside a stale
    .png is silently ignored and the old placeholder keeps showing - which is
    exactly what happened to void_fanatic and blight_hound.
  * The artwork window is 420x320. A 2400x1792 source is six times oversampled:
    it costs ~17 MB of texture memory per card and looks no better on screen.

It never upscales, and once a master is stored it regenerates from that, so
running it twice does not re-compress your art.
"""

import io
import json
import os
import shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARDS = os.path.join(ROOT, "assets", "cards")
# Masters live outside assets/ so the build never ships them: CMake copies
# the whole assets tree next to the executable.
SOURCE = os.path.join(ROOT, "art_source", "cards")

# The artwork window the game draws into, and the ceiling we downscale to.
# 2x the window stays crisp even when a 4K screen scales the whole board up.
WINDOW = (420, 320)
MAX_SIZE = (840, 640)
EXTENSIONS = (".png", ".jpg", ".jpeg", ".webp", ".bmp")


def card_ids():
    path = os.path.join(ROOT, "assets", "data", "cards.json")
    data = json.load(io.open(path, encoding="utf-8"))
    return [(c["id"], c["textureFile"]) for c in data["cards"]]


def pixels(path):
    try:
        with Image.open(path) as img:
            return img.size[0] * img.size[1]
    except Exception:
        return 0


def candidates_for(card_id):
    """Every file that could be this card's art, master folder first."""
    found = []
    for folder in (SOURCE, CARDS):
        if not os.path.isdir(folder):
            continue
        for name in os.listdir(folder):
            stem, ext = os.path.splitext(name)
            if ext.lower() not in EXTENSIONS:
                continue
            # `id`, `id_extra`, `id_alt2` all belong to the same card.
            if stem == card_id or stem.startswith(card_id + "_"):
                found.append(os.path.join(folder, name))
    return found


def output_size(source_size):
    """Largest 4:3 box that fits the source, capped at MAX_SIZE."""
    ratio = MAX_SIZE[0] / MAX_SIZE[1]
    w, h = source_size
    # The crop first: how much 4:3 can we actually take from this image?
    crop_w = min(w, int(round(h * ratio)))
    crop_h = int(round(crop_w / ratio))
    return (min(MAX_SIZE[0], max(WINDOW[0], crop_w)),
            min(MAX_SIZE[1], max(WINDOW[1], crop_h)))


def crop_to_ratio(img, ratio):
    """Centre-crop to the target aspect without ever stretching the picture."""
    w, h = img.size
    if w / h > ratio:
        new_w = int(round(h * ratio))
        left = (w - new_w) // 2
        return img.crop((left, 0, left + new_w, h))
    new_h = int(round(w / ratio))
    top = (h - new_h) // 2
    return img.crop((0, top, w, top + new_h))


def main():
    os.makedirs(SOURCE, exist_ok=True)
    ratio = MAX_SIZE[0] / MAX_SIZE[1]

    changed, kept, missing = [], [], []

    for card_id, texture in card_ids():
        found = candidates_for(card_id)
        if not found:
            missing.append((card_id, texture))
            continue

        target = os.path.join(CARDS, card_id + ".jpg")

        # Highest resolution wins; a tie goes to whichever was written last.
        found.sort(key=lambda p: (pixels(p), os.path.getmtime(p)), reverse=True)
        master = found[0]

        # The generated texture is never an alternate of itself. Without this
        # the script files its own output away and the card loses its art.
        losers = [f for f in found[1:]
                  if os.path.abspath(f) != os.path.abspath(target)]

        # Nothing to do when the shipped file already IS the only master.
        already_built = (os.path.abspath(master) == os.path.abspath(target)
                         and not losers)

        with Image.open(master) as img:
            source_size = img.size
            out_size = output_size(source_size)
            if already_built and source_size == out_size:
                kept.append(card_id)
                continue
            out = crop_to_ratio(img.convert("RGB"), ratio)
            if out.size != out_size:
                out = out.resize(out_size, Image.LANCZOS)

            # Write to a scratch name first so a crash cannot leave a
            # half-written texture where the game expects a card.
            tmp = os.path.join(CARDS, card_id + ".tmp.jpg")
            out.save(tmp, format="JPEG", quality=90, optimize=True,
                     progressive=True)

        # Archive the master under its canonical name, then swap in the output.
        ext = os.path.splitext(master)[1].lower()
        stored = os.path.join(SOURCE, card_id + ext)
        if os.path.abspath(master) != os.path.abspath(stored):
            if os.path.exists(stored):
                os.remove(stored)
            shutil.move(master, stored)

        if os.path.exists(target):
            os.remove(target)
        os.rename(tmp, target)

        # Park the alternates so they never win a future run by accident.
        for index, loser in enumerate(losers):
            if not os.path.exists(loser):
                continue
            lext = os.path.splitext(loser)[1].lower()
            parked = os.path.join(SOURCE, "{}_alt{}{}".format(card_id, index + 1, lext))
            if os.path.abspath(loser) == os.path.abspath(parked):
                continue
            if os.path.exists(parked):
                os.remove(parked)
            shutil.move(loser, parked)

        changed.append((card_id, source_size, out_size, os.path.getsize(target)))

    def mb(n):
        return "{:.2f} MB".format(n / 1024 / 1024)

    if changed:
        print("Built:")
        for card_id, src, out, size in changed:
            note = "" if src[0] >= out[0] else "   (source is smaller than the window)"
            print("  {:<22} {:>5}x{:<5} -> {:>4}x{:<4} {:>9}{}".format(
                card_id, src[0], src[1], out[0], out[1], mb(size), note))
    if kept:
        print("Already current: {} cards".format(len(kept)))
    if missing:
        print("\nNo art found - these show a magenta placeholder in game:")
        for card_id, texture in missing:
            print("  {:<22} expected {}".format(card_id, texture))

    total = sum(os.path.getsize(os.path.join(CARDS, f))
                for f in os.listdir(CARDS)
                if os.path.isfile(os.path.join(CARDS, f)))
    print("\nassets/cards ships {} of textures".format(mb(total)))


if __name__ == "__main__":
    main()

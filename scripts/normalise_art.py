"""
Bake EXIF orientation into the pixels of every image the game ships.

Run from the project root:   python scripts/normalise_art.py [--commit]

Why this exists: SFML's loadFromFile reads the stored pixels and ignores the
EXIF Orientation tag entirely. Every other program the artist uses - Windows
Photos, a browser, Pinterest - honours it. So an image that looks landscape
everywhere else loads into the game rotated ninety degrees, and no amount of
staring at the file in a viewer will show you why.

menu_bg.jpg arrived as 736x1308 stored pixels with Orientation 8 ("rotate 90
CCW to display"). It looked correct in every viewer and sideways in the game.

This rewrites such files so the stored pixels are already in display
orientation and the tag is gone. The untouched original is kept in art_source/.
"""

import os
import shutil
import sys

from PIL import Image, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
BACKUP = os.path.join(ROOT, "art_source", "pre_exif")

EXTENSIONS = (".png", ".jpg", ".jpeg", ".webp", ".bmp")
ORIENTATION_TAG = 274

LABELS = {
    1: "normal", 2: "mirrored", 3: "180", 4: "mirrored 180",
    5: "mirrored 90 CW", 6: "rotate 90 CW", 7: "mirrored 90 CCW", 8: "rotate 90 CCW",
}


def offenders():
    found = []
    for folder, _, files in os.walk(ASSETS):
        for name in sorted(files):
            if not name.lower().endswith(EXTENSIONS):
                continue
            path = os.path.join(folder, name)
            try:
                with Image.open(path) as im:
                    tag = im.getexif().get(ORIENTATION_TAG)
                    if tag and tag != 1:
                        found.append((path, im.size, tag))
            except Exception as exc:
                print("  could not read %s: %s" % (path, exc))
    return found


def main():
    commit = "--commit" in sys.argv
    found = offenders()

    if not found:
        print("No image carries a rotating EXIF tag. Nothing to do.")
        return

    print("%d image(s) whose stored pixels are not in display orientation:" % len(found))
    for path, size, tag in found:
        rel = os.path.relpath(path, ROOT)
        print("  %-40s %5dx%-5d  orientation %d (%s)"
              % (rel, size[0], size[1], tag, LABELS.get(tag, "?")))

    if not commit:
        print("\nRerun with --commit to bake the rotation in.")
        return

    os.makedirs(BACKUP, exist_ok=True)
    for path, _, _ in found:
        # Keep the file exactly as delivered before touching it.
        shutil.copy2(path, os.path.join(BACKUP, os.path.basename(path)))

        with Image.open(path) as im:
            fixed = ImageOps.exif_transpose(im)          # applies the tag
            fixed = fixed.convert("RGB")
            if path.lower().endswith((".jpg", ".jpeg")):
                # exif is dropped by not passing it through.
                fixed.save(path, format="JPEG", quality=92, optimize=True,
                           progressive=True)
            else:
                fixed.save(path, format="PNG", optimize=True)
        with Image.open(path) as check:
            print("  %-40s -> %dx%d, tag cleared"
                  % (os.path.relpath(path, ROOT), check.size[0], check.size[1]))

    print("\nOriginals kept in art_source/pre_exif/")


if __name__ == "__main__":
    main()

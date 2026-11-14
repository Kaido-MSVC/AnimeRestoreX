"""Reference images for tst_core loaderAppliesExifOrientation (v2.1 P5).

For each EXIF Orientation 1-8 and each of PNG (eXIf chunk) and lossless WebP (EXIF chunk),
writes a small source image stored with that orientation, and the result of Pillow's
ImageOps.exif_transpose (the function nunif/utils/pil_io.py uses) as a plain PNG:

    orientation/src_<n>.png   orientation/ref_png_<n>.png
    orientation/src_<n>.webp  orientation/ref_webp_<n>.png

The image is 7 x 4 (not square, so 90-degree turns change the size) with a different colour
in every pixel. PNG uses RGBA for even orientations, WebP for odd ones, so every orientation
is covered with and without alpha. Alpha stays >= 100 so that no decoder can lose colour
to premultiplication.

Usage (tools/winml-probe/_work/venv-torch, Pillow 13.0.0):
    python -I make_orientation_reference.py <tests/data folder>
"""
import os
import sys

from PIL import Image, ImageOps

W, H = 7, 4


def source(rgba: bool) -> Image.Image:
    im = Image.new("RGBA" if rgba else "RGB", (W, H))
    for y in range(H):
        for x in range(W):
            i = y * W + x
            px = (17 + 9 * i, 250 - 7 * i, (31 * i) % 256)
            im.putpixel((x, y), px + (100 + 5 * i,) if rgba else px)
    return im


def main() -> None:
    out = os.path.join(sys.argv[1], "orientation")
    os.makedirs(out, exist_ok=True)
    for n in range(1, 9):
        for fmt, rgba in (("png", n % 2 == 0), ("webp", n % 2 == 1)):
            exif = Image.Exif()
            exif[0x0112] = n
            src = os.path.join(out, f"src_{n}.{fmt}")
            if fmt == "png":
                source(rgba).save(src, exif=exif.tobytes())
            else:
                source(rgba).save(src, lossless=True, exact=True, exif=exif.tobytes())
            with Image.open(src) as im:
                assert im.getexif().get(0x0112) == n, (src, im.getexif())
                upright = ImageOps.exif_transpose(im)
                upright.save(os.path.join(out, f"ref_{fmt}_{n}.png"))
            print(f"{fmt} {n}: {'RGBA' if rgba else 'RGB'} -> {upright.size}")


if __name__ == "__main__":
    main()

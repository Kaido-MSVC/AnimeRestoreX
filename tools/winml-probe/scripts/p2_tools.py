"""P2 helpers (dev only): synthetic test images, PNG <-> ARX8, comparison with the nunif
reference including the spatial error distribution (tile seams / image border / interior),
difference heatmaps and zoomed alpha crops.

  python -I p2_tools.py synth <out dir>                 synthetic PNG set (no copyrighted content)
  python -I p2_tools.py full <out.png> <w> <h>          large synthetic image (full-size run)
  python -I p2_tools.py to-arx <in.png> <out.arx>       8-bit RGB/RGBA (palette/L/LA expanded like pil_io)
  python -I p2_tools.py from-arx <in.arx> <out.png>
  python -I p2_tools.py info <png> [...]                mode, size, ICC / gamma chunks
  python -I p2_tools.py compare <ours.arx|png> <ref.png> <scale> <tile> <offset> <blend> <heatmap.png> <tag> <json>
  python -I p2_tools.py zoom <out.png> <x> <y> <w> <h> <factor> <img.png> [...]   crops side by side
                                                         (RGBA shown over a checkerboard + alpha view)
ARX8: b"ARX8" + uint32 w, h, c + w*h*c bytes (R,G,B[,A], tight rows).
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def read_arx(p):
    b = Path(p).read_bytes()
    assert b[:4] == b"ARX8", p
    w, h, c = struct.unpack("<III", b[4:16])
    return np.frombuffer(b, np.uint8, count=w * h * c, offset=16).reshape(h, w, c)


def write_arx(p, a):
    h, w, c = a.shape
    Path(p).write_bytes(b"ARX8" + struct.pack("<III", w, h, c) + np.ascontiguousarray(a, np.uint8).tobytes())


def load_png(p):
    """Same mode handling as nunif pil_io._load_image for 8-bit PNGs without ICC (P/L -> RGB(A))."""
    im = Image.open(p)
    t = im.info.get("transparency")
    if im.mode in ("RGB", "P") and t is not None:
        im = im.convert("RGBA")
    elif im.mode == "L" and t is not None:
        im = im.convert("LA")
    if im.mode == "LA":
        im = im.convert("RGBA")
    elif im.mode not in ("RGB", "RGBA"):
        im = im.convert("RGB")
    return np.asarray(im)


def load_any(p):
    return read_arx(p) if str(p).endswith(".arx") else load_png(p)


# ---------------------------------------------------------------- synthetic images
def _font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def draw_scene(w, h, seed, text=True):
    rng = np.random.default_rng(seed)
    im = Image.new("RGB", (w, h), (246, 238, 226))
    d = ImageDraw.Draw(im)
    for y in range(h // 3):  # vertical gradient band
        c = int(110 + 140 * y / max(1, h // 3))
        d.line([(0, y), (w, y)], fill=(c, min(255, c + 25), 255))
    for x in range(0, w, 3):  # horizontal gradient strip
        g = int(255 * x / max(1, w - 1))
        d.line([(x, h // 3), (x, h // 3 + 12)], fill=(g, 255 - g, 128))
    for k in range(int(6 + w * h / 20000)):  # flat filled shapes with dark outlines
        x0, y0 = rng.integers(0, w), rng.integers(h // 3, h)
        r = int(rng.integers(8, 60))
        col = tuple(int(v) for v in rng.integers(40, 255, 3))
        if k % 2:
            d.ellipse([x0 - r, y0 - r, x0 + r, y0 + r], fill=col, outline=(30, 20, 20), width=2)
        else:
            d.polygon([(x0, y0 - r), (x0 + r, y0 + r), (x0 - r, y0 + r)], fill=col, outline=(20, 20, 40))
    for i in range(0, w, 17):  # 1-px thin lines at many positions (cross tile seams)
        d.line([(i, 0), (i + h // 2, h)], fill=(40, 25, 20), width=1)
    for j in range(0, h, 23):
        d.line([(0, j), (w, j + 7)], fill=(60, 40, 90), width=1)
    if text:
        f = _font(14)
        for j in range(h // 3 + 20, h, 60):
            d.text((5, j), "AnimeRestoreX 0123456789 waifu2x seam", fill=(10, 10, 10), font=f)
    return im


def synth(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    draw_scene(640, 480, 1).save(out / "s01_scene_640x480.png")
    draw_scene(257, 131, 2).save(out / "s02_odd_257x131.png")
    draw_scene(1, 300, 3, text=False).save(out / "s03_strip_1x300.png")
    draw_scene(400, 3, 4, text=False).save(out / "s04_strip_400x3.png")
    draw_scene(37, 23, 5, text=False).save(out / "s05_tiny_37x23.png")
    draw_scene(300, 300, 7).save(out / "s07_seam_300x300.png")
    # RGBA: anti-aliased (semi-transparent) shape edges, an alpha gradient, a fully transparent
    # area whose hidden RGB is loud noise (must not bleed into visible pixels).
    w, h = 320, 240
    rgb = np.asarray(draw_scene(w, h, 6)).copy()
    rng = np.random.default_rng(66)
    big = Image.new("L", (w * 4, h * 4), 0)
    d = ImageDraw.Draw(big)
    d.ellipse([40 * 4, 30 * 4, 200 * 4, 200 * 4], fill=255)
    d.rectangle([150 * 4, 120 * 4, 300 * 4, 220 * 4], fill=255)
    d.polygon([(230 * 4, 10 * 4), (310 * 4, 100 * 4), (210 * 4, 110 * 4)], fill=255)
    alpha = np.asarray(big.resize((w, h), Image.LANCZOS)).astype(np.float32)  # soft AA edges
    grad = np.tile(np.linspace(0, 255, 120, dtype=np.float32), (40, 1))  # alpha ramp 0..255
    alpha[195:235, 10:130] = np.maximum(alpha[195:235, 10:130], grad)
    alpha = np.clip(alpha, 0, 255).astype(np.uint8)
    noise = rng.integers(0, 256, rgb.shape, dtype=np.uint8)
    rgb[alpha == 0] = noise[alpha == 0]  # garbage colour under fully transparent pixels
    Image.fromarray(np.dstack([rgb, alpha]), "RGBA").save(out / "s06_alpha_320x240.png")
    print("wrote", sorted(p.name for p in out.glob("*.png")))


def full(path, w, h):
    draw_scene(w, h, 99).save(path)
    print("wrote", path, w, h)


# ---------------------------------------------------------------- comparison
def seam_mask(H, W, scale, tile, offset, blend):
    """Output pixels in a blending overlap (width = blend) of the upstream tile plan, +-blend."""
    io_ = math.ceil(offset / scale)
    ib = math.ceil(blend / scale)
    step_out = (tile - (2 * io_ + ib)) * scale
    m = np.zeros((H, W), bool)
    for k in range(1, H // step_out + 2):
        y0 = k * step_out
        m[max(0, y0 - blend):min(H, y0 + 2 * blend), :] = True  # overlap is [y0, y0+blend)
    for k in range(1, W // step_out + 2):
        x0 = k * step_out
        m[:, max(0, x0 - blend):min(W, x0 + 2 * blend)] = True
    return m


def compare(ours, ref, scale, tile, offset, blend, heat_png, tag, json_path):
    a, b = load_any(ours).astype(np.int16), load_any(ref).astype(np.int16)
    row = dict(tag=tag, ours=str(ours), ref=str(ref), shape_ours=list(a.shape), shape_ref=list(b.shape))
    if a.shape != b.shape:
        row["error"] = "shape mismatch"
        print(tag, "SHAPE MISMATCH", a.shape, b.shape)
    else:
        H, W, C = a.shape
        d = np.abs(a - b)
        dr = d[..., :3].max(axis=2)
        mse = float(((a[..., :3] - b[..., :3]).astype(np.float64) ** 2).mean())
        row.update(psnr_rgb_8bit=math.inf if mse == 0 else 10 * math.log10(255.0 ** 2 / mse),
                   max_rgb=int(dr.max()), pct_rgb_diff=float((dr > 0).mean() * 100))
        if C == 4:
            da = d[..., 3]
            msea = float(((a[..., 3] - b[..., 3]).astype(np.float64) ** 2).mean())
            row.update(psnr_alpha_8bit=math.inf if msea == 0 else 10 * math.log10(255.0 ** 2 / msea),
                       max_alpha=int(da.max()), pct_alpha_diff=float((da > 0).mean() * 100))
            dr = np.maximum(dr, da)
        seam = seam_mask(H, W, scale, tile, offset, blend)
        border = np.zeros((H, W), bool)
        e = 2 * offset
        border[:e, :] = border[-e:, :] = True
        border[:, :e] = border[:, -e:] = True
        interior = ~(seam | border)
        dist = {}
        for name, m in (("seam", seam & ~border), ("border", border), ("interior", interior)):
            n = int(m.sum())
            dist[name] = dict(pixels=n, max=int(dr[m].max()) if n else 0,
                              pct_diff=float((dr[m] > 0).mean() * 100) if n else 0.0)
        row["distribution"] = dist
        heat = np.clip(dr.astype(np.float32) * 255.0, 0, 255).astype(np.uint8)  # any diff -> white
        hm = np.dstack([heat, np.zeros_like(heat), np.zeros_like(heat)])
        hm[seam & (heat == 0)] = (0, 40, 0)  # seam bands tinted green for orientation
        Image.fromarray(hm, "RGB").save(heat_png)
        print(f"{tag:<44} PSNR {row['psnr_rgb_8bit']:7.2f} dB max {row['max_rgb']} diff {row['pct_rgb_diff']:.4f}% "
              + (f"| alpha PSNR {row['psnr_alpha_8bit']:.2f} max {row['max_alpha']} " if C == 4 else "")
              + f"| seam {dist['seam']['pct_diff']:.4f}% border {dist['border']['pct_diff']:.4f}% "
              f"interior {dist['interior']['pct_diff']:.4f}%")
    with open(json_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(row) + "\n")


def zoom(out, x, y, w, h, factor, imgs):
    tiles = []
    for p in imgs:
        a = load_any(p)
        crop = a[y:y + h, x:x + w]
        if crop.shape[2] == 4:
            yy, xx = np.mgrid[0:crop.shape[0], 0:crop.shape[1]]
            checker = np.where(((yy // 4 + xx // 4) % 2)[..., None] == 0, 200, 120).astype(np.float32)
            al = crop[..., 3:4].astype(np.float32) / 255.0
            comp = (crop[..., :3] * al + checker * (1 - al)).astype(np.uint8)
            tiles.append(comp)
            tiles.append(np.repeat(crop[..., 3:4], 3, axis=2))  # alpha view
        else:
            tiles.append(crop[..., :3])
    ims = [Image.fromarray(np.ascontiguousarray(t)).resize((t.shape[1] * factor, t.shape[0] * factor), Image.NEAREST)
           for t in tiles]
    W = sum(i.width for i in ims) + 4 * (len(ims) - 1)
    sheet = Image.new("RGB", (W, max(i.height for i in ims)), (255, 0, 255))
    xo = 0
    for i in ims:
        sheet.paste(i, (xo, 0))
        xo += i.width + 4
    sheet.save(out)
    print("wrote", out)


def info(paths):
    for p in paths:
        im = Image.open(p)
        print(Path(p).name, im.mode, im.size, "icc" if im.info.get("icc_profile") else "no-icc",
              "gamma=%s" % im.info.get("gamma"), "transparency" if "transparency" in im.info else "",
              "exif" if im.info.get("exif") else "")


def main():
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == "synth":
        synth(a[0])
    elif cmd == "full":
        full(a[0], int(a[1]), int(a[2]))
    elif cmd == "to-arx":
        write_arx(a[1], load_png(a[0]))
    elif cmd == "from-arx":
        x = read_arx(a[0])
        Image.fromarray(np.ascontiguousarray(x), "RGBA" if x.shape[2] == 4 else "RGB").save(a[1])
    elif cmd == "info":
        info(a)
    elif cmd == "compare":
        compare(a[0], a[1], int(a[2]), int(a[3]), int(a[4]), int(a[5]), a[6], a[7], a[8])
    elif cmd == "zoom":
        zoom(a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4]), int(a[5]), a[6:])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Turn art/cats.png (transparent kittens, made by tools/cutout_cats.py) into
  - src/core/cats_data.c : RGBA pixels for the loading-screen texture, and
                           the kittens' outlines (drawn in light while loading)
  - pkgfiles/ICON0.PNG   : 320x176 XMB icon with a transparent background
Transparent pixels get their color bled in from the nearest opaque pixel so
linear texture filtering never pulls dark fringes into the edges."""
import os
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "art", "cats.png")
TEX_W = 384


def bleed(img):
    a = np.asarray(img).astype(np.float32)
    alpha = a[..., 3]
    solid = alpha > 200
    _, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
    rgb = a[iy, ix, :3]
    keep = (alpha > 0)[..., None]
    out = np.where(keep, a[..., :3], rgb)
    return Image.fromarray(np.dstack([out, alpha]).round().clip(0, 255).astype(np.uint8), "RGBA")


def fit(img, w, h):
    s = min(w / img.width, h / img.height)
    return img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.LANCZOS)



# ---- 0.7.3: the kittens drawn in light. Outlines of the texture: the
# silhouette (alpha) and the main lines inside it (where black fur meets
# white fur, the eyes); simplified, longest first, at most MAX_PTS points.
from skimage import measure

MAX_PTS = 700


def _chaikin(c, n, closed):
    """corner cutting: rounder strokes"""
    for _ in range(n):
        p = c[:-1] if closed else c
        q = np.roll(p, -1, axis=0) if closed else p[1:]
        p0 = p if closed else p[:-1]
        a = 0.75 * p0 + 0.25 * q
        b = 0.25 * p0 + 0.75 * q
        m = np.empty((len(a) * 2, 2)); m[0::2] = a; m[1::2] = b
        if closed:
            c = np.vstack([m, m[:1]])
        else:
            c = np.vstack([c[:1], m, c[-1:]])
    return c


def _runs(c, ok, min_len):
    """split contour c into runs of points where ok is true"""
    out, cur = [], []
    for p, k in zip(c, ok):
        if k:
            cur.append(p)
        elif cur:
            out.append(np.array(cur)); cur = []
    if cur:
        out.append(np.array(cur))
    return [r for r in out if len(r) >= min_len]


def trace(tex):
    a = np.asarray(tex).astype(np.float32)
    alpha = a[..., 3] / 255.0
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    lum = (0.30 * r + 0.59 * g + 0.11 * b) / 255.0
    h, w = alpha.shape
    inner = ndimage.binary_erosion(alpha > 0.9, iterations=5)

    def at(m, c):
        ys = np.clip(np.round(c[:, 0]).astype(int), 0, h - 1)
        xs = np.clip(np.round(c[:, 1]).astype(int), 0, w - 1)
        return m[ys, xs]

    lines = []
    # 0: the silhouette
    for c in measure.find_contours(ndimage.gaussian_filter(alpha, 1.8), 0.5):
        lines.append((0, c))
    # 1: where the white kitten meets the black one (inside the picture only)
    white = ndimage.binary_opening(lum > 0.42, iterations=2)
    blue = (b > r + 45) & (b > g + 20) & (alpha > 0.5)
    blue = ndimage.binary_closing(blue, iterations=2)
    near_eye = ndimage.binary_dilation(blue, iterations=7)    # the eye whites are not fur edges
    for c in measure.find_contours(ndimage.gaussian_filter(white.astype(np.float32), 2.2), 0.5):
        for run in _runs(c, at(inner & ~near_eye, c), 12):
            lines.append((1, run))
    # 2: the eyes (the blue irises), as ovals
    lb, nb = ndimage.label(blue)
    for rp in measure.regionprops(lb):
        if rp.area < 25:
            continue
        cy, cx = rp.centroid
        ry, rx = rp.axis_major_length * 0.5, rp.axis_minor_length * 0.5
        th = rp.orientation
        t = np.linspace(0, 2 * np.pi, 17)
        # skimage: orientation is the angle between the row axis and the major axis
        ys = cy + ry * np.cos(t) * np.cos(th) - rx * np.sin(t) * np.sin(th)
        xs = cx + ry * np.cos(t) * np.sin(th) + rx * np.sin(t) * np.cos(th)
        lines.append((2, np.stack([ys, xs], axis=1)))
    out = []
    for kind, c in lines:
        if kind != 2:
            c = measure.approximate_polygon(c, tolerance=0.8 if kind else 1.1)
            c = _chaikin(c, 2, closed=bool(np.allclose(c[0], c[-1])))
            c = measure.approximate_polygon(c, tolerance=0.35)
        if len(c) < 3:
            continue
        length = np.sqrt((np.diff(c, axis=0) ** 2).sum(axis=1)).sum()
        if length < (60, 25, 18)[kind]:
            continue
        out.append((kind, length, c))
    out.sort(key=lambda t: (t[0], -t[1]))
    kept, total = [], 0
    for kind, length, c in out:
        if total + len(c) > MAX_PTS:
            continue
        kept.append((kind, c))
        total += len(c)
    return kept


def main():
    src = Image.open(SRC).convert("RGBA")
    src = bleed(src)

    # loading-screen texture
    tex = fit(src, TEX_W - 4, 10000)
    # 2 px transparent margin so texture filtering at the edges never picks
    # up the opposite edge
    padded = Image.new("RGBA", (tex.width + 4, tex.height + 4), (0, 0, 0, 0))
    padded.paste(tex, (2, 2))
    tex = bleed(padded)
    data = tex.tobytes()
    # 0.7.3: the outlines for the kittens drawn in light
    lines = trace(tex)
    out = os.path.join(ROOT, "src", "core", "cats_data.c")
    with open(out, "w") as f:
        f.write("/* cats_data.c - GENERATED by tools/gen_cats.py from art/cats.png. Do not edit. */\n")
        f.write('#include "cats.h"\n\n')
        f.write("const int g_cats_w = %d;\nconst int g_cats_h = %d;\n" % tex.size)
        f.write("const unsigned char g_cats_rgba[%d] = {\n" % len(data))
        for i in range(0, len(data), 32):
            f.write(",".join(str(b) for b in data[i:i + 32]) + ",\n")
        f.write("};\n")
        # outlines: texture coordinates 0..1 (u, v); kind 0 silhouette, 1 fur edge, 2 eye
        pts = [p for _, c in lines for p in c]
        f.write("const int g_kitline_count = %d;\n" % len(lines))
        f.write("const int g_kitline_start[%d] = { %s };\n" % (len(lines) + 1, ", ".join(
            str(v) for v in np.cumsum([0] + [len(c) for _, c in lines]))))
        f.write("const unsigned char g_kitline_kind[%d] = { %s };\n" % (len(lines), ", ".join(str(k) for k, _ in lines)))
        f.write("const float g_kitline_uv[%d][2] = {\n" % len(pts))
        for y, x in pts:
            f.write("    { %.4ff, %.4ff },\n" % (x / tex.width, y / tex.height))
        f.write("};\n")
    print("wrote", out, tex.size, len(data), "bytes,", len(lines), "outlines,", sum(len(c) for _, c in lines), "points")

    # XMB icon: 320x176, transparent, kittens centered with a small margin
    icon = Image.new("RGBA", (320, 176), (0, 0, 0, 0))
    k = fit(src, 312, 168)
    icon.alpha_composite(k, ((320 - k.width) // 2, (176 - k.height) // 2))
    ipath = os.path.join(ROOT, "pkgfiles", "ICON0.PNG")
    icon.save(ipath)
    print("wrote", ipath, k.size)


if __name__ == "__main__":
    main()

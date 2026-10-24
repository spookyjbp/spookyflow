#!/usr/bin/env python3
"""Generate src/core/icons_data.c: button icons drawn like a DualShock 3,
rendered at 2x and downsampled.

  0 cross    - black round button, blue X
  1 triangle - black round button, green outline triangle (optically lowered)
  2 circle   - black round button, red ring
  3 start    - black right-pointing arrow button, like the DualShock 3's START
  4..7 L1 R1 L2 R2 - black rounded shoulder buttons, white label
  8 select   - black flat rounded-rectangle button, like the DualShock 3's SELECT
Every button has the same near-black body and the same thin gray rim, so they
read as one family and stay visible on the dark UI.
Stored as planar R,G,B,A in one RLE stream (same codec as the fonts), with a
rect table (x, width) per icon; height is the atlas height. RGB is extended
under transparent pixels so bilinear filtering never produces dark fringes.
"""
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_font import rle, unrle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = 64          # final badge size (px)
SS = 2            # supersampling factor
S = OUT * SS

BODY = (0x17, 0x17, 0x1A)       # button plastic
RIM = (0x5A, 0x5A, 0x63)        # thin edge so black buttons read on the dark bar
SYMBOL = {                       # DualShock 3 symbol colors
    "cross":    (0x7F, 0xB2, 0xF0),
    "triangle": (0x3C, 0xD0, 0xA0),
    "circle":   (0xF2, 0x5C, 0x6A),
}
BADGES = ["cross", "triangle", "circle", "start", "L1", "R1", "L2", "R2", "select"]
SHOULDER_W = 104   # final px width of shoulder buttons (height = OUT)
START_W = 150      # final px width of the START pill
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def pill(label, width, font_frac):
    WS = width * SS
    pad = 2 * SS
    rim = round(S * 0.045)
    rgb = Image.new("RGB", (WS, S), RIM)
    d = ImageDraw.Draw(rgb)
    d.rounded_rectangle([pad + rim, pad + rim, WS - pad - 1 - rim, S - pad - 1 - rim],
                        radius=int(S * 0.30) - rim, fill=BODY)
    font = ImageFont.truetype(FONT_BOLD, int(S * font_frac))
    d.text((WS / 2, S / 2 + S * 0.01), label, font=font, fill=(255, 255, 255), anchor="mm")
    alpha = Image.new("L", (WS, S), 0)
    ImageDraw.Draw(alpha).rounded_rectangle([pad, pad, WS - pad - 1, S - pad - 1], radius=int(S * 0.30), fill=255)
    return rgb.resize((width, OUT), Image.LANCZOS), alpha.resize((width, OUT), Image.LANCZOS)

def rounded_tri(draw, pts, r, fill):
    """filled triangle with rounded corners of radius r (pts = outer shape)"""
    cx = sum(p[0] for p in pts) / 3.0
    cy = sum(p[1] for p in pts) / 3.0
    inner = []
    for (x, y) in pts:          # pull each corner toward the center by ~2r
        dx, dy = cx - x, cy - y
        d = math.hypot(dx, dy)
        inner.append((x + dx / d * 2.0 * r, y + dy / d * 2.0 * r))
    draw.polygon(inner, fill=fill)
    draw.line(inner + [inner[0]], fill=fill, width=int(2 * r), joint="curve")
    for (x, y) in inner:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def start_button():
    """DualShock 3 START: a small black right-pointing arrow-shaped button"""
    pad = 2 * SS
    rim = round(S * 0.045)
    outer = [(S * 0.20, pad + S * 0.06), (S * 0.20, S - pad - S * 0.06), (S - pad - S * 0.04, S * 0.5)]
    alpha = Image.new("L", (S, S), 0)
    rounded_tri(ImageDraw.Draw(alpha), outer, S * 0.08, 255)
    rgb = Image.new("RGB", (S, S), RIM)
    body = Image.new("L", (S, S), 0)
    rounded_tri(ImageDraw.Draw(body), outer, S * 0.08, 255)
    from PIL import ImageFilter
    body = body.filter(ImageFilter.MinFilter(2 * rim + 1))      # inset by the rim width
    rgb.paste(Image.new("RGB", (S, S), BODY), (0, 0), body)
    return rgb.resize((OUT, OUT), Image.LANCZOS), alpha.resize((OUT, OUT), Image.LANCZOS)


def select_button():
    """DualShock 3 SELECT: a small flat black rounded rectangle, wider than tall"""
    rim = round(S * 0.045)
    x0, x1, y0, y1 = S * 0.10, S * 0.90, S * 0.29, S * 0.71
    rad = (y1 - y0) * 0.42
    alpha = Image.new("L", (S, S), 0)
    ImageDraw.Draw(alpha).rounded_rectangle([x0, y0, x1, y1], radius=rad, fill=255)
    rgb = Image.new("RGB", (S, S), RIM)
    ImageDraw.Draw(rgb).rounded_rectangle([x0 + rim, y0 + rim, x1 - rim, y1 - rim], radius=rad - rim, fill=BODY)
    return rgb.resize((OUT, OUT), Image.LANCZOS), alpha.resize((OUT, OUT), Image.LANCZOS)


def badge(kind):
    pad = 2 * SS
    stroke = round(S * 0.085)
    rim = round(S * 0.045)
    rgb = Image.new("RGB", (S, S), RIM)
    d = ImageDraw.Draw(rgb)
    d.ellipse([pad + rim, pad + rim, S - pad - 1 - rim, S - pad - 1 - rim], fill=BODY)
    white = SYMBOL[kind]
    c = S / 2
    if kind == "cross":
        k = S * 0.20
        for a, b in (((c - k, c - k), (c + k, c + k)), ((c - k, c + k), (c + k, c - k))):
            d.line([a, b], fill=white, width=stroke)
        for p in ((c - k, c - k), (c + k, c + k), (c - k, c + k), (c + k, c - k)):
            r = stroke / 2
            d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=white)
    elif kind == "triangle":
        R = S * 0.235                   # circumradius
        cy = c + S * 0.035              # optical correction: sit a little low
        pts = [(c + R * math.sin(a), cy - R * math.cos(a)) for a in (0, 2 * math.pi / 3, 4 * math.pi / 3)]
        d.line(pts + [pts[0]], fill=white, width=stroke, joint="curve")
        for p in pts:
            r = stroke / 2
            d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=white)
    elif kind == "circle":
        r = S * 0.215
        d.ellipse([c - r, c - r, c + r, c + r], outline=white, width=stroke)
    alpha = Image.new("L", (S, S), 0)
    ImageDraw.Draw(alpha).ellipse([pad, pad, S - pad - 1, S - pad - 1], fill=255)
    rgb = rgb.resize((OUT, OUT), Image.LANCZOS)
    alpha = alpha.resize((OUT, OUT), Image.LANCZOS)
    return rgb, alpha

def main():
    imgs = []
    for kind in BADGES:
        if kind == "start":
            imgs.append(start_button())
        elif kind == "select":
            imgs.append(select_button())
        elif kind[0] in "LR":
            imgs.append(pill(kind, SHOULDER_W, 0.42))
        else:
            imgs.append(badge(kind))
    gap = 2
    total = sum(im[0].size[0] + gap for im in imgs)
    W = 1
    while W < total:
        W *= 2
    H = OUT
    rgb_all = Image.new("RGB", (W, H), RIM)
    a_all = Image.new("L", (W, H), 0)
    rects = []
    x = 0
    for rgb, a in imgs:
        rgb_all.paste(rgb, (x, 0))
        a_all.paste(a, (x, 0))
        rects.append((x, rgb.size[0]))
        x += rgb.size[0] + gap
    r, g, b = rgb_all.split()
    planar = r.tobytes() + g.tobytes() + b.tobytes() + a_all.tobytes()
    comp = rle(planar)
    assert unrle(comp, len(planar)) == planar
    preview = rgb_all.convert("RGBA")
    preview.putalpha(a_all)
    preview.save(os.path.join(ROOT, "tools", "atlas_icons.png"))
    L = ["/* GENERATED by tools/gen_icons.py - do not edit. */",
         '#include "icons.h"', "",
         "static const unsigned char icons_rle[%d] = {" % len(comp)]
    for i in range(0, len(comp), 24):
        L.append("  " + ",".join(str(x) for x in comp[i:i + 24]) + ",")
    L.append("};")
    L.append("static const IconRect icons_rects[%d] = {" % len(rects))
    for x0, w in rects:
        L.append("  {%d, %d}," % (x0, w))
    L.append("};")
    L.append("const IconAtlas g_icons = { %d, %d, %d, icons_rects, icons_rle, %d };" % (W, H, len(BADGES), len(comp)))
    with open(os.path.join(ROOT, "src", "core", "icons_data.c"), "w") as f:
        f.write("\n".join(L) + "\n")
    print("icons: %dx%d, rle %d bytes (raw %d)" % (W, H, len(comp), len(planar)))

if __name__ == "__main__":
    main()

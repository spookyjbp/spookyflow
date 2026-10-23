#!/usr/bin/env python3
"""make_stage.py - build the Stage background assets from the asset folder
(Background/, Ribbons/, Lighting/, Particles/, Atmosphere/, Symbols/).
Needs Pillow + numpy.

  art/stage.jpg         1280x720 base scene, brightness only (tinted by the app)
  art/stage_rib.jpg     1024x904 light ribbons (left | right; bodies over white-hot cores)
  art/stage_glow.jpg    512x512 glow halos: scene (top), left | right ribbons (bottom)
  art/stage_fx.png      1024x1024 brightness atlas (glows, mist, particles) + art/stage_fx.json
  art/stage_shapes.png  1024x896 RGBA: 4 shapes x 2 angles x (glass body, white-hot edges)

Everything becomes brightness (premultiplied by the asset's alpha, so dark
cut-out edges and leftover checkerboard fade away) so the app can tint it
with the UI color.

usage: python3 tools/make_stage.py <asset folder> <base picture> <particle png folder> <base picture with ribbons>
       then: python3 tools/gen_stage.py
"""
import os, sys, json
import numpy as np
from PIL import Image, ImageFilter

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = sys.argv[1]
P = lambda *a: os.path.join(src, *a)

def rgba(path):
    return np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)

def bright(px):
    """premultiplied brightness; low-saturation mid grays (checkerboard left by
    the color key) are suppressed, white-hot cores are kept"""
    rgb, a = px[..., :3], px[..., 3] / 255.0
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1), 0)
    keep = np.clip((sat - 0.15) / 0.25, 0, 1)
    keep = np.maximum(keep, np.clip((mx - 215) / 30, 0, 1))
    return mx * a * keep

def whiteness(px):
    rgb, a = px[..., :3], px[..., 3] / 255.0
    return rgb.min(axis=2) * a

def feather(v, px):
    h, w = v.shape
    yy, xx = np.mgrid[0:h, 0:w]
    e = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)).astype(np.float32)
    return v * np.clip(e / px, 0, 1)

# 1. base scene
base_src = sys.argv[2] if len(sys.argv) > 2 else P('Background', 'base_stage_1920x1080.png')   # optional replacement scene
_rgb = np.asarray(Image.open(base_src).convert('RGB')).astype(np.float32)
# brightness: the strongest channel with a gamma (colored glow stays mid-bright),
# lifted toward white only where the picture itself is white-hot (all channels high)
_mx, _mn = _rgb.max(axis=2), _rgb.min(axis=2)
base = 0.78 * 255.0 * np.clip((_mx - 6.0) / 249.0, 0, 1) ** 1.6 + 0.22 * _mn
base = np.clip(base, 0, 255)
Image.fromarray(base.astype(np.uint8), 'L').resize((1280, 720), Image.LANCZOS).save(
    os.path.join(root, 'art', 'stage.jpg'), quality=92, optimize=True)

# 1b. ribbons: the same scene with its light ribbons (fourth argument) minus the
# plain scene = just the ribbons. Split into a left and a right wave set
# (cross-fading over the middle fifth) so each can sway on its own.
# art/stage_rib.jpg 1024x904: top = left | right bodies, bottom = their white-hot cores.
rib_src = sys.argv[4] if len(sys.argv) > 4 else None
if rib_src:
    _a = np.asarray(Image.open(rib_src).convert('RGB').resize(Image.open(base_src).size, Image.LANCZOS)).astype(np.float32)
    body = np.clip(_a - _rgb, 0, 255).max(axis=2)
    body = 255.0 * np.clip(body / 235.0, 0, 1) ** 1.15
    hot = 255.0 * np.clip((_a.min(axis=2) - _rgb.min(axis=2) - 70.0) / 130.0, 0, 1) ** 1.2
    H0, W0 = body.shape
    sky = int(round(H0 * 0.627))                        # LY_HORIZON_Y
    fade = np.clip((sky - np.arange(H0)) / (H0 * 0.03), 0, 1)[:, None]
    body, hot = (body * fade)[:sky], (hot * fade)[:sky]
    xs = np.arange(W0) / W0
    wl = np.clip((0.6 - xs) / 0.2, 0, 1)[None, :]
    wr = 1.0 - wl
    out = np.zeros((904, 1024), np.float32)
    for k, m in enumerate((body, hot)):
        for side, (wgt, x0) in enumerate(((wl, 0.0), (wr, 0.4))):
            part = (m * wgt)[:, int(x0 * W0):int((x0 + 0.6) * W0)]
            im = Image.fromarray(np.clip(part, 0, 255).astype(np.uint8), 'L').resize((512, 452), Image.LANCZOS)
            out[k * 452:(k + 1) * 452, side * 512:(side + 1) * 512] = np.asarray(im)
    Image.fromarray(out.astype(np.uint8), 'L').save(os.path.join(root, 'art', 'stage_rib.jpg'), quality=92, optimize=True)

    # 1c. glow (bloom): soft halos around everything bright. art/stage_glow.jpg 512x512:
    # rows 0..287 = the scene (whole screen), rows 288..511 = left | right ribbon sets
    def bloom(v, scale):
        b = 255.0 * np.clip((v - 105.0) / 150.0, 0, 1) ** 1.5
        im = Image.fromarray(b.astype(np.uint8), 'L')
        acc = np.zeros(b.shape, np.float32)
        for r, wt in ((6, 0.45), (18, 0.35), (48, 0.30)):
            acc += wt * np.asarray(im.filter(ImageFilter.GaussianBlur(r * scale))).astype(np.float32)
        return 255.0 * np.clip(acc / max(acc.max(), 1.0), 0, 1) ** 0.8
    glow = np.zeros((512, 512), np.float32)
    scene = np.asarray(Image.open(os.path.join(root, 'art', 'stage.jpg')).convert('L')).astype(np.float32)
    glow[:288] = np.asarray(Image.fromarray(bloom(scene, 1.0).astype(np.uint8)).resize((512, 288), Image.LANCZOS))
    ribs = np.maximum(out[:452], out[452:])
    for side in range(2):
        g = bloom(ribs[:, side * 512:(side + 1) * 512], 0.8)
        glow[288:, side * 256:(side + 1) * 256] = np.asarray(Image.fromarray(g.astype(np.uint8)).resize((256, 224), Image.LANCZOS))
    Image.fromarray(glow.astype(np.uint8), 'L').save(os.path.join(root, 'art', 'stage_glow.jpg'), quality=92, optimize=True)

# 2. effects atlas (brightness), packed in rows
elems = [
    ('glow',     P('Lighting', 'glow_01.png')),
    ('line',     P('Lighting', 'glow_04.png')),
    ('star',     P('Lighting', 'glow_05.png')),
]
# particles: single sprites from the XMB particle PNGs (third argument: their
# folder), each animated on its own by the app. Those files were cut out of a
# 4x4 sheet off-grid, so the columns are stacked back together first.
# DOTS = (column, y, x, w, h) in the stacked columns: 8 bright sparks, 5 soft
# glow dots, 4 bokeh orbs -> a 16 x 2 grid of 64x64 cells ('dots').
DOTS = [
    (0, 830, 211, 11, 13), (0, 843, 260, 10, 11), (3, 37, 223, 10, 11), (2, 124, 163, 9, 10),
    (0, 545, 149, 9, 8), (2, 552, 323, 9, 9), (0, 416, 305, 8, 8), (1, 424, 74, 8, 8),
    (1, 108, 260, 14, 15), (1, 9, 264, 10, 11), (2, 179, 120, 11, 10), (2, 176, 188, 9, 10), (3, 313, 234, 9, 10),
    (3, 802, 325, 42, 42), (2, 841, 104, 19, 19), (2, 826, 172, 14, 15), (3, 834, 23, 15, 15),
]
COMET = (3, 605, 274, 124, 26)          # one light streak, head to the right
part_dir = sys.argv[3] if len(sys.argv) > 3 else None
def part_cols():
    import glob
    fs = sorted(glob.glob(os.path.join(part_dir, '*.png')))
    return [np.concatenate([rgba(fs[r * 4 + c]) for r in range(4)], 0) for c in range(4)]
def dots_grid():
    cols = part_cols()
    out = np.zeros((128, 1024, 4), np.float32)
    for n, (c, y, x, w, h) in enumerate(DOTS):
        pad = max(3, (w + h) // 6)
        crop = cols[c][max(0, y - pad):y + h + pad, max(0, x - pad):x + w + pad]
        im = Image.fromarray(crop.astype(np.uint8), 'RGBA')
        s = 56.0 / max(im.size)
        im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
        cell = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
        cell.paste(im, ((64 - im.width) // 2, (64 - im.height) // 2))
        out[(n // 16) * 64:(n // 16 + 1) * 64, (n % 16) * 64:(n % 16 + 1) * 64] = np.asarray(cell)
    return out
def comet():
    c, y, x, w, h = COMET
    return part_cols()[c][y - 4:y + h + 4, x - 4:x + w + 4]
AW, AH = 1024, 1024
atlas = np.zeros((AH, AW), np.float32)
rects = {}
x = y = rowh = 0
def smooth(v, close=3, blur=1.4, floor=14.0):
    """soft glows only: close dark seams, melt the checkerboard, drop the haze"""
    im = Image.fromarray(np.clip(v, 0, 255).astype(np.uint8), 'L')
    if close >= 3: im = im.filter(ImageFilter.MaxFilter(close))
    im = im.filter(ImageFilter.GaussianBlur(blur))
    return np.clip((np.asarray(im).astype(np.float32) - floor) * 255.0 / (255.0 - floor), 0, 255)

def fog(w=1024, h=256, seed=7):
    """seamless (wraps left-right) soft fog: periodic fractal noise, stretched
    sideways into wisps, denser in the middle band and fading out above and
    below. Returned as an RGBA-like array (white, alpha = density)."""
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :] * 3.0           # x3: wisps 3x wider than tall
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0
    spec = np.fft.fft2(rng.standard_normal((h, w))) / f ** 1.8
    spec[np.sqrt(fx * fx + fy * fy) < 7.5 / w] = 0    # no single tile-wide blob
    spec[0, 0] = 0
    spec *= np.exp(-(f / 0.06) ** 2)                # soft: no fine grain
    n = np.real(np.fft.ifft2(spec))
    n = (n - n.mean()) / n.std()
    d = np.clip((n + 1.1) / 3.4, 0, 1) ** 1.5      # soft wisps, thin between them
    yy = (np.arange(h) / (h - 1))[:, None]
    env = np.exp(-((yy - 0.55) / 0.22) ** 2)         # thickest a little below the middle
    a = 255.0 * d * env
    return np.dstack([np.full_like(a, 255.0)] * 3 + [a])

srcs = [(n, rgba(p)) for n, p in elems] + [('dots', dots_grid()), ('comet', comet()), ('mist', fog())]
for name, px in srcs:
    v = bright(px)
    if name in ('glow', 'line', 'star'):           # no checkerboard here: keep the white cores
        v = px[..., :3].max(axis=2) * px[..., 3] / 255.0
    if name not in ('mist', 'dots'):                 # fog wraps left-right; dot cells are already padded
        v = feather(v, 4)
    h, w = v.shape
    if x + w > AW: x = 0; y += rowh + 4; rowh = 0
    atlas[y:y + h, x:x + w] = v
    rects[name] = (x, y, w, h)
    x += w + 4
    rowh = max(rowh, h)
assert y + rowh <= AH, 'atlas too small'
Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8), 'L').save(os.path.join(root, 'art', 'stage_fx.png'), optimize=True)
json.dump({'size': [AW, AH], 'rects': rects}, open(os.path.join(root, 'art', 'stage_fx.json'), 'w'), indent=1)

# 3. symbols: 4 shapes x 2 angles, each as glass body + white-hot edges
CW, CH = 256, 224
kinds = ['triangle', 'circle', 'cross', 'square']
angles = ['01', '04']
shapes = np.zeros((CH * 4, CW * 4, 4), np.uint8)
for k, kind in enumerate(kinds):
    for vi, ang in enumerate(angles):
        px = rgba(P('Symbols', '%s_angle_%s.png' % (kind, ang)))
        body = feather(bright(px), 3)
        hot = feather(255.0 * np.clip((whiteness(px) - 150.0) / 90.0, 0, 1) ** 1.3, 3)
        for layer, (rgb, a) in enumerate(((np.clip(body * 1.1, 0, 255), np.clip(body * 1.6, 0, 255)),
                                         (np.full_like(hot, 255.0), hot))):
            im = Image.fromarray(np.dstack([rgb, rgb, rgb, a]).astype(np.uint8), 'RGBA')
            im.thumbnail((CW - 4, CH - 4), Image.LANCZOS)
            cell = Image.new('RGBA', (CW, CH), (0, 0, 0, 0))
            cell.paste(im, ((CW - im.width) // 2, (CH - im.height) // 2))
            row = vi * 2 + layer
            shapes[row * CH:(row + 1) * CH, k * CW:(k + 1) * CW] = np.asarray(cell)
Image.fromarray(shapes, 'RGBA').save(os.path.join(root, 'art', 'stage_shapes.png'), optimize=True)
print('wrote art/stage.jpg, art/stage_fx.png (+ .json), art/stage_shapes.png')

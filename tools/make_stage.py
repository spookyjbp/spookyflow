#!/usr/bin/env python3
"""make_stage.py - build the Stage background assets from the asset folder
(Background/, Ribbons/, Lighting/, Particles/, Atmosphere/, Symbols/).
Needs Pillow + numpy.

  art/stage.jpg         1280x720 base scene, brightness only (tinted by the app)
  art/stage_fx.png      1024x1024 brightness atlas (ribbons, glows, stars, mist) + art/stage_fx.json
  art/stage_shapes.png  1024x896 RGBA: 4 shapes x 2 angles x (glass body, white-hot edges)

Everything becomes brightness (premultiplied by the asset's alpha, so dark
cut-out edges and leftover checkerboard fade away) so the app can tint it
with the UI color.

usage: python3 tools/make_stage.py <asset folder>   then: python3 tools/gen_stage.py
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

# 2. effects atlas (brightness), packed in rows
elems = [
    ('ribbon_a', P('Ribbons', 'large_wave_01.png')),
    ('ribbon_b', P('Ribbons', 'large_wave_02.png')),
    ('ribbon_c', P('Ribbons', 'large_wave_03.png')),
    ('ribbon_d', P('Ribbons', 'large_wave_04.png')),
    ('glow',     P('Lighting', 'glow_01.png')),
    ('line',     P('Lighting', 'glow_04.png')),
    ('star',     P('Lighting', 'glow_05.png')),
    ('stars_a',  P('Particles', 'particles_01.png')),
    ('stars_b',  P('Particles', 'particles_05.png')),
    ('mist',     P('Atmosphere', 'mist_01.png')),
]
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

for name, path in elems:
    v = bright(rgba(path))
    if name.startswith('ribbon') or name == 'mist':
        v = smooth(v)
    elif name.startswith('stars'):
        v = np.clip((v - 40.0) * 1.6, 0, 255)       # only the points of light, no cloudy haze
    v = feather(v, 12 if name.startswith("ribbon") else 4)
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

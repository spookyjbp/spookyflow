#!/usr/bin/env python3
"""make_stage.py - rebuild art/stage.jpg and art/stage_shapes.png from the
source picture (needs opencv-python and Pillow). The button shapes are
painted out of the picture (so the app can float its own copies) and cut
out as additive-light sprites.

usage: python3 tools/make_stage.py <source picture>   then: python3 tools/gen_stage.py
"""
import os, sys
import cv2, numpy as np
from PIL import Image

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = cv2.imread(sys.argv[1])
H, W = src.shape[:2]
# shape boxes in the 1672x941 source (x0, y0, x1, y1)
boxes = [(25,0,125,82),(148,126,214,197),(293,198,374,274),(10,246,150,362),(220,308,302,399),(360,383,402,427),
         (498,381,562,440),(538,326,594,374),(648,358,688,394),(355,45,405,100),(118,372,165,412),
         (1446,26,1492,84),(1482,116,1604,230),(1236,198,1342,304),(1513,280,1642,388),(1348,358,1402,414),
         (1223,406,1287,470),(1138,470,1174,502),(1098,303,1142,347),(1088,373,1122,402),(1000,310,1035,345)]
mask = np.zeros((H, W), np.uint8)
for (x0, y0, x1, y1) in boxes:
    cv2.rectangle(mask, (x0 - 5, y0 - 5), (x1 + 5, y1 + 5), 255, -1)
clean = cv2.inpaint(src, mask, 15, cv2.INPAINT_TELEA)
blur = cv2.GaussianBlur(clean, (0, 0), 9)
soft = cv2.GaussianBlur(mask.astype(np.float32) / 255.0, (0, 0), 5)[..., None]
clean = (clean * (1 - soft) + blur * soft).astype(np.uint8)

pick = [(10,246,150,362), (1482,116,1604,230), (1513,280,1642,388), (220,308,302,399)]   # cross circle triangle square
S = 128
atlas = np.zeros((S, S * 4, 4), np.uint8)
for i, (x0, y0, x1, y1) in enumerate(pick):
    x0 -= 4; y0 -= 4; x1 += 4; y1 += 4
    o = src[y0:y1, x0:x1].astype(np.float32); c = clean[y0:y1, x0:x1].astype(np.float32)
    d = np.clip(o - c - 4.0, 0, 255)
    a = d.max(axis=2)
    h, w = a.shape
    yy, xx = np.mgrid[0:h, 0:w]
    edge = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)).astype(np.float32)
    a = np.clip(a * np.clip(edge / 5.0, 0, 1) * 1.9, 0, 255)
    col = np.where(a[..., None] > 0, np.clip(d * 2.2 * 255.0 / np.maximum(a[..., None], 1), 0, 255), 0)   # brighter: the app blends, it does not add light
    im = Image.fromarray(np.dstack([col[..., 2], col[..., 1], col[..., 0], a]).astype(np.uint8), 'RGBA')
    sc = (S - 6) / max(w, h)
    im = im.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.LANCZOS)
    cell = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    cell.paste(im, ((S - im.width) // 2, (S - im.height) // 2))
    atlas[:, i * S:(i + 1) * S] = np.array(cell)
# shapes become white glass (brightness only) so the app can tint them with the UI color
rgb = atlas[..., :3].astype(np.float32)
v = rgb.max(axis=2, keepdims=True)
atlas[..., :3] = np.clip(v * 1.15, 0, 255).astype(np.uint8)
Image.fromarray(atlas, 'RGBA').save(os.path.join(root, 'art', 'stage_shapes.png'), optimize=True)
# the background is stored as brightness (the max of R, G, B) so the app can
# tint it with the UI color; the white-hot cores are recovered from it at load
v = clean.max(axis=2)
bg = Image.fromarray(v, 'L')
ch = W * 9 // 16
bg = bg.crop((0, (H - ch) // 2, W, (H - ch) // 2 + ch)).resize((1280, 720), Image.LANCZOS)
bg.save(os.path.join(root, 'art', 'stage.jpg'), quality=92, optimize=True)
print('wrote art/stage.jpg and art/stage_shapes.png')

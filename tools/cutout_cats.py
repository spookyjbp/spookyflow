"""Cut the two kittens out of art/cats_source.png (dark navy background) and
write art/cats.png with a transparent background.

Background pixels are dark and clearly blue (B well above R and G); the black
kitten is dark but neutral, so blueness separates them. Only background that
is connected to the image border is removed, so the kittens' blue eyes stay.
Edge pixels get soft alpha, and the navy is un-mixed from their color so no
blue fringe shows on other backgrounds."""
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

src = sys.argv[1] if len(sys.argv) > 1 else "art/cats_source.png"
dst = sys.argv[2] if len(sys.argv) > 2 else "art/cats.png"

im = np.asarray(Image.open(src).convert("RGB")).astype(np.float32)
R, G, B = im[..., 0], im[..., 1], im[..., 2]
mx = np.maximum(R, G)
blue = (B - mx) / np.maximum(B, 1.0)          # ~0.5 on the navy, <0.3 on fur
lum = 0.299 * R + 0.587 * G + 0.114 * B

cand = (blue > 0.34) & (lum < 90)
lab, n = ndimage.label(cand)
border = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
border = border[border > 0]
bg = np.isin(lab, border)
# enclosed pockets of navy between the kittens (not touching the border).
# They look like the background: mean blue 20..50, dim. The eyes are much
# brighter blue (mean B > 60) and black fur is nearly neutral (mean B < 15).
for i, sl in enumerate(ndimage.find_objects(lab), 1):
    if sl is None or i in border:
        continue
    m = lab[sl] == i
    if m.sum() < 100:
        continue
    px = im[sl][m]
    mr, mg, mb = px.mean(0)
    if 20.0 <= mb <= 50.0 and mr < 12.0 and (0.299 * px[:, 0] + 0.587 * px[:, 1] + 0.114 * px[:, 2]).max() < 45.0:
        bg[sl] |= m
bg = ndimage.binary_opening(bg, iterations=1)
bg[:3, :] = True; bg[-3:, :] = True; bg[:, :3] = True; bg[:, -3:] = True   # opening eats the corners

# soft edge in a thin band around the cut line
fg = ~bg
inner = ndimage.binary_erosion(fg, iterations=2)
band = fg & ~inner
soft = np.clip((0.46 - blue) / (0.46 - 0.26), 0.0, 1.0)
alpha = np.where(bg, 0.0, 1.0)
alpha = np.where(band, np.minimum(alpha, np.maximum(soft, 0.15)), alpha)
alpha = ndimage.gaussian_filter(alpha, 0.6)
alpha = np.where(inner, 1.0, alpha)

# local background color (normalized convolution over bg pixels)
w = bg.astype(np.float32)
bgc = np.zeros_like(im)
den = ndimage.gaussian_filter(w, 25) + 1e-6
for c in range(3):
    bgc[..., c] = ndimage.gaussian_filter(im[..., c] * w, 25) / den

a3 = np.clip(alpha, 1e-3, 1.0)[..., None]
un = (im - (1.0 - a3) * bgc) / a3
rgb = np.where((alpha < 0.999)[..., None], np.clip(un, 0, 255), im)

out = np.dstack([rgb, alpha * 255.0]).round().clip(0, 255).astype(np.uint8)
img = Image.fromarray(out, "RGBA")
img = img.crop(img.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox())
img.save(dst)
print("wrote", dst, img.size)

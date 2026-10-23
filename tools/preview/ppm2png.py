#!/usr/bin/env python3
"""Convert every .ppm in a folder to .png and delete the .ppm."""
import os
import sys
from PIL import Image

folder = sys.argv[1] if len(sys.argv) > 1 else "preview"
for name in sorted(os.listdir(folder)):
    if name.endswith(".ppm"):
        src = os.path.join(folder, name)
        Image.open(src).save(src[:-4] + ".png")
        os.remove(src)
        print("png:", src[:-4] + ".png")

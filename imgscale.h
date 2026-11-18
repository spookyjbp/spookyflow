/* imgscale.h - high-quality RGBA downscaling for small UI images (tab logos).
 * The GPU's bilinear filter skips source pixels when an image is shown much
 * smaller than it is, which makes thin lines jagged; shrinking once on the
 * CPU with an area average avoids that. */
#ifndef SF_IMGSCALE_H
#define SF_IMGSCALE_H

#include <stdint.h>

/* Fit (w x h) inside (max_w x max_h) keeping proportions; never enlarges.
 * Writes the size to *ow, *oh. */
void img_fit_size(int w, int h, float max_w, float max_h, int *ow, int *oh);

/* Area-average resample src (w x h RGBA, straight alpha) to dst (ow x oh),
 * averaging in premultiplied alpha so transparent pixels don't darken edges,
 * then filling fully transparent pixels with their neighbours' color so the
 * GPU's filtering never pulls in black. dst must hold ow*oh*4 bytes and may
 * not overlap src. */
void img_resample_rgba(const uint8_t *src, int w, int h, uint8_t *dst, int ow, int oh);

/* Copy src (w x h RGBA) into dst ((w+2*pad) x (h+2*pad)) with a fully
 * transparent border whose color repeats the nearest edge pixel, so texture
 * filtering at the image's edge never picks up anything visible. */
void img_pad_rgba(const uint8_t *src, int w, int h, int pad, uint8_t *dst);

#endif

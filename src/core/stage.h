/* stage.h - the built-in "Stage" background: a picture plus glassy button
 * shapes (cross, circle, triangle, square) floating over it. */
#ifndef SF_STAGE_H
#define SF_STAGE_H

extern const unsigned char g_stage_jpg[];
extern const unsigned int g_stage_jpg_size;
extern const unsigned char g_stage_shapes_png[];
extern const unsigned int g_stage_shapes_png_size;

/* From the brightness map (one byte per pixel) make the "white-hot" layer:
 * the brightest streaks glow white whatever the UI color. */
void stage_highlight(const unsigned char *v, int n, unsigned char *out);

#define STAGE_SHAPE_CELL 128      /* atlas: 4 cells of 128x128 in a row */
#define STAGE_SHAPES     4        /* 0 cross, 1 circle, 2 triangle, 3 square */

#endif

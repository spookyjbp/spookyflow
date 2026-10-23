/* stage.h - the built-in "Stage" background: a base scene, moving wave
 * ribbons, star field and light flares (all stored as brightness and tinted
 * with the UI color), plus glassy button shapes floating over it. */
#ifndef SF_STAGE_H
#define SF_STAGE_H

extern const unsigned char g_stage_jpg[];          /* 1280x720 base scene, brightness */
extern const unsigned int g_stage_jpg_size;
extern const unsigned char g_stage_rib_jpg[];      /* 1024x904 light ribbons, brightness: top = left | right
                                                       wave sets, bottom = their white-hot cores */
extern const unsigned int g_stage_rib_jpg_size;
extern const unsigned char g_stage_glow_jpg[];     /* 512x512 glow halos (brightness): rows 0..287 the scene,
                                                       rows 288..511 left | right ribbon sets */
extern const unsigned int g_stage_glow_jpg_size;
#define STAGE_GLOW_SPLIT (288.0f / 512.0f)
#define STAGE_RIB_SPLIT 0.4f   /* left set covers screen x 0..0.6, right set 0.4..1 (cross-faded) */
extern const unsigned char g_stage_fx_png[];       /* brightness atlas, rects below */
extern const unsigned int g_stage_fx_png_size;
extern const unsigned char g_stage_shapes_png[];   /* RGBA 1024x896: columns = shape, rows = angle 1 body,
                                                       angle 1 edges, angle 2 body, angle 2 edges */
extern const unsigned int g_stage_shapes_png_size;

enum { STAGE_FX_GLOW, STAGE_FX_LINE,
       STAGE_FX_STAR, STAGE_FX_MIST,
       STAGE_FX_DOTS, STAGE_FX_COMET, STAGE_FX_COUNT };
/* STAGE_FX_DOTS is a 16 x 2 grid of single particles: */
#define STAGE_DOT_COLS   16
#define STAGE_DOT_SPARK  0    /* 8 bright sparks */
#define STAGE_DOT_NSPARK 8
#define STAGE_DOT_GLOW   8    /* 5 soft glow dots */
#define STAGE_DOT_NGLOW  5
#define STAGE_DOT_BOKEH  13   /* 4 bokeh orbs */
#define STAGE_DOT_NBOKEH 4
extern const int g_stage_fx_size[2];
extern const int g_stage_fx_rect[STAGE_FX_COUNT][4];   /* x, y, w, h in the atlas */

#define STAGE_SHAPES 4    /* 0 triangle, 1 circle, 2 cross, 3 square */
#define STAGE_ANGLES 2

/* From the brightness map (one byte per pixel) make the "white-hot" layer:
 * the brightest streaks glow white whatever the UI color. */
void stage_highlight(const unsigned char *v, int n, unsigned char *out);

#endif

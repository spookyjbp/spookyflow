/* render_soft.h - host software rasterizer control (preview tool only). */
#ifndef SF_RENDER_SOFT_H
#define SF_RENDER_SOFT_H

/* Physical canvas pw x ph showing a logical space logical_w x logical_h. */
int  soft_init(int pw, int ph, float logical_w, float logical_h);
void soft_shutdown(void);
int  soft_write_ppm(const char *path);
extern long g_soft_vertex_count;
extern long g_soft_pixel_count;

#endif

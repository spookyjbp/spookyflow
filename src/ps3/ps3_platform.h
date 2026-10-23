/* ps3_platform.h - glue between main.c and the PS3-specific modules. */
#ifndef SF_PS3_PLATFORM_H
#define SF_PS3_PLATFORM_H

#include <ppu-types.h>
#include <stdint.h>
#include "scanner.h"

#define SF_APPID         "SPKF00001"   /* must match APPID in the Makefile */
#define SF_DATA_DIR      "/dev_hdd0/tmp/spookyflow"
#define SF_TABS_DIR      SF_DATA_DIR "/tabs"
#define SF_SOUNDS_DIR    SF_DATA_DIR "/sounds"
#define SF_BG_IMAGE      SF_DATA_DIR "/background"   /* + .jpg or .png */
#define SF_CASES_DIR     SF_DATA_DIR "/cases"    /* optional case images */
#define SF_COVERS_DIR    SF_DATA_DIR "/covers"   /* processed cover cache (safe to delete) */
#define SF_LOG_PATH      SF_DATA_DIR "/log.txt"
#define SF_SETTINGS_PATH SF_DATA_DIR "/settings.ini"

/* render_tiny3d.c */
int  ps3_render_init(u32 pool_bytes);
int  ps3_render_init_covers(void);   /* cover slot textures; -1 = placeholders only */
void ps3_render_frame_begin(void);

/* ps3_pad.c: abstract SFB_* mask from all connected pads */
void     ps3_pad_init(void);
uint32_t ps3_pad_read(void);
void     ps3_pad_end(void);

/* ps3_fs.c */
int  ps3_fs_list(void *ctx, const char *dir, FsEntry *out, int max);
int  ps3_fs_exists(const char *path);
void ps3_fs_mkdirs(void);

/* ps3_tabimg.c: load optional tab images (called during loading) */
void ps3_load_tab_images(void);

/* ps3_webman.c: send a web command to webMAN MOD (0 = it replied) */
int ps3_webman_get(const char *command);

/* ps3_audio.c: UI sounds */
void ps3_load_sounds(void);    /* optional user WAVs; call before ps3_audio_start */
void ps3_audio_start(void);
void ps3_audio_stop(void);

/* ps3_covers.c: cover art worker */
#include "coverart.h"
CoverLoader *ps3_covers_start(void);   /* NULL = placeholders only */
void ps3_covers_stop(void);
void ps3_decode_lock(void);            /* one JPEG/PNG decode at a time */
void ps3_decode_unlock(void);
int  ps3_decode_image(const char *path, int min_w, int min_h, int max_dim, int *w, int *h, uint8_t **rgba);

#endif

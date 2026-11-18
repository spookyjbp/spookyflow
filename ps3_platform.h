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
#define SF_MUSIC_DIR     SF_DATA_DIR "/music"    /* background music: .mp3 / .wav */
#define SF_BG_DIR        SF_DATA_DIR "/backgrounds"   /* the user's background pictures */
#define SF_CASES_DIR     SF_DATA_DIR "/cases"    /* optional case images */
#define SF_COVERS_DIR    SF_DATA_DIR "/covers"   /* processed cover cache (safe to delete) */
#define SF_LOG_PATH      SF_DATA_DIR "/log.txt"
#define SF_SETTINGS_PATH SF_DATA_DIR "/settings.ini"

/* render_tiny3d.c */
int  ps3_render_init(u32 pool_bytes);
int  ps3_render_init_covers(void);   /* cover slot textures; -1 = placeholders only */
void ps3_render_frame_begin(void);   /* call after tiny3d_Clear */
/* per-frame counters (what a frame sends to the RSX) */
typedef struct { unsigned batches, tex_binds, shader_switches, vertices; } R3Stats;
extern R3Stats g_r3_stats;

/* ps3_pad.c: abstract SFB_* mask from all connected pads */
void     ps3_pad_init(void);
uint32_t ps3_pad_read(void);
void     ps3_pad_end(void);

/* ps3_fs.c */
int  ps3_fs_list(void *ctx, const char *dir, FsEntry *out, int max);
int  ps3_fs_exists(const char *path);
void ps3_fs_mkdirs(void);
int  ps3_fs_is_file(void *ctx, const char *path);
int  ps3_fs_write_file(const char *path, const void *data, uint32_t len);   /* 0 = written */
int  ps3_fs_remove_file(const char *path);                                   /* 0 = removed */

/* ps3_tabimg.c: load optional tab images (called during loading) */
void ps3_load_tab_images(void);
int  ps3_load_bg_image(const char *path);   /* a background picture -> TEX_BG_IMAGE; 0 = done */

/* ps3_webman.c: send a web command to webMAN MOD (0 = it replied) */
int ps3_webman_get(const char *command);
/* send without waiting for the answer (launch requests); 0 = sent */
int ps3_webman_send(const char *command);
/* send and wait up to timeout_s for webMAN's answer; the answer's body goes
 * to out (may be NULL). 0 = it answered */
int ps3_webman_get_wait(const char *command, int timeout_s, char *out, size_t cap);
/* 0.9.2: plain HTTP GET of a URL on the internet (Download Missing Covers;
 * coverart.h CoverHttpFn). https:// gives COVER_HTTP_TLS. */
int ps3_http_get(void *ctx, const char *url, uint8_t *buf, uint32_t cap, uint32_t *len, char *loc, size_t loc_cap);
/* read part of a file (ps3_covers.c) */
int ps3_read_at(void *ctx, const char *path, uint64_t off, void *buf, uint32_t len);

/* ps3_audio.c: UI sounds and the background music decoder thread */
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

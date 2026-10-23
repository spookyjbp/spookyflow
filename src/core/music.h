/* music.h - background music: a playlist of the user's MP3 / WAV files,
 * streamed and decoded on a decoder thread, mixed under the UI sounds.
 *
 * Files: SF_MUSIC_DIR (/dev_hdd0/tmp/spookyflow/music) and one level of
 * sub-folders; .mp3 (any MPEG-1/2/2.5 layer III, mono/stereo, any rate) and
 * .wav (PCM 16-bit). MP3 decoding: minimp3 by lieff (CC0,
 * https://github.com/lieff/minimp3). Output is resampled to 48 kHz stereo.
 *
 * Threads:
 *   main thread    music_set_playlist / music_set_* / music_next / music_current
 *   decoder thread music_decode_step() in a loop (sleep for the ms it returns)
 *   audio thread   music_mix() adds the music to each block, after sound_mix()
 * They share a single-producer/single-consumer PCM ring (about 2 s), so a
 * slow disk or a busy frame never makes the music stutter. */
#ifndef SF_MUSIC_H
#define SF_MUSIC_H

#include <stddef.h>
#include <stdint.h>
#include "discid.h"     /* ReadAtFn */
#include "scanner.h"    /* FsListFn */

#define MUSIC_MAX_TRACKS   512
#define MUSIC_PATH_MAX     512
#define MUSIC_RATE         48000
#define MUSIC_RING_FRAMES  (MUSIC_RATE * 2)

/* io for reading the files (decoder thread). Call once. */
void music_init(ReadAtFn rd, void *ctx);

/* Find the music files in dir (and its sub-folders, one level) and make them
 * the playlist (sorted by name). Returns how many. Main thread. */
int  music_scan(FsListFn list, void *ctx, const char *dir);
/* Or set the playlist directly (copies the paths). */
int  music_set_playlist(const char *const *paths, int n);
int  music_track_count(void);

void music_set_volume(float v);   /* 0..1 (the Music Volume setting); 0 = silent, decoding stops */
void music_set_shuffle(int on);   /* random order, no repeat until every track has played */
void music_set_duck(float g);     /* 0..1 extra gain: lowered while the screen sleeps */
void music_set_playing(int on);   /* start / stop (both fade) */
void music_set_seed(uint32_t seed);
void music_next(void);            /* skip to the next track */
/* Name of the track playing (file name without extension); returns a
 * counter that changes whenever a new track starts (0 = none yet). */
int  music_current(char *name, size_t cap);

/* Decoder thread: do a slice of work. Returns how many ms to sleep before
 * the next call (0 = call again right away). */
int  music_decode_step(void);
/* Audio thread: add `frames` stereo frames of music to out (interleaved
 * L,R floats, -1..1), with the volume and fades applied. */
void music_mix(float *out, int frames);

/* tests */
uint32_t music_buffered(void);    /* frames waiting in the ring */
int      music_failed(void);      /* files that could not be played */
void     music_reset(void);       /* back to the state after init (tests) */

#endif

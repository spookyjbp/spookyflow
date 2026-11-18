/* music.c - background music (see music.h). */
#include "music.h"
#include "log.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_FLOAT_OUTPUT
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ---- playlist (main thread writes under the lock; decoder reads) ---- */
static char  g_paths[MUSIC_MAX_TRACKS][MUSIC_PATH_MAX];
static int   g_ntracks;
static int   g_order[MUSIC_MAX_TRACKS];
static int   g_order_pos;
static volatile int g_lock;
static ReadAtFn g_rd;
static void *g_rd_ctx;

/* ---- controls (main thread -> decoder / audio threads) ---- */
static volatile float g_volume = 0.5f, g_duck = 1.0f;
static volatile int   g_playing, g_shuffle = 1, g_skip, g_list_gen, g_failed;
static uint32_t g_rand = 0x9E3779B9u;

/* ---- now playing (decoder writes, main reads) ---- */
static char g_cur_name[128];
static volatile int g_cur_gen;            /* decoder side: counts track starts */
/* what music_current() reports: a new track only once it is actually heard
 * (the ring holds up to 2 s, plus the pause between songs) */
static char g_pend_name[128], g_pub_name[128];
static volatile int g_pend, g_pend_gen, g_pub_gen;
static volatile uint32_t g_pend_at;
/* pause between songs (a track that ends on its own; skipping is immediate) */
static volatile float g_gap_s = MUSIC_GAP_SECONDS;
static uint32_t g_gap_left;

/* ---- PCM ring: decoder writes, audio thread reads ---- */
static float g_ring[MUSIC_RING_FRAMES * 2];
static volatile uint32_t g_w, g_r, g_flush_to, g_flush_req;

/* ---- decoder state (decoder thread only) ---- */
enum { FMT_NONE = 0, FMT_MP3, FMT_WAV };
static int g_fmt, g_cur = -1, g_list_seen = -1, g_fail_run;
static uint32_t g_track_frames;
static char g_path[MUSIC_PATH_MAX];
static uint64_t g_off, g_end;
static unsigned char g_in[16 * 1024];
static int g_have, g_pos, g_eof;
static mp3dec_t g_mp3;
static float g_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int g_wav_ch, g_wav_rate;
/* resampler: linear, streaming */
static float g_prev_l, g_prev_r, g_t;

/* ---- audio thread state ---- */
static float g_gain;

static void lock(void) { while (__sync_lock_test_and_set(&g_lock, 1)) { } }
static void unlock(void) { __sync_lock_release(&g_lock); }

static uint32_t rnd(void)
{
    g_rand ^= g_rand << 13; g_rand ^= g_rand >> 17; g_rand ^= g_rand << 5;
    return g_rand;
}

void music_init(ReadAtFn rd, void *ctx) { g_rd = rd; g_rd_ctx = ctx; }
void music_set_seed(uint32_t seed) { g_rand = seed ? seed : 0x9E3779B9u; }

static int is_music_file(const char *name)
{
    return sf_ends_with_i(name, ".mp3") || sf_ends_with_i(name, ".wav");
}

static int cmp_path(const void *a, const void *b) { return sf_stricmp((const char *)a, (const char *)b); }

/* new play order: shuffled (the track that just played doesn't come first) or by name */
static void make_order(int avoid_first)
{
    int i;
    for (i = 0; i < g_ntracks; i++) g_order[i] = i;
    if (g_shuffle && g_ntracks > 1) {
        for (i = g_ntracks - 1; i > 0; i--) {
            int j = (int)(rnd() % (uint32_t)(i + 1)), t = g_order[i];
            g_order[i] = g_order[j]; g_order[j] = t;
        }
        if (g_order[0] == avoid_first) { int t = g_order[0]; g_order[0] = g_order[1]; g_order[1] = t; }
    }
    g_order_pos = 0;
}

int music_set_playlist(const char *const *paths, int n)
{
    int i, k = 0;
    lock();
    for (i = 0; i < n && k < MUSIC_MAX_TRACKS; i++)
        if (paths[i] && strlen(paths[i]) < MUSIC_PATH_MAX) sf_strlcpy(g_paths[k++], paths[i], MUSIC_PATH_MAX);
    g_ntracks = k;
    qsort(g_paths, (size_t)k, MUSIC_PATH_MAX, cmp_path);
    make_order(-1);
    g_list_gen++;
    g_failed = 0;
    unlock();
    return k;
}

int music_scan(FsListFn list, void *ctx, const char *dir)
{
    static FsEntry ent[512];
    static char sub[32][MUSIC_PATH_MAX];
    static const char *ptrs[MUSIC_MAX_TRACKS];
    static char found[MUSIC_MAX_TRACKS][MUSIC_PATH_MAX];
    int n, i, nsub = 0, nf = 0, d;
    if (!list || !dir) return music_set_playlist(NULL, 0);
    n = list(ctx, dir, ent, 512);
    for (i = 0; i < n && nf < MUSIC_MAX_TRACKS; i++) {
        if (ent[i].name[0] == '.') continue;
        if (ent[i].is_dir) {
            if (nsub < 32 && snprintf(sub[nsub], MUSIC_PATH_MAX, "%s/%s", dir, ent[i].name) < MUSIC_PATH_MAX) nsub++;
            continue;
        }
        if (is_music_file(ent[i].name) && snprintf(found[nf], MUSIC_PATH_MAX, "%s/%s", dir, ent[i].name) < MUSIC_PATH_MAX) nf++;
    }
    for (d = 0; d < nsub; d++) {
        n = list(ctx, sub[d], ent, 512);
        for (i = 0; i < n && nf < MUSIC_MAX_TRACKS; i++) {
            if (ent[i].name[0] == '.' || ent[i].is_dir || !is_music_file(ent[i].name)) continue;
            if (snprintf(found[nf], MUSIC_PATH_MAX, "%s/%s", sub[d], ent[i].name) < MUSIC_PATH_MAX) nf++;
        }
    }
    for (i = 0; i < nf; i++) ptrs[i] = found[i];
    n = music_set_playlist(ptrs, nf);
    if (n > 0) log_printf("music: %d tracks in %s", n, dir);
    else log_printf("music: no .mp3/.wav files in %s", dir);
    return n;
}

int  music_track_count(void) { return g_ntracks; }
void music_set_volume(float v) { g_volume = sf_clampf(v, 0.0f, 1.0f); }
void music_set_duck(float g) { g_duck = sf_clampf(g, 0.0f, 1.0f); }
void music_set_playing(int on) { g_playing = on ? 1 : 0; }
void music_next(void) { g_skip = 1; }
int  music_failed(void) { return g_failed; }

void music_set_shuffle(int on)
{
    on = on ? 1 : 0;
    if (on == g_shuffle) return;
    lock();
    g_shuffle = on;
    make_order(g_cur);
    unlock();
}

int music_current(char *name, size_t cap)
{
    lock();
    if (g_pend && (int32_t)(g_r - g_pend_at) >= 0) {   /* the audio thread got to its first frame */
        sf_strlcpy(g_pub_name, g_pend_name, sizeof(g_pub_name));
        g_pub_gen = g_pend_gen;
        g_pend = 0;
    }
    if (name && cap) sf_strlcpy(name, g_pub_name, cap);
    unlock();
    return g_pub_gen;
}

int music_decoder_current(char *name, size_t cap)
{
    if (name && cap) sf_strlcpy(name, g_cur_name, cap);
    return g_cur_gen;
}

void music_set_gap(float seconds) { g_gap_s = sf_clampf(seconds, 0.0f, 10.0f); }

uint32_t music_buffered(void) { return g_w - g_r; }

void music_reset(void)
{
    lock();
    g_ntracks = 0; g_order_pos = 0; g_list_gen++;
    unlock();
    g_volume = 0.5f; g_duck = 1.0f; g_playing = 0; g_shuffle = 1; g_skip = 0; g_failed = 0;
    g_cur_name[0] = '\0'; g_cur_gen = 0;
    g_pend = 0; g_pend_gen = 0; g_pub_gen = 0; g_pend_at = 0; g_pend_name[0] = g_pub_name[0] = '\0';
    g_gap_s = MUSIC_GAP_SECONDS; g_gap_left = 0;
    g_w = g_r = g_flush_to = g_flush_req = 0;
    g_fmt = FMT_NONE; g_cur = -1; g_list_seen = -1; g_fail_run = 0; g_gain = 0.0f;
}

/* ------------------------------------------------------------------ */
/* Decoder                                                             */

static int fill(void)
{
    int got;
    if (g_pos > 0 && g_pos < g_have) memmove(g_in, g_in + g_pos, (size_t)(g_have - g_pos));
    g_have -= g_pos; g_pos = 0;
    if (g_eof || g_have >= (int)sizeof(g_in)) return 0;
    {
        uint32_t want = (uint32_t)sizeof(g_in) - (uint32_t)g_have;
        if (g_end && g_off + want > g_end) want = (uint32_t)(g_end - g_off);
        got = want ? g_rd(g_rd_ctx, g_path, g_off, g_in + g_have, want) : 0;
    }
    if (got <= 0) { g_eof = 1; return 0; }
    g_have += got; g_off += (uint64_t)got;
    return got;
}

static uint32_t rd32le(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* open g_path: detects MP3 (skipping an ID3v2 tag) or PCM WAV */
static int open_track(void)
{
    unsigned char h[64];
    int got;
    g_fmt = FMT_NONE; g_off = 0; g_end = 0; g_have = 0; g_pos = 0; g_eof = 0;
    g_t = 0.0f; g_prev_l = g_prev_r = 0.0f;
    if (!g_rd) return 0;
    got = g_rd(g_rd_ctx, g_path, 0, h, sizeof(h));
    if (got < 12) return 0;
    if (memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0) {
        uint64_t off = 12;
        int fmt_ok = 0;
        for (;;) {                                     /* walk the chunks to "fmt " and "data" */
            unsigned char c[24];
            uint32_t sz;
            if (g_rd(g_rd_ctx, g_path, off, c, 8) != 8) return 0;
            sz = rd32le(c + 4);
            if (memcmp(c, "fmt ", 4) == 0) {
                if (sz < 16 || g_rd(g_rd_ctx, g_path, off + 8, c, 16) != 16) return 0;
                if ((c[0] | (c[1] << 8)) != 1 || (c[14] | (c[15] << 8)) != 16) return 0;   /* PCM 16-bit only */
                g_wav_ch = c[2] | (c[3] << 8);
                g_wav_rate = (int)rd32le(c + 4);
                if (g_wav_ch < 1 || g_wav_ch > 2 || g_wav_rate < 8000 || g_wav_rate > 192000) return 0;
                fmt_ok = 1;
            } else if (memcmp(c, "data", 4) == 0) {
                if (!fmt_ok) return 0;
                g_off = off + 8; g_end = g_off + sz;
                g_fmt = FMT_WAV;
                return 1;
            }
            off += 8 + sz + (sz & 1);
            if (off > (1ull << 32)) return 0;
        }
    }
    if (memcmp(h, "ID3", 3) == 0 && got >= 10) {      /* skip the tag (it can hold big cover pictures) */
        uint32_t sz = ((uint32_t)(h[6] & 0x7F) << 21) | ((uint32_t)(h[7] & 0x7F) << 14) | ((uint32_t)(h[8] & 0x7F) << 7) | (h[9] & 0x7F);
        g_off = 10 + sz + ((h[5] & 0x10) ? 10 : 0);
    }
    mp3dec_init(&g_mp3);
    g_fmt = FMT_MP3;
    return 1;
}

static void name_from_path(const char *p, char *out, size_t cap)
{
    const char *s = strrchr(p, '/');
    char *dot;
    sf_strlcpy(out, s ? s + 1 : p, cap);
    dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

/* pick and open the next track; 0 if there is nothing to play */
static int next_track(void)
{
    int tries;
    lock();
    if (g_list_seen != g_list_gen) { g_list_seen = g_list_gen; g_cur = -1; g_fail_run = 0; }
    for (tries = 0; tries < g_ntracks; tries++) {
        int t;
        if (g_order_pos >= g_ntracks) make_order(g_cur);
        t = g_order[g_order_pos++];
        sf_strlcpy(g_path, g_paths[t], sizeof(g_path));
        unlock();
        g_cur = t;
        if (open_track()) {
            name_from_path(g_path, g_cur_name, sizeof(g_cur_name));
            g_track_frames = 0;
            g_cur_gen++;
            lock();                                /* reported once it is heard */
            sf_strlcpy(g_pend_name, g_cur_name, sizeof(g_pend_name));
            g_pend_gen = g_cur_gen;
            g_pend_at = g_w;
            g_pend = 1;
            unlock();
            log_printf("music: playing %s", g_path);
            return 1;
        }
        log_printf("music: can't play %s", g_path);
        g_failed++;
        g_fail_run++;
        lock();
    }
    unlock();
    g_fmt = FMT_NONE;
    return 0;
}

/* the track ended: one that gave no sound at all counts as a failure */
static int end_track(void)
{
    if (g_track_frames == 0) {
        log_printf("music: no audio in %s", g_path);
        g_failed++;
        g_fail_run++;
    } else {
        g_fail_run = 0;
        g_gap_left = (uint32_t)(g_gap_s * (float)MUSIC_RATE);   /* a pause before the next song */
    }
    g_fmt = FMT_NONE;
    return 0;
}

/* push frames at `rate` (mono or stereo) through the resampler into the ring */
static void push(const float *pcm, int frames, int ch, int rate)
{
    float step = (float)rate / (float)MUSIC_RATE;
    int i;
    for (i = 0; i < frames; i++) {
        float l = pcm[i * ch], r = ch > 1 ? pcm[i * ch + 1] : l;
        while (g_t < 1.0f) {
            uint32_t w = g_w % MUSIC_RING_FRAMES;
            g_ring[w * 2] = g_prev_l + (l - g_prev_l) * g_t;
            g_ring[w * 2 + 1] = g_prev_r + (r - g_prev_r) * g_t;
            __sync_synchronize();
            g_w = g_w + 1;
            g_t += step;
        }
        g_t -= 1.0f;
        g_prev_l = l; g_prev_r = r;
    }
    g_track_frames += (uint32_t)frames;
}

/* leave room for the biggest push: an 8 kHz MP3 frame (1152 samples) is 6912 frames at 48 kHz */
#define RING_ROOM  8192
#define PUSH_MAX   6000     /* WAV: output frames per call */

int music_decode_step(void)
{
    if (g_skip || g_list_seen != g_list_gen) {
        if (g_skip) g_fail_run = 0;
        g_skip = 0;
        g_gap_left = 0;
        g_fmt = FMT_NONE;
        g_flush_to = g_w;                     /* the audio thread drops what's buffered */
        __sync_synchronize();
        g_flush_req = g_flush_req + 1;
    }
    if (!g_playing || g_volume <= 0.0f || g_ntracks <= 0) return 50;
    if (g_list_seen == g_list_gen && g_fail_run >= g_ntracks) return 500;   /* nothing plays: idle */
    if (MUSIC_RING_FRAMES - (g_w - g_r) < RING_ROOM) return 15;              /* ring full enough */
    if (g_gap_left > 0 && g_fmt == FMT_NONE) {             /* silence between songs */
        uint32_t n = g_gap_left < 4800 ? g_gap_left : 4800, i;
        for (i = 0; i < n; i++) {
            uint32_t w = g_w % MUSIC_RING_FRAMES;
            g_ring[w * 2] = 0.0f; g_ring[w * 2 + 1] = 0.0f;
            __sync_synchronize();
            g_w = g_w + 1;
        }
        g_gap_left -= n;
        return 0;
    }
    if (g_fmt == FMT_NONE && !next_track()) return 200;
    if (g_fmt == FMT_MP3) {
        int k;
        for (k = 0; k < 4; k++) {             /* a few frames per call */
            mp3dec_frame_info_t info;
            int samples;
            if (g_have - g_pos < 2048) fill();
            if (g_have - g_pos <= 0) return end_track();
            samples = mp3dec_decode_frame(&g_mp3, g_in + g_pos, g_have - g_pos, g_pcm, &info);
            if (info.frame_bytes == 0) {      /* needs more data, or nothing left that decodes */
                if (g_eof) return end_track();
                if (g_have - g_pos >= (int)sizeof(g_in)) g_pos = g_have;   /* a full buffer of junk */
                fill();
                continue;
            }
            g_pos += info.frame_bytes;
            if (samples > 0 && info.hz > 0) push(g_pcm, samples, info.channels > 1 ? 2 : 1, info.hz);
            if (MUSIC_RING_FRAMES - (g_w - g_r) < RING_ROOM) break;
        }
        return 0;
    }
    if (g_fmt == FMT_WAV) {
        int frames, bpf = 2 * g_wav_ch, i;
        int max = (int)((int64_t)PUSH_MAX * g_wav_rate / MUSIC_RATE);
        int cap = (int)(sizeof(g_pcm) / sizeof(g_pcm[0])) / g_wav_ch;
        if (max > cap) max = cap;
        if (max < 1) max = 1;
        if (g_have - g_pos < bpf * 512) fill();
        frames = (g_have - g_pos) / bpf;
        if (frames <= 0) return end_track();
        if (frames > max) frames = max;
        for (i = 0; i < frames * g_wav_ch; i++) {
            const unsigned char *p = g_in + g_pos + i * 2;
            g_pcm[i] = (float)(int16_t)(p[0] | (p[1] << 8)) / 32768.0f;
        }
        g_pos += frames * bpf;
        push(g_pcm, frames, g_wav_ch, g_wav_rate);
        return 0;
    }
    return 20;
}

/* ------------------------------------------------------------------ */
/* Audio thread                                                        */

void music_mix(float *out, int frames)
{
    static uint32_t seen_flush;
    float target = g_playing ? g_volume * g_duck : 0.0f;
    float up = 1.0f / (0.45f * (float)MUSIC_RATE);    /* full fade in: 0.45 s */
    float down = 1.0f / (0.25f * (float)MUSIC_RATE);  /* fade out: 0.25 s (done before the app's exit fade ends) */
    int i;
    if (g_flush_req != seen_flush) {
        __sync_synchronize();
        seen_flush = g_flush_req;
        if ((int32_t)(g_flush_to - g_r) > 0) g_r = g_flush_to;
        g_gain = 0.0f;                                  /* the new track fades in */
    }
    for (i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        if (g_gain < target) g_gain = sf_minf(target, g_gain + up);
        else if (g_gain > target) g_gain = sf_maxf(target, g_gain - down);
        if (g_w != g_r) {
            uint32_t k = g_r % MUSIC_RING_FRAMES;
            __sync_synchronize();
            l = g_ring[k * 2]; r = g_ring[k * 2 + 1];
            if (g_gain > 0.0f) g_r = g_r + 1;   /* at gain 0 the music waits (stopped, or ducked all the way) */
        }
        l = out[i * 2] + l * g_gain;
        r = out[i * 2 + 1] + r * g_gain;
        out[i * 2] = sf_clampf(l, -1.0f, 1.0f);
        out[i * 2 + 1] = sf_clampf(r, -1.0f, 1.0f);
    }
}

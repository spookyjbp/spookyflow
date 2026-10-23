/* sound.c - see sound.h. All sounds are mono float at SND_RATE. */
#include "sound.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI2 6.28318530718f

typedef struct { float *pcm; int len; int custom; } Sound;
typedef struct { int id; int pos; float gain; int active; } Voice;

static Sound g_snd[SND_COUNT];
static float *g_builtin[SND_COUNT];
static int g_builtin_len[SND_COUNT];
static Voice g_voice[SND_VOICES];

/* play requests: main thread writes g_req[head], audio thread reads g_req[tail] */
#define REQ_MAX 32
static volatile int g_req[REQ_MAX];
static volatile unsigned g_req_head, g_req_tail;
static volatile int g_enabled = 1;
static volatile float g_volume = 0.8f;
static uint32_t g_play_count[SND_COUNT];

static const char *const k_names[SND_COUNT] = {
    "move.wav", "tab.wav", "open.wav", "close.wav", "change.wav", "confirm.wav"
};

const char *sound_file_name(SoundId id) { return (id >= 0 && id < SND_COUNT) ? k_names[id] : ""; }

/* ---- synthesis -------------------------------------------------------- */

static float env_ad(float t, float attack, float decay)
{
    if (t < attack) return t / attack;
    return expf(-(t - attack) / decay);
}

/* soft bell note: fundamental + a quiet octave, attack/decay envelope */
static void add_note(float *b, int len, float start_s, float freq, float amp, float decay)
{
    int i, s0 = (int)(start_s * SND_RATE);
    for (i = s0; i < len; i++) {
        float t = (float)(i - s0) / SND_RATE;
        float e = env_ad(t, 0.003f, decay);
        if (e < 0.0005f && t > 0.01f) break;
        b[i] += amp * e * (sinf(PI2 * freq * t) + 0.25f * sinf(PI2 * 2.0f * freq * t));
    }
}

static int make(SoundId id, float seconds)
{
    int len = (int)(seconds * SND_RATE);
    float *b = (float *)calloc((size_t)len, sizeof(float));
    if (!b) return -1;
    switch (id) {
    case SND_MOVE:      /* short, soft tick */
        add_note(b, len, 0.0f, 1760.0f, 0.22f, 0.018f);
        break;
    case SND_CHANGE:    /* slightly higher, shorter tick */
        add_note(b, len, 0.0f, 2349.0f, 0.18f, 0.012f);
        break;
    case SND_TAB: {     /* airy whoosh: noise through a sweeping low-pass */
        unsigned seed = 12345u;
        float lp = 0.0f;
        int i;
        for (i = 0; i < len; i++) {
            float t = (float)i / SND_RATE, n, cut, k;
            seed = seed * 1664525u + 1013904223u;
            n = ((float)(seed >> 9) / 4194304.0f) - 1.0f;
            cut = 600.0f + 2600.0f * (t / seconds);
            k = 1.0f - expf(-PI2 * cut / SND_RATE);
            lp += k * (n - lp);
            b[i] = 0.30f * lp * env_ad(t, 0.035f, 0.045f);
        }
        add_note(b, len, 0.0f, 523.25f, 0.08f, 0.05f);
        break;
    }
    case SND_OPEN:      /* two rising notes */
        add_note(b, len, 0.000f, 659.25f, 0.16f, 0.07f);
        add_note(b, len, 0.055f, 987.77f, 0.16f, 0.09f);
        break;
    case SND_CLOSE:     /* two falling notes */
        add_note(b, len, 0.000f, 987.77f, 0.14f, 0.06f);
        add_note(b, len, 0.050f, 659.25f, 0.14f, 0.08f);
        break;
    case SND_CONFIRM:   /* bright three-note arpeggio */
        add_note(b, len, 0.000f, 783.99f, 0.14f, 0.08f);
        add_note(b, len, 0.050f, 987.77f, 0.14f, 0.08f);
        add_note(b, len, 0.100f, 1318.5f, 0.14f, 0.14f);
        break;
    default: break;
    }
    {   /* bring every built-in sound to the same peak (~0.45 at 100%) */
        int i;
        float pk = 0.0f;
        for (i = 0; i < len; i++) if (fabsf(b[i]) > pk) pk = fabsf(b[i]);
        if (pk > 0.0f) for (i = 0; i < len; i++) b[i] *= 0.45f / pk;
    }
    g_builtin[id] = b;
    g_builtin_len[id] = len;
    return 0;
}

int sound_init(void)
{
    static const float secs[SND_COUNT] = { 0.09f, 0.18f, 0.30f, 0.28f, 0.06f, 0.45f };
    int i;
    memset(g_voice, 0, sizeof(g_voice));
    g_req_head = g_req_tail = 0;
    for (i = 0; i < SND_COUNT; i++) {
        if (!g_builtin[i] && make((SoundId)i, secs[i]) != 0) continue;
        if (!g_snd[i].custom) { g_snd[i].pcm = g_builtin[i]; g_snd[i].len = g_builtin_len[i]; }
    }
    return 0;
}

/* ---- WAV ---------------------------------------------------------------- */

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t le16(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

int sound_load_wav_mem(SoundId id, const unsigned char *d, size_t len)
{
    size_t pos = 12;
    uint32_t fmt = 0, ch = 0, rate = 0, bits = 0;
    const unsigned char *data = NULL;
    size_t data_len = 0, frames, out_len, i;
    float *out;
    if (id < 0 || id >= SND_COUNT) return -2;
    if (len < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return -2;
    while (pos + 8 <= len) {
        uint32_t clen = le32(d + pos + 4);
        const unsigned char *c = d + pos + 8;
        size_t avail = len - pos - 8;
        if (memcmp(d + pos, "fmt ", 4) == 0 && clen >= 16 && avail >= 16) {
            fmt = le16(c); ch = le16(c + 2); rate = le32(c + 4); bits = le16(c + 14);
        } else if (memcmp(d + pos, "data", 4) == 0) {
            data = c;
            data_len = clen <= avail ? clen : avail;
        }
        pos += 8 + (size_t)clen + (clen & 1u);
    }
    if (!data || fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate < 4000 || rate > 192000) return -2;
    frames = data_len / (ch * (bits / 8));
    if (frames == 0) return -2;
    out_len = (size_t)((double)frames * SND_RATE / rate);
    if (out_len > (size_t)SND_MAX_SECONDS * SND_RATE) out_len = (size_t)SND_MAX_SECONDS * SND_RATE;
    if (out_len == 0) return -2;
    out = (float *)malloc(out_len * sizeof(float));
    if (!out) return -3;
    for (i = 0; i < out_len; i++) {
        double src = (double)i * rate / SND_RATE;
        size_t f0 = (size_t)src, f1 = f0 + 1 < frames ? f0 + 1 : f0;
        float fr = (float)(src - (double)f0), s0 = 0.0f, s1 = 0.0f;
        uint32_t c;
        for (c = 0; c < ch; c++) {
            if (bits == 16) {
                s0 += (float)(int16_t)le16(data + (f0 * ch + c) * 2) / 32768.0f;
                s1 += (float)(int16_t)le16(data + (f1 * ch + c) * 2) / 32768.0f;
            } else {
                s0 += ((float)data[f0 * ch + c] - 128.0f) / 128.0f;
                s1 += ((float)data[f1 * ch + c] - 128.0f) / 128.0f;
            }
        }
        out[i] = (s0 + (s1 - s0) * fr) / (float)ch;
    }
    /* swap in; the old custom buffer is freed only after the voices let go
     * of it, which is simplest at load time: loading happens before audio
     * starts, so no voice can be playing it. */
    if (g_snd[id].custom) free(g_snd[id].pcm);
    g_snd[id].pcm = out;
    g_snd[id].len = (int)out_len;
    g_snd[id].custom = 1;
    return 0;
}

int sound_load_wav_file(SoundId id, const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *buf;
    int rc;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || n > 8L * 1024L * 1024L) { fclose(f); return -2; }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)n);
    if (!buf) { fclose(f); return -3; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return -2; }
    fclose(f);
    rc = sound_load_wav_mem(id, buf, (size_t)n);
    free(buf);
    return rc;
}

int sound_is_custom(SoundId id) { return (id >= 0 && id < SND_COUNT) ? g_snd[id].custom : 0; }
int sound_length(SoundId id) { return (id >= 0 && id < SND_COUNT) ? g_snd[id].len : 0; }
uint32_t sound_play_count(SoundId id) { return (id >= 0 && id < SND_COUNT) ? g_play_count[id] : 0; }

void sound_set_enabled(int on) { g_enabled = on ? 1 : 0; }
void sound_set_volume(float v) { g_volume = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

void sound_play(SoundId id)
{
    unsigned h;
    if (id < 0 || id >= SND_COUNT) return;
    g_play_count[id]++;
    if (!g_enabled) return;
    h = g_req_head;
    if (h - g_req_tail >= REQ_MAX) return;          /* ring full: drop */
    g_req[h % REQ_MAX] = (int)id;
    __sync_synchronize();
    g_req_head = h + 1;
}

void sound_mix(float *out, int frames)
{
    int i, v;
    float vol = g_volume;
    /* take new requests: a repeat of a sound already near its start restarts
     * it instead of stacking (fast scrolling stays clean) */
    while (g_req_tail != g_req_head) {
        int id, slot = -1, oldest = -1, oldest_pos = -1;
        __sync_synchronize();
        id = g_req[g_req_tail % REQ_MAX];
        g_req_tail = g_req_tail + 1;
        if (!g_snd[id].pcm || g_snd[id].len <= 0) continue;
        for (v = 0; v < SND_VOICES; v++) {
            if (g_voice[v].active && g_voice[v].id == id && g_voice[v].pos < SND_RATE / 50) { slot = v; break; }
            if (!g_voice[v].active && slot < 0) slot = v;
            if (g_voice[v].active && g_voice[v].pos > oldest_pos) { oldest_pos = g_voice[v].pos; oldest = v; }
        }
        if (slot < 0) slot = oldest;
        if (slot < 0) continue;
        g_voice[slot].id = id; g_voice[slot].pos = 0; g_voice[slot].gain = 1.0f; g_voice[slot].active = 1;
    }
    for (i = 0; i < frames; i++) {
        float s = 0.0f;
        for (v = 0; v < SND_VOICES; v++) {
            Voice *vo = &g_voice[v];
            const Sound *sd;
            if (!vo->active) continue;
            sd = &g_snd[vo->id];
            if (vo->pos >= sd->len) { vo->active = 0; continue; }
            s += sd->pcm[vo->pos++] * vo->gain;
        }
        s *= g_enabled ? vol : 0.0f;
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
}

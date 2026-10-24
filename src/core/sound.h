/* sound.h - UI sound effects: built-in sounds (sounds_data.c), optional user WAV
 * overrides, and a small mixer the platform audio thread pulls from.
 *
 * Threading: sound_play() is called from the main thread and only pushes a
 * request into a single-producer/single-consumer ring; sound_mix() runs on
 * the audio thread and is the only code touching the voices. */
#ifndef SF_SOUND_H
#define SF_SOUND_H

#include <stddef.h>
#include <stdint.h>

#define SND_RATE        48000      /* PS3 audio port rate */
#define SND_MAX_SECONDS 3          /* user WAVs are cut to this length */
#define SND_VOICES      8

typedef enum {
    SND_MOVE = 0,   /* next/previous game, menu row */
    SND_TAB,        /* tab switch, letter jump */
    SND_OPEN,       /* a Settings panel opens */
    SND_CLOSE,      /* a panel closes / exit starts */
    SND_CHANGE,     /* a setting value changes */
    SND_CONFIRM,    /* launch, menu actions */
    SND_COUNT
} SoundId;

/* File names users can drop in the sounds folder (e.g. "move.wav"). */
const char *sound_file_name(SoundId id);

/* Load the built-in sounds. Call once before anything else. Returns 0. */
int  sound_init(void);
/* Replace one sound with a WAV file's contents (PCM 8/16-bit, mono/stereo,
 * any rate; converted to 48 kHz mono). 0 = loaded, -1 = can't open,
 * -2 = not a supported WAV, -3 = out of memory. */
int  sound_load_wav_file(SoundId id, const char *path);
int  sound_load_wav_mem(SoundId id, const unsigned char *data, size_t len);
/* 1 if the sound currently comes from a user file. */
int  sound_is_custom(SoundId id);

void sound_set_enabled(int on);       /* the "Sound effects" setting */
void sound_set_volume(float v);       /* master, 0..1 */
void sound_play(SoundId id);          /* main thread */
/* Audio thread: mix `frames` stereo frames (interleaved L,R floats, -1..1). */
void sound_mix(float *out, int frames);

/* Test hooks */
uint32_t sound_play_count(SoundId id); /* how many times sound_play was called */
int      sound_length(SoundId id);     /* samples */

#endif

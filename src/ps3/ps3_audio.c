/* ps3_audio.c - feeds the portable sound mixer (sound.c) to the PS3 audio
 * port on its own thread.
 *
 * Mirrors PSL1GHT's samples/audio/audiosync: audioInit, a 2-channel 8-block
 * port at AUDIO_PORT_INITLEVEL / level 1.0, a notify event queue, and a loop
 * that waits for the "block consumed" event and fills the block after the
 * one being read (readIndex is the ADDRESS of a u64 block counter).
 * Blocks are AUDIO_BLOCK_SAMPLES (256) stereo float frames at 48 kHz. */
#include <audio/audio.h>
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include "log.h"
#include "ps3_platform.h"
#include "sound.h"

static u32 g_port;
static audioPortConfig g_cfg;
static sys_event_queue_t g_queue;
static u64 g_key;
static sys_ppu_thread_t g_tid;
static volatile int g_run;
static int g_state;   /* 0 off, 1 audio open, 2 thread running */

static void audio_thread(void *arg)
{
    (void)arg;
    while (g_run) {
        sys_event_t ev;
        u64 current = *(u64 *)((u64)g_cfg.readIndex);
        f32 *start = (f32 *)((u64)g_cfg.audioDataStart);
        u32 block = (u32)((current + 1) % g_cfg.numBlocks);
        sysEventQueueReceive(g_queue, &ev, 20 * 1000);
        sound_mix(start + g_cfg.channelCount * AUDIO_BLOCK_SAMPLES * block, AUDIO_BLOCK_SAMPLES);
    }
    sysThreadExit(0);
}

void ps3_audio_start(void)
{
    audioPortParam params;
    s32 rc;
    if (g_state) return;
    rc = audioInit();
    if (rc != 0) { log_printf("audio: audioInit failed (%08x)", (unsigned)rc); return; }
    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = AUDIO_PORT_INITLEVEL;
    params.level = 1.0f;
    rc = audioPortOpen(&params, &g_port);
    if (rc != 0) { log_printf("audio: audioPortOpen failed (%08x)", (unsigned)rc); audioQuit(); return; }
    audioGetPortConfig(g_port, &g_cfg);
    /* start silent: clear every block */
    memset((void *)(u64)g_cfg.audioDataStart, 0,
           (size_t)g_cfg.numBlocks * (size_t)g_cfg.channelCount * AUDIO_BLOCK_SAMPLES * sizeof(f32));
    rc = audioCreateNotifyEventQueue(&g_queue, &g_key);
    if (rc != 0) { log_printf("audio: event queue failed (%08x)", (unsigned)rc); audioPortClose(g_port); audioQuit(); return; }
    audioSetNotifyEventQueue(g_key);
    sysEventQueueDrain(g_queue);
    rc = audioPortStart(g_port);
    if (rc != 0) {
        log_printf("audio: audioPortStart failed (%08x)", (unsigned)rc);
        audioRemoveNotifyEventQueue(g_key); audioPortClose(g_port); sysEventQueueDestroy(g_queue, 0); audioQuit();
        return;
    }
    g_state = 1;
    g_run = 1;
    /* priority 100: well above the main thread so sound never starves */
    if (sysThreadCreate(&g_tid, audio_thread, NULL, 100, 64 * 1024, THREAD_JOINABLE, (char *)"sf_audio") != 0) {
        log_printf("audio: thread create failed, sounds off");
        g_run = 0;
        ps3_audio_stop();
        return;
    }
    g_state = 2;
    log_printf("audio: started (%u channels, %u blocks)", (unsigned)g_cfg.channelCount, (unsigned)g_cfg.numBlocks);
}

void ps3_audio_stop(void)
{
    u64 rv;
    if (!g_state) return;
    if (g_state == 2) { g_run = 0; sysThreadJoin(g_tid, &rv); }
    audioPortStop(g_port);
    audioRemoveNotifyEventQueue(g_key);
    audioPortClose(g_port);
    sysEventQueueDestroy(g_queue, 0);
    audioQuit();
    g_state = 0;
    log_printf("audio: stopped");
}

/* Optional user sounds: /dev_hdd0/tmp/spookyflow/sounds/move.wav etc.
 * Loaded before the audio thread starts (nothing is playing yet). */
void ps3_load_sounds(void)
{
    int i;
    for (i = 0; i < SND_COUNT; i++) {
        char path[160];
        int rc;
        snprintf(path, sizeof(path), "%s/%s", SF_SOUNDS_DIR, sound_file_name((SoundId)i));
        if (!ps3_fs_exists(path)) continue;
        rc = sound_load_wav_file((SoundId)i, path);
        if (rc == 0) log_printf("sounds: using %s", path);
        else log_printf("sounds: %s not used (%s)", path,
                        rc == -2 ? "not a 8/16-bit PCM WAV" : rc == -3 ? "out of memory" : "can't open");
    }
}

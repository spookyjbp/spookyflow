/* main.c - spookyflow PS3 entry point (PSL1GHT + Tiny3D).
 *
 * Frame: read pads -> app_update (portable core) -> ui_draw (portable,
 * through render_tiny3d.c) -> tiny3d_Flip (waits for vblank, also runs
 * sysUtilCheckCallback).
 *
 * Exit paths (all end in a normal return from main = back to XMB):
 *   - hold ◯ 1.5 s on the cover flow  -> fade -> loop ends
 *   - Flow settings > Quit to XMB      -> fade -> loop ends
 *   - XMB "Quit game" (SYSUTIL_EXIT_GAME): our slot-0 callback saves settings
 *     and logs first; Tiny3D's own slot-3 callback then calls exit(0)
 *     (see tiny3d.c), which runs our atexit handler. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#include <tiny3d.h>

#include "app.h"
#include "draw.h"
#include "log.h"
#include "sound.h"
#include "ps3_platform.h"
#include "ui.h"

#define VERTEX_BUFFER_BYTES (4u * 1024u * 1024u)
#define TEXTURE_POOL_BYTES  (32u * 1024u * 1024u)   /* UI atlases, case images, backgrounds (1280x720 each) */

static Library g_lib;
static App g_app;
static unsigned char g_scratch[UI_SCRATCH_BYTES];
static volatile int g_sys_exit;
static int g_saved_on_exit;

static double now_seconds(void)
{
    u64 sec = 0, nsec = 0;
    sysGetCurrentTime(&sec, &nsec);
    return (double)sec + (double)nsec * 1e-9;
}

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    switch (status) {
    case SYSUTIL_EXIT_GAME:
        g_sys_exit = 1;
        log_printf("sysutil: exit requested from XMB");
        /* Tiny3D's callback (slot 3) calls exit(0) right after this one */
        if (!g_saved_on_exit) { app_save(&g_app); g_saved_on_exit = 1; }
        break;
    case SYSUTIL_MENU_OPEN:
        log_printf("sysutil: XMB menu opened");
        break;
    case SYSUTIL_MENU_CLOSE:
        log_printf("sysutil: XMB menu closed");
        break;
    default:
        break;
    }
}

/* Background thread for the library scan (sys/thread.h, verified against
 * PSL1GHT: sysThreadCreate(id, entry, arg, prio, stack, flags, name),
 * sysThreadExit(u64)). Priority 1500 is below the main thread's usual 1000
 * so drawing always wins. */
typedef struct { void (*fn)(void *); void *arg; } AsyncJob;
static AsyncJob g_job;

static void async_entry(void *p)
{
    AsyncJob *j = (AsyncJob *)p;
    j->fn(j->arg);
    sysThreadExit(0);
}

static int run_async(void (*fn)(void *), void *arg)
{
    sys_ppu_thread_t tid;
    g_job.fn = fn;
    g_job.arg = arg;
    if (sysThreadCreate(&tid, async_entry, &g_job, 1500, 256 * 1024, 0, (char *)"sf_scan") != 0) return -1;
    return 0;
}

/* Clean power off: webMAN MOD's /shutdown.ps3 shuts down through the VSH,
 * exactly like the XMB's "Turn Off System". (lv2 syscall 379 with 0x1100,
 * used in fix 12, cuts power without the VSH saving its state, so the next
 * boot reported "The system was not turned off properly".) */
static int power_off_via_webman(void)
{
    return ps3_webman_get("/shutdown.ps3");
}

/* Launch: webMAN mounts and starts the game once spookyflow is back at the XMB */
static int launch_via_webman(const char *request)
{
    return ps3_webman_send(request);
}

static void read_wm_config(WmConfig *cfg)
{
    unsigned char buf[1024];
    size_t n = 0;
    FILE *f = fopen(WM_CONFIG_PATH, "rb");
    if (f) { n = fread(buf, 1, sizeof(buf), f); fclose(f); }
    if (!launch_parse_wm_config(buf, n, cfg) && f) log_printf("launch: %s not understood (%u bytes)", WM_CONFIG_PATH, (unsigned)n);
}

static void on_exit_handler(void)
{
    static int done;
    if (done) return;           /* called directly before power-off, then again by atexit */
    done = 1;
    if (!g_saved_on_exit) { app_save(&g_app); g_saved_on_exit = 1; }
    ps3_covers_stop();
    ps3_audio_stop();
    log_printf("shutdown");
    log_close();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    ps3_pad_end();
}

/* Wait until the flip queued by the last tiny3d_Flip has happened. The RSX
 * runs the command stream in order, so once that flip is done every draw of
 * the previous frame is finished and the vertex buffer is free to reuse.
 * Same flip-status poll Tiny3D's own waitFlip() uses (rsxutil.c), but WITHOUT
 * gcmResetFlipStatus(): Tiny3D's next waitFlip() then sees "done" at once and
 * does the reset itself. gcmGetFlipStatus() comes from <rsx/gcm_sys.h>,
 * which tiny3d.h already includes (declared there as u32; Tiny3D casts it to
 * a signed int). 0 = flip done, >0 = still pending, <0 = error. */
static void wait_gpu_frame_done(void)
{
    int n = 500;                      /* 500 x 200 us = 100 ms safety cap */
    while (n-- > 0 && (s32)gcmGetFlipStatus() > 0) usleep(200);
}

/* Frame-time log: a summary every 10 s plus any frame slower than 50 ms
 * (rate-limited), so a pasted log shows whether the app keeps 60 fps. */
static void frame_stats(float dt, double draw_s, double wait_s)
{
    static float acc, worst;
    static double draw_acc, wait_acc, draw_worst;
    static int frames, slow_logged;
    acc += dt; frames++;
    draw_acc += draw_s; wait_acc += wait_s;
    if (dt > worst) worst = dt;
    if (draw_s > draw_worst) draw_worst = draw_s;
    if (dt >= 0.05f && slow_logged < 40) {
        log_printf("perf: slow frame %.0f ms (draw %.1f ms, gpu wait %.1f ms)", dt * 1000.0f, draw_s * 1000.0, wait_s * 1000.0);
        slow_logged++;
    }
    if (acc >= 10.0f) {
        log_printf("perf: %.1f fps average, worst frame %.0f ms; draw avg %.1f ms (worst %.1f), gpu wait avg %.1f ms",
                   (float)frames / acc, worst * 1000.0f, draw_acc * 1000.0 / frames, draw_worst * 1000.0,
                   wait_acc * 1000.0 / frames);
        acc = 0.0f; worst = 0.0f; frames = 0;
        draw_acc = wait_acc = draw_worst = 0.0;
    }
}

static void setup_screen(void)
{
    Screen s;
    float vw = (float)Video_Resolution.width, vh = (float)Video_Resolution.height;
    float display_aspect;
    /* HD modes have square pixels. For SD (480/576) use the console's TV
     * aspect setting. UNVERIFIED: Tiny3D's Video_aspect == 1 means 4:3
     * (VIDEO_ASPECT_4_3) as in PSL1GHT's video.h. */
    if (vh >= 700.0f) display_aspect = vw / vh;
    else display_aspect = (Video_aspect == 1) ? (4.0f / 3.0f) : (16.0f / 9.0f);
    s.W = 848.0f;            /* Tiny3D 2D virtual space */
    s.H = 512.0f;
    s.pa = display_aspect * 512.0f / 848.0f;
    s.px = vh / 512.0f;
    d_set_screen(&s);
    /* Map the 848x512 virtual space onto the WHOLE screen (Tiny3D's default
     * 720p viewport leaves an overscan border). UNVERIFIED semantics:
     * scale = output pixels per virtual unit, inferred from tiny3d.c's own
     * defaults (e.g. 848/611 at 720p). */
    tiny3d_UserViewport(1, 0.0f, 0.0f, vw / 848.0f, vh / 512.0f, vw / 1920.0f, vh / 1080.0f);
    log_printf("video: %dx%d, display aspect %.3f, pixel aspect %.4f", (int)vw, (int)vh, display_aspect, s.pa);
}

int main(int argc, const char *argv[])
{
    AppHost host;
    double prev;
    int rc;
    (void)argc; (void)argv;

    tiny3d_Init(VERTEX_BUFFER_BYTES);
    ps3_pad_init();
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);

    ps3_fs_mkdirs();
    if (log_open(SF_LOG_PATH) == 0) log_printf("==== %s %s starting ====", APP_NAME, APP_VERSION);
    atexit(on_exit_handler);   /* registered after tiny3d_Init: runs before tiny3d_Exit */

    sound_init();
    ps3_load_sounds();
    ps3_audio_start();
    if (ps3_render_init(TEXTURE_POOL_BYTES) != 0) log_printf("render: texture pool allocation failed");
    if (ps3_render_init_covers() != 0) log_printf("render: cover texture allocation failed");
    rc = ui_upload_textures(g_scratch, sizeof(g_scratch));
    if (rc != 0) log_printf("render: texture upload failed (%d)", rc);
    setup_screen();

    memset(&host, 0, sizeof(host));
    host.fs_list = ps3_fs_list;
    host.settings_path = SF_SETTINGS_PATH;
    host.prepare = ps3_load_tab_images;
    host.run_async = run_async;
    host.own_sfo_path = "/dev_hdd0/game/" SF_APPID "/PARAM.SFO";
    host.power_off = power_off_via_webman;
    host.launch = launch_via_webman;
    host.read_wm_config = read_wm_config;
    host.covers = ps3_covers_start();
    app_init(&g_app, &g_lib, &host);

    prev = now_seconds();
    while (!g_app.quit) {
        double t = now_seconds(), tw0, tw1, td1;
        float dt = (float)(t - prev);
        uint32_t held;
        prev = t;
        if (dt < 0.0f) dt = 1.0f / 60.0f;
        if (dt > 0.1f) dt = 0.1f;          /* slow frame: keep time moving, avoid jumps */

        held = ps3_pad_read();
        if (tiny3d_MenuActive()) held = 0;          /* XMB overlay open: ignore pads */
        if (g_sys_exit) app_request_exit(&g_app, "sysutil");
        app_update(&g_app, held, dt);

        /* Tiny3D reuses ONE vertex buffer every frame and its Flip does not
         * wait for the GPU (tiny3d_WaitRSX is only a comment in tiny3d.c and
         * is not in the library). Without a wait we overwrite vertices the
         * RSX is still drawing: flashes, torn/stretched triangles, ghosts. */
        tw0 = now_seconds();
        wait_gpu_frame_done();
        tw1 = now_seconds();
        tiny3d_Clear(0xff000000, TINY3D_CLEAR_ALL);
        /* discard only fully transparent pixels; blending does the rest */
        tiny3d_AlphaTest(1, 0x00, TINY3D_ALPHA_FUNC_GREATER);
        tiny3d_BlendFunc(1, TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
                         TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO,
                         TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);
        tiny3d_Project2D();
        ps3_render_frame_begin();
        ui_draw(&g_app);
        td1 = now_seconds();
        tiny3d_Flip();
        frame_stats(dt, td1 - tw1, tw1 - tw0);
    }

    if (g_app.poweroff) log_printf("main loop finished, webMAN is powering the system off");
    if (g_app.launched) log_printf("main loop finished, webMAN starts \"%s\" at the XMB", g_app.last_launch);
    log_printf("main loop finished, returning to XMB");
    return 0;   /* atexit handler saves, closes the log and releases pads */
}

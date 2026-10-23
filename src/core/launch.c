/* launch.c - build the webMAN MOD mount/play request (see launch.h). */
#include "launch.h"
#include "layout.h"     /* PLAT_* */
#include "settings.h"   /* EMU_SCREEN_*, EMU_SMOOTH_* */
#include "util.h"
#include <stdio.h>
#include <string.h>

int launch_parse_wm_config(const unsigned char *buf, size_t len, WmConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    if (!buf || len < 584) return 0;
    if (((buf[0] << 8) | buf[1]) != 0x1337) return 0;
    cfg->ps1_netemu = buf[581] ? 1 : 0;
    cfg->autoplay = buf[582] ? 1 : 0;
    cfg->ps2_netemu = buf[583] ? 1 : 0;
    cfg->valid = 1;
    return 1;
}

int launch_url_encode(const char *path, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    const unsigned char *p;
    if (!path || !out || cap == 0) return -1;
    for (p = (const unsigned char *)path; *p; p++) {
        unsigned char c = *p;
        int keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == '(' || c == ')' ||
                   c == '!' || c == '*' || c == '\'';
        if (keep) {
            if (o + 1 >= cap) { out[0] = '\0'; return -1; }
            out[o++] = (char)c;
        } else {
            if (o + 3 >= cap) { out[0] = '\0'; return -1; }
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
    return 0;
}

int launch_build(const LaunchReq *req, const WmConfig *wm, char *out, size_t cap)
{
    char enc[LAUNCH_CMD_MAX], disp[96];
    const char *emu = NULL;
    int wm_valid = wm && wm->valid, n;
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (!req || !req->path || !req->path[0] || req->path[0] != '/') return -1;
    if (req->plat < PLAT_PS1 || req->plat > PLAT_PS3) return -1;
    if (launch_url_encode(req->path, enc, sizeof(enc)) != 0) return -1;
    if (req->plat == PLAT_PS1) {
        emu = req->emulator == 1 ? "ps1_netemu.self" : "ps1_emu.self";
    } else if (req->plat == PLAT_PS2) {
        if (req->emulator == 1) emu = "ps2_netemu.self";
        else if (wm_valid) emu = wm->ps2_netemu ? "ps2_netemu.self" : "ps2_emu.self";   /* the user's webMAN default */
    }
    /* PS1/PS2 display options (system registry, see launch.h) */
    disp[0] = '\0';
    if (req->plat != PLAT_PS3) {
        static const int up[EMU_SCREEN_COUNT] = { -1, 0, 1, 2 };     /* Off, Normal, Full Screen */
        static const int sm[EMU_SMOOTH_COUNT] = { -1, 1, 0 };        /* On, Off */
        int u = (req->emu_screen > 0 && req->emu_screen < EMU_SCREEN_COUNT) ? up[req->emu_screen] : -1;
        int m = (req->emu_smooth > 0 && req->emu_smooth < EMU_SMOOTH_COUNT) ? sm[req->emu_smooth] : -1;
        size_t o = 0;
        if (u >= 0) o += (size_t)snprintf(disp + o, sizeof(disp) - o, "/xmb.ps3$xregistry(80)=%d;", u);
        if (m >= 0) o += (size_t)snprintf(disp + o, sizeof(disp) - o, "/xmb.ps3$xregistry(81)=%d;", m);
    }
    n = snprintf(out, cap, "%s/wait.ps3?xmb;/mount_ps3%s?random=0%s%s%s%s",
                 disp, enc,
                 (req->offline && req->plat != PLAT_PS1) ? "&offline=1" : "",
                 emu ? "&emu=" : "", emu ? emu : "",
                 (req->autoplay && !(wm_valid && wm->autoplay)) ? ";/wait.ps3?2;/play.ps3" : "");
    if (n < 0 || (size_t)n >= cap || n >= LAUNCH_CMD_MAX - 64) { out[0] = '\0'; return -1; }
    return 0;
}

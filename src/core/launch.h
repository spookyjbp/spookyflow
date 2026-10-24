/* launch.h - build the webMAN MOD request that mounts and starts a game.
 *
 * spookyflow sends one multi-command request to webMAN MOD
 * (http://127.0.0.1/...) and then exits to the XMB:
 *
 *   /wait.ps3?xmb                      wait until the XMB is back (webMAN won't
 *                                      mount while an app is running; max 30 s)
 *   ;/mount_ps3<path>?random=0         mount the game (ISO, folder, PS1/PS2 image)
 *        [&offline=1]                  turn the network off while it runs
 *        [&emu=<emulator>.self]        PS1/PS2 emulator
 *   ;/wait.ps3?2;/play.ps3             start the disc icon (Auto-play)
 *
 * Verified against the webMAN MOD source (include/cmd/mount_copy.h,
 * include/mount/mount.h, include/cmd/wait.h, include/www/www_client.h):
 * - the query must start with "?random=" (webMAN strips it); "offline=" has
 *   to come before "emu=" because webMAN cuts the query at the separator in
 *   front of "emu=";
 * - "emu=" also changes webMAN's default emulator for the session, so PS1
 *   always names its emulator and PS2 "Default" names the one saved in
 *   webMAN's config (/dev_hdd0/tmp/wm_config.bin);
 * - when webMAN's own Auto-play is on it starts the game after the mount by
 *   itself, so spookyflow doesn't add /play.ps3 (that would start it twice).
 *   With webMAN's Auto-play on, games always start. */
#ifndef SF_LAUNCH_H
#define SF_LAUNCH_H

#include <stddef.h>

#define LAUNCH_CMD_MAX   2048     /* webMAN accepts requests up to 2 KB */
#define WM_CONFIG_PATH   "/dev_hdd0/tmp/wm_config.bin"

/* the few webMAN settings spookyflow needs (from wm_config.bin) */
typedef struct {
    int valid;          /* 0 = file missing or not understood: webMAN's defaults apply */
    int ps1_netemu;     /* [581] 0 = ps1_emu, 1 = ps1_netemu */
    int autoplay;       /* [582] 1 = webMAN starts games after mounting */
    int ps2_netemu;     /* [583] 0 = ps2_emu, 1 = ps2_netemu */
} WmConfig;

/* Parse wm_config.bin (version 0x1337, big-endian). Returns cfg->valid. */
int launch_parse_wm_config(const unsigned char *buf, size_t len, WmConfig *cfg);

/* Percent-encode a path for a webMAN URL (keeps letters, digits, / - _ . ~ ( )
 * and !*'; encodes everything else, including spaces, + % & ? # ; [ ]).
 * Returns 0, or -1 if it doesn't fit. */
int launch_url_encode(const char *path, char *out, size_t cap);

typedef struct {
    int plat;           /* PLAT_PS1 / PLAT_PS2 / PLAT_PS3 */
    const char *path;   /* game file or folder */
    int autoplay;       /* start the game, not only mount it */
    int offline;        /* PS2/PS3: network off while it runs */
    int emulator;       /* PS1: 0 Standard (ps1_emu), 1 Netemu; PS2: 0 Default, 1 Netemu */
} LaunchReq;

/* Build the request. wm may be NULL (treated as not valid). Returns 0, or -1
 * (no path, unknown console, or too long). */
int launch_build(const LaunchReq *req, const WmConfig *wm, char *out, size_t cap);

/* ---- PS2 CONFIG files ------------------------------------------------
 * ps2_netemu reads compatibility patches from "<iso>.CONFIG" next to a PS2
 * ISO. When that file is missing, webMAN MOD (include/mount/mount_cobra.h,
 * ps2_classic.h: copy_ps2config_iso) copies the first one it finds for the
 * game's ID (SLUS_209.14), in this order:
 *   PS2CFG_APP_DIR/<ID>.CONFIG                       (the PS2 Config app's own)
 *   then for CUSTOM, NET, GX, SOFT:
 *     PS2CFG_APP_DIR/CONFIG/<type>/<ID>.CONFIG
 *     PS2CFG_MGZ_DIR/CONFIG/<type>/<ID>.CONFIG       (ManaGunZ's set)
 * and it never replaces an existing one. So spookyflow picks a config by
 * copying it to <iso>.CONFIG before launching, and goes back to "Auto" by
 * removing the copy (a file of the user's own is kept aside as
 * <iso>.CONFIG.sfbak and put back). */
enum { PS2CFG_AUTO = 0, PS2CFG_CUSTOM = 1, PS2CFG_NET = 2, PS2CFG_GX = 3, PS2CFG_SOFT = 4,
       PS2CFG_APP = 5, PS2CFG_COUNT = 6,
       PS2CFG_OWN = 6 /* only as an Auto result: the game's own <iso>.CONFIG */ };
#define PS2CFG_APP_DIR    "/dev_hdd0/game/PS2CONFIG/USRDIR"
#define PS2CFG_MGZ_DIR    "/dev_hdd0/game/MANAGUNZ0/USRDIR/sys"
#define PS2CFG_BACKUP_EXT ".sfbak"

typedef int (*IsFileFn)(void *ctx, const char *path);
const char *ps2cfg_name(int type);           /* "Auto", "CUSTOM", "NET", "GX", "SOFT", "PS2 Config app" */
/* 1 if path is a PS2 ISO that gets a CONFIG (.iso, or split .iso.0) */
int ps2cfg_is_iso(const char *path);
/* "SLUS20914" / "SLUS-20914" / "SLUS_209.14" -> "SLUS_209.14"; 0 if not an ID */
int ps2cfg_game_id(const char *id, char *out, size_t cap);
/* where webMAN finds this type's file for the game (first match); 1 = found */
int ps2cfg_find(int type, const char *gid, IsFileFn is_file, void *ctx, char *out, size_t cap);

#endif

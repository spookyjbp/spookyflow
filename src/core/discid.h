/* discid.h - read a game's title ID ("SLUS20914", "BLES80608") from inside
 * its disc image, for games whose file name doesn't carry one.
 *
 *   PS3 ISO      : disc info sector, "BLES-80608" at byte 0x810 (what webMAN reads)
 *   PS3 folder   : <folder>/PS3_GAME/PARAM.SFO, TITLE_ID
 *   PS1/PS2 image: ISO 9660 root directory -> SYSTEM.CNF "BOOT2 = cdrom0:\SLUS_209.14;1"
 *                  (PS1: "BOOT = cdrom:\..."), falling back to an executable
 *                  named like "SLUS_209.14" in the root. 2048-byte sectors
 *                  (.iso) and raw 2352-byte sectors (.bin, mode 2 or mode 1).
 *   PS1 .cue     : the first FILE "...bin" it lists.
 *
 * All file access goes through one callback so it works on the PS3 (sysFs),
 * in host tests (memory images) and later over NTFS. */
#ifndef SF_DISCID_H
#define SF_DISCID_H

#include <stddef.h>
#include <stdint.h>

/* Read up to len bytes at byte offset off of path. Return bytes read
 * (may be short at end of file) or -1 if the file can't be read. */
typedef int (*ReadAtFn)(void *ctx, const char *path, uint64_t off, void *buf, uint32_t len);

/* plat = PLAT_PS1/PS2/PS3, is_dir = path is a game folder. Writes a 9-char
 * ID to out and returns 1, or returns 0 (out = ""). */
int discid_read(int plat, const char *path, int is_dir, ReadAtFn rd, void *ctx, char *out, size_t cap);

/* Parse SYSTEM.CNF text; returns 1 and the ID from its BOOT2/BOOT line. */
int discid_parse_system_cnf(const char *txt, size_t len, char *out, size_t cap);

/* First FILE entry of a .cue sheet, joined to the cue's folder. Returns 1. */
int discid_cue_first_file(const char *cue_path, const char *txt, size_t len, char *out, size_t cap);

#endif

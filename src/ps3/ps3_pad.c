/* ps3_pad.c - read all pads into the abstract SFB_* mask.
 * Verified against PSL1GHT ppu/include/io/pad.h: ioPadInit(u32), ioPadEnd(),
 * ioPadGetInfo(padInfo*), ioPadGetData(u32, padData*), padInfo.status[],
 * padData.len and BTN_* / ANA_L_* fields. */
#include <io/pad.h>
#include <string.h>
#include "input.h"
#include "ps3_platform.h"

#define SF_PADS 7

static padInfo g_info;
static padData g_data;
static uint32_t g_last[SF_PADS];

void ps3_pad_init(void)
{
    ioPadInit(SF_PADS);
    memset(g_last, 0, sizeof(g_last));
}

void ps3_pad_end(void) { ioPadEnd(); }

static uint32_t map(const padData *d)
{
    uint32_t b = 0;
    if (d->BTN_LEFT) b |= SFB_LEFT;
    if (d->BTN_RIGHT) b |= SFB_RIGHT;
    if (d->BTN_UP) b |= SFB_UP;
    if (d->BTN_DOWN) b |= SFB_DOWN;
    if (d->BTN_CROSS) b |= SFB_CROSS;
    if (d->BTN_CIRCLE) b |= SFB_CIRCLE;
    if (d->BTN_TRIANGLE) b |= SFB_TRIANGLE;
    if (d->BTN_SQUARE) b |= SFB_SQUARE;
    if (d->BTN_L1) b |= SFB_L1;
    if (d->BTN_R1) b |= SFB_R1;
    if (d->BTN_L2) b |= SFB_L2;
    if (d->BTN_R2) b |= SFB_R2;
    if (d->BTN_START) b |= SFB_START;
    if (d->BTN_SELECT) b |= SFB_SELECT;
    /* left stick, 0..255 centered at 128, generous dead zone */
    /* stick directions have their own bits (the D-pad bits stay D-pad only) */
    if (d->ANA_L_H < 56) b |= SFB_STICK_LEFT;
    else if (d->ANA_L_H > 200) b |= SFB_STICK_RIGHT;
    if (d->ANA_L_V < 56) b |= SFB_STICK_UP;
    else if (d->ANA_L_V > 200) b |= SFB_STICK_DOWN;
    return b;
}

uint32_t ps3_pad_read(void)
{
    uint32_t mask = 0;
    int i;
    if (ioPadGetInfo(&g_info) != 0) return 0;
    for (i = 0; i < SF_PADS && i < MAX_PADS; i++) {
        if (!g_info.status[i]) { g_last[i] = 0; continue; }
        if (ioPadGetData((u32)i, &g_data) == 0 && g_data.len > 0) g_last[i] = map(&g_data);
        /* len == 0 means "no change since last read": keep the previous state */
        mask |= g_last[i];
    }
    return mask;
}

/* input.h - abstract buttons, edge detection, hold-repeat, hold-to-exit. */
#ifndef SF_INPUT_H
#define SF_INPUT_H

#include <stdint.h>

/* Prefixed SFB_ to avoid clashing with PSL1GHT's padData BTN_* fields. */
enum {
    SFB_LEFT = 1u << 0, SFB_RIGHT = 1u << 1, SFB_UP = 1u << 2, SFB_DOWN = 1u << 3,
    SFB_CROSS = 1u << 4, SFB_CIRCLE = 1u << 5, SFB_TRIANGLE = 1u << 6, SFB_SQUARE = 1u << 7,
    SFB_L1 = 1u << 8, SFB_R1 = 1u << 9, SFB_L2 = 1u << 10, SFB_R2 = 1u << 11,
    SFB_START = 1u << 12, SFB_SELECT = 1u << 13,
    /* LEFT STICK directions. Kept separate from the D-pad bits above: the
     * cover flow scrolls with them, the Settings menus ignore them. */
    SFB_STICK_LEFT = 1u << 14, SFB_STICK_RIGHT = 1u << 15,
    SFB_STICK_UP = 1u << 16, SFB_STICK_DOWN = 1u << 17
};

/* Hold-repeat timing for the four directions */
#define REPEAT_DELAY     0.30f   /* first repeat after this hold time */
#define REPEAT_SLOW      0.11f   /* first repeat interval */
#define REPEAT_FAST      0.035f  /* fastest interval */
#define REPEAT_ACCEL     1.40f   /* seconds of holding to reach the fastest interval */

typedef struct {
    uint32_t held, pressed, released;
    uint32_t fired;          /* pressed + auto-repeats (directions only) */
    float hold_t[8];         /* how long each direction (D-pad 4 + stick 4) is held */
    float next_rep[8];       /* hold time at which the next repeat fires */
} Input;

void input_reset(Input *in);
void input_update(Input *in, uint32_t held_now, float dt);

/* ---- Hold ◯ to exit ----
 * Arms only on a FRESH ◯ press made while on the main cover-flow screen
 * (main_context). Holding then fills a ring for HOLD_EXIT_TIME seconds.
 * Releasing early (or leaving the main context) cancels; the displayed ring
 * animates back down. A press that closed a panel never arms because the
 * panel was open (main_context = 0) when the press happened. */
#define HOLD_EXIT_TIME    1.5f
#define HOLD_EXIT_UNWIND  3.0f   /* ring units per second when animating back */

typedef struct {
    int armed;
    float t;        /* seconds held while armed */
    float shown;    /* 0..1 ring fraction for drawing */
    int done;       /* latched when the hold completes */
} HoldExit;

void hold_exit_reset(HoldExit *h);
/* pressed_edge/held refer to ◯ this frame. Returns 1 on the frame the hold completes. */
int  hold_exit_update(HoldExit *h, int pressed_edge, int held, int main_context, float dt);

#endif

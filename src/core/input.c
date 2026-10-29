/* input.c - edge detection, accelerating hold-repeat, hold-to-exit. */
#include "input.h"
#include <string.h>

static const uint32_t k_dirs[8] = { SFB_LEFT, SFB_RIGHT, SFB_UP, SFB_DOWN,
                                     SFB_STICK_LEFT, SFB_STICK_RIGHT, SFB_STICK_UP, SFB_STICK_DOWN };

void input_reset(Input *in) { memset(in, 0, sizeof(*in)); }

void input_update(Input *in, uint32_t held_now, float dt)
{
    int i;
    uint32_t prev = in->held;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    in->held = held_now;
    in->pressed = held_now & ~prev;
    in->released = prev & ~held_now;
    in->fired = in->pressed;
    for (i = 0; i < 8; i++) {
        uint32_t b = k_dirs[i];
        if (!(held_now & b)) { in->hold_t[i] = 0.0f; in->next_rep[i] = REPEAT_DELAY; continue; }
        if (in->pressed & b) { in->hold_t[i] = 0.0f; in->next_rep[i] = REPEAT_DELAY; continue; }
        in->hold_t[i] += dt;
        if (in->hold_t[i] >= in->next_rep[i]) {
            float held_for = in->hold_t[i] - REPEAT_DELAY;
            float k = held_for / REPEAT_ACCEL;
            float interval;
            if (k > 1.0f) k = 1.0f;
            if (k < 0.0f) k = 0.0f;
            interval = REPEAT_SLOW + (REPEAT_FAST - REPEAT_SLOW) * k;
            in->fired |= b;
            in->next_rep[i] += interval;
            /* after a long hitch don't burst-fire; resync */
            if (in->next_rep[i] < in->hold_t[i]) in->next_rep[i] = in->hold_t[i] + interval;
        }
    }
}

void hold_exit_reset(HoldExit *h) { memset(h, 0, sizeof(*h)); }

int hold_exit_update(HoldExit *h, int pressed_edge, int held, int main_context, float dt)
{
    if (h->done) { h->shown = 1.0f; return 0; }
    if (pressed_edge && main_context) { h->armed = 1; h->t = 0.0f; }
    if (!held || !main_context) { h->armed = 0; h->t = 0.0f; }
    if (h->armed) {
        h->t += dt;
        h->shown = h->t / HOLD_EXIT_TIME;
        if (h->t >= HOLD_EXIT_TIME) {
            h->shown = 1.0f;
            h->done = 1;
            h->armed = 0;
            return 1;
        }
    } else {
        h->shown -= HOLD_EXIT_UNWIND * dt;
        if (h->shown < 0.0f) h->shown = 0.0f;
    }
    return 0;
}

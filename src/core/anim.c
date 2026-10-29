/* anim.c - critically damped spring and easing. */
#include "anim.h"
#include "util.h"
#include <math.h>

void spring_init(Spring *s, float x, float omega)
{
    s->x = x; s->v = 0.0f; s->target = x; s->omega = omega;
}

void spring_set(Spring *s, float target) { s->target = target; }

void spring_snap(Spring *s, float x) { s->x = x; s->v = 0.0f; s->target = x; }

void spring_update(Spring *s, float dt)
{
    float w = s->omega, x0, e, tmp;
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;               /* avoid huge jumps after hitches */
    x0 = s->x - s->target;
    e = expf(-w * dt);
    tmp = (s->v + w * x0) * dt;
    s->x = s->target + (x0 + tmp) * e;
    s->v = (s->v - w * tmp) * e;
    if (sf_absf(s->x - s->target) < 1e-4f && sf_absf(s->v) < 1e-3f) {
        s->x = s->target; s->v = 0.0f;
    }
}

int spring_settled(const Spring *s) { return s->x == s->target && s->v == 0.0f; }

int anim_approach(float *v, float target, float rate, float dt)
{
    float step = rate * dt;
    if (*v < target) { *v += step; if (*v >= target) { *v = target; return 1; } return 0; }
    if (*v > target) { *v -= step; if (*v <= target) { *v = target; return 1; } return 0; }
    return 1;
}

float ease_out_cubic(float t)
{
    t = sf_clampf(t, 0.0f, 1.0f);
    t = 1.0f - t;
    return 1.0f - t * t * t;
}

float ease_in_out(float t)
{
    t = sf_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float ease_out_back(float t)
{
    const float c1 = 1.20f, c3 = c1 + 1.0f;
    float u;
    t = sf_clampf(t, 0.0f, 1.0f);
    u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

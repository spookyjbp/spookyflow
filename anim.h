/* anim.h - frame-time based animation helpers. */
#ifndef SF_ANIM_H
#define SF_ANIM_H

/* Critically damped spring, solved analytically per step (stable for any dt). */
typedef struct {
    float x, v, target;
    float omega;          /* angular frequency; settle(1%) ~= 6.6/omega seconds */
} Spring;

#define SPRING_OMEGA_300MS 22.0f

void  spring_init(Spring *s, float x, float omega);
void  spring_set(Spring *s, float target);
void  spring_snap(Spring *s, float x);
void  spring_update(Spring *s, float dt);
int   spring_settled(const Spring *s);

/* Move *v toward target by at most rate*dt. Returns 1 when reached. */
int   anim_approach(float *v, float target, float rate, float dt);

float ease_out_cubic(float t);
float ease_in_out(float t);       /* smoothstep */
float ease_out_back(float t);

#endif

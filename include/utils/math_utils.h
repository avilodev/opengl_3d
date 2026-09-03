#ifndef MATH_UTILS_H
#define MATH_UTILS_H

/* Small scalar helpers shared across gameplay and rendering. Header-only so
   they inline and need no translation unit of their own. */

#define MATH_PI 3.14159265358979323846
#define MATH_DEG_TO_RAD 0.01745329251994329577f

static inline float clampf(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Moves `value` toward `target` by at most `amount`, without overshooting. */
static inline float approachf(float value, float target, float amount) {
    if (value < target) {
        float stepped = value + amount;
        return stepped > target ? target : stepped;
    }
    float stepped = value - amount;
    return stepped < target ? target : stepped;
}

static inline float lerpf(float from, float to, float t) {
    return from + (to - from) * t;
}

/* Smoothstep easing on an already-normalized t. */
static inline float smoothstepf(float t) {
    return t * t * (3.0f - 2.0f * t);
}

#endif

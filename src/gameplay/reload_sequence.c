#include "gameplay/reload_sequence.h"
#include "utils/math_utils.h"

#include <math.h>

/* The ease shapes. Every one of them maps 0 to 0 and 1 to 1 - what differs is
   only where the speed is, which is the whole content of them.

   Shaped HERE, inside the sample, rather than by each consumer. The arms' hand
   target, the shoulder placement and the loose magazine all take `t` and lerp
   with it, and so does the check in `make probe`; a shape applied by each of
   them separately is three chances for the hand, the shoulder behind it and the
   magazine in it to be at three different points along the same segment. */
static float ease(ReloadEase shape, float t) {
    switch (shape) {
    case RELOAD_EASE_SMOOTH:
        return t * t * (3.0f - 2.0f * t);
    case RELOAD_EASE_SNAP:
        /* Cubic out. Most of the distance is covered in the first third, which
           is what a strip is: the release lets go and the arm is already
           moving. */
        {
            float remaining = 1.0f - t;
            return 1.0f - remaining * remaining * remaining;
        }
    case RELOAD_EASE_DRIVE:
        /* Quadratic in - fastest at the end, so the motion arrives rather than
           settles. Squared and not cubed: a hand driving a magazine home starts
           from a hand that is already carrying it, so there is momentum in it
           from the first frame. */
        return t * t;
    case RELOAD_EASE_HOLD:
    case RELOAD_EASE_THROUGH:
    default:
        return t;
    }
}

/* Sampling is a scan from the front. The track is a dozen keys read once a
   frame, so anything cleverer would cost more to write than it saves. */
void reload_sequence_sample(const ReloadSequence* sequence, float progress,
                            ReloadSample* out) {
    const ReloadKey* keys = sequence->keys;
    int count = sequence->count;

    if (progress <= keys[0].at) {
        out->from = out->to = &keys[0];
        out->t = 0.0f;
        return;
    }
    for (int i = 1; i < count; ++i) {
        if (progress > keys[i].at) continue;
        float span = keys[i].at - keys[i - 1].at;
        out->from = &keys[i - 1];
        out->to = &keys[i];
        float linear = span > 1e-6f ? (progress - keys[i - 1].at) / span : 0.0f;
        /* The shape belongs to the key being arrived at, so it is the far end
           of the span that decides how the hand crosses it. */
        out->t = ease(keys[i].ease, linear);
        return;
    }
    out->from = out->to = &keys[count - 1];
    out->t = 0.0f;
}

/* Smoothstep in and out, so the gun neither snaps into the reload nor stops
   dead coming out of it. The rise is quicker than the fall: bringing the weapon
   in is the player's decision and reads as deliberate, while letting it back
   out is the weapon settling and wants to be unhurried. */
#define RELOAD_GUN_IN 0.16f
#define RELOAD_GUN_OUT 0.26f

/* How long a jolt takes to die, as a fraction of the reload. About four
   hundredths of a second on a 2.4s reload: an impact, not a wobble. */
#define RELOAD_JOLT_DECAY 0.018f

/* On instantly and out fast. Squared rather than linear so the fall is quick
   at first and then trails, which is what a hand absorbing a knock does - a
   linear decay reads as the gun being lowered back into place. */
static float spike(float progress, float at) {
    float since = progress - at;
    if (since < 0.0f || since >= RELOAD_JOLT_DECAY) return 0.0f;
    float remaining = 1.0f - since / RELOAD_JOLT_DECAY;
    return remaining * remaining;
}

float reload_sequence_seat_jolt(const ReloadSequence* sequence, float progress) {
    /* The larger of the two rather than their sum. They do not overlap at the
       timings any sane track states, and if a track ever puts them close enough
       to, two knocks adding into one twice as large is not what a tap closer to
       the seat should look like. */
    float seat = spike(progress, sequence->seat_at);
    float tap = spike(progress, sequence->tap_at);
    return seat > tap ? seat : tap;
}

float reload_sequence_gun_blend(float progress) {
    float shape = 1.0f;
    if (progress < RELOAD_GUN_IN)
        shape = progress / RELOAD_GUN_IN;
    else if (progress > 1.0f - RELOAD_GUN_OUT)
        shape = (1.0f - progress) / RELOAD_GUN_OUT;
    if (shape <= 0.0f) return 0.0f;
    if (shape >= 1.0f) return 1.0f;
    return shape * shape * (3.0f - 2.0f * shape);
}

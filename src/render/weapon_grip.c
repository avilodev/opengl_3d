/* See include/render/weapon_grip.h for why one module rather than a table in
   each of the two views that need it. */
#include "render/weapon_grip.h"

#include <math.h>
#include <stddef.h>

/* Which way the fingers point, in the WEAPON's own frame, so the wrists follow
   the weapon rather than the body. Rotating a bone onto a direction leaves its
   roll free, and these are half of what decides it.

   This rig's hand is a closed fist, 0.13m from wrist to fingertip against a
   handguard 0.06m across - it cannot wrap the tube, so whatever it does not
   wrap overshoots. Aimed across and up the support fist crossed the bore and
   came out the far side of the receiver level with the optic; run forward along
   the tube the overshoot goes where a support hand already is. */
static const float HAND_DIR[WEAPON_HOLD_KIND_COUNT][2][3] = {
    /* TWO_POINT: firing hand forward across the grip, support hand along the
       handguard with a little wrap. */
    { { 0.00f, -0.15f, -0.99f }, { 0.30f,  0.10f, -0.95f } },
    /* HANDS_TOGETHER: both forward, the support hand rolled slightly under the
       firing one rather than across a tube it does not have. */
    { { 0.00f, -0.15f, -0.99f }, { 0.18f, -0.28f, -0.94f } },
    /* HILT: both hands the same, because on a tsuka they are doing the same
       thing - two fists in a row wrapped around one shaft, thumbs toward the
       guard. Run along the hilt for the reason the rifle's support hand is: a
       0.03m grip is far thinner than this rig's 0.13m fist, so whatever the
       fist does not wrap has to overshoot somewhere, and along the hilt is
       where the other hand already is. */
    { { 0.08f, -0.12f, -0.99f }, { 0.08f, -0.12f, -0.99f } },
};

/* Which way the PALM faces, in the weapon's frame. The second half of a wrist,
   and the half that was missing.

   Pointing a bone onto a direction is a minimal rotation, so aiming the fingers
   settles where they go and leaves the palm free to spin around them - and what
   it spun to was whatever the elbow's own unconstrained roll happened to hand
   down. Measured in the gun's frame, the same firing grip came out palm-inward
   at the hip and palm-DOWN at the shoulder and through the sprint carry: a
   quarter turn of wrist between two poses of one hold, on a hand that never let
   go of the grip. The arm was tracking the gun and the wrist was not.

   The shooting hand wraps the grip from its right, so the palm faces across the
   weapon toward the centreline, tilted a fifth down - which is what puts the
   thumb side up the grip's rearward cant rather than straight up.

   A gun's support hand is deliberately left free (all zeroes). It is the hand
   the reload track authors a roll for, key by key, against this same spin -
   constraining it means re-basing every one of those angles, and a pass at that
   is what an earlier attempt reverted from. Its wrist has the same defect and
   wants the same fix, once the reload rolls are re-measured against a base that
   holds still and the result is looked at rather than reasoned about. */
static const float PALM_DIR[WEAPON_HOLD_KIND_COUNT][2][3] = {
    /* TWO_POINT */
    { { -1.00f, -0.20f, 0.00f }, { 0.00f, 0.00f, 0.00f } },
    /* HANDS_TOGETHER */
    { { -1.00f, -0.20f, 0.00f }, { 0.00f, 0.00f, 0.00f } },
    /* HILT: both constrained, and to the SAME direction - two hands wrapping
       one shaft wrap it from the same side, so their palms face the same way
       across it. The support hand is left free on a gun only because the reload
       track authors a roll for it key by key against that freedom; a sword has
       no reload, so nothing here is owed that debt and the wrist that would
       otherwise roll to wherever its elbow left it is pinned. */
    { { -1.00f, -0.15f, 0.00f }, { -1.00f, -0.15f, 0.00f } },
};

static int in_range(int hold, int hand) {
    return hold >= 0 && hold < WEAPON_HOLD_KIND_COUNT && hand >= 0 && hand < 2;
}

const float* weapon_grip_hand_dir(int hold, int hand) {
    return in_range(hold, hand) ? HAND_DIR[hold][hand] : NULL;
}

const float* weapon_grip_palm_dir(int hold, int hand) {
    return in_range(hold, hand) ? PALM_DIR[hold][hand] : NULL;
}

static int normalize3(const float in[3], float out[3]) {
    float len = sqrtf(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (len < 1e-6f) return 0;
    for (int c = 0; c < 3; ++c) out[c] = in[c] / len;
    return 1;
}

int weapon_grip_hand_rotation(int hold, int hand, Mat4* out) {
    if (!out || !in_range(hold, hand)) return 0;

    float fingers[3];
    if (!normalize3(HAND_DIR[hold][hand], fingers)) return 0;

    /* A free palm has no rotation to give. Saying so is the point: inventing
       one here is the exact defect this module exists to remove, and a caller
       that needs a full orientation for a hand the table leaves loose is asking
       a question nobody has answered yet. */
    float palm[3];
    if (!normalize3(PALM_DIR[hold][hand], palm)) return 0;

    /* Gram-Schmidt, and the fingers win. The palm supplies only the roll around
       them, because a hand that has let go of the grip reads as broken in a way
       a hand rolled five degrees off does not - so the axis that must be exact
       is the one the grip runs along. */
    float dot = palm[0] * fingers[0] + palm[1] * fingers[1] + palm[2] * fingers[2];
    float across[3];
    for (int c = 0; c < 3; ++c) across[c] = palm[c] - dot * fingers[c];
    if (!normalize3(across, across)) return 0;

    /* The third axis, which lands on +z: the rotation takes `fingers` onto +y
       and `across` onto -x, and y cross -x is +z. */
    float up[3] = {
        fingers[1] * across[2] - fingers[2] * across[1],
        fingers[2] * across[0] - fingers[0] * across[2],
        fingers[0] * across[1] - fingers[1] * across[0],
    };

    /* The ROWS are the weapon-frame vectors that map onto the hand's own axes:
       -across onto x, fingers onto y, up onto z. Storage is column-major, so
       element (row r, column c) is m[c*4+r] - see utils/mat4.h. */
    Mat4 m = mat4_identity();
    for (int c = 0; c < 3; ++c) {
        m.m[c * 4 + 0] = -across[c];
        m.m[c * 4 + 1] = fingers[c];
        m.m[c * 4 + 2] = up[c];
    }
    *out = m;
    return 1;
}

/* See include/render/viewmodel_neck.h for what this is and why it is one
   module rather than an angle recomposed at each of its three call sites. */
#include "render/viewmodel_neck.h"
#include "gameplay/movement_tuning.h"
#include "utils/mat4.h"

#include <math.h>
#include <string.h>

/* Where the skull turns, once measured. Module state rather than a parameter
   because it is a property of the rig and not of the frame: it is measured once
   when the arms are built and read by three consumers that have no reason to
   each be handed it. The eye until then - see the header. */
static float g_pivot[3] = {0.0f, 0.0f, 0.0f};

void viewmodel_neck_set_pivot(const float pivot[3]) {
    if (!pivot) return;
    for (int c = 0; c < 3; ++c) g_pivot[c] = pivot[c];
}

/* The frame's head-off-chest yaw - see the header for why this is module state
   like the pivot and for a different reason than the pivot's. Zero until the
   renderer speaks, which is a neck that does not turn: the right fallback for a
   frame before the body has been animated, and exactly what this module did
   before the yaw existed. */
static float g_look_yaw = 0.0f;

void viewmodel_neck_set_look_yaw(float degrees) { g_look_yaw = degrees; }

float viewmodel_neck_head_yaw(float look_yaw_degrees) {
    /* The same curve as the pitch's, against a limit less than half as long.

       The shape is right here for the same reason it is right there - a clamp
       would put a corner in the middle of a mouse movement - but the limit is
       doing a different job. The pitch's is anatomy: where a neck runs out and a
       spine takes over. This one is not, because the body ANSWERS a turn. The
       chest comes round and the feet step, so whatever this saturates is gone
       within a fraction of a second no matter how far the player swung; the
       limit is only how much of that is allowed to show on the way. */
    float limit = VIEWMODEL_NECK_YAW_LIMIT;
    if (limit <= 0.0f) return 0.0f;
    return limit * tanhf(look_yaw_degrees / limit);
}

float viewmodel_neck_head_pitch(float view_pitch_degrees) {
    /* A saturation, not a clamp. The neck supplies the first part of a look and
       the spine supplies the rest, and where one hands over to the other is not
       a corner: a clamp would stop the viewmodel dead in the middle of a mouse
       movement, at a pitch the player crosses constantly.

       Near zero this is the identity - a small look is all neck, which is
       true. Past the limit it flattens out, which is also true: at forty-odd
       degrees you have stopped craning your head and started leaning your
       whole back, and a leaning back carries the arms with it. The flattening
       is what stops the viewmodel from rotating eighty-nine degrees out of the
       world at the pitch clamp and never coming back. */
    float limit = VIEWMODEL_HEAD_PITCH_LIMIT;
    if (limit <= 0.0f) return 0.0f;
    return limit * tanhf(view_pitch_degrees / limit);
}

void viewmodel_neck_build(ViewmodelNeck* out, const WeaponSpec* spec,
                          float view_pitch_degrees, float lean, float ads) {
    if (!out) return;
    float follow = spec ? spec->neck_follow : 0.0f;
    /* The aim fade. At full aim the neck is zero, the gun is welded to the eye,
       and the optic lands on the crosshair at every pitch - which is the one
       thing here that is not allowed to be a matter of taste. It is also the
       anatomy: bringing a gun up to aim is bringing the eye, the head and the
       gun into one line. */
    float fade = 1.0f - ads;
    if (fade < 0.0f) fade = 0.0f;
    /* Negative, because the torso did not move. Relative to a camera that
       pitched up, the body pitched down. */
    out->pitch = -viewmodel_neck_head_pitch(view_pitch_degrees) * follow * fade;
    /* Negative for the same reason, and about the other axis: the chest stayed
       where it was, so relative to a head that turned left the body is right.

       Off module state rather than a parameter because the one module that can
       answer this is the renderer - see the header. A caller that has never set
       it gets zero, which is a body square under its own head. */
    out->yaw = -viewmodel_neck_head_yaw(g_look_yaw) * follow * fade;
    out->roll = lean * VIEWMODEL_LEAN_ROLL;
    for (int c = 0; c < 3; ++c) out->pivot[c] = g_pivot[c];
}

void viewmodel_neck_matrix(const ViewmodelNeck* neck, float out[16]) {
    if (!out) return;
    Mat4 m = mat4_identity();
    if (neck) {
        /* The lean outside the neck, which is the order the GL draw named them
           in before this module existed: the cant is applied to the whole
           carried body, and the neck turns the body inside it. Nine degrees of
           roll against forty-five of pitch, so the two orders differ by little
           on screen - but they differ, and one of them has to be written down
           somewhere the three consumers all read. */
        m = mat4_multiply(m, mat4_rotation_degrees(neck->roll, 0, 0, 1));
        m = mat4_multiply(m, mat4_translation(neck->pivot[0], neck->pivot[1],
                                              neck->pivot[2]));
        /* Yaw outside the pitch, which is the order the joints are in: a head
           turns low in the neck and nods at the skull on top of that. Swapped,
           a body that had turned would nod about an axis that had gone with it,
           and a look up-and-left would arrive with a roll in it that nobody
           asked for. */
        m = mat4_multiply(m, mat4_rotation_degrees(neck->yaw, 0, 1, 0));
        m = mat4_multiply(m, mat4_rotation_degrees(neck->pitch, 1, 0, 0));
        m = mat4_multiply(m, mat4_translation(-neck->pivot[0], -neck->pivot[1],
                                              -neck->pivot[2]));
    }
    memcpy(out, m.m, sizeof m.m);
}

void viewmodel_neck_transform_point(const ViewmodelNeck* neck,
                                    const float in[3], float out[3]) {
    if (!in || !out) return;
    Mat4 m;
    viewmodel_neck_matrix(neck, m.m);
    float p[3] = {in[0], in[1], in[2]};
    mat4_transform_point(&m, p, out);
}

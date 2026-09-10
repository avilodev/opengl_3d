#include "gameplay/traversal.h"
#include "gameplay/arena.h"
#include "utils/math_utils.h"

#include <math.h>
#include <stddef.h>

/* The view matrix rotates world into view as Rz(-roll)Rx(-pitch)Ry(-yaw), so
   its inverse - view into world - is M = Ry(yaw)Rx(pitch)Rz(roll). The three
   basis vectors are M applied to view-space right, up and forward, which is
   just three columns of M. */
void traversal_view_basis(float yaw_degrees, float pitch_degrees,
                          float roll_degrees,
                          float forward[3], float right[3], float up[3]) {
    float ca = cosf(yaw_degrees * MATH_DEG_TO_RAD);
    float sa = sinf(yaw_degrees * MATH_DEG_TO_RAD);
    float cb = cosf(pitch_degrees * MATH_DEG_TO_RAD);
    float sb = sinf(pitch_degrees * MATH_DEG_TO_RAD);
    float cc = cosf(roll_degrees * MATH_DEG_TO_RAD);
    float sc = sinf(roll_degrees * MATH_DEG_TO_RAD);

    /* Third column, negated: view forward is (0, 0, -1). Roll cancels out of
       it, which is why aiming can ignore roll and the gun cannot. */
    if (forward) {
        forward[0] = -sa * cb;
        forward[1] = sb;
        forward[2] = -ca * cb;
    }
    if (right) {
        right[0] = ca * cc + sa * sb * sc;
        right[1] = cb * sc;
        right[2] = -sa * cc + ca * sb * sc;
    }
    if (up) {
        up[0] = -ca * sc + sa * sb * cc;
        up[1] = cb * cc;
        up[2] = sa * sc + ca * sb * cc;
    }
}

void traversal_aim_direction(float yaw_degrees, float pitch_degrees,
                             float* x, float* y, float* z) {
    float forward[3];
    traversal_view_basis(yaw_degrees, pitch_degrees, 0.0f, forward, NULL, NULL);
    *x = forward[0];
    *y = forward[1];
    *z = forward[2];
}

/* Slab test against an axis-aligned box. The last axis to raise the entry time
   is the face the ray came in through, which is what gives the hit its normal:
   travelling along +axis enters through the low face, so the outward normal
   there points -axis. */

typedef struct {
    float near_time;
    float far_time;
    int entry_axis;      /* -1 until a slab has claimed the entry */
    float entry_sign;
} SlabState;

static int ray_slab(SlabState* state, int axis, float origin, float direction,
                    float minimum, float maximum) {
    if (fabsf(direction) < 0.00001f)
        return origin >= minimum && origin <= maximum;
    float enter = (minimum - origin) / direction;
    float exit = (maximum - origin) / direction;
    float sign = -1.0f;
    if (enter > exit) {
        float swap = enter; enter = exit; exit = swap;
        sign = 1.0f;
    }
    if (enter > state->near_time) {
        state->near_time = enter;
        state->entry_axis = axis;
        state->entry_sign = sign;
    }
    if (exit < state->far_time) state->far_time = exit;
    return state->near_time <= state->far_time;
}

int traversal_raycast_walls(float ox, float oy, float oz,
                            float dx, float dy, float dz,
                            float max_distance, TraversalHit* hit) {
    float closest = max_distance;
    int found = 0;
    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        SlabState state = {0.0f, closest, -1, 0.0f};
        if (!ray_slab(&state, 0, ox, dx, wall->x - wall->width * .5f,
                      wall->x + wall->width * .5f) ||
            !ray_slab(&state, 1, oy, dy, wall->base, wall->top) ||
            !ray_slab(&state, 2, oz, dz, wall->z - wall->depth * .5f,
                      wall->z + wall->depth * .5f)) continue;
        /* entry_axis stays -1 only when the origin is already inside the box,
           and a ray does not hit the thing it starts in. */
        if (state.entry_axis < 0 || state.near_time >= closest) continue;
        closest = state.near_time;
        hit->x = ox + dx * state.near_time;
        hit->y = oy + dy * state.near_time;
        hit->z = oz + dz * state.near_time;
        hit->normal_x = state.entry_axis == 0 ? state.entry_sign : 0.0f;
        hit->normal_y = state.entry_axis == 1 ? state.entry_sign : 0.0f;
        hit->normal_z = state.entry_axis == 2 ? state.entry_sign : 0.0f;
        hit->wall_index = i;
        found = 1;
    }
    return found;
}

#include "render/arm_placement.h"
#include "utils/math_utils.h"

#include <math.h>
#include <stddef.h>

/* Written out rather than built as matrices because the inverse is wanted every
   frame and a yaw inverts by negating one term - no general inverse to get
   wrong. */
void arm_placement_rig_to_camera(const float in[3], float blade, float out[3]) {
    float a = blade * MATH_DEG_TO_RAD, c = cosf(a), sn = sinf(a);
    /* The facing: a 180 degree yaw mirrors x and z. */
    float x = -in[0], z = -in[2];
    out[0] = c * x - sn * z;
    out[1] = in[1];
    out[2] = sn * x + c * z;
}

void arm_placement_rig_from_camera(const float in[3], float blade,
                                   float out[3]) {
    float a = blade * MATH_DEG_TO_RAD, c = cosf(a), sn = sinf(a);
    float x =  c * in[0] + sn * in[2];
    float z = -sn * in[0] + c * in[2];
    out[0] = -x;
    out[1] =  in[1];
    out[2] = -z;
}

void arm_placement_anchor(ArmPlacement placement, float out[3]) {
    out[0] = 0.0f;
    out[1] = -ARMS_SHOULDER_DOWN;
    out[2] = placement.z;
}

void arm_placement_shoulder(const ArmRig* rig, int arm,
                            ArmPlacement placement, float out[3]) {
    float turned[3];
    arm_placement_rig_to_camera(rig->shoulder_offset[arm], placement.blade,
                                turned);
    float anchor[3];
    arm_placement_anchor(placement, anchor);
    out[0] = anchor[0] + turned[0] * rig->scale;
    out[1] = anchor[1] + turned[1] * rig->scale;
    out[2] = anchor[2] + turned[2] * rig->scale;
}

float arm_placement_reach_fraction(const ArmRig* rig, int arm,
                                   const float target[3],
                                   ArmPlacement placement) {
    float shoulder[3];
    arm_placement_shoulder(rig, arm, placement, shoulder);
    float d = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float e = target[c] - shoulder[c];
        d += e * e;
    }
    return sqrtf(d) / ARMS_REACH;
}

/* How badly a candidate placement misses. Zero is both arms at ARMS_COMFORT.
   A straightened arm and a shoulder inside the frame are refused outright
   rather than scored: the first stops the hand tracking the gun and the second
   puts a deltoid on screen, and neither is worth trading a better bend for. */
static float placement_cost(const ArmRig* rig, const float targets[2][3],
                            ArmPlacement placement, float half_fov_degrees) {
    float cost = 0.0f;
    for (int a = 0; a < 2; ++a) {
        float shoulder[3];
        arm_placement_shoulder(rig, a, placement, shoulder);

        /* In front of the eye and inside the vertical field of view means the
           shoulder is on screen. Vertical only, because it is the tighter of
           the two and needs no aspect ratio to test. */
        if (shoulder[2] < -0.02f) {
            float below = atanf(fabsf(shoulder[1]) / -shoulder[2]) /
                          MATH_DEG_TO_RAD;
            if (below < half_fov_degrees + 4.0f) return 1e9f;
        }

        float fraction = arm_placement_reach_fraction(rig, a, targets[a],
                                                      placement);
        if (fraction > ARMS_COMFORT_MAX) return 1e9f;
        cost += (fraction - ARMS_COMFORT) * (fraction - ARMS_COMFORT);
    }
    return cost;
}

int arm_placement_solve_near(const ArmRig* rig, const float targets[2][3],
                             float half_fov_degrees,
                             const ArmPlacement* previous, ArmPlacement* out) {
    float best = 1e9f;
    for (float blade = 0.0f; blade <= ARMS_SEARCH_BLADE_MAX;
         blade += ARMS_SEARCH_BLADE_STEP) {
        for (float z = ARMS_SEARCH_Z_MIN; z <= ARMS_SEARCH_Z_MAX;
             z += ARMS_SEARCH_Z_STEP) {
            ArmPlacement candidate = {z, blade};
            float cost = placement_cost(rig, targets, candidate,
                                        half_fov_degrees);
            /* Added after the refusals, never before: a placement that puts a
               shoulder on screen or straightens an arm is out regardless of how
               close to the last one it sits, and 1e9 plus a nudge is still
               out. */
            if (cost < 1e9f && previous) {
                float dz = candidate.z - previous->z;
                float db = (candidate.blade - previous->blade) /
                           ARMS_CONTINUITY_BLADE;
                cost += ARMS_CONTINUITY * (dz * dz + db * db);
            }
            if (cost >= best) continue;
            best = cost;
            *out = candidate;
        }
    }
    return best < 1e9f;
}

int arm_placement_solve(const ArmRig* rig, const float targets[2][3],
                        float half_fov_degrees, ArmPlacement* out) {
    return arm_placement_solve_near(rig, targets, half_fov_degrees, NULL, out);
}

void arm_placement_to_model_space(const ArmRig* rig, const float camera[3],
                                  const float anchor[3], float blade,
                                  float out[3]) {
    float local[3], turned[3];
    for (int c = 0; c < 3; ++c) local[c] = (camera[c] - anchor[c]) / rig->scale;
    arm_placement_rig_from_camera(local, blade, turned);
    for (int c = 0; c < 3; ++c) out[c] = rig->shoulder_mid[c] + turned[c];
}

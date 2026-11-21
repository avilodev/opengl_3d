#include "render/character_rig_internal.h"
#include "render/arm_geometry.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

/* Pose layers applied over the base clip: the aim offset, the facing twist and
   lean channels, and the additive hit reaction. */

/* ---- channel shares ----
   Per-bone shares for each channel, applied about a model-space axis.

   legs:  pelvis turns the legs; spine returns it (sums to 0 above the pelvis,
          so the chest stays on aim). Spread over three joints to avoid folding.
   look:  head turn; sums to 1. Chest takes a quarter so the turn reads as the
          body, not just the eyes.
   lean:  sideways peek about model forward; accumulates up the chain. No pelvis
          share, since that would slide the feet.
   hit_*: measured from Hit_Chest / Hit_Head at peak frames. The animator's
          pelvis share on Hit_Chest was moved to spine_01 to keep legs free.
            Hit_Chest 0.33s, 49.6 deg: pelvis 20.9 spine_02 19.2 spine_03 19.3
                                        neck 13.4 head 27.2 (%)
            Hit_Head  0.23s, 38.5 deg: spine_02 10.3 spine_03 10.3 neck 22.7
                                        head 56.6 (%) */
static const struct {
    const char* bone;
    float legs;
    float look;
    float lean;
    float hit_body;
    float hit_head;
} TWIST_BONES[CHARACTER_TWIST_BONES] = {
    {"pelvis",    1.00f,  0.00f,  0.00f,  0.000f,  0.000f},
    {"spine_01", -0.34f,  0.00f,  0.28f,  0.209f,  0.000f},
    {"spine_02", -0.33f,  0.00f,  0.28f,  0.192f,  0.103f},
    {"spine_03", -0.33f,  0.25f,  0.24f,  0.193f,  0.103f},
    {"neck_01",   0.00f,  0.40f,  0.12f,  0.134f,  0.227f},
    {"Head",      0.00f,  0.35f,  0.08f,  0.272f,  0.566f},
};

/* Total chain bend at flinch 1.0 (measured peaks above). */
#define HIT_BODY_DEGREES 49.6f
#define HIT_HEAD_DEGREES 38.5f

/* ---- aim layer ---- */
#define AIM_UPPER_ROOT "spine_02"
#define AIM_ARM_ROOT_L "clavicle_l"
#define AIM_ARM_ROOT_R "clavicle_r"

/* Blend parameter at level pitch. */
#define AIM_LEVEL 0.442f

/* Blend per degree of pitch at the hip. Deliberately under 1:1 (1/173) so the
   extremes don't read as a stoop or a salute. */
#define AIM_PITCH_GAIN 0.0040f

/* On the sight, track pitch exactly. */
#define AIM_PITCH_GAIN_ADS (1.0f / 173.0f)

/* How far the arms drop toward Aim_Down at the hip. Arms only; blending the
   spine would bow the head. */
#define AIM_CARRY_LOWER 0.60f

/* Accumulated rotation of every ancestor of `node`. Exact because skeleton
   chains carry no scale. */
static void chain_rotation(const Skeleton* skeleton, const Pose* pose, int node,
                           float out[4]) {
    int chain[SKIN_MAX_NODES];
    int depth = 0;
    for (int i = skeleton->parent[node]; i >= 0 && depth < SKIN_MAX_NODES;
         i = skeleton->parent[i])
        chain[depth++] = i;
    out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
    for (int i = depth - 1; i >= 0; --i)
        quat_multiply(out, pose->local[chain[i]].rotation, out);
}

/* Inverse of a unit quaternion. */
static void quat_inverse(const float q[4], float out[4]) {
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

/* Fraction of a rotation via nlerp from identity, taking the short way. */
static void quat_fraction(const float q[4], float amount, float out[4]) {
    float sign = q[3] >= 0.0f ? 1.0f : -1.0f;
    float x = q[0] * sign * amount;
    float y = q[1] * sign * amount;
    float z = q[2] * sign * amount;
    float w = 1.0f - amount + q[3] * sign * amount;
    float length = sqrtf(x*x + y*y + z*z + w*w);
    if (length < 1e-8f) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }
    out[0] = x / length; out[1] = y / length; out[2] = z / length; out[3] = w / length;
}

static void resolve_twist_bones(CharacterAnimation* animation, const SkinnedModel* model) {
    animation->twist_legs = 1;
    for (int i = 0; i < CHARACTER_TWIST_BONES; ++i) {
        animation->twist_node[i] = arm_find_node(&model->skeleton, TWIST_BONES[i].bone);
        if (animation->twist_node[i] < 0 && i < CHARACTER_TWIST_LEG_BONES)
            animation->twist_legs = 0;
    }
    /* Leg chain is all-or-nothing: a partial chain won't cancel above the
       waist and would swing the gun. Look bones degrade individually. */
    if (!animation->twist_legs)
        for (int i = 0; i < CHARACTER_TWIST_LEG_BONES; ++i)
            animation->twist_node[i] = -1;
    animation->twist_count = 0;
    for (int i = 0; i < CHARACTER_TWIST_BONES; ++i)
        if (animation->twist_node[i] >= 0) animation->twist_count++;
}

void character_layers_init(CharacterAnimation* animation, const SkinnedModel* model) {
    /* Missing bones give empty masks, which disables the layer. */
    skinned_model_mask_subtree(model, AIM_UPPER_ROOT, &animation->upper);
    animation->upper_root = arm_find_node(&model->skeleton, AIM_UPPER_ROOT);
    skinned_model_mask_subtree(model, AIM_ARM_ROOT_L, &animation->arms);
    PoseMask right;
    if (skinned_model_mask_subtree(model, AIM_ARM_ROOT_R, &right))
        for (int i = 0; i < SKIN_MAX_NODES; ++i)
            animation->arms.node[i] |= right.node[i];
    resolve_twist_bones(animation, model);
}

/* Locks chest orientation to the authored aim pose while aiming, cancelling
   pelvis motion from the locomotion clip (measured up to 22 degrees of
   barrel error). Orientation only; the chest still bobs with the stride.
   Scaled by ADS so hip carry keeps natural sway. */
static void stabilise_chest(const CharacterAnimation* animation,
                            const SkinnedModel* model, const Pose* layer,
                            Pose* out) {
    float amount = animation->aim_ads * animation->aim_weight;
    if (animation->upper_root < 0 || amount <= 0.001f) return;

    float now[4], authored[4], inverse[4], correction[4];
    chain_rotation(&model->skeleton, out, animation->upper_root, now);
    chain_rotation(&model->skeleton, layer, animation->upper_root, authored);
    quat_inverse(now, inverse);
    quat_multiply(inverse, authored, correction);
    quat_fraction(correction, amount, correction);
    /* Composed in the parent's frame, like the twist. */
    quat_multiply(correction, out->local[animation->upper_root].rotation,
                  out->local[animation->upper_root].rotation);
}

/* Upper-body aim: pick a point on the Down/Neutral/Up offset from pitch, lower
   the arms by how far off the sight, then mask over the base pose. */
void character_aim_layer_apply(const CharacterAnimation* animation,
                               const SkinnedModel* model, Pose* out) {
    if (animation->aim_weight <= 0.0f) return;
    const int* clip = animation->aim_clip;
    if (clip[CHARACTER_AIM_DOWN] < 0 || clip[CHARACTER_AIM_NEUTRAL] < 0 ||
        clip[CHARACTER_AIM_UP] < 0)
        return;

    float gain = lerpf(AIM_PITCH_GAIN, AIM_PITCH_GAIN_ADS, animation->aim_ads);
    float a = clampf(AIM_LEVEL + animation->aim_pitch * gain, 0.0f, 1.0f);
    /* Piecewise: Down->Neutral below half, Neutral->Up above. */
    int from = a <= 0.5f ? CHARACTER_AIM_DOWN : CHARACTER_AIM_NEUTRAL;
    int to   = a <= 0.5f ? CHARACTER_AIM_NEUTRAL : CHARACTER_AIM_UP;
    float t  = a <= 0.5f ? a * 2.0f : (a - 0.5f) * 2.0f;

    /* Per-call scratch on the stack so posing stays re-entrant. */
    Pose layer, scratch;

    /* Aim poses are static; sample time is irrelevant. */
    skinned_model_sample(model, clip[from], 0.0f, &layer);
    skinned_model_sample(model, clip[to], 0.0f, &scratch);
    skinned_model_blend(model, &layer, &scratch, t, &layer);

    float lower = AIM_CARRY_LOWER * (1.0f - animation->aim_ads);
    if (lower > 0.0f) {
        skinned_model_sample(model, clip[CHARACTER_AIM_DOWN], 0.0f, &scratch);
        skinned_model_blend_masked(model, &layer, &scratch, lower,
                                   &animation->arms, &layer);
    }

    skinned_model_blend_masked(model, out, &layer, animation->aim_weight,
                               &animation->upper, out);
    stabilise_chest(animation, model, &layer, out);
}

void character_axis_in_parent(const Mat4* parent, const float axis[3], float out[3]) {
    if (!parent) { out[0] = axis[0]; out[1] = axis[1]; out[2] = axis[2]; return; }
    for (int c = 0; c < 3; ++c) {
        float length = sqrtf(parent->m[c*4+0]*parent->m[c*4+0] +
                             parent->m[c*4+1]*parent->m[c*4+1] +
                             parent->m[c*4+2]*parent->m[c*4+2]);
        if (length < 1e-8f) length = 1.0f;
        out[c] = (parent->m[c*4+0]*axis[0] + parent->m[c*4+1]*axis[1] +
                  parent->m[c*4+2]*axis[2]) / length;
    }
}

/* Rotates each twist bone by its share about one model-space axis. Because all
   rotations share an axis, the chain telescopes to the sum of shares, so
   cancelling shares cancel exactly. */
static void apply_axis_channel(const CharacterAnimation* animation,
                               const SkinnedModel* model, const float axis_model[3],
                               const float degrees[CHARACTER_TWIST_BONES], Pose* out) {
    const Skeleton* skeleton = &model->skeleton;
    Mat4 global[SKIN_MAX_NODES];
    int remaining = animation->twist_count;
    for (int i = 0; i < skeleton->node_count && remaining > 0; ++i) {
        int node = skeleton->order[i];
        int parent = skeleton->parent[node];

        for (int e = 0; e < CHARACTER_TWIST_BONES; ++e) {
            if (animation->twist_node[e] != node) continue;
            float axis[3];
            character_axis_in_parent(parent >= 0 ? &global[parent] : NULL,
                                     axis_model, axis);
            float turn[4];
            quat_from_axis_angle(axis, degrees[e] * (float)MATH_DEG_TO_RAD, turn);
            /* Pre-multiply: turn in the parent's frame. */
            quat_multiply(turn, out->local[node].rotation, out->local[node].rotation);
            remaining--;
            break;
        }

        const Transform* local = &out->local[node];
        Mat4 matrix = mat4_from_trs(local->translation, local->rotation, local->scale);
        global[node] = parent < 0 ? matrix : mat4_multiply(global[parent], matrix);
    }
}

/* Leg/look turn about model up, then lean about model forward. Separate passes:
   rotations about different axes don't commute, and interleaving them breaks
   the above-waist cancellation (measured 11 degrees of aim error). */
void character_twist_apply(const CharacterAnimation* animation,
                           const SkinnedModel* model, Pose* out) {
    if (animation->twist_count <= 0) return;

    float turn[CHARACTER_TWIST_BONES], lean[CHARACTER_TWIST_BONES];
    int turning = 0, leaning = 0;
    /* +roll about +z moves the head toward -x; +x is the body's left. */
    float lean_degrees = -animation->lean * CHARACTER_LEAN_ROLL;
    for (int e = 0; e < CHARACTER_TWIST_BONES; ++e) {
        turn[e] = animation->leg_yaw * TWIST_BONES[e].legs +
                  animation->look_yaw * TWIST_BONES[e].look;
        lean[e] = lean_degrees * TWIST_BONES[e].lean;
        if (fabsf(turn[e]) >= 0.02f) turning = 1;
        if (fabsf(lean[e]) >= 0.02f) leaning = 1;
    }

    static const float MODEL_UP[3] = {0.0f, 1.0f, 0.0f};
    static const float MODEL_FORWARD[3] = {0.0f, 0.0f, 1.0f};
    if (turning) apply_axis_channel(animation, model, MODEL_UP, turn, out);
    if (leaning) apply_axis_channel(animation, model, MODEL_FORWARD, lean, out);
}

/* Additive spine bend from a hit, applied last. Composes on top of the aim
   layer (a hit should disturb aim) and leaves the pelvis alone (movement
   continues). Two passes for the same non-commuting reason as the twist. */
void character_hit_reaction_apply(const CharacterAnimation* animation,
                                  const SkinnedModel* model, Pose* out) {
    if (animation->twist_count <= 0 || animation->flinch <= 0.001f) return;

    /* Head hits use the head column; everything else uses the body column. */
    float head = animation->flinch_region == CHARACTER_HIT_HEAD ? 1.0f : 0.0f;
    float total = lerpf(HIT_BODY_DEGREES, HIT_HEAD_DEGREES, head) *
                  animation->flinch;

    float pitch[CHARACTER_TWIST_BONES], roll[CHARACTER_TWIST_BONES];
    int pitching = 0, rolling = 0;
    for (int e = 0; e < CHARACTER_TWIST_BONES; ++e) {
        float share = lerpf(TWIST_BONES[e].hit_body, TWIST_BONES[e].hit_head, head);
        /* +rotation about +x bends forward, with the push. */
        pitch[e] = total * share * animation->flinch_forward;
        /* +roll about +z moves the head toward -x (body's right). */
        roll[e] = -total * share * animation->flinch_left;
        if (fabsf(pitch[e]) >= 0.02f) pitching = 1;
        if (fabsf(roll[e]) >= 0.02f) rolling = 1;
    }

    static const float MODEL_RIGHT[3] = {1.0f, 0.0f, 0.0f};
    static const float MODEL_FORWARD_HIT[3] = {0.0f, 0.0f, 1.0f};
    if (pitching) apply_axis_channel(animation, model, MODEL_RIGHT, pitch, out);
    if (rolling) apply_axis_channel(animation, model, MODEL_FORWARD_HIT, roll, out);
}

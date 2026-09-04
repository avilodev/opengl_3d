/* Where the scabbard is worn, and whether the sword in it agrees.

   `make sheathprobe` from the project root, on the host compiler - a bone
   matrix and a blade axis need neither a window nor a GL context.

   These three facts have to agree and cannot be seen agreeing by playing. A
   sheathed sword drawn a hand's breadth out of its scabbard reads as an art
   problem at a glance and stays wrong forever, because the two are drawn from
   different transforms and nothing in the game ever compares them. A blade
   that leaves sideways passes through the scabbard wall, which on a katana -
   which is curved - is the default outcome of any draw that is not along the
   blade's own axis. And a scabbard worn on the right hip is unreachable by the
   hand that draws it, which looks fine standing still and impossible the
   moment anybody moves. */
#include "gameplay/weapon_registry.h"
#include "gameplay/weapon_spec.h"
#include "render/character_transform.h"
#include "render/static_model.h"
#include "render/skinned_model.h"
#include "render/arm_placement.h"
#include "render/weapon_grip.h"
#include "gameplay/movement_tuning.h"
#include "utils/mat4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* How deep the scabbard is, measured off the scabbard's own mesh rather than
   transcribed from the importer's output.

   This used to be `#define SAYA_DEPTH 0.7907f`, copied by hand out of what
   tools/katana_to_glb.py prints. That number was right, and it was right only
   for the katana and only until somebody re-exported it: a shorter saya would
   have left the constant standing and the check passing against an asset that
   no longer existed. A number a machine computed and a human retyped is a
   number that can be retyped wrong, and nothing downstream can tell.

   The mesh's origin is the koiguchi - the mouth - and the blade goes in along
   its own axis, so the depth is simply how far the furthest vertex reaches in
   that direction. Which makes this work for the second sword without anybody
   measuring anything.

   Returns 0 for a scabbard that will not load, which the caller reports. */
static float scabbard_depth(const char* path, const float axis[3]) {
    StaticModel saya;
    char error[256];
    if (!static_model_load_glb(&saya, path, error, sizeof error)) return 0.0f;

    float deepest = 0.0f;
    for (int m = 0; m < saya.mesh_count; ++m) {
        const StaticMesh* mesh = &saya.meshes[m];
        for (int v = 0; v < mesh->vertex_count; ++v) {
            const float* p = &mesh->positions[v * 3];
            float along = p[0] * axis[0] + p[1] * axis[1] + p[2] * axis[2];
            if (along > deepest) deepest = along;
        }
    }
    /* Not freed, for the reason weapon_spec_probe gives: static_model_free
       lives beside the GL calls, and linking those would drag a context into a
       probe whose whole point is not needing one. */
    return deepest;
}

static int failures = 0;

static void check(const char* what, float got, float want, float tol) {
    int ok = fabsf(got - want) <= tol;
    if (!ok) ++failures;
    printf("  %s %-44s got %+.4f want %+.4f (+-%.3f)\n", ok ? "ok  " : "FAIL",
           what, got, want, tol);
}

static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

/* The blade's own axis in the weapon's frame, normalised - the direction a
   sword has to travel to leave its scabbard. */
static void blade_axis(const MeleeSpec* melee, float out[3]) {
    float d[3];
    float length = 0.0f;
    for (int c = 0; c < 3; ++c) {
        d[c] = melee->blade_tip[c] - melee->blade_base[c];
        length += d[c] * d[c];
    }
    length = sqrtf(length);
    for (int c = 0; c < 3; ++c) out[c] = length > 1e-6f ? d[c] / length : 0.0f;
}

/* The first-person scabbard, in camera space.

   Asked of weapon_grip_worn_transforms rather than rebuilt here, and that is a
   correction. It WAS rebuilt - "three lines, and a probe that read the
   renderer's answer could not catch the renderer getting it wrong" - and the
   second half of that is true right up until the two copies stop agreeing, at
   which point the probe is checking the track against a scabbard nobody draws.
   That is exactly what happened: the renderer learned that a hip sits behind
   the eye as well as below it, this copy did not, and the draw track went on
   passing against a mouth 25cm from the one it actually has to reach.

   What guards the renderer instead is that the same function it calls is the
   one under test here, and that the numbers it produces are printed. */
static void camera_scabbard(const MeleeSpec* melee, float mouth[3],
                            float axis[3]) {
    float rig_axis[3];
    blade_axis(melee, rig_axis);

    Mat4 worn;
    weapon_grip_worn_transforms(melee, &worn, NULL);

    const float origin[3] = {0, 0, 0};
    float turned[3];
    mat4_transform_point(&worn, rig_axis, turned);
    mat4_transform_point(&worn, origin, mouth);
    for (int c = 0; c < 3; ++c) axis[c] = turned[c] - mouth[c];
}

/* The draw track, against the scabbard it is drawing out of.

   Two things have to hold and neither can be seen holding by playing, because
   the whole middle of a katana draw happens behind the camera. The hand has to
   START at the sword - a track whose first key is anywhere else snaps the
   sword across the screen on the frame the draw begins - and the sword has to
   LEAVE along the blade's own axis, because a katana is curved and any other
   direction takes the blade through the wall of its own sheath. */
static void check_draw_track(const MeleeSpec* melee) {
    printf("\nthe draw track\n");
    const ActionTrack* draw = melee->draw_track;
    if (!draw || draw->count < 3) { expect(0, "the draw names a track"); return; }
    expect(melee->sheathe_track != NULL, "and so does the sheathe");

    float mouth[3], axis[3];
    camera_scabbard(melee, mouth, axis);

    /* Where the grip sits with the sword home: back from the mouth, along the
       axis, by the step from the koiguchi to the middle of the tsuka. */
    float home[3];
    for (int c = 0; c < 3; ++c)
        home[c] = mouth[c] - axis[c] * melee->sheathed_offset;

    /* The key that grips it - the second one. */
    const ActionKey* grip = &draw->keys[1];
    float gap = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float e = grip->hand[c] - home[c];
        gap += e * e;
    }
    check("the draw reaches the sheathed grip", sqrtf(gap), 0.0f, 0.02f);

    /* And the segment after it comes out along the axis and nowhere else. */
    const ActionKey* out = &draw->keys[2];
    float travel[3];
    float length = 0.0f;
    for (int c = 0; c < 3; ++c) {
        travel[c] = out->hand[c] - grip->hand[c];
        length += travel[c] * travel[c];
    }
    length = sqrtf(length);
    float dot = 0.0f;
    if (length > 1e-6f)
        for (int c = 0; c < 3; ++c) dot += (travel[c] / length) * axis[c];
    check("the blade leaves along its own axis", dot, 1.0f, 0.02f);

    /* Far enough that the tip is actually clear. Short of this the sword is
       swung forward with its point still inside the sheath. */
    check("and travels far enough to clear the mouth", length,
          -melee->blade_tip[2] - melee->sheathed_offset, 0.02f);

    /* Keys in order. An `at` that goes backwards makes the sampler pick the
       wrong pair and the sword jumps. */
    for (int k = 1; k < draw->count; ++k)
        expect(draw->keys[k].at > draw->keys[k - 1].at,
               "the draw's keys run forwards");
    for (int k = 1; k < melee->sheathe_track->count; ++k)
        expect(melee->sheathe_track->keys[k].at >
               melee->sheathe_track->keys[k - 1].at,
               "and so do the sheathe's");
}

/* Everything that has to agree about one weapon being worn. Takes the spec
   rather than naming the katana, so the second sword is checked by being in
   the registry and nothing here changes.

   Returns without complaint for a weapon carried in the hand. Being worn is a
   feature; weapon_spec_probe is the one that checks a weapon has not stated
   half of it. */
static void check_worn(const SkinnedModel* model, const WeaponSpec* spec) {
    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee || !melee->scabbard_path) return;

    printf("\n%s: the scabbard on the hip\n", spec->name);

    Pose pose;
    skinned_model_bind_pose(model, &pose);

    Mat4 saya;
    if (!character_scabbard_transform(model, &pose, melee, &saya)) {
        expect(0, "no pelvis on the rig; nothing to hang a scabbard off");
        return;
    }

    /* Left hip, not right, and not the small of the back. Worn on the wrong
       side the tsuka is unreachable by the hand that draws it.

       Which side is which is established from the rig rather than assumed. The
       bind pose puts thigh_l and hand_l at POSITIVE x and their _r twins at
       negative, so this rig's own left is +x - which is also what a body
       facing +Z with +Y up gives: right is forward cross up, and Z cross Y is
       -X. Both halves of the sword's placement are checked against that one
       measurement rather than against anybody's intuition. */
    Mat4 hips;
    if (!skinned_model_bone_matrix(model, &pose, "pelvis", &hips)) {
        expect(0, "the rig has a pelvis to wear a scabbard on");
        return;
    }
    Mat4 left_hand, right_hand;
    int has_hands =
        skinned_model_bone_matrix(model, &pose, "hand_l", &left_hand) &&
        skinned_model_bone_matrix(model, &pose, "hand_r", &right_hand);
    expect(has_hands && left_hand.m[12] > right_hand.m[12],
           "the rig's left is +x, measured off hand_l against hand_r");

    expect(saya.m[12] > hips.m[12], "the scabbard is on the body's LEFT hip");

    /* And the sword itself is in the other hand. character_held_weapon_transform
       hangs it off hand_r, so this is really asking whether that bone is the
       one at negative x - which is the same question as above, asked from the
       side that would put a katana in the wrong fist. */
    Mat4 held;
    if (!character_held_weapon_transform(model, &pose, spec, &held)) {
        expect(0, "the rig has a hand to hold the sword with");
        return;
    }
    expect(held.m[12] < hips.m[12], "the sword is in the RIGHT hand");

    /* The two are on opposite sides of the body, which is the whole point:
       a katana is drawn by crossing the right hand to the left hip. */
    expect(held.m[12] < hips.m[12] && saya.m[12] > hips.m[12],
           "hand and scabbard are on opposite sides");

    /* The blade has to fit in the scabbard it is drawn from.

       Sheathed, the sword's origin sits sheathed_offset back from the mouth
       along the blade axis, so the tip reaches that much PLUS the blade's own
       length inside. Against the depth measured off the scabbard mesh itself,
       this catches the two failures that look identical on screen and have
       nothing to do with each other: an offset stated as the blade's depth
       rather than the grip's, which hangs the sword a blade's length off the
       hip, and a blade longer than the sheath somebody modelled for it, whose
       tip comes out of the bottom. */
    float axis[3];
    blade_axis(melee, axis);

    float depth = scabbard_depth(melee->scabbard_path, axis);
    if (depth <= 0.0f) {
        expect(0, melee->scabbard_path);
        return;
    }
    printf("  ---- %s is %.4f deep, measured off its own mesh\n",
           melee->scabbard_path, (double)depth);

    /* The origin sits sheathed_offset back from the mouth on the OUTSIDE - it
       is the middle of the grip - so the blade reaches its own length inside
       LESS that step, not plus it. */
    float tip_inside = -melee->sheathed_offset;
    for (int c = 0; c < 3; ++c)
        tip_inside += axis[c] * melee->blade_tip[c];
    check("the tip reaches the bottom of the saya", tip_inside, depth, 0.01f);

    /* And it runs BACK along the hip, not forward off it. A katana is worn
       with the tsuka forward, which is the only way the right hand can cross
       to it; blade forward and the grip is behind the hip where no hand goes.
       The rig faces +Z, so back is negative. */
    float blade_world[3], mouth[3] = { saya.m[12], saya.m[13], saya.m[14] };
    mat4_transform_point(&saya, axis, blade_world);
    expect(blade_world[2] - mouth[2] < -0.5f,
           "the blade runs back along the hip, tsuka forward");

    check_draw_track(melee);
}

int main(void) {
    SkinnedModel model;
    char error[256];
    if (!skinned_model_load_glb(&model, "models/animations/UAL1_Standard.glb",
                                error, sizeof error)) {
        printf("could not load the rig: %s\n", error);
        return 1;
    }

    /* Every weapon there is, and the ones that are worn answer. A weapon
       carried in the hand states no scabbard and is passed over in silence -
       whether it should have stated one is weapon_spec_probe's question, not
       this file's. */
    int worn = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        const WeaponSpec* spec = weapon_registry_get(i);
        const MeleeSpec* melee = weapon_spec_melee(spec);
        if (!melee || !melee->scabbard_path) continue;
        check_worn(&model, spec);
        ++worn;
    }

    if (worn == 0) {
        printf("no weapon in the registry is worn on the hip\n");
        return 1;
    }

    printf("\n%d worn, %s\n", worn, failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}

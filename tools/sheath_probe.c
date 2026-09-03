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
#include "gameplay/weapon_spec.h"
#include "render/character_transform.h"
#include "render/skinned_model.h"
#include "render/arm_placement.h"
#include "gameplay/movement_tuning.h"
#include "utils/mat4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The saya's own depth, in metres, as tools/katana_to_glb.py measures and
   prints it when it splits the mesh out. Stated here because it is a property
   of the ASSET rather than of the spec, and the check worth making is that the
   spec's numbers still fit the asset they were tuned against. */
#define SAYA_DEPTH 0.7907f

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

/* The first-person scabbard, in camera space: the spec's own rig-space
   offsets through the half turn the arms take, dropped to where a hip is.
   Rebuilt here rather than exported because it is three lines, and because a
   probe that read the renderer's answer could not catch the renderer getting
   it wrong. */
static void camera_scabbard(const MeleeSpec* melee, float mouth[3],
                            float axis[3]) {
    float rig_axis[3];
    blade_axis(melee, rig_axis);

    /* The cant, then the facing. Both are yaw-pitch-roll about the vertical,
       so composing them by hand here is the same arithmetic the renderer names
       to GL in the reverse order. */
    Mat4 cant = mat4_multiply(
        mat4_rotation_degrees(melee->scabbard_yaw, 0, 1, 0),
        mat4_rotation_degrees(melee->scabbard_pitch, 1, 0, 0));
    cant = mat4_multiply(cant,
        mat4_rotation_degrees(melee->scabbard_roll, 0, 0, 1));
    Mat4 facing = mat4_rotation_degrees(ARMS_FACING_DEGREES, 0, 1, 0);
    Mat4 worn = mat4_multiply(facing, cant);

    float origin[3] = {0, 0, 0};
    float turned[3];
    mat4_transform_point(&worn, rig_axis, turned);
    mat4_transform_point(&worn, origin, mouth);
    for (int c = 0; c < 3; ++c) axis[c] = turned[c] - mouth[c];

    arm_placement_rig_to_camera(melee->scabbard_offset, 0.0f, mouth);
    mouth[1] -= VIEWMODEL_HIP_BELOW_EYE;
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

int main(void) {
    SkinnedModel model;
    char error[256];
    if (!skinned_model_load_glb(&model, "models/animations/UAL1_Standard.glb",
                                error, sizeof error)) {
        printf("could not load the rig: %s\n", error);
        return 1;
    }

    const WeaponSpec* spec = &WEAPON_SPEC_KATANA;
    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee) { printf("the katana has no melee half\n"); return 1; }

    Pose pose;
    skinned_model_bind_pose(&model, &pose);

    Mat4 saya;
    if (!character_scabbard_transform(&model, &pose, melee, &saya)) {
        printf("no pelvis on the rig; nothing to hang a scabbard off\n");
        return 1;
    }

    printf("the scabbard on the hip\n");

    /* Left hip, not right, and not the small of the back. Worn on the wrong
       side the tsuka is unreachable by the hand that draws it. The rig faces
       +Z and its own left is -X, which is the axis this reads. */
    Mat4 hips;
    if (!skinned_model_bone_matrix(&model, &pose, "pelvis", &hips)) {
        printf("no pelvis bone\n");
        return 1;
    }
    /* This rig's own left is +X, measured off it rather than assumed: thigh_l
       and hand_l both sit at positive x in the bind pose, so a scabbard at
       negative x would be worn on the right hip - where the drawing hand
       cannot reach it, and where nothing about a still screenshot says so. */
    expect(saya.m[12] > hips.m[12], "the scabbard is on the body's left");

    /* The blade has to fit in the scabbard it is drawn from.

       Sheathed, the sword's origin sits sheathed_offset back from the mouth
       along the blade axis, so the tip reaches that much PLUS the blade's own
       length inside. Against SAYA_DEPTH - which tools/katana_to_glb.py prints
       every time it runs - this catches the two failures that look identical
       on screen and have nothing to do with each other: an offset stated as
       the blade's depth rather than the grip's, which hangs the sword a
       blade's length off the hip, and a blade longer than the sheath somebody
       modelled for it, whose tip comes out of the bottom. */
    float axis[3];
    blade_axis(melee, axis);
    /* The origin sits sheathed_offset back from the mouth on the OUTSIDE - it
       is the middle of the grip - so the blade reaches its own length inside
       LESS that step, not plus it. */
    float tip_inside = -melee->sheathed_offset;
    for (int c = 0; c < 3; ++c)
        tip_inside += axis[c] * melee->blade_tip[c];
    check("the tip reaches the bottom of the saya", tip_inside, SAYA_DEPTH,
          0.01f);

    /* And it runs BACK along the hip, not forward off it. A katana is worn
       with the tsuka forward, which is the only way the right hand can cross
       to it; blade forward and the grip is behind the hip where no hand goes.
       The rig faces +Z, so back is negative. */
    float blade_world[3], mouth[3] = { saya.m[12], saya.m[13], saya.m[14] };
    mat4_transform_point(&saya, axis, blade_world);
    expect(blade_world[2] - mouth[2] < -0.5f,
           "the blade runs back along the hip, tsuka forward");

    check_draw_track(melee);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}

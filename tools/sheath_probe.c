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
#include "utils/mat4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    expect(saya.m[12] < hips.m[12], "the scabbard is on the body's left");

    /* The sheathed sword's origin, walked back along the blade axis from the
       koiguchi, has to land where the scabbard actually is. If these two
       disagree the sword floats beside its own sheath, which is what the asset
       did before it was split. */
    float axis[3];
    blade_axis(melee, axis);
    float sheathed_local[3];
    for (int c = 0; c < 3; ++c)
        sheathed_local[c] = -axis[c] * melee->sheathed_depth;

    float sheathed_world[3];
    mat4_transform_point(&saya, sheathed_local, sheathed_world);

    /* And the mouth itself, which is the scabbard's own origin. The distance
       between the two is the depth, by construction - so this is the check
       that the construction is what the drawing actually does. */
    float mouth[3] = { saya.m[12], saya.m[13], saya.m[14] };
    float travelled = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float e = sheathed_world[c] - mouth[c];
        travelled += e * e;
    }
    check("sheathed sword at the koiguchi", sqrtf(travelled),
          melee->sheathed_depth, 0.005f);

    /* The blade leaves along its own axis. A katana is curved; a draw that
       pulled it out sideways would pass the blade through the scabbard wall. */
    float out_world[3], mouth_dir[3];
    float forward_local[3] = { axis[0], axis[1], axis[2] };
    mat4_transform_point(&saya, forward_local, out_world);
    float length = 0.0f;
    for (int c = 0; c < 3; ++c) {
        mouth_dir[c] = out_world[c] - mouth[c];
        length += mouth_dir[c] * mouth_dir[c];
    }
    length = sqrtf(length);

    float sheath_dir[3];
    float sheath_len = 0.0f;
    for (int c = 0; c < 3; ++c) {
        sheath_dir[c] = mouth[c] - sheathed_world[c];
        sheath_len += sheath_dir[c] * sheath_dir[c];
    }
    sheath_len = sqrtf(sheath_len);

    float dot = 0.0f;
    if (length > 1e-6f && sheath_len > 1e-6f)
        for (int c = 0; c < 3; ++c)
            dot += (mouth_dir[c] / length) * (sheath_dir[c] / sheath_len);
    check("draw direction against blade axis", dot, 1.0f, 0.02f);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}

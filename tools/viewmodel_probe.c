/* Headless checks for the viewmodel: that a point taken off the pose lands
   where GL draws it, that adding the sprint carry changed nothing at sprint
   zero, that the stock clears the near plane, and what the arm placements
   actually solve to against the real rig.

   `make probe` from the project root, on the host compiler rather than the
   cross one - none of this needs a window, a GL context or Windows.

   It exists because the two things most likely to go quietly wrong here cannot
   be seen by looking at the game. A tracer whose origin has drifted off the
   barrel is a few centimetres in a streak that lives for sixty milliseconds,
   and the arm search FALLS BACK when it fails rather than refusing to draw, so
   a stance nobody can reach looks like a stance somebody chose. Both are
   arithmetic, and arithmetic can be checked. */
#include "gameplay/weapon.h"
#include "gameplay/combatant.h"
#include "gameplay/combat_tuning.h"
#include "gameplay/third_person_camera.h"
#include "gameplay/traversal.h"
#include "gameplay/reload_sequence.h"
#include "gameplay/weapon_spec.h"
#include "gameplay/weapon_tuning.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/viewmodel_pose.h"
#include "render/arm_placement.h"
#include "render/viewmodel_clip_arms.h"
#include "render/skinned_model.h"
#include "utils/mat4.h"
#include "utils/math_utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* This probe poses guns and only guns, so it takes the class half once and
   refuses outright if it is missing rather than testing for it at every read.
   A viewmodel check running against a weapon with no barrel is not a failing
   check, it is a check that was handed the wrong weapon. */
static const FirearmSpec* firearm_of(const WeaponSpec* spec) {
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    if (!gun) {
        printf("   FAIL %s has no gun half; this probe needs one\n", spec->name);
        exit(1);
    }
    return gun;
}

static int failures = 0;
static void check(const char* what, float got, float want, float tol) {
    int ok = fabsf(got - want) <= tol;
    if (!ok) ++failures;
    printf("  %s %-46s got %+.6f want %+.6f\n", ok ? "ok  " : "FAIL", what,
           got, want);
}

/* For the checks whose answer is yes or no rather than a number. */
static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
}

/* The renderer's own transform chain, rebuilt as arithmetic. If a point through
   this and the same point through viewmodel_pose_transform_point disagree, the
   tracer leaves a muzzle the mesh has rotated away from - which is the bug the
   pose type exists to close. */
static void gl_chain(const ViewmodelPose* pose, const float local[3],
                     float out[3]) {
    Mat4 m = mat4_translation(pose->x, pose->y, pose->z);
    /* The recoil, between the position and the carry, exactly as
       weapon_renderer names it. This pair was missing from the pose type
       entirely: the gun was drawn through it and every point taken off the gun
       was not, so a tracer left the barrel's unkicked position while the barrel
       was somewhere else - centimetres at the hip, and most of the receiver
       through a burst at the sprint carry, which turns the muzzle furthest from
       the origin the kick turns about. */
    m = mat4_multiply(m, mat4_rotation_degrees(pose->kick_pitch, 1, 0, 0));
    m = mat4_multiply(m, mat4_rotation_degrees(pose->kick_yaw, 0, 1, 0));
    m = mat4_multiply(m, mat4_rotation_degrees(pose->yaw, 0, 1, 0));
    m = mat4_multiply(m, mat4_rotation_degrees(pose->pitch, 1, 0, 0));
    m = mat4_multiply(m, mat4_rotation_degrees(pose->roll, 0, 0, 1));
    mat4_transform_point(&m, local, out);
}

static void check_pose_matches_gl(const WeaponSpec* spec) {
    printf("\n1. a point off the pose lands where GL draws it (%s)\n",
           spec->name);
    const float local[3][3] = {
        {0.0f, firearm_of(spec)->muzzle_up, -firearm_of(spec)->muzzle_forward},
        {0.0f, weapon_spec_sight_height(spec), -weapon_spec_sight_forward(spec)},
        {spec->grip_left[0], spec->grip_left[1], spec->grip_left[2]},
    };
    const char* name[3] = {"muzzle tip", "sight glass", "support grip"};
    /* Sprint, ads, and the recoil the gun is drawn through - a burst on this
       rifle walks the muzzle up several degrees and wanders it sideways, and
       the pose has to hold at the top of one as well as at rest. */
    const float blend[7][4] = {
        {0, 0, 0.0f, 0.0f}, {1, 0, 0.0f, 0.0f}, {0.5f, 0, 0.0f, 0.0f},
        {0, 1, 0.0f, 0.0f},
        {0, 0, 4.0f, -1.2f}, {1, 0, 4.0f, -1.2f}, {0, 1, 6.5f, 2.0f},
    };
    for (int b = 0; b < 7; ++b) {
        ViewmodelPose pose;
        weapon_spec_rest_pose(spec, blend[b][0], 0.0f, blend[b][1], &pose);
        pose.kick_pitch = blend[b][2];
        pose.kick_yaw = blend[b][3];
        for (int i = 0; i < 3; ++i) {
            float mine[3], theirs[3];
            viewmodel_pose_transform_point(&pose, local[i], mine);
            gl_chain(&pose, local[i], theirs);
            char label[96];
            snprintf(label, sizeof label, "%s at sprint %.1f ads %.1f kick %.1f",
                     name[i], blend[b][0], blend[b][1], blend[b][2]);
            float d = sqrtf((mine[0]-theirs[0])*(mine[0]-theirs[0]) +
                            (mine[1]-theirs[1])*(mine[1]-theirs[1]) +
                            (mine[2]-theirs[2])*(mine[2]-theirs[2]));
            check(label, d, 0.0f, 1e-5f);
        }
    }
}

/* Step 1 was meant to be a pure refactor: at sprint zero the pose has to
   reproduce the offsets the old weapon_viewmodel_offset composed, to the float.
   Anything else is a visible change smuggled into a fix. */
static void check_no_visible_change(const WeaponSpec* spec) {
    printf("\n2. at sprint 0 the pose reproduces the old offsets (%s)\n",
           spec->name);
    for (float ads = 0.0f; ads <= 1.0f; ads += 0.25f) {
        ViewmodelPose pose;
        weapon_spec_rest_pose(spec, 0.0f, 0.0f, ads, &pose);
        char label[64];
        snprintf(label, sizeof label, "x at ads %.2f", ads);
        check(label, pose.x, lerpf(spec->view_right, 0.0f, ads), 1e-6f);
        snprintf(label, sizeof label, "y at ads %.2f", ads);
        check(label, pose.y, lerpf(-spec->view_down,
                                   -weapon_spec_sight_height(spec), ads), 1e-6f);
        snprintf(label, sizeof label, "z at ads %.2f", ads);
        check(label, pose.z, -spec->view_forward -
                             firearm_of(spec)->view_ads_forward * ads, 1e-6f);
        snprintf(label, sizeof label, "hip cant at ads %.2f", ads);
        check(label, pose.yaw, -3.0f * (1.0f - ads), 1e-6f);
    }
}

/* Step 2's number to watch: the stock is the rearmost point of the model, and
   the sprint carry is what brings it toward the eye. */
static void check_near_plane(const WeaponSpec* spec, float near_plane,
                             float stock_back) {
    printf("\n3. the buttplate against a %.2fm near plane (%s)\n",
           near_plane, spec->name);
    for (float sprint = 0.0f; sprint <= 1.0f; sprint += 0.5f) {
        ViewmodelPose pose;
        weapon_spec_rest_pose(spec, sprint, 0.0f, 0.0f, &pose);
        const float local[3] = {0.0f, 0.0f, stock_back};
        float tip[3];
        viewmodel_pose_transform_point(&pose, local, tip);
        printf("  sprint %.1f: stock at z %+.4f, margin %+.4fm%s\n",
               sprint, tip[2], -tip[2] - near_plane,
               (-tip[2] - near_plane) < 0.0f ? "   <-- CLIPS" : "");
        if (-tip[2] - near_plane < 0.0f) ++failures;
    }
}

/* Step 3, against the rig the game actually loads: both arms between two
   thirds and four fifths of reach in all three poses, and no shoulder inside
   the frame - which the solve refuses outright, so a returned placement has
   already passed that half. */
/* The wrist target for one hand of a pose, which is the grip walked back down
   the fist. Defined with the rest of the replicated arm code below; declared
   here because the placement checks are the first thing that needs it, and they
   need it for the same reason the renderer does - a placement solved against
   the grip is solved for a wrist a hand's length too far out. */
static int probe_arms_ready(const SkinnedModel* model);
static void probe_wrist_targets(const WeaponSpec* spec,
                                const ViewmodelPose* pose,
                                const float grip[2][3], float targets[2][3]);

static void pose_targets(const WeaponSpec* spec, float sprint, float ads,
                         float targets[2][3]) {
    ViewmodelPose pose;
    weapon_spec_rest_pose(spec, sprint, 0.0f, ads, &pose);
    float grip[2][3];
    for (int a = 0; a < 2; ++a)
        viewmodel_pose_transform_point(&pose,
                                       a ? spec->grip_left : spec->grip_right,
                                       grip[a]);
    probe_wrist_targets(spec, &pose, grip, targets);
}

static void report(const ArmRig* rig, const char* label,
                   const float targets[2][3], ArmPlacement p, int found) {
    float firing = arm_placement_reach_fraction(rig, 0, targets[0], p);
    float support = arm_placement_reach_fraction(rig, 1, targets[1], p);
    int ok = found && firing >= 0.66f && firing <= 0.80f &&
             support >= 0.66f && support <= 0.80f;
    if (!ok) ++failures;
    printf("  %s %-34s z %+.2f blade %2.0f deg   firing %.2f  support %.2f\n",
           ok ? "ok  " : "FAIL", label, p.z, p.blade, firing, support);
}

static int check_arms(const WeaponSpec* spec, const SkinnedModel* model,
                      ArmRig* out_rig) {
    printf("\n4. arm placements against the real rig (%s)\n", spec->name);

    /* Mirrors build() exactly: the shoulder MIDPOINT off the bind pose, since
       that is what places the rig, and each shoulder's OFFSET off the grip clip,
       since that is the pose the arms are actually drawn in. */
    ArmRig rig;
    int clip = -1;
    for (int c = 0; c < model->clip_count; ++c)
        if (strcmp(model->clips[c].name, "Idle_Loop") == 0) clip = c;
    if (clip < 0) { printf("  FAIL no Idle_Loop clip\n"); ++failures; return 0; }

    if (!probe_arms_ready(model)) {
        printf("  FAIL the arm bones would not resolve\n"); ++failures; return 0;
    }
    const Skeleton* sk = &model->skeleton;
    const char* upper[2] = {"upperarm_r", "upperarm_l"};
    const char* lower = "lowerarm_r", *hand = "hand_r";
    int node[2] = {-1, -1}, lower_node = -1, hand_node = -1;
    for (int n = 0; n < sk->node_count; ++n) {
        for (int a = 0; a < 2; ++a)
            if (strcmp(sk->node_names[n], upper[a]) == 0) node[a] = n;
        if (strcmp(sk->node_names[n], lower) == 0) lower_node = n;
        if (strcmp(sk->node_names[n], hand) == 0) hand_node = n;
    }
    if (node[0] < 0 || node[1] < 0 || lower_node < 0 || hand_node < 0) {
        printf("  FAIL arm bones not found\n"); ++failures; return 0;
    }

    Pose bind_pose;
    skinned_model_bind_pose(model, &bind_pose);
    for (int a = 0; a < 2; ++a) {
        Mat4 m;
        if (!skinned_model_bone_matrix(model, &bind_pose, upper[a], &m)) {
            printf("  FAIL no bind matrix for %s\n", upper[a]); ++failures;
            return 0;
        }
        for (int c = 0; c < 3; ++c)
            rig.shoulder_mid[c] = a == 0 ? m.m[12+c] * 0.5f
                                         : rig.shoulder_mid[c] + m.m[12+c] * 0.5f;
    }

    Pose pose;
    skinned_model_sample(model, clip, 0.0f, &pose);
    for (int a = 0; a < 2; ++a) {
        Mat4 m;
        if (!skinned_model_bone_matrix(model, &pose, upper[a], &m)) continue;
        for (int c = 0; c < 3; ++c)
            rig.shoulder_offset[a][c] = m.m[12+c] - rig.shoulder_mid[c];
    }

    /* Reach is the two bone lengths, each recorded on the child that ends it -
       the same measurement bone_length() takes. */
    float reach = 0.0f;
    for (int i = 0; i < 2; ++i) {
        const float* t = sk->bind_local[i ? hand_node : lower_node].translation;
        reach += sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
    }
    rig.scale = reach > 1e-4f ? ARMS_REACH / reach : 1.0f;
    printf("  rig arm %.3f units, scale %.4f\n", reach, rig.scale);

    float targets[2][3];
    ArmPlacement hip = {0,0}, sprint = {0,0}, ads = {0,0};

    pose_targets(spec, 0.0f, 0.0f, targets);
    int f_hip = arm_placement_solve(&rig, targets, spec->view_fov * 0.5f, &hip);
    report(&rig, "hip, own placement", targets, hip, f_hip);

    /* The doc's claim: the carry is not reachable from the hip's placement. */
    pose_targets(spec, 1.0f, 0.0f, targets);
    printf("  ---- sprint carry from the HIP placement (expected to fail) ----\n");
    printf("       firing %.2f  support %.2f\n",
           arm_placement_reach_fraction(&rig, 0, targets[0], hip),
           arm_placement_reach_fraction(&rig, 1, targets[1], hip));
    int f_sprint = arm_placement_solve(&rig, targets, spec->view_fov * 0.5f,
                                       &sprint);
    report(&rig, "sprint, own placement", targets, sprint, f_sprint);

    pose_targets(spec, 0.0f, 1.0f, targets);
    int f_ads = arm_placement_solve(&rig, targets, firearm_of(spec)->view_ads_fov * 0.5f,
                                    &ads);
    report(&rig, "aimed, own placement", targets, ads, f_ads);

    /* And through the crossfade, which is where a blended placement could still
       leave an arm straight even though both ends are comfortable. */
    /* And through the crossfade, which is where a blended placement could still
       leave an arm straight even though both ends are comfortable. The anchor
       is nested exactly as shoulder_anchor nests it - sprint first, then aim
       over the top - because that is the path the shoulders actually take, and
       lerping straight from one solved end to another is a different curve. */
    printf("  ---- through the blend, as shoulder_anchor walks it ----\n");
    for (int leg = 0; leg < 2; ++leg) {
        for (float t = 0.25f; t <= 0.76f; t += 0.25f) {
            /* Dropping into a sprint from the hip, then aiming out of one. The
               sprint weight collapses as the scope comes up, which is what
               weapon_sprint_blend does to it. */
            float s = leg == 0 ? t : (1.0f - t);
            float a = leg == 0 ? 0.0f : t;
            ArmPlacement mid;
            mid.z = lerpf(lerpf(hip.z, sprint.z, s), ads.z, a);
            mid.blade = lerpf(lerpf(hip.blade, sprint.blade, s), ads.blade, a);
            pose_targets(spec, s, a, targets);
            char label[64];
            snprintf(label, sizeof label, "%s, t %.2f",
                     leg == 0 ? "hip to sprint" : "sprint to aimed", t);
            report(&rig, label, targets, mid, 1);
        }
    }

    *out_rig = rig;
    return 1;
}

/* The gait, traced over one full stride. What is being checked is that there
   are TWO rates in it and not one: the muzzle has to come back to the same
   place having crossed its own path, which is what a figure of eight does and
   what a single side-to-side wave cannot. A run driven off one sine looks like
   the gun sliding on a rail, and that is invisible in a still - it needs either
   a moving capture or this. */
static void check_gait(const WeaponSpec* spec) {
    printf("\n5. the sprint gait over one stride (%s)\n", spec->name);

    Weapon weapon;
    weapon_init(&weapon, spec);
    weapon.sprint = 1.0f;

    PlayerController player;
    memset(&player, 0, sizeof player);
    player.ground.is_grounded = 1;
    /* Past WEAPON_VIEW_BOB_SPEED, so the gait is at full amplitude. */
    player.velocity_z = -9.0f;

    /* walk_time counts footfalls, so a full stride is two of them: 4*pi. */
    const int steps = 16;
    float x[17], y[17], roll[17], pitch[17];
    printf("   phase      x        y      roll    pitch\n");
    for (int i = 0; i <= steps; ++i) {
        player.walk_time = (float)i / steps * 4.0f * (float)MATH_PI;
        ViewmodelPose pose;
        weapon_viewmodel_pose(&weapon, &player, &pose);
        ViewmodelPose rest;
        weapon_spec_rest_pose(spec, 1.0f, 0.0f, 0.0f, &rest);
        x[i] = pose.x - rest.x;
        y[i] = pose.y - rest.y;
        roll[i] = pose.roll - rest.roll;
        pitch[i] = pose.pitch - rest.pitch;
        if (i % 2 == 0)
            printf("   %4.2f  %+7.4f %+7.4f  %+6.2f  %+6.2f\n",
                   (float)i / steps, x[i], y[i], roll[i], pitch[i]);
    }

    /* The lateral goes round once and the vertical twice. Counting sign changes
       is the cheapest way to say so, and it is exactly what distinguishes the
       figure of eight from the rail. */
    int x_cross = 0, y_cross = 0;
    for (int i = 1; i <= steps; ++i) {
        if ((x[i] > 0.0f) != (x[i-1] > 0.0f)) ++x_cross;
        if ((y[i] > 0.0f) != (y[i-1] > 0.0f)) ++y_cross;
    }
    int ok_rates = (x_cross == 2 && y_cross == 4);
    if (!ok_rates) ++failures;
    printf("   %s lateral crosses zero %d times, vertical %d "
           "(want 2 and 4: one swing per stride, one heave per footfall)\n",
           ok_rates ? "ok  " : "FAIL", x_cross, y_cross);

    /* The gun must be at the bottom of its travel as a foot lands. walk_time at
       0 and 2pi are footfalls. */
    int ok_land = y[0] < 0.0f && y[steps/2] < 0.0f;
    if (!ok_land) ++failures;
    printf("   %s gun is low on each footfall (y %+.4f and %+.4f)\n",
           ok_land ? "ok  " : "FAIL", y[0], y[steps/2]);

    /* The rock has to lead the swing, not sit in phase with it - in phase the
       two cancel and the carry looks stiff. A quarter cycle apart means the
       roll peaks where the sway is crossing zero. */
    int swing_peak = 0;
    for (int i = 1; i <= steps; ++i) if (x[i] > x[swing_peak]) swing_peak = i;
    int rock_peak = 0;
    for (int i = 1; i <= steps; ++i) if (roll[i] > roll[rock_peak]) rock_peak = i;
    int gap = abs(swing_peak - rock_peak);
    if (gap > steps / 2) gap = steps - gap;
    int ok_phase = gap >= steps / 8 && gap <= 3 * steps / 8;
    if (!ok_phase) ++failures;
    printf("   %s rock leads the swing by %d/%d of a stride (want about a quarter)\n",
           ok_phase ? "ok  " : "FAIL", gap, steps);

    /* And it must all stop when the player does, or a standing gun breathes. */
    player.velocity_z = 0.0f;
    ViewmodelPose still, rest;
    weapon_viewmodel_pose(&weapon, &player, &still);
    weapon_spec_rest_pose(spec, 1.0f, 0.0f, 0.0f, &rest);
    float moved = fabsf(still.x - rest.x) + fabsf(still.y - rest.y) +
                  fabsf(still.roll - rest.roll) + fabsf(still.pitch - rest.pitch);
    if (moved > 1e-6f) ++failures;
    printf("   %s the gait stops when the player does (residual %.6f)\n",
           moved <= 1e-6f ? "ok  " : "FAIL", moved);
}

/* The reload, walked key by key against the real rig. The support hand leaves
   the weapon here, and the placement that reaches a handguard reaches neither
   the magazine well nor a pouch on the chest - so what has to hold is that
   EVERY key has a placement of its own at a comfortable bend, and that the
   shoulders moving between them never leave an arm straight or folded halfway.

   None of this is visible in a still, and a folded arm throws its elbow across
   the sight picture, which is the failure this codebase has already been bitten
   by twice. */
static void check_reload(const WeaponSpec* spec, const ArmRig* rig) {
    printf("\n6. the reload track against the real rig (%s)\n", spec->name);

    const ReloadSequence* track = weapon_spec_reload_sequence(spec);
    ViewmodelPose held;
    weapon_spec_rest_pose(spec, 0.0f, 1.0f, 0.0f, &held);

    ArmPlacement solved[32];
    int count = track->count < 32 ? track->count : 32;

    printf("   key   at    hand              placement        firing  support"
           "  screen\n");
    for (int i = 0; i < count; ++i) {
        const ReloadKey* key = &track->keys[i];
        float targets[2][3], grip[2][3];
        /* The firing hand never leaves the pistol grip through a reload. */
        viewmodel_pose_transform_point(&held, spec->grip_right, grip[0]);
        if (key->hand_frame == RELOAD_IN_VIEW)
            for (int c = 0; c < 3; ++c) grip[1][c] = key->hand[c];
        else
            viewmodel_pose_transform_point(&held, key->hand, grip[1]);
        probe_wrist_targets(spec, &held, grip, targets);

        /* In order and each drawn toward the last, exactly as the renderer
           solves them - a probe that solved them independently would be
           checking a different set of placements than the game uses. */
        if (!arm_placement_solve_near(rig, targets, spec->view_fov * 0.5f,
                                      i ? &solved[i - 1] : NULL, &solved[i])) {
            printf("   %2d  %.2f   FAIL nothing reaches this key\n", i, key->at);
            ++failures;
            continue;
        }
        float f = arm_placement_reach_fraction(rig, 0, targets[0], solved[i]);
        float sp = arm_placement_reach_fraction(rig, 1, targets[1], solved[i]);
        int ok = f >= 0.62f && f <= 0.85f && sp >= 0.62f && sp <= 0.85f;
        if (!ok) ++failures;
        /* Where the hand is on screen, as a fraction of half the viewport: 0 is
           the middle and -1 the bottom edge. Printed rather than asserted
           because both answers are wanted somewhere - the magazine WELL has to
           be visible or the whole change happens below the frame, and the pouch
           has to be out of it, because a hand that reached a belt on screen
           would be a hand holding the belt up by the player's chin. */
        float depth = -targets[1][2];
        float screen = depth > 0.02f
            ? (targets[1][1] / depth) /
              tanf(spec->view_fov * 0.5f * MATH_DEG_TO_RAD) : 0.0f;
        printf("   %2d  %.2f   %-16s  z %+.2f blade %2.0f   %.2f    %.2f  "
               "%+.2f  %s\n",
               i, key->at,
               key->hand_frame == RELOAD_IN_VIEW ? "on the body" : "on the gun",
               solved[i].z, solved[i].blade, f, sp, screen,
               ok ? "ok" : "OUT OF BAND");
    }

    /* And between the keys, which is where a blended placement can still leave
       an arm straight even though both ends are comfortable. */
    printf("   ---- between the keys ----\n");
    float worst_low = 9.0f, worst_high = 0.0f;
    float worst_at = 0.0f;
    for (int step = 0; step <= 200; ++step) {
        float progress = (float)step / 200.0f;
        ReloadSample sample;
        reload_sequence_sample(track, progress, &sample);
        int from = (int)(sample.from - track->keys);
        int to = (int)(sample.to - track->keys);
        if (from >= count) from = count - 1;
        if (to >= count) to = count - 1;
        ArmPlacement mid = {
            lerpf(solved[from].z, solved[to].z, sample.t),
            lerpf(solved[from].blade, solved[to].blade, sample.t),
        };
        /* The hand, resolved in camera space at both ends before blending -
           exactly as the renderer does it, because keys on the gun and keys on
           the body are not in the same frame. */
        float ends[2][3];
        const ReloadKey* key[2] = {sample.from, sample.to};
        for (int e = 0; e < 2; ++e) {
            if (key[e]->hand_frame == RELOAD_IN_VIEW)
                for (int c = 0; c < 3; ++c) ends[e][c] = key[e]->hand[c];
            else
                viewmodel_pose_transform_point(&held, key[e]->hand, ends[e]);
        }
        float grip[2][3], wrists[2][3];
        viewmodel_pose_transform_point(&held, spec->grip_right, grip[0]);
        for (int c = 0; c < 3; ++c)
            grip[1][c] = lerpf(ends[0][c], ends[1][c], sample.t);
        /* Walked back down the fist, because that is the point the placements
           were solved for. Measuring the reach to the GRIP against a placement
           solved for the WRIST compares two different arms. */
        probe_wrist_targets(spec, &held, grip, wrists);
        float sp = arm_placement_reach_fraction(rig, 1, wrists[1], mid);
        if (sp < worst_low) { worst_low = sp; worst_at = progress; }
        if (sp > worst_high) worst_high = sp;
    }
    int ok = worst_low >= 0.55f && worst_high <= 0.90f;
    if (!ok) ++failures;
    printf("   %s support arm stays within %.2f..%.2f through the whole "
           "track (worst at %.2f)\n", ok ? "ok  " : "FAIL",
           worst_low, worst_high, worst_at);

    /* The magazine has to actually leave, and the hand has to actually be empty
       for a beat - otherwise this is a refill with extra steps. */
    int empty = 0, off_gun = 0;
    for (int i = 0; i < count; ++i) {
        if (!track->keys[i].drawn) ++empty;
        if (track->keys[i].hand_frame == RELOAD_IN_VIEW) ++off_gun;
    }
    if (!empty) ++failures;
    printf("   %s the hand is empty for %d key(s) - a magazine is changed, "
           "not refilled\n", empty ? "ok  " : "FAIL", empty);
    if (!off_gun) ++failures;
    printf("   %s the hand leaves the weapon for %d key(s)\n",
           off_gun ? "ok  " : "FAIL", off_gun);

    /* The gun's own swell has to start and end at nothing, or the weapon snaps
       into and out of the reload pose. */
    float in = reload_sequence_gun_blend(0.0f);
    float out = reload_sequence_gun_blend(1.0f);
    int settled = in <= 1e-6f && out <= 1e-6f;
    if (!settled) ++failures;
    printf("   %s the gun starts and ends at rest (%.3f, %.3f)\n",
           settled ? "ok  " : "FAIL", in, out);
}

/* The spent magazine, from the hand to the floor. What has to hold is that
   exactly ONE is dropped per reload - a crossing test written as a range spawns
   one every frame - that it falls, that it lands, that it then lies still for
   the rest time, and that it is eventually collected. All of it is state that
   outlives a frame and none of it is visible in a screenshot. */
static void check_dropped_mag(const WeaponSpec* spec) {
    printf("\n7. the spent magazine, dropped into the world (%s)\n", spec->name);

    Weapon weapon;
    weapon_init(&weapon, spec);
    weapon.ammo_in_magazine = 0;          /* so a reload has something to do */

    PlayerController player;
    memset(&player, 0, sizeof player);
    player.y = 0.0f;
    player.ground.is_grounded = 1;

    WeaponCommand command;
    memset(&command, 0, sizeof command);
    command.reload_pressed = 1;

    const float step = 1.0f / 120.0f;
    int live_peak = 0, drops = 0;
    float landed_at = -1.0f, gone_at = -1.0f, rest_seen = 0.0f;
    int was_live = 0;

    for (int i = 0; i < 1200; ++i) {           /* ten seconds */
        float t = (float)i * step;
        weapon_update(&weapon, &command, &player, 0.0f, 0.0f, 0.0f, 1.6f,
                      75.0f, step);
        command.reload_pressed = 0;            /* one press, one reload */

        int live = 0, grounded = 0;
        for (int m = 0; m < WEAPON_DROPPED_MAG_COUNT; ++m) {
            if (weapon.dropped_mags[m].life <= 0.0f) continue;
            ++live;
            if (weapon.dropped_mags[m].grounded) ++grounded;
        }
        if (live > live_peak) live_peak = live;
        if (live > was_live) ++drops;
        if (grounded && landed_at < 0.0f) landed_at = t;
        if (grounded) rest_seen = t - landed_at;
        if (was_live && !live && gone_at < 0.0f) gone_at = t;
        was_live = live;
    }

    int one = drops == 1 && live_peak == 1;
    if (!one) ++failures;
    printf("   %s exactly one magazine was dropped (%d spawned, %d at once)\n",
           one ? "ok  " : "FAIL", drops, live_peak);

    int landed = landed_at > 0.0f;
    if (!landed) ++failures;
    /* Reported against the moment the hand let go, not against the start of the
       reload, or the number reads as a magazine hanging in the air for most of
       a second. */
    float let_go = weapon_spec_reload_sequence(spec)->drop_at * firearm_of(spec)->reload_time;
    printf("   %s it left the hand %.2fs in and reached the floor %.2fs later\n",
           landed ? "ok  " : "FAIL", let_go, landed_at - let_go);

    /* The whole reason it is simulated rather than drawn and forgotten. */
    int rested = rest_seen >= WEAPON_MAG_REST_TIME - 0.05f;
    if (!rested) ++failures;
    printf("   %s it lay on the floor for %.2fs (want %.1f)\n",
           rested ? "ok  " : "FAIL", rest_seen, (double)WEAPON_MAG_REST_TIME);

    int collected = gone_at > 0.0f;
    if (!collected) ++failures;
    printf("   %s and was collected at %.2fs, not left forever\n",
           collected ? "ok  " : "FAIL", gone_at);

    /* It has to land ON something, not at the world origin - a magazine that
       spawned at (0,0,0) would pass every check above and be nowhere near the
       player. */
    float dropped_x = 0.0f, dropped_z = 0.0f;
    weapon_init(&weapon, spec);
    weapon.ammo_in_magazine = 0;
    command.reload_pressed = 1;
    player.x = 12.0f; player.z = -7.0f;
    for (int i = 0; i < 400; ++i) {
        weapon_update(&weapon, &command, &player, 0.0f, 0.0f, 0.0f, 1.6f,
                      75.0f, step);
        command.reload_pressed = 0;
        for (int m = 0; m < WEAPON_DROPPED_MAG_COUNT; ++m)
            if (weapon.dropped_mags[m].life > 0.0f) {
                dropped_x = weapon.dropped_mags[m].x;
                dropped_z = weapon.dropped_mags[m].z;
            }
    }
    float dx = dropped_x - player.x, dz = dropped_z - player.z;
    float near = sqrtf(dx * dx + dz * dz);
    int placed = near > 0.05f && near < 1.5f;
    if (!placed) ++failures;
    printf("   %s it landed %.2fm from the player who dropped it\n",
           placed ? "ok  " : "FAIL", near);
}

/* Firing takes the gun out of the sprint carry.

   The carry pulls the weapon in against the chest and rolls it a quarter turn
   over. Aiming has always collapsed it; shooting did not, so a player holding
   the trigger at a sprint watched rounds leave a gun that was plainly pointed
   somewhere else - and the streak, correctly, came out of that barrel. Both
   halves of that were right; the pose was the part that was wrong.

   Checked as a sequence rather than a state, because it is one: hold the
   trigger and the carry has to go, let go and it has to come back. */
static void check_sprint_while_firing(const WeaponSpec* spec) {
    printf("\n6. firing takes the gun out of the sprint carry (%s)\n", spec->name);

    Weapon weapon;
    weapon_init(&weapon, spec);
    PlayerController player;
    memset(&player, 0, sizeof player);
    player.ground.is_grounded = 1;
    player.is_sprinting = 1;
    player.velocity_z = -MOVE_SPRINT_SPEED;

    WeaponCommand command;
    memset(&command, 0, sizeof command);

    const float step = 1.0f / 120.0f;
    /* Sprinting, trigger up: the carry comes in and stays. */
    for (int i = 0; i < 120; ++i)
        weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
    check("carried at a sprint", weapon_sprint_blend(&weapon), 1.0f, 0.01f);

    /* Trigger down. The raise is 0.08s, so by a tenth of a second the gun is up
       and it stays up for as long as the trigger is - which is the whole of the
       complaint: not that the first round leaves early, but that the gun stayed
       rolled over for the entire magazine. */
    command.fire_held = 1;
    for (int i = 0; i < 12; ++i)
        weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
    check("a tenth of a second in, the carry is gone",
          weapon_sprint_blend(&weapon), 0.0f, 0.01f);
    float worst = 0.0f;
    for (int i = 0; i < 240; ++i) {
        weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
        if (weapon_sprint_blend(&weapon) > worst) worst = weapon_sprint_blend(&weapon);
    }
    check("and stays gone for the whole magazine", worst, 0.0f, 0.01f);

    /* A single tap from a full carry. The gun cannot already be up on the frame
       the trigger goes down - nothing can see the future - but it must not sit
       there either: the shot's own cooldown holds the carry off long enough for
       the raise to finish, so the round is followed by a gun that is up rather
       than by one that never moved. */
    weapon_init(&weapon, spec);
    command.fire_held = 0;
    for (int i = 0; i < 120; ++i)
        weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
    command.fire_held = 1;
    weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
    command.fire_held = 0;
    for (int i = 0; i < 9; ++i)
        weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
    check("a tap raises the gun rather than leaving it",
          weapon_sprint_blend(&weapon), 0.0f, 0.10f);

    /* Trigger up for good, and it comes back - a carry the gun could never
       return to would be a different bug wearing this one's clothes. */
    for (int i = 0; i < 120; ++i)
        weapon_update(&weapon, &command, &player, 0, 0, 0, 1.6f, 75.0f, step);
    check("and returns once the trigger is up", weapon_sprint_blend(&weapon),
          1.0f, 0.01f);
}


/* ---- 8. the hitbox ----

   The one part of combat that cannot be checked by playing: a band boundary
   half a metre out is a headshot multiplier paid on a shoulder, and nobody sees
   that happen - they see a number that is sometimes bigger. The bands, the
   silhouette and the rule that a body behind a wall is behind a wall are all
   arithmetic against the arena the game actually loads. */
static void check_hitbox(void) {
    printf("\n8. the hitbox, banded and the same width from every angle\n");

    CombatantSet set;
    combatant_set_init(&set);
    expect(set.count > 0, "the arena stands some combatants up");

    /* The first one is on the Bastion approach floor, which is all this needs
       of it - the bands are measured from the feet. */
    const Combatant* body = &set.items[0];
    check("the hitbox is as tall as the drawn body", COMBAT_BODY_HEIGHT,
          CHARACTER_RIG_HEIGHT, 0.0001f);

    /* Straight in at each band's middle, from four metres out along -x, so the
       entry point is the near face of the cylinder and the distance is known. */
    const float bands[3] = {COMBAT_LEGS_TOP * 0.5f,
                            (COMBAT_LEGS_TOP + COMBAT_HEAD_BOTTOM) * 0.5f,
                            (COMBAT_HEAD_BOTTOM + 1.0f) * 0.5f};
    const int want[3] = {COMBATANT_REGION_LEGS, COMBATANT_REGION_CHEST,
                         COMBATANT_REGION_HEAD};
    const char* named[3] = {"legs", "chest", "head"};
    for (int i = 0; i < 3; ++i) {
        float y = body->y + bands[i] * COMBAT_BODY_HEIGHT;
        CombatantHit hit;
        int found = combatant_raycast(&set, body->x - 4.0f, y, body->z,
                                      1.0f, 0.0f, 0.0f, 100.0f, &hit);
        char label[96];
        snprintf(label, sizeof label,
                 "a round through the middle of the %s reads as the %s",
                 named[i], named[i]);
        expect(found && hit.index == 0 && hit.region == want[i], label);
        if (found)
            check("   and enters at the near face", hit.distance,
                  4.0f - COMBAT_BODY_RADIUS, 0.002f);
    }

    /* Why it is a cylinder. A box would be wider across its diagonal than
       across its faces, so a target would be quietly easier to hit from one
       bearing than another - and these bodies turn, so the bearing changes
       while the player is shooting. Grazing rays from all the way round: inside
       the radius every one connects, outside it none of them does. */
    int inside_hits = 0, outside_hits = 0;
    const int bearings = 64;
    float chest = body->y + 0.65f * COMBAT_BODY_HEIGHT;
    for (int i = 0; i < bearings; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i / (float)bearings;
        float dx = cosf(angle), dz = sinf(angle);
        /* Perpendicular offset from the axis, so the ray passes the body at
           exactly this distance from its centre. */
        for (int pass = 0; pass < 2; ++pass) {
            float offset = COMBAT_BODY_RADIUS * (pass == 0 ? 0.9f : 1.1f);
            float ox = body->x - dx * 6.0f - dz * offset;
            float oz = body->z - dz * 6.0f + dx * offset;
            CombatantHit hit;
            if (combatant_raycast(&set, ox, chest, oz, dx, 0.0f, dz, 100.0f,
                                  &hit) && hit.index == 0) {
                if (pass == 0) ++inside_hits; else ++outside_hits;
            }
        }
    }
    expect(inside_hits == bearings && outside_hits == 0,
           "the silhouette is the same width from all 64 bearings");

    /* A muzzle inside a body does not resolve against it, which is the rule the
       arena's own raycast follows and what stops a shot fired while standing in
       a target from hitting it from the inside out. */
    CombatantHit hit;
    expect(!combatant_raycast(&set, body->x, chest, body->z, 0.0f, 0.0f, 1.0f,
                              100.0f, &hit),
           "a ray that starts inside a body does not hit that body");

    /* The whole reason bodies are traced against the distance the geometry has
       already shortened the ray to. */
    expect(combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                             1.0f, 0.0f, 0.0f, 100.0f, &hit),
           "a body four metres away is hit at a hundred metres of range");
    expect(!combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                              1.0f, 0.0f, 0.0f, 2.0f, &hit),
           "and is not, once something nearer has cut the ray to two");
}

/* ---- 9. what a hit does ----

   The bands multiply, the pool drains and comes back, and the figure the player
   is shown is the figure that came off the pool. That last one is the check
   worth having: showing one number and applying another is a bug nobody can see
   and everybody feels. */
static void check_damage(void) {
    printf("\n9. what a round does when it lands\n");

    expect(combatant_region_multiplier(COMBATANT_REGION_HEAD) >
           combatant_region_multiplier(COMBATANT_REGION_CHEST) &&
           combatant_region_multiplier(COMBATANT_REGION_CHEST) >
           combatant_region_multiplier(COMBATANT_REGION_LEGS),
           "the head is worth more than the chest, and the chest than the legs");

    CombatantSet set;
    combatant_set_init(&set);
    const Combatant* body = &set.items[0];
    float chest = body->y + 0.65f * COMBAT_BODY_HEIGHT;

    CombatantHit hit;
    expect(combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                             1.0f, 0.0f, 0.0f, 100.0f, &hit), "a chest hit");
    float paid = combatant_apply_damage(&set, &hit, 20.0f);
    check("the chest pays the flat damage", paid,
          20.0f * COMBAT_DAMAGE_CHEST, 0.001f);
    check("and it is what came off the pool", set.items[0].health,
          COMBAT_DUMMY_HEALTH - paid, 0.001f);

    float age;
    int region;
    expect(damage_numbers_latest(&set.numbers, &age, &region) &&
           region == COMBATANT_REGION_CHEST,
           "a figure was thrown, tagged with the band that was hit");
    /* The figure the player reads is the figure that was applied, and it is
       anchored where the round landed rather than over the body's middle. */
    int matched = 0;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i) {
        const DamageNumber* number = &set.numbers.items[i];
        if (number->life <= 0.0f) continue;
        if (fabsf(number->amount - paid) < 0.001f &&
            fabsf(number->x - hit.x) < 0.001f &&
            fabsf(number->y - hit.y) < 0.001f) ++matched;
    }
    expect(matched == 1, "one figure, showing what was paid, at the hit point");

    /* Sustained fire on one body accumulates into that figure instead of
       filling the pool with one entry per round - at a hundred rounds a minute
       the unmerged version is ten figures a second and the player reads none. */
    for (int i = 0; i < 9; ++i) {
        combatant_set_update(&set, body->x - 4.0f, body->z,
                             DAMAGE_NUMBER_MERGE_TIME * 0.4f);
        combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                          1.0f, 0.0f, 0.0f, 100.0f, &hit);
        combatant_apply_damage(&set, &hit, 20.0f);
    }
    int live = 0;
    float total = 0.0f;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i)
        if (set.numbers.items[i].life > 0.0f) {
            ++live;
            total = set.numbers.items[i].amount;
        }
    expect(live == 1, "ten rounds in a burst are one figure, not ten");
    check("   carrying the running total", total,
          10.0f * 20.0f * COMBAT_DAMAGE_CHEST, 0.001f);

    /* Long enough apart and they are separate hits again, which is what says
       the merge is a burst and not a bucket. */
    combatant_set_update(&set, body->x - 4.0f, body->z,
                         DAMAGE_NUMBER_MERGE_TIME * 2.0f);
    combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                      1.0f, 0.0f, 0.0f, 100.0f, &hit);
    combatant_apply_damage(&set, &hit, 20.0f);
    live = 0;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i)
        if (set.numbers.items[i].life > 0.0f) ++live;
    expect(live == 2, "a round after the window is a figure of its own");

    /* The figure a round throws is bounded by the pool it came out of, which is
       what stops the last round of a kill from printing more than it took. The
       state on the far side of that - the body going down, and the trace losing
       it - belongs to `make deathprobe`; what is checked here is only that this
       file's subject, the number, agrees with it. */
    combatant_set_init(&set);
    float pool = set.items[0].max_health;
    float paid_total = 0.0f;
    int rounds = 0;
    for (int i = 0; i < 400; ++i) {
        if (!combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                               1.0f, 0.0f, 0.0f, 100.0f, &hit)) break;
        paid_total += combatant_apply_damage(&set, &hit, 20.0f);
        ++rounds;
    }
    check("the figures thrown add up to the pool, and no more", paid_total,
          pool, 0.001f);
    expect(rounds < 400, "the body stops taking rounds once the pool is gone");
    expect(!set.items[0].alive, "because an emptied pool is a dead body");

    /* A round into a body that is already down. The figure is what has to stay
       quiet: the mark still lands and the player still sees they connected, but
       a number reporting nothing would sit there being re-popped by every round
       for as long as the trigger was held. */
    int figures_before = 0;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i)
        if (set.numbers.items[i].life > 0.0f) ++figures_before;
    paid = combatant_apply_damage(&set, &hit, 20.0f);
    check("   a round into a dead body takes nothing", paid, 0.0f, 0.001f);
    int figures_after = 0;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i)
        if (set.numbers.items[i].life > 0.0f) ++figures_after;
    expect(figures_after == figures_before,
           "   and a round that took nothing throws no figure");

    /* Regeneration, on a body that was left alive. Taken off a fresh set, since
       the one above is down and a body that is down does not heal. */
    combatant_set_init(&set);
    combatant_raycast(&set, body->x - 4.0f, chest, body->z,
                      1.0f, 0.0f, 0.0f, 100.0f, &hit);
    combatant_apply_damage(&set, &hit, 20.0f);
    float hurt = set.items[0].health;
    combatant_set_update(&set, body->x - 4.0f, body->z,
                         COMBAT_REGEN_DELAY * 0.5f);
    check("nothing regrows inside the delay", set.items[0].health, hurt, 0.001f);
    for (int i = 0; i < 600; ++i)
        combatant_set_update(&set, body->x - 4.0f, body->z, 1.0f / 120.0f);
    check("and it is full again after being left alone",
          set.items[0].health, COMBAT_DUMMY_HEALTH, 0.001f);
}

/* ---- 10. a body behind a wall ----

   The ordering inside trace_shot, checked through the real weapon against the
   real arena rather than by reading it. Bodies are resolved LAST, against the
   distance the geometry has already cut the ray to; reversed, a round would
   pass through a tower to reach a dummy on the far side of it, and there is no
   way to notice that by playing except as targets that die through walls.

   The spillway is the case with no arithmetic in it: a body inside its paired
   walls and a shooter standing outside the west wall. */
static void check_wall_blocks(const WeaponSpec* spec) {
    printf("\n10. a body behind a wall is behind the wall (%s)\n", spec->name);

    CombatantSet set;
    combatant_set_init(&set);
    /* The one in the southern spillway. Found by position rather than by index so
       re-ordering the spawn table cannot quietly turn this check off. */
    int spillway = -1;
    for (int i = 0; i < set.count; ++i)
        if (set.items[i].x > 40.0f && set.items[i].z > 25.0f) spillway = i;
    if (spillway < 0) {
        expect(0, "the arena has a body in the spillway");
        return;
    }

    PlayerController player;
    WeaponCommand command;
    Weapon weapon;
    const float step = 1.0f / 120.0f;
    const float eye = 1.6f;

    /* Outside the lane, with the x=39 wall squarely between. A yaw of -90
       looks down +X; moving two metres left of the body is inside the lane. */
    for (int outside = 1; outside >= 0; --outside) {
        combatant_set_init(&set);
        memset(&player, 0, sizeof player);
        player.ground.is_grounded = 1;
        player.x = outside ? 35.0f : set.items[spillway].x - 2.0f;
        player.z = set.items[spillway].z;

        memset(&command, 0, sizeof command);
        weapon_init(&weapon, spec);
        weapon_set_targets(&weapon, &set);
        /* A gun comes out of weapon_init with the trigger latched, so it wants
           a frame with the button up before a held press counts as a pull. */
        weapon_update(&weapon, &command, &player, -90.0f, 0.0f, 0.0f, eye,
                      75.0f, step);
        command.fire_held = 1;
        /* Half a second of fire down +Z: a look yaw of zero looks down -Z, so
           a hundred and eighty is the other way. */
        for (int i = 0; i < 60; ++i)
            weapon_update(&weapon, &command, &player, -90.0f, 0.0f, 0.0f, eye,
                          75.0f, step);

        float lost = COMBAT_DUMMY_HEALTH - set.items[spillway].health;
        if (outside)
            check("nothing reaches it through the spillway wall", lost, 0.0f,
                  0.001f);
        else
            expect(lost > 0.0f,
                   "and inside the lane the same shots land on it");
    }
}

/* ---- 11. the mark and the thing it is marking ----

   Every figure and bar the HUD draws is a world point put through
   third_person_camera_project. If that disagrees with the pass the world is
   drawn through, every number in the game floats somewhere near the body it
   belongs to rather than on it - and "near" is not something a screenshot
   settles. */
static void check_projection(void) {
    printf("\n11. a world point lands where the view puts it\n");

    ThirdPersonCamera camera;
    PlayerController player;
    third_person_camera_init(&camera);
    memset(&player, 0, sizeof player);
    camera.pitch_degrees = 0.0f;
    camera.yaw_degrees = 0.0f;
    camera.distance = 0.0f;
    camera.lean_offset = 0.0f;
    camera.roll_degrees = 0.0f;

    /* Ten metres along the camera's own forward, which is what the crosshair is
       over - so it has to come out at the middle of the screen. */
    float fx, fy, fz;
    traversal_aim_direction(camera.yaw_degrees, camera.pitch_degrees,
                            &fx, &fy, &fz);
    float eye_y = player.y + camera.target_height;
    float sx, sy, depth;
    expect(third_person_camera_project(&camera, &player, fx * 10.0f,
                                       eye_y + fy * 10.0f, fz * 10.0f,
                                       16.0f / 9.0f, &sx, &sy, &depth),
           "a point down the view axis projects");
    check("it lands on the middle of the screen, across", sx, 0.5f, 0.0005f);
    check("and down", sy, 0.5f, 0.0005f);
    check("at the distance it actually is", depth, 10.0f, 0.001f);

    /* Half the vertical field of view above the axis is the top edge, by
       definition of the projection - so this is the one number that says the
       tangent and the aspect are the right way up and the right way round. */
    float half = tanf(camera.fov_degrees * 0.5f * MATH_DEG_TO_RAD) * 10.0f;
    third_person_camera_project(&camera, &player, fx * 10.0f,
                                eye_y + fy * 10.0f + half, fz * 10.0f,
                                16.0f / 9.0f, &sx, &sy, &depth);
    check("half a field of view up is the top edge", sy, 0.0f, 0.0005f);

    expect(!third_person_camera_project(&camera, &player, -fx * 10.0f,
                                        eye_y - fy * 10.0f, -fz * 10.0f,
                                        16.0f / 9.0f, &sx, &sy, &depth),
           "and a point behind the eye does not project at all");

    /* The orbit. A third-person camera sits back along the view axis, so the
       same point is further away and nearer the middle - and if the sign of the
       distance term were wrong it would be nearer instead, which is the mistake
       this exists to catch. */
    camera.distance = 4.0f;
    float orbit_depth;
    third_person_camera_project(&camera, &player, fx * 10.0f,
                                eye_y + fy * 10.0f, fz * 10.0f,
                                16.0f / 9.0f, &sx, &sy, &orbit_depth);
    check("the orbit puts the same point further off", orbit_depth,
          14.0f, 0.001f);

    /* And now against the pass itself, rebuilt as arithmetic - the same trick
       section 1 plays on the viewmodel, and for the same reason. Everything
       above would still pass with the roll or the lean's sign reversed, because
       both are zero on the view axis and at the top of the screen. Off-axis
       with the view rolled, leaned and orbited, they are not. */
    camera.yaw_degrees = 37.0f;
    camera.pitch_degrees = -14.0f;
    camera.roll_degrees = 9.0f;
    camera.lean_offset = 0.06f;
    camera.distance = 3.5f;
    player.x = 12.0f; player.y = 3.0f; player.z = -7.0f;

    const float target[3] = {21.5f, 6.4f, -19.0f};
    float view_yaw, view_pitch, view_roll;
    third_person_camera_view_angles(&camera, &view_yaw, &view_pitch, &view_roll);
    /* draw_world's chain, in its order: back along the view axis, across for
       the lean, then roll, pitch, yaw, then the world onto the eye. GL
       post-multiplies, so each step applies inside the ones before it. */
    Mat4 view = mat4_translation(0.0f, 0.0f, -camera.distance);
    view = mat4_multiply(view, mat4_translation(-camera.lean_offset, 0, 0));
    view = mat4_multiply(view, mat4_rotation_degrees(-view_roll, 0, 0, 1));
    view = mat4_multiply(view, mat4_rotation_degrees(-view_pitch, 1, 0, 0));
    view = mat4_multiply(view, mat4_rotation_degrees(-view_yaw, 0, 1, 0));
    view = mat4_multiply(view, mat4_translation(-player.x,
                                                -(player.y + camera.target_height),
                                                -player.z));
    float v[3];
    mat4_transform_point(&view, target, v);
    float tangent = tanf(camera.fov_degrees * 0.5f * MATH_DEG_TO_RAD);
    float want_x = 0.5f + 0.5f * (v[0] / -v[2]) / (tangent * 16.0f / 9.0f);
    float want_y = 0.5f - 0.5f * (v[1] / -v[2]) / tangent;

    third_person_camera_project(&camera, &player, target[0], target[1],
                                target[2], 16.0f / 9.0f, &sx, &sy, &depth);
    check("rolled, leaned and orbited: across", sx, want_x, 0.0002f);
    check("                            down", sy, want_y, 0.0002f);
    check("                            depth", depth, -v[2], 0.0005f);
}

/* ---- 12. how far the player can look ----

   A clamp is one line and a feel decision, and this one was wrong for as long
   as it was a third-person orbit's range being applied to a first-person view.
   What is worth pinning is not the numbers themselves but the two properties
   they exist for: that the range reaches effectively vertical both ways, and
   that it stops short of the pole where yaw stops meaning anything. */
static void check_pitch_range(void) {
    printf("\n12. the look range reaches vertical without landing on it\n");

    ThirdPersonCamera camera;
    CameraCommand command;
    third_person_camera_init(&camera);
    check("the view starts level", camera.pitch_degrees, 0.0f, 0.0001f);

    /* All the way up, then all the way down. A mouse delta far larger than any
       real one, so what is left is the clamp and nothing else. */
    memset(&command, 0, sizeof command);
    command.look_y = -10000.0f;
    third_person_camera_apply_look(&camera, &command);
    check("looking up stops at the maximum", camera.pitch_degrees,
          VIEW_PITCH_MAX, 0.0001f);

    float up[3];
    traversal_aim_direction(camera.yaw_degrees, camera.pitch_degrees,
                            &up[0], &up[1], &up[2]);
    expect(up[1] > 0.999f, "and the aim is within a thousandth of straight up");
    /* The point of the held-back degree: there is still a horizontal component
       to the look direction, so yaw is still a direction and not a spin. */
    expect(sqrtf(up[0] * up[0] + up[2] * up[2]) > 0.01f,
           "with enough of a heading left in it for yaw to still mean something");

    command.look_y = 10000.0f;
    third_person_camera_apply_look(&camera, &command);
    check("looking down stops at the minimum", camera.pitch_degrees,
          VIEW_PITCH_MIN, 0.0001f);

    float down[3];
    traversal_aim_direction(camera.yaw_degrees, camera.pitch_degrees,
                            &down[0], &down[1], &down[2]);
    expect(down[1] < -0.999f,
           "and the aim is within a thousandth of straight down");
    expect(fabsf(VIEW_PITCH_MIN) == fabsf(VIEW_PITCH_MAX),
           "the range is symmetric - there is no reason for it not to be");
}


/* ---- 13. the shape of the reload ----

   Section 6 checks that every key on the reload track is somewhere an arm can
   reach. This checks the thing the player actually complains about, which is
   not where the hand goes but HOW: whether the magazine comes out faster than
   it goes in, whether anybody seats it, and whether the motion between keys is
   continuous rather than a series of teleports.

   None of it is visible in a still, and all of it is arithmetic. */
static void check_reload_shape(const WeaponSpec* spec) {
    printf("\n13. the reload is paced like a person doing it (%s)\n",
           spec->name);

    const ReloadSequence* track = weapon_spec_reload_sequence(spec);
    const float* grip = weapon_spec_mag_grip(spec);

    /* Found by what they ARE and not by index, so re-ordering or inserting a
       key cannot quietly turn these checks off. */
    int touched = -1, empty = -1;
    for (int i = 0; i < track->count; ++i) {
        const ReloadKey* key = &track->keys[i];
        if (touched < 0 && key->hand_frame == RELOAD_ON_GUN &&
            fabsf(key->hand[1] - grip[1]) < 1e-4f &&
            fabsf(key->hand[2] - grip[2]) < 1e-4f) touched = i;
        if (!key->drawn) empty = i;
    }
    expect(touched >= 0, "the hand takes hold of the magazine at some point");
    expect(empty >= 0, "and is empty at some point");
    if (touched < 0 || empty < 0) return;

    /* Out is a release and a rip: nothing has to be aligned, so it is quick.
       In is a fit, and it is not. This is the asymmetry the track did not have
       when every segment ran at the same length. */
    float out_span = (track->drop_at - track->keys[touched].at) *
                     firearm_of(spec)->reload_time;
    float in_span = (track->tap_at - track->keys[empty].at) * firearm_of(spec)->reload_time;
    printf("   the spent one is out in %.2fs; the fresh one is in and "
           "confirmed %.2fs after the hand is empty\n", out_span, in_span);
    expect(in_span > out_span * 1.5f,
           "getting one in takes over half again as long as getting one out");

    /* Somebody seats it. Between the magazine arriving and the tap, the support
       hand has to come OFF what it just pushed and come back to it - which is
       the whole of what a tap is, and the thing whose absence made the old
       version read as a magazine that arrived and was simply there. */
    expect(track->tap_at > track->seat_at,
           "the tap comes after the seat, not with it");
    float seated_y = 0.0f, tapped_y = 0.0f, lowest_y = 9.0f;
    for (int step = 0; step <= 200; ++step) {
        float progress = track->seat_at +
            (track->tap_at - track->seat_at) * (float)step / 200.0f;
        ReloadSample sample;
        reload_sequence_sample(track, progress, &sample);
        /* Along the well, in the gun's own space - which is where the hand is
           stated, so no transform is needed to compare two of them. */
        float y = lerpf(sample.from->hand[1], sample.to->hand[1], sample.t);
        if (step == 0) seated_y = y;
        if (step == 200) tapped_y = y;
        if (y < lowest_y) lowest_y = y;
    }
    printf("   hand along the well: %.3f seated, %.3f at its lowest, "
           "%.3f tapping\n", seated_y, lowest_y, tapped_y);
    expect(lowest_y < tapped_y - 0.015f && lowest_y < seated_y - 0.015f,
           "the hand comes off the floorplate and drives back into it");

    /* The gun is knocked twice, with a gap. Counted as rising edges rather than
       sampled at the two stated times, because what would actually be wrong is
       a jolt that fired once, three times, or never stopped. */
    int edges = 0;
    float previous = reload_sequence_seat_jolt(track, 0.0f);
    float peak = 0.0f;
    for (int step = 1; step <= 4000; ++step) {
        float jolt = reload_sequence_seat_jolt(track, (float)step / 4000.0f);
        if (previous <= 0.0f && jolt > 0.0f) ++edges;
        if (jolt > peak) peak = jolt;
        previous = jolt;
    }
    expect(edges == 2, "the gun is knocked exactly twice through a reload");
    check("the knock reaches full strength", peak, 1.0f, 0.02f);
    check("and there is none of it at rest, before",
          reload_sequence_seat_jolt(track, 0.0f), 0.0f, 1e-6f);
    check("or after", reload_sequence_seat_jolt(track, 1.0f), 0.0f, 1e-6f);

    /* Where the magazine comes FROM. The two halves of the reload want opposite
       answers about being on screen, and the old track gave the same answer to
       both: the pouch sat 14cm below the eye, which is under the chin and
       eighteen centimetres ABOVE the shoulder line the arms hang from, so the
       hand went backward toward the receiver at nearly constant height and the
       magazine read as coming off the base of the gun - which is very nearly
       where it came from, the pouch being 28cm from the well and 3cm above it.

       The well has to be visible, because that is where the interesting part
       happens. The pouch must not be, because a support hand dropping out of
       frame and coming back holding a magazine is what a reload looks like from
       behind a rifle, and a belt that appeared on screen would be a belt held up
       by the player's chin. */
    float tangent = tanf(spec->view_fov * 0.5f * MATH_DEG_TO_RAD);
    ViewmodelPose reload_pose;
    weapon_spec_rest_pose(spec, 0.0f, 1.0f, 0.0f, &reload_pose);
    float well[3];
    viewmodel_pose_transform_point(&reload_pose, weapon_spec_mag_grip(spec),
                                   well);
    float well_screen = (well[1] / -well[2]) / tangent;
    const ReloadKey* pouch = &track->keys[empty];
    float pouch_screen = (pouch->hand[1] / -pouch->hand[2]) / tangent;
    printf("   on screen, as a fraction of half the viewport: the well %+.2f, "
           "the pouch %+.2f\n", well_screen, pouch_screen);
    expect(pouch->hand_frame == RELOAD_IN_VIEW,
           "the pouch is on the body and not on the weapon");
    expect(well_screen > -1.0f, "the magazine well is in frame");
    expect(pouch_screen < -1.5f, "and the pouch is well below it, out of frame");
    /* And it is genuinely a reach DOWN rather than a reach back. The shoulder
       line is ARMS_SHOULDER_DOWN below the eye; a pouch above that is not a
       pouch, whatever else is true of it. */
    expect(-pouch->hand[1] > ARMS_SHOULDER_DOWN,
           "the pouch is below the shoulder line, which is what makes it kit");

    /* Continuity. Easing per key is what stops the motion reading as a machine,
       and an ease that failed to reach 1 by the end of its segment would leave
       the hand short of the key and then teleport it there on the next frame -
       which is the one way this change could make things worse rather than
       better. Measured as the largest step the hand takes between two adjacent
       samples over the whole track. */
    ViewmodelPose held;
    weapon_spec_rest_pose(spec, 0.0f, 1.0f, 0.0f, &held);
    const int steps = 2000;
    float largest = 0.0f;
    float largest_at = 0.0f;
    float previous_hand[3] = {0.0f, 0.0f, 0.0f};
    for (int step = 0; step <= steps; ++step) {
        float progress = (float)step / (float)steps;
        ReloadSample sample;
        reload_sequence_sample(track, progress, &sample);
        float ends[2][3];
        const ReloadKey* key[2] = {sample.from, sample.to};
        for (int e = 0; e < 2; ++e) {
            if (key[e]->hand_frame == RELOAD_IN_VIEW)
                for (int c = 0; c < 3; ++c) ends[e][c] = key[e]->hand[c];
            else
                viewmodel_pose_transform_point(&held, key[e]->hand, ends[e]);
        }
        float hand[3];
        for (int c = 0; c < 3; ++c)
            hand[c] = lerpf(ends[0][c], ends[1][c], sample.t);
        if (step > 0) {
            float dx = hand[0] - previous_hand[0];
            float dy = hand[1] - previous_hand[1];
            float dz = hand[2] - previous_hand[2];
            float moved = sqrtf(dx * dx + dy * dy + dz * dz);
            if (moved > largest) { largest = moved; largest_at = progress; }
        }
        for (int c = 0; c < 3; ++c) previous_hand[c] = hand[c];
    }
    printf("   largest step between %d samples: %.1fmm, at %.3f\n",
           steps, largest * 1000.0f, largest_at);
    expect(largest < 0.015f,
           "the hand never jumps - every ease reaches its key");
}

/* --- the arm solve, replicated ---
   viewmodel_arms.c is the original and cannot be linked here: it opens with
   GLFW and ends with glPushMatrix. What follows is the same arithmetic against
   the same rig, and it is worth the duplication for the same reason the
   placement search was worth splitting out - a wrist is a rotation, and a
   rotation can be checked without a window. Keep these in step with the
   originals; the tables are copied deliberately, so a change there that this
   does not see will show up as this check disagreeing with the game. */
static const float PROBE_HAND_DIR[2][2][3] = {
    { { 0.00f, -0.15f, -0.99f }, { 0.30f,  0.10f, -0.95f } },
    { { 0.00f, -0.15f, -0.99f }, { 0.18f, -0.28f, -0.94f } },
};
static const float PROBE_PALM_DIR[2][2][3] = {
    { { -1.00f, -0.20f, 0.00f }, { 0.00f, 0.00f, 0.00f } },
    { { -1.00f, -0.20f, 0.00f }, { 0.00f, 0.00f, 0.00f } },
};
static const float PROBE_ELBOW_POLE[3] = {0.55f, -0.78f, 0.30f};

typedef struct {
    short upper, lower, hand;
    float upper_axis[3], lower_axis[3], hand_axis[3];
    float hand_dir[3], palm_axis[3], palm_dir[3];
    int has_palm;
    float grip_centre[3];
    int has_grip;
    float upper_length, lower_length, side;
} ProbeArm;

static short probe_find(const Skeleton* s, const char* name) {
    for (int i = 0; i < s->node_count; ++i)
        if (strcmp(s->node_names[i], name) == 0) return (short)i;
    return -1;
}

static int probe_rest_axis(const Skeleton* s, int node, float out[3]) {
    for (int i = 0; i < s->node_count; ++i) {
        if (s->parent[i] != node) continue;
        const float* t = s->bind_local[i].translation;
        float l = sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
        if (l < 1e-6f) continue;
        out[0] = t[0]/l; out[1] = t[1]/l; out[2] = t[2]/l;
        return 1;
    }
    return 0;
}

static float probe_bone_length(const Skeleton* s, int node) {
    const float* t = s->bind_local[node].translation;
    return sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
}

static int probe_resolve(const Skeleton* s, ProbeArm* a, const char* upper,
                         const char* lower, const char* hand, float side) {
    a->upper = probe_find(s, upper);
    a->lower = probe_find(s, lower);
    a->hand = probe_find(s, hand);
    if (a->upper < 0 || a->lower < 0 || a->hand < 0) return 0;
    if (!probe_rest_axis(s, a->upper, a->upper_axis)) return 0;
    if (!probe_rest_axis(s, a->lower, a->lower_axis)) return 0;
    if (!probe_rest_axis(s, a->hand, a->hand_axis)) return 0;
    a->upper_length = probe_bone_length(s, a->lower);
    a->lower_length = probe_bone_length(s, a->hand);
    a->side = side;
    return 1;
}

static void probe_measure_palm(const Skeleton* s, ProbeArm* a,
                               const char* index, const char* pinky,
                               const char* thumb) {
    a->has_palm = 0;
    short i = probe_find(s, index), p = probe_find(s, pinky);
    short t = probe_find(s, thumb);
    if (i < 0 || p < 0 || t < 0) return;
    const float* fi = s->bind_local[i].translation;
    const float* fp = s->bind_local[p].translation;
    float knuckles[3];
    for (int c = 0; c < 3; ++c) knuckles[c] = fi[c] - fp[c];
    float n[3] = {
        knuckles[1]*a->hand_axis[2] - knuckles[2]*a->hand_axis[1],
        knuckles[2]*a->hand_axis[0] - knuckles[0]*a->hand_axis[2],
        knuckles[0]*a->hand_axis[1] - knuckles[1]*a->hand_axis[0],
    };
    float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (l < 1e-6f) return;
    for (int c = 0; c < 3; ++c) n[c] /= l;
    const float* ft = s->bind_local[t].translation;
    if (n[0]*ft[0] + n[1]*ft[1] + n[2]*ft[2] < 0.0f)
        for (int c = 0; c < 3; ++c) n[c] = -n[c];
    memcpy(a->palm_axis, n, sizeof a->palm_axis);
    a->has_palm = 1;
}

static void probe_unit(float v[3]) {
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (l > 1e-8f) for (int c = 0; c < 3; ++c) v[c] /= l;
}

static void probe_hand_basis(const float along[3], const float palm[3],
                             float u[3], float v[3], float w[3]) {
    memcpy(u, along, sizeof(float) * 3);
    probe_unit(u);
    float dot = palm[0]*u[0] + palm[1]*u[1] + palm[2]*u[2];
    for (int c = 0; c < 3; ++c) v[c] = palm[c] - u[c]*dot;
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (l < 1e-8f) { v[0] = u[1]; v[1] = -u[0]; v[2] = 0.0f; }
    probe_unit(v);
    w[0] = u[1]*v[2] - u[2]*v[1];
    w[1] = u[2]*v[0] - u[0]*v[2];
    w[2] = u[0]*v[1] - u[1]*v[0];
}

/* The fist's own middle, off the mesh - see measure_grip_centre. */
static void probe_measure_grip(const SkinnedModel* model, const Pose* fist,
                               SkinnedInstance* skinned, ProbeArm* arm,
                               const char* hand) {
    arm->has_grip = 0;
    arm->grip_centre[0] = arm->grip_centre[1] = arm->grip_centre[2] = 0.0f;
    if (!arm->has_palm) return;
    PoseMask owned;
    if (!skinned_model_mask_subtree(model, hand, &owned)) return;
    Mat4 hand_global;
    if (!skinned_model_bone_matrix(model, fist, hand, &hand_global)) return;
    skinned_model_skin(model, fist, skinned);

    const Skeleton* sk = &model->skeleton;
    float centre[3] = {0,0,0};
    int counted = 0;
    for (int m = 0; m < model->mesh_count && m < skinned->mesh_count; ++m) {
        const SkinnedMesh* mesh = &model->meshes[m];
        const SkinnedMeshVertices* posed = &skinned->meshes[m];
        if (!mesh->joints || !mesh->weights || !posed->vertices) continue;
        int count = mesh->vertex_count < posed->vertex_count
                  ? mesh->vertex_count : posed->vertex_count;
        for (int v = 0; v < count; ++v) {
            int best = -1; float most = 0.0f;
            for (int i = 0; i < SKIN_INFLUENCES; ++i) {
                float w = mesh->weights[v*SKIN_INFLUENCES + i];
                if (w <= most) continue;
                most = w; best = mesh->joints[v*SKIN_INFLUENCES + i];
            }
            if (best < 0 || best >= sk->joint_count) continue;
            int node = sk->joint_node[best];
            if (node < 0 || node >= SKIN_MAX_NODES || !owned.node[node]) continue;
            const float* p = &posed->vertices[v*SKIN_VERTEX_FLOATS];
            for (int c = 0; c < 3; ++c) centre[c] += p[c];
            ++counted;
        }
    }
    if (!counted) return;
    for (int c = 0; c < 3; ++c) centre[c] /= (float)counted;

    float basis[3][3];
    for (int c = 0; c < 3; ++c) {
        float l = sqrtf(hand_global.m[c*4+0]*hand_global.m[c*4+0] +
                        hand_global.m[c*4+1]*hand_global.m[c*4+1] +
                        hand_global.m[c*4+2]*hand_global.m[c*4+2]);
        if (l < 1e-8f) l = 1.0f;
        for (int r = 0; r < 3; ++r) basis[c][r] = hand_global.m[c*4+r] / l;
    }
    float world[3], local[3];
    for (int c = 0; c < 3; ++c) world[c] = centre[c] - hand_global.m[12+c];
    for (int c = 0; c < 3; ++c)
        local[c] = basis[c][0]*world[0] + basis[c][1]*world[1] + basis[c][2]*world[2];
    float u[3], v[3], w[3];
    probe_hand_basis(arm->hand_axis, arm->palm_axis, u, v, w);
    arm->grip_centre[0] = local[0]*u[0] + local[1]*u[1] + local[2]*u[2];
    arm->grip_centre[1] = local[0]*v[0] + local[1]*v[1] + local[2]*v[2];
    arm->grip_centre[2] = local[0]*w[0] + local[1]*w[1] + local[2]*w[2];
    arm->has_grip = 1;
}

static void probe_wrist_from_grip(const ProbeArm* arm, const float fingers[3],
                                  const float palm[3], const float grip[3],
                                  float out[3]) {
    memcpy(out, grip, sizeof(float) * 3);
    if (!arm->has_grip) return;
    float u[3], v[3], w[3];
    probe_hand_basis(fingers, palm, u, v, w);
    for (int c = 0; c < 3; ++c)
        out[c] -= u[c]*arm->grip_centre[0] + v[c]*arm->grip_centre[1] +
                  w[c]*arm->grip_centre[2];
}

/* One resolved, measured pair, shared by every check that needs a wrist target.
   Resolving them per call was fine while the answer was a grip point; now that
   it depends on the rig's own fist it wants measuring once. */
static ProbeArm g_probe_arms[2];
static int g_probe_arms_ready = 0;

static int probe_arms_ready(const SkinnedModel* model) {
    if (g_probe_arms_ready) return 1;
    const Skeleton* sk = &model->skeleton;
    if (!probe_resolve(sk, &g_probe_arms[0], "upperarm_r", "lowerarm_r",
                       "hand_r", -1.0f) ||
        !probe_resolve(sk, &g_probe_arms[1], "upperarm_l", "lowerarm_l",
                       "hand_l", 1.0f))
        return 0;
    probe_measure_palm(sk, &g_probe_arms[0], "index_01_r", "pinky_01_r",
                       "thumb_01_r");
    probe_measure_palm(sk, &g_probe_arms[1], "index_01_l", "pinky_01_l",
                       "thumb_01_l");
    Pose fist;
    int clip = -1;
    for (int c = 0; c < model->clip_count; ++c)
        if (strcmp(model->clips[c].name, "Idle_Loop") == 0) clip = c;
    if (clip >= 0) skinned_model_sample(model, clip, 0.0f, &fist);
    else skinned_model_bind_pose(model, &fist);
    const char* hand[2] = {"hand_r", "hand_l"};
    static SkinnedInstance skinned;
    static int skinned_ready = 0;
    if (!skinned_ready && !skinned_instance_init(&skinned, model)) return 0;
    skinned_ready = 1;
    for (int a = 0; a < 2; ++a)
        probe_measure_grip(model, &fist, &skinned, &g_probe_arms[a], hand[a]);
    g_probe_arms_ready = 1;
    return 1;
}

/* Grips to wrists for one pose. `on_gun` says whether the support grip is the
   handguard, which is aimed off the gun, or a reload key, which is not. */
static void probe_wrist_targets(const WeaponSpec* spec,
                                const ViewmodelPose* pose,
                                const float grip[2][3], float targets[2][3]) {
    /* Only the firing grip is a point on the GUN. The support hand's grip and
       every reload key are authored wrist positions already - see
       wrist_from_grip in viewmodel_arms.c for why that distinction is the whole
       of this. */
    memcpy(targets[1], grip[1], sizeof(float) * 3);
    float fingers[3], palm[3];
    viewmodel_pose_transform_direction(pose, PROBE_HAND_DIR[spec->hold][0],
                                       fingers);
    viewmodel_pose_transform_direction(pose, PROBE_PALM_DIR[spec->hold][0],
                                       palm);
    probe_wrist_from_grip(&g_probe_arms[0], fingers, palm, grip[0],
                          targets[0]);
}

static void probe_point_bone(const Skeleton* s, const Mat4 g[], int node,
                             const float axis[3], const float target[3],
                             Pose* pose) {
    float l = sqrtf(target[0]*target[0] + target[1]*target[1] +
                    target[2]*target[2]);
    if (l < 1e-6f) return;
    float dir[3] = {target[0]/l, target[1]/l, target[2]/l};
    int parent = s->parent[node];
    float local[3] = {dir[0], dir[1], dir[2]};
    if (parent >= 0) {
        const Mat4* p = &g[parent];
        for (int c = 0; c < 3; ++c) {
            float sc = sqrtf(p->m[c*4]*p->m[c*4] + p->m[c*4+1]*p->m[c*4+1] +
                             p->m[c*4+2]*p->m[c*4+2]);
            if (sc < 1e-8f) sc = 1.0f;
            local[c] = (p->m[c*4]*dir[0] + p->m[c*4+1]*dir[1] +
                        p->m[c*4+2]*dir[2]) / sc;
        }
    }
    quat_between(axis, local, pose->local[node].rotation);
}

static float probe_palm_roll(const Skeleton* s, const Mat4 g[], int node,
                             const ProbeArm* arm, const Pose* pose) {
    if (!arm->has_palm) return 0.0f;
    float want[3] = {arm->palm_dir[0], arm->palm_dir[1], arm->palm_dir[2]};
    if (want[0]*want[0] + want[1]*want[1] + want[2]*want[2] < 1e-8f) return 0.0f;
    float axis[3] = {arm->hand_dir[0], arm->hand_dir[1], arm->hand_dir[2]};
    float l = sqrtf(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
    if (l < 1e-6f) return 0.0f;
    for (int c = 0; c < 3; ++c) axis[c] /= l;
    const Transform* local = &pose->local[node];
    Mat4 m = mat4_from_trs(local->translation, local->rotation, local->scale);
    int parent = s->parent[node];
    Mat4 hand = parent < 0 ? m : mat4_multiply(g[parent], m);
    float have[3];
    mat4_transform_direction(&hand, arm->palm_axis, have);
    float ah = have[0]*axis[0] + have[1]*axis[1] + have[2]*axis[2];
    float aw = want[0]*axis[0] + want[1]*axis[1] + want[2]*axis[2];
    for (int c = 0; c < 3; ++c) { have[c] -= axis[c]*ah; want[c] -= axis[c]*aw; }
    float hl = sqrtf(have[0]*have[0] + have[1]*have[1] + have[2]*have[2]);
    float wl = sqrtf(want[0]*want[0] + want[1]*want[1] + want[2]*want[2]);
    if (hl < 1e-4f || wl < 1e-4f) return 0.0f;
    for (int c = 0; c < 3; ++c) { have[c] /= hl; want[c] /= wl; }
    float cross[3] = {
        have[1]*want[2] - have[2]*want[1],
        have[2]*want[0] - have[0]*want[2],
        have[0]*want[1] - have[1]*want[0],
    };
    float sine = cross[0]*axis[0] + cross[1]*axis[1] + cross[2]*axis[2];
    float cosine = have[0]*want[0] + have[1]*want[1] + have[2]*want[2];
    return atan2f(sine, cosine) / MATH_DEG_TO_RAD;
}

static void probe_reach(const float sh[3], const float t[3], float ul, float ll,
                        float side, float ud[3], float ld[3]) {
    float to[3];
    for (int c = 0; c < 3; ++c) to[c] = t[c] - sh[c];
    float d = sqrtf(to[0]*to[0] + to[1]*to[1] + to[2]*to[2]);
    if (d < 1e-5f) { d = 1e-5f; to[1] = -d; }
    float axis[3];
    for (int c = 0; c < 3; ++c) axis[c] = to[c] / d;
    float shortest = fabsf(ul - ll) + 1e-4f, longest = ul + ll - 1e-4f;
    if (d < shortest) d = shortest;
    if (d > longest) d = longest;
    float cosine = (ul*ul + d*d - ll*ll) / (2.0f * ul * d);
    float angle = acosf(clampf(cosine, -1.0f, 1.0f));
    float pole[3] = {PROBE_ELBOW_POLE[0]*side, PROBE_ELBOW_POLE[1],
                     PROBE_ELBOW_POLE[2]};
    float dot = pole[0]*axis[0] + pole[1]*axis[1] + pole[2]*axis[2];
    float bend[3];
    for (int c = 0; c < 3; ++c) bend[c] = pole[c] - axis[c]*dot;
    float l = sqrtf(bend[0]*bend[0] + bend[1]*bend[1] + bend[2]*bend[2]);
    if (l < 1e-5f) { bend[0] = axis[1]; bend[1] = -axis[0]; bend[2] = 0.0f;
                     l = sqrtf(bend[0]*bend[0] + bend[1]*bend[1]);
                     if (l < 1e-5f) { bend[0] = 1.0f; bend[1] = bend[2] = 0.0f;
                                      l = 1.0f; } }
    for (int c = 0; c < 3; ++c) bend[c] /= l;
    float elbow[3];
    for (int c = 0; c < 3; ++c) {
        ud[c] = axis[c]*cosf(angle) + bend[c]*sinf(angle);
        elbow[c] = sh[c] + ud[c]*ul;
        ld[c] = t[c] - elbow[c];
    }
    l = sqrtf(ld[0]*ld[0] + ld[1]*ld[1] + ld[2]*ld[2]);
    if (l < 1e-5f) memcpy(ld, ud, sizeof(float) * 3);
    else for (int c = 0; c < 3; ++c) ld[c] /= l;
}

static void probe_solve_arms(const SkinnedModel* model, ProbeArm arms[2],
                             const float targets[2][3], Pose* pose,
                             Mat4 out[SKIN_MAX_NODES]) {
    const Skeleton* s = &model->skeleton;
    Mat4 global[SKIN_MAX_NODES];
    float ud[2][3], ld[2][3];
    for (int i = 0; i < s->node_count; ++i) {
        int node = s->order[i];
        for (int a = 0; a < 2; ++a) {
            const ProbeArm* arm = &arms[a];
            if (node == arm->upper) {
                int p = s->parent[node];
                float sh[3] = {0, 0, 0};
                if (p >= 0) {
                    float off[3];
                    mat4_transform_direction(&global[p],
                                             pose->local[node].translation, off);
                    for (int c = 0; c < 3; ++c) sh[c] = global[p].m[12+c] + off[c];
                }
                probe_reach(sh, targets[a], arm->upper_length, arm->lower_length,
                            arm->side, ud[a], ld[a]);
                probe_point_bone(s, global, node, arm->upper_axis, ud[a], pose);
            } else if (node == arm->lower) {
                probe_point_bone(s, global, node, arm->lower_axis, ld[a], pose);
            } else if (node == arm->hand) {
                probe_point_bone(s, global, node, arm->hand_axis, arm->hand_dir,
                                 pose);
                float roll = probe_palm_roll(s, global, node, arm, pose);
                if (fabsf(roll) > 0.001f) {
                    int p = s->parent[node];
                    float local_axis[3] = {arm->hand_dir[0], arm->hand_dir[1],
                                           arm->hand_dir[2]};
                    if (p >= 0) {
                        const Mat4* pm = &global[p];
                        for (int c = 0; c < 3; ++c) {
                            float sc = sqrtf(pm->m[c*4]*pm->m[c*4] +
                                             pm->m[c*4+1]*pm->m[c*4+1] +
                                             pm->m[c*4+2]*pm->m[c*4+2]);
                            if (sc < 1e-8f) sc = 1.0f;
                            local_axis[c] = (pm->m[c*4]*arm->hand_dir[0] +
                                             pm->m[c*4+1]*arm->hand_dir[1] +
                                             pm->m[c*4+2]*arm->hand_dir[2]) / sc;
                        }
                    }
                    float turn[4], pointed[4];
                    memcpy(pointed, pose->local[node].rotation, sizeof pointed);
                    quat_from_axis_angle(local_axis, roll * MATH_DEG_TO_RAD,
                                         turn);
                    quat_multiply(turn, pointed, pose->local[node].rotation);
                }
            }
        }
        const Transform* l = &pose->local[node];
        Mat4 m = mat4_from_trs(l->translation, l->rotation, l->scale);
        int p = s->parent[node];
        global[node] = p < 0 ? m : mat4_multiply(global[p], m);
    }
    memcpy(out, global, sizeof(Mat4) * SKIN_MAX_NODES);
}

/* The gun's rotation as a basis in camera space, so a direction can be reported
   in the gun's own frame rather than the eye's - which is the only frame in
   which "the wrist did not move" is a meaningful thing to say, since the gun
   itself moves between the poses being compared. */
static void probe_gun_basis(const ViewmodelPose* pose, float basis[3][3]) {
    const float axis[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    const float zero[3] = {0,0,0};
    float origin[3];
    viewmodel_pose_transform_point(pose, zero, origin);
    for (int a = 0; a < 3; ++a) {
        float p[3];
        viewmodel_pose_transform_point(pose, axis[a], p);
        for (int c = 0; c < 3; ++c) basis[a][c] = p[c] - origin[c];
        probe_unit(basis[a]);
    }
}

/* The firing hand's palm and fingers for one pose, in the gun's frame. */
static int wrist_in_gun_frame(const WeaponSpec* spec, const SkinnedModel* model,
                              const ArmRig* rig, float sprint, float ads,
                              float palm_out[3], float fingers_out[3]) {
    const Skeleton* sk = &model->skeleton;
    ProbeArm arms[2];
    if (!probe_resolve(sk, &arms[0], "upperarm_r", "lowerarm_r", "hand_r", -1.0f) ||
        !probe_resolve(sk, &arms[1], "upperarm_l", "lowerarm_l", "hand_l", 1.0f))
        return 0;
    probe_measure_palm(sk, &arms[0], "index_01_r", "pinky_01_r", "thumb_01_r");
    probe_measure_palm(sk, &arms[1], "index_01_l", "pinky_01_l", "thumb_01_l");

    ViewmodelPose vp;
    weapon_spec_rest_pose(spec, sprint, 0.0f, ads, &vp);
    float grips[2][3];
    for (int a = 0; a < 2; ++a)
        viewmodel_pose_transform_point(&vp, a ? spec->grip_left
                                              : spec->grip_right, grips[a]);
    ArmPlacement p = {0, 0};
    if (!arm_placement_solve(rig, grips,
                             (ads > 0.5f ? weapon_spec_view_ads_fov(spec) : spec->view_fov)
                             * 0.5f, &p))
        return 0;

    float anchor[3];
    arm_placement_anchor(p, anchor);
    float targets[2][3], grip[2][3], wrists[2][3];
    viewmodel_pose_transform_point(&vp, spec->grip_right, grip[0]);
    viewmodel_pose_transform_point(&vp, spec->grip_left, grip[1]);
    probe_wrist_targets(spec, &vp, grip, wrists);
    for (int a = 0; a < 2; ++a)
        arm_placement_to_model_space(rig, wrists[a], anchor, p.blade,
                                     targets[a]);

    float basis[3][3];
    probe_gun_basis(&vp, basis);
    for (int a = 0; a < 2; ++a) {
        const float* want[2] = {PROBE_HAND_DIR[spec->hold][a],
                                PROBE_PALM_DIR[spec->hold][a]};
        float* into[2] = {arms[a].hand_dir, arms[a].palm_dir};
        for (int w = 0; w < 2; ++w) {
            if (want[w][0] == 0.0f && want[w][1] == 0.0f && want[w][2] == 0.0f) {
                memset(into[w], 0, sizeof(float) * 3);
                continue;
            }
            float in_camera[3];
            for (int c = 0; c < 3; ++c)
                in_camera[c] = basis[0][c]*want[w][0] + basis[1][c]*want[w][1] +
                               basis[2][c]*want[w][2];
            arm_placement_rig_from_camera(in_camera, p.blade, into[w]);
        }
    }

    Pose pose;
    int clip = -1;
    for (int c = 0; c < model->clip_count; ++c)
        if (strcmp(model->clips[c].name, "Idle_Loop") == 0) clip = c;
    if (clip >= 0) skinned_model_sample(model, clip, 0.0f, &pose);
    else skinned_model_bind_pose(model, &pose);

    Mat4 global[SKIN_MAX_NODES];
    probe_solve_arms(model, arms, targets, &pose, global);

    /* The hand's own axes out of its global, back through the placement into
       the gun's frame. The palm axis is measured; the fingers are the hand's
       rest axis, which is what the pointing aimed. */
    const Mat4* h = &global[arms[0].hand];
    const float* local[2] = {arms[0].palm_axis, arms[0].hand_axis};
    float* out[2] = {palm_out, fingers_out};
    for (int w = 0; w < 2; ++w) {
        float model_dir[3];
        mat4_transform_direction(h, local[w], model_dir);
        probe_unit(model_dir);
        float camera_dir[3];
        arm_placement_rig_to_camera(model_dir, p.blade, camera_dir);
        for (int c = 0; c < 3; ++c)
            out[w][c] = basis[c][0]*camera_dir[0] + basis[c][1]*camera_dir[1] +
                        basis[c][2]*camera_dir[2];
        probe_unit(out[w]);
    }
    return 1;
}

/* The firing wrist, in the gun's own frame.

   What has to hold is that it does not move: the shooting hand never leaves the
   pistol grip, so its palm and its fingers should read the same against the gun
   at the hip, through the sprint carry and at full aim. Before the palm was
   constrained they did not - the same grip measured palm-inward at the hip and
   palm-DOWN at the shoulder, a quarter turn of wrist between two poses of one
   hold - because pointing the fingers is a minimal rotation and left the spin
   around them to whatever the elbow arrived at.

   Checked here and not by eye for the reason the placements are: a wrist rolled
   ninety degrees is a hand that still sits on the grip, still tracks the gun and
   still looks like a hand, and the pose it is wrong in is the one the player
   spends least time looking at closely. */
static void check_firing_wrist(const WeaponSpec* spec, const SkinnedModel* model,
                               const ArmRig* rig) {
    printf("\n11. the firing wrist against the gun (%s)\n", spec->name);

    const char* label[3] = {"hip", "sprint", "aimed"};
    const float sprint_at[3] = {0.0f, 1.0f, 0.0f};
    const float ads_at[3] = {0.0f, 0.0f, 1.0f};
    float first[2][3];
    int have_first = 0;

    for (int i = 0; i < 3; ++i) {
        float palm[3], fingers[3];
        if (!wrist_in_gun_frame(spec, model, rig, sprint_at[i], ads_at[i],
                                palm, fingers)) {
            printf("   FAIL %s: no placement to measure against\n", label[i]);
            ++failures;
            continue;
        }
        printf("   %-6s palm %+.2f %+.2f %+.2f   fingers %+.2f %+.2f %+.2f\n",
               label[i], palm[0], palm[1], palm[2],
               fingers[0], fingers[1], fingers[2]);
        if (!have_first) {
            memcpy(first[0], palm, sizeof first[0]);
            memcpy(first[1], fingers, sizeof first[1]);
            have_first = 1;
            continue;
        }
        /* Against the hip, which is the pose the other two are reached from.
           Five degrees is tighter than anything a player could see and far
           looser than the arithmetic actually needs - it held to under one. */
        float palm_dot = first[0][0]*palm[0] + first[0][1]*palm[1] +
                         first[0][2]*palm[2];
        float finger_dot = first[1][0]*fingers[0] + first[1][1]*fingers[1] +
                           first[1][2]*fingers[2];
        char what[64];
        snprintf(what, sizeof what, "the palm holds still from the hip to %s",
                 label[i]);
        expect(acosf(clampf(palm_dot, -1.0f, 1.0f)) < 5.0f * MATH_DEG_TO_RAD,
               what);
        snprintf(what, sizeof what, "the fingers hold still from the hip to %s",
                 label[i]);
        expect(acosf(clampf(finger_dot, -1.0f, 1.0f)) < 5.0f * MATH_DEG_TO_RAD,
               what);
    }

    /* And that it is the RIGHT stillness: a wrist frozen palm-up is as still as
       one frozen correctly. The shooting hand wraps the grip from its right, so
       the palm faces across the gun toward the centreline. */
    if (have_first)
        expect(first[0][0] < -0.85f,
               "the palm faces across the gun, not up, down or outward");
}

/* The clip-driven arms, sampled across every attack's live window.

   Not precision tuning - the numbers are a box, not a target. What this
   catches is the conversion going wrong wholesale: a dropped facing rotation
   puts the hands behind the eye, a missing rig scale puts them two metres
   away, and both draw perfectly happily off screen where nobody sees the arms
   are gone rather than merely misplaced. */
static void check_clip_arms(const WeaponSpec* spec, const SkinnedModel* model) {
    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee) return;
    printf("\n%s clip arms, hand_r in camera space\n", spec->name);
    for (int a = 0; a < melee->attack_count; ++a) {
        const MeleeAttack* attack = &melee->attacks[a];
        int clip = skinned_model_find_clip(model, attack->clip);
        if (clip < 0) { expect(0, attack->clip); continue; }
        float duration = model->clips[clip].duration;
        /* A block has no live window, so it is sampled across the whole clip
           rather than across a window stated backwards. */
        float from = attack->active_to > attack->active_from
                     ? attack->active_from : 0.0f;
        float to = attack->active_to > attack->active_from
                   ? attack->active_to : 1.0f;
        const float at[3] = { from, (from + to) * 0.5f, to };
        for (int k = 0; k < 3; ++k) {
            Pose pose;
            Mat4 hand;
            if (!viewmodel_clip_arms_pose(model, clip, at[k] * duration, melee,
                                          &pose, &hand)) {
                expect(0, attack->clip);
                break;
            }
            float x = hand.m[12], y = hand.m[13], z = hand.m[14];
            int ok = fabsf(x) < 0.9f && y > -0.9f && y < 0.7f && z < 0.3f;
            if (!ok) ++failures;
            printf("  %s %-28s at %.2f  %+.3f %+.3f %+.3f\n",
                   ok ? "ok  " : "FAIL", attack->clip, at[k], x, y, z);
        }
    }
}

/* The sword and the hand holding it must not be independently derived.

   Both come off viewmodel_clip_arms_base, and the check that they still do is
   that the pose weapon_viewmodel_pose composes lands where the hand transform
   said. Metres apart is the failure this exists for: a sword that took its
   base from the spec's rest pose instead would sit exactly where the rifle
   does, look entirely reasonable in a screenshot, and be nowhere near the
   hands that are supposedly gripping it. */
static void check_melee_rides_hand(const SkinnedModel* model) {
    const WeaponSpec* spec = &WEAPON_SPEC_KATANA;
    const MeleeSpec* melee = weapon_spec_melee(spec);
    printf("\n%s rides the hand\n", spec->name);

    float time = 0.0f;
    Weapon weapon;
    weapon_init(&weapon, spec);
    const char* name = weapon_melee_clip(&weapon, &time);
    int clip = viewmodel_clip_arms_find(model, name);
    if (clip < 0) { expect(0, name ? name : "(no hold clip)"); return; }

    Mat4 hand;
    Pose arms;
    ViewmodelPose base;
    if (!viewmodel_clip_arms_pose(model, clip, time, melee, &arms, &hand) ||
        !viewmodel_clip_arms_base(model, clip, time, melee, &arms, &base)) {
        expect(0, "the hold pose samples");
        return;
    }
    weapon_set_melee_base(&weapon, &base, 1);

    PlayerController player;
    memset(&player, 0, sizeof player);
    player.ground.is_grounded = 1;

    ViewmodelPose pose;
    weapon_viewmodel_pose(&weapon, &player, &pose);
    check("sword x against hand_r", pose.x, hand.m[12], 0.03f);
    check("sword y against hand_r", pose.y, hand.m[13], 0.03f);
    check("sword z against hand_r", pose.z, hand.m[14], 0.03f);

    /* And the regression guard: a firearm still takes its base from the spec,
       so nothing above has moved the rifle. */
    Weapon rifle;
    weapon_init(&rifle, &WEAPON_SPEC_ASSAULT_RIFLE);
    ViewmodelPose rifle_pose, rest;
    weapon_viewmodel_pose(&rifle, &player, &rifle_pose);
    weapon_spec_rest_pose(&WEAPON_SPEC_ASSAULT_RIFLE, 0.0f, 0.0f, 0.0f, &rest);
    check("the rifle is still at its rest pose", rifle_pose.z, rest.z, 0.0005f);
}

int main(void) {
    const WeaponSpec* spec = &WEAPON_SPEC_ASSAULT_RIFLE;
    check_pose_matches_gl(spec);
    check_pose_matches_gl(&WEAPON_SPEC_AUTO_PISTOL);
    check_no_visible_change(spec);
    check_no_visible_change(&WEAPON_SPEC_AUTO_PISTOL);
    /* The M4's receiver runs back from the origin; the stock is the far end. */
    check_near_plane(spec, 0.02f, 0.19f);

    static SkinnedModel model;
    char error[256];
    if (!skinned_model_load_glb(&model, "models/animations/UAL1_Standard.glb",
                                error, sizeof error)) {
        printf("\n4. arm placements: SKIPPED, rig would not load: %s\n", error);
        ++failures;
    } else {
        /* The sword clips ship in the second library, so a probe that loaded
           only the first would report every one of them missing. */
        if (!skinned_model_add_clips(&model,
                                     "models/animations/UAL2_Standard.glb",
                                     error, sizeof error))
            printf("note: extra clips not loaded (%s)\n", error);
        check_clip_arms(&WEAPON_SPEC_KATANA, &model);
        check_melee_rides_hand(&model);
        ArmRig rig;
        if (check_arms(spec, &model, &rig)) {
            check_reload(spec, &rig);
            check_firing_wrist(spec, &model, &rig);
            check_firing_wrist(&WEAPON_SPEC_AUTO_PISTOL, &model, &rig);
        }
    }

    check_gait(spec);
    check_sprint_while_firing(spec);
    check_dropped_mag(spec);
    check_hitbox();
    check_damage();
    check_wall_blocks(spec);
    check_projection();
    check_pitch_range();
    check_reload_shape(spec);

    printf("\n%s (%d failing checks)\n", failures ? "FAILED" : "all checks pass",
           failures);
    return failures != 0;
}

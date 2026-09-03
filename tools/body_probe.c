/* Headless checks for the character rig: that a peek bends the body the way the
   player asked, that it takes the gun with it, that it leaves the feet where
   they were, and that nothing on the body jumps when the clip under it
   changes.

   `make leanprobe` from the project root, on the host compiler rather than the
   cross one - posing a skeleton needs no window and no GL context.

   It exists because the sign of a lean is exactly the kind of thing that looks
   plausible either way in a screenshot and is unarguable in the numbers. The
   rig faces its own +z with +y up, so up cross forward is +x and +x is the
   model's own left: pressing Q publishes a positive lean and every point above
   the waist should move that way. The previous version of this code moved them
   the other way, and did it by rolling the whole transform, so the feet went
   with them. Both of those are one printed number each. */
#include "render/character_animation.h"
#include "render/character_transform.h"
#include "render/skinned_model.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/weapon_spec.h"
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

static void check_min(const char* what, float got, float least) {
    int ok = got >= least;
    if (!ok) ++failures;
    printf("  %s %-44s got %+.4f want >= %+.4f\n", ok ? "ok  " : "FAIL", what,
           got, least);
}

/* Every clip a spec names has to exist in the rig. A name that resolves to -1
   is a hold that silently does not happen: the aim layer contributes nothing
   and the body stands there with its arms in the locomotion pose, holding a
   weapon that is still drawn, still fires, and still hits. Nothing errors, and
   the only symptom is a soldier walking around with a rifle floating beside a
   swinging arm. */
static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

static void check_clip(const SkinnedModel* model, const char* name) {
    expect(name && skinned_model_find_clip(model, name) >= 0,
           name ? name : "(unnamed clip)");
}

static void check_clip_set(const SkinnedModel* model, const WeaponSpec* spec) {
    printf("\n%s clip set\n", spec->name);
    for (int i = 0; i < CHARACTER_AIM_COUNT; ++i)
        check_clip(model, spec->clips.aim[i]);

    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee) return;

    /* Every attack the table names, and every window it states. A clip that
       resolves to -1 is a swing that does not happen and does not complain; a
       window outside 0..1 is a blade that is live before the clip starts or
       after it ends; and a `next` off the end of the table is a combo that
       reads garbage on the third hit, which is the one of the three that can
       corrupt rather than merely disappoint. */
    for (int a = 0; a < melee->attack_count; ++a) {
        const MeleeAttack* attack = &melee->attacks[a];
        check_clip(model, attack->clip);
        if (attack->recovery) check_clip(model, attack->recovery);
        if (attack->active_to >= attack->active_from) {
            expect(attack->active_from >= 0.0f && attack->active_to <= 1.0f,
                   "the live window lies inside the clip");
            expect(attack->active_to > attack->active_from,
                   "the live window has width");
        }
        expect(attack->next == -1 ||
               (attack->next >= 0 && attack->next < melee->attack_count),
               "next is -1 or an attack that exists");
    }
    expect(melee->attack_count > 0 && melee->attack_count <= MELEE_ATTACK_MAX,
           "the attack table is neither empty nor overfull");
    expect(melee->light_attack >= 0 && melee->light_attack < melee->attack_count,
           "the light attack exists");
    expect(melee->heavy_attack >= 0 && melee->heavy_attack < melee->attack_count,
           "the heavy attack exists");
    expect(melee->block_attack >= 0 && melee->block_attack < melee->attack_count,
           "the block exists");
}

/* A standing, grounded body looking straight down its own forward, which is
   the one state where nothing but the lean is moving anything. */
static CharacterVisualState standing(float lean, float ads) {
    CharacterVisualState state;
    memset(&state, 0, sizeof state);
    /* Heading is atan2(vx, vz) and a look yaw of zero looks down -z, so this
       pair is a body facing the way it is looking - no facing twist, and the
       lean is then the only thing in the pose that is not the clip. */
    state.heading_degrees = 180.0f;
    state.look_yaw_degrees = 0.0f;
    state.grounded = 1;
    state.lean = lean;
    state.ads = ads;
    return state;
}

/* Where a bone ends up in the model's own space, after settling the animation
   at the given lean for long enough that the facing has stopped moving. */
static int bone_at(CharacterAnimation* animation, const SkinnedModel* model,
                   float lean, float ads, const char* bone, float out[3]) {
    CharacterVisualState state = standing(lean, ads);
    for (int i = 0; i < 240; ++i)
        character_animation_update(animation, model, &state, 1.0f / 120.0f);
    Pose pose;
    character_animation_pose(animation, model, &pose);
    Mat4 m;
    if (!skinned_model_bone_matrix(model, &pose, bone, &m)) {
        printf("  FAIL bone '%s' is not in this rig\n", bone);
        ++failures;
        return 0;
    }
    out[0] = m.m[12]; out[1] = m.m[13]; out[2] = m.m[14];
    return 1;
}

/* --- moving while aiming ---

   The case the rig is hardest on itself: aiming pins the chest and the rifle to
   the crosshair, and moving anywhere but straight at it winds the legs round
   underneath - up to FACING_LEG_YAW_MAX of twist held all the time. Every clip
   change then happens on a body already wound to its limit, which is where a
   pose that is composed wrong stops being subtle.

   The invariant these check is the one the whole facing split exists for: the
   gun points where the crosshair is, whatever the legs are doing. A clip change
   underneath is a leg event. It must not move the hands. */

/* Strafing across the crosshair at `speed`, sight up. Heading a quarter turn
   off the look direction is the worst case for the twist and the one a player
   holds for whole fights. */
static CharacterVisualState strafing(float speed, float ads) {
    CharacterVisualState state = standing(0.0f, ads);
    state.heading_degrees = 90.0f;
    state.velocity_x = speed * sinf(90.0f * 3.14159265f / 180.0f);
    state.velocity_z = 0.0f;
    state.moving = speed > 0.05f;
    state.has_movement_input = state.moving;
    return state;
}

/* The largest single-frame move any of the watched bones makes while `state` is
   held, in metres. Frame to frame at 120Hz a body walking moves its hands by a
   centimetre or two; a pose that is composed wrong moves them by a tenth of a
   metre on one frame and back on the next, which is what "bugs out" looks like
   in numbers. */
static float largest_jump(CharacterAnimation* animation, const SkinnedModel* model,
                          const CharacterVisualState* state, int frames,
                          float scale, const char* bone, const char** worst_frame) {
    static char where[64];
    float previous[3] = {0, 0, 0};
    float largest = 0.0f;
    for (int i = 0; i < frames; ++i) {
        character_animation_update(animation, model, state, 1.0f / 120.0f);
        Pose pose;
        character_animation_pose(animation, model, &pose);
        Mat4 m;
        if (!skinned_model_bone_matrix(model, &pose, bone, &m)) return -1.0f;
        float now[3] = {m.m[12], m.m[13], m.m[14]};
        if (i > 0) {
            float dx = now[0] - previous[0], dy = now[1] - previous[1],
                  dz = now[2] - previous[2];
            float moved = sqrtf(dx*dx + dy*dy + dz*dz) * scale;
            if (moved > largest) {
                largest = moved;
                snprintf(where, sizeof where, "frame %d", i);
            }
        }
        previous[0] = now[0]; previous[1] = now[1]; previous[2] = now[2];
    }
    if (worst_frame) *worst_frame = where;
    return largest;
}

/* Where the rifle in the body's right hand actually points, in the WORLD, as a
   yaw and a pitch off the direction the player is looking. Zero and zero is a
   rifle on the crosshair.

   The world and not the model's own space, because the two only agree while the
   body is standing where it is looking - and the states worth checking are the
   ones where it is not. A body on a wall keeps its heading and aims with
   everything above the waist; measuring the gun against the body it hangs off
   would call that perfect and say nothing.

   This is the invariant the whole aim layer exists for, measured on the thing
   the player sees: not "the spine got a rotation" but "the barrel points at the
   crosshair". The transform chain is character_renderer's grip, rebuilt here -
   the bone, into the palm, the quarter turn that stands the gun up in the fist,
   then back by the grip point - and the rifle's own forward is -z, which is the
   axis its muzzle is measured along. */
static int gun_angles(const CharacterAnimation* animation, const SkinnedModel* model,
                      float look_yaw_degrees, float look_pitch_degrees,
                      float* yaw_degrees, float* pitch_degrees) {
    Pose pose;
    character_animation_pose(animation, model, &pose);
    Mat4 hand;
    if (!skinned_model_bone_matrix(model, &pose, "hand_r", &hand)) return 0;
    Mat4 grip = mat4_multiply(hand, mat4_translation(-0.020f, 0.060f, 0.0f));
    grip = mat4_multiply(grip, mat4_rotation_degrees(90.0f, 1, 0, 0));

    const float barrel[3] = {0.0f, 0.0f, -1.0f};
    float d[3];
    mat4_transform_direction(&grip, barrel, d);
    float length = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (length < 1e-6f) return 0;
    for (int i = 0; i < 3; ++i) d[i] /= length;
    /* Out of the model's space and into the world: the body is drawn turned by
       its own body yaw, in the atan2(vx, vz) degrees headings are measured in.
       Then measured against where the player is looking, which in those same
       degrees is the look yaw plus half a turn. */
    float model_yaw = atan2f(d[0], d[2]) * 180.0f / 3.14159265f;
    float world_yaw = model_yaw + character_animation_body_yaw(animation);
    float error = world_yaw - (look_yaw_degrees + 180.0f);
    while (error > 180.0f) error -= 360.0f;
    while (error <= -180.0f) error += 360.0f;
    *yaw_degrees = error;
    *pitch_degrees = asinf(d[1]) * 180.0f / 3.14159265f - look_pitch_degrees;
    return 1;
}

/* Holds `state` until everything has settled, then reports where the gun ended
   up pointing. */
static void settle_and_aim(CharacterAnimation* animation, const SkinnedModel* model,
                           const CharacterVisualState* state, const char* label,
                           float limit) {
    for (int i = 0; i < 360; ++i)
        character_animation_update(animation, model, state, 1.0f / 120.0f);
    float yaw = 0.0f, pitch = 0.0f;
    if (!gun_angles(animation, model, state->look_yaw_degrees,
                    state->look_pitch_degrees, &yaw, &pitch)) {
        printf("  FAIL %s: no hand_r on this rig\n", label);
        ++failures;
        return;
    }
    printf("       %-26s off the crosshair by %+6.1f yaw %+6.1f pitch\n",
           label, yaw, pitch);
    /* The states where the sight owns the body are held to it. The ones where a
       clip owns the heading - a body on a wall, a body pulling itself over a
       ledge - are printed and not checked: their legs are on the geometry, and
       what the arms above them can do about the crosshair is bounded by how far
       a neck and a waist turn. */
    if (limit > 0.0f) {
        char label_yaw[80], label_pitch[80];
        snprintf(label_yaw, sizeof label_yaw, "%s, yaw off the crosshair", label);
        snprintf(label_pitch, sizeof label_pitch, "%s, pitch off the crosshair", label);
        check(label_yaw, yaw, 0.0f, limit);
        check(label_pitch, pitch, 0.0f, limit);
    }
}

int main(void) {
    SkinnedModel model;
    char error[256];
    if (!skinned_model_load_glb(&model, "models/animations/UAL1_Standard.glb",
                                error, sizeof error)) {
        printf("could not load the rig: %s\n", error);
        return 1;
    }
    if (!skinned_model_add_clips(&model, "models/animations/UAL2_Standard.glb",
                                 error, sizeof error))
        printf("note: extra clips not loaded (%s)\n", error);

    float scale = (model.max_y - model.min_y) > 0.01f
        ? CHARACTER_RIG_HEIGHT / (model.max_y - model.min_y) : 1.0f;

    CharacterAnimation animation;
    character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);

    const char* BONES[] = {"Head", "hand_r", "foot_l", "foot_r"};
    float upright[4][3], left[4][3], right[4][3];
    for (int b = 0; b < 4; ++b) {
        character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
        if (!bone_at(&animation, &model, 0.0f, 0.0f, BONES[b], upright[b])) return 1;
        character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
        if (!bone_at(&animation, &model, 1.0f, 0.0f, BONES[b], left[b])) return 1;
        character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
        if (!bone_at(&animation, &model, -1.0f, 0.0f, BONES[b], right[b])) return 1;
    }

    /* And the gun on the sight, which is the peek anyone actually takes: the
       rifle is up at the shoulder there, next to the head, so it travels with
       the head rather than trailing near the hip the way a carried one does. */
    float aimed_upright[3], aimed_left[3];
    character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    if (!bone_at(&animation, &model, 0.0f, 1.0f, "hand_r", aimed_upright)) return 1;
    character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    if (!bone_at(&animation, &model, 1.0f, 1.0f, "hand_r", aimed_left)) return 1;

    printf("\n1. a left lean takes the head and the gun to the model's own left\n");
    /* Sized against the eye. The camera slides VIEW_LEAN_OFFSET off centre and
       the hips slide CHARACTER_LEAN_OFFSET, so the spine has the difference to
       find if the drawn head is to arrive where the eye already is. A quarter
       of a metre of tolerance on a body two and a half metres tall: this is
       checking that the bend is worth the name, not that it is to the
       millimetre. */
    float want_head = VIEW_LEAN_OFFSET - CHARACTER_LEAN_OFFSET;
    check("head sideways, metres", (left[0][0] - upright[0][0]) * scale,
          want_head, 0.25f);
    /* The carried rifle hangs low, near the hips the bend pivots about, so it
       goes some of the way rather than all of it. That is the geometry being
       honest and not a shortfall: what matters is that it goes, and that it
       goes the same way the chest carrying it does. */
    check_min("carried gun hand sideways, metres",
              (left[1][0] - upright[1][0]) * scale, 0.05f);
    check_min("aimed gun hand sideways, metres",
              (aimed_left[0] - aimed_upright[0]) * scale, 0.12f);

    printf("\n2. and a right lean is its mirror\n");
    check("head sideways, metres", (right[0][0] - upright[0][0]) * scale,
          -want_head, 0.25f);
    check("head left plus head right", (left[0][0] + right[0][0] -
          2.0f * upright[0][0]) * scale, 0.0f, 0.02f);

    printf("\n3. the feet stay planted\n");
    for (int b = 2; b < 4; ++b) {
        char label[64];
        snprintf(label, sizeof label, "%s sideways, metres", BONES[b]);
        check(label, (left[b][0] - upright[b][0]) * scale, 0.0f, 0.02f);
        snprintf(label, sizeof label, "%s vertical, metres", BONES[b]);
        check(label, (left[b][1] - upright[b][1]) * scale, 0.0f, 0.02f);
    }

    printf("\n4. and the head goes further sideways than the hips do\n");
    /* The whole shape of a peek in one number: if the head does not outrun the
       body's own lateral shift, what is on screen is a sidestep. */
    check_min("head over hip shift, metres",
              (left[0][0] - upright[0][0]) * scale - CHARACTER_LEAN_OFFSET, 0.10f);

    /* Held states, then the transitions between them. A clip change is the
       moment the composition has to be right, and while aiming and moving it is
       also the moment the body is wound furthest round. */
    printf("\n5. moving while aiming does not throw the gun about\n");
    {
        const char* worst = "";
        CharacterVisualState jog = strafing(6.0f, 1.0f);
        CharacterVisualState walk = strafing(2.0f, 1.0f);
        CharacterVisualState idle = strafing(0.0f, 1.0f);

        character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
        for (int i = 0; i < 240; ++i)
            character_animation_update(&animation, &model, &jog, 1.0f / 120.0f);
        float held = largest_jump(&animation, &model, &jog, 120, scale,
                                  "hand_r", &worst);
        check("gun hand, per frame at a steady jog", held, 0.0f, 0.035f);

        /* Straight into the clip change, on the wound-up body the jog left. */
        float changing = largest_jump(&animation, &model, &walk, 60, scale,
                                      "hand_r", &worst);
        printf("       (worst at %s)\n", worst);
        check("gun hand, per frame across jog to walk", changing, 0.0f, 0.035f);

        float stopping = largest_jump(&animation, &model, &idle, 60, scale,
                                      "hand_r", &worst);
        printf("       (worst at %s)\n", worst);
        check("gun hand, per frame across walk to idle", stopping, 0.0f, 0.035f);

        /* And the hips, which is where a double-applied twist lands: the pelvis
           carries the whole leg turn, so anything that composes it twice shows
           here first and largest. */
        character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
        for (int i = 0; i < 240; ++i)
            character_animation_update(&animation, &model, &jog, 1.0f / 120.0f);
        float hips = largest_jump(&animation, &model, &walk, 60, scale,
                                  "thigh_l", &worst);
        printf("       (worst at %s)\n", worst);
        check("thigh, per frame across jog to walk", hips, 0.0f, 0.035f);
    }

    printf("\n6. where the rifle actually points, by state (sight up, look level)\n");
    {
        CharacterVisualState idle = strafing(0.0f, 1.0f);
        CharacterVisualState jog = strafing(6.0f, 1.0f);
        CharacterVisualState crouch = strafing(2.0f, 1.0f);
        crouch.crouching = 1;
        CharacterVisualState air = strafing(6.0f, 1.0f);
        air.grounded = 0;
        CharacterVisualState slide = strafing(9.0f, 1.0f);
        slide.sliding = 1; slide.crouching = 1;
        CharacterVisualState climb = strafing(2.0f, 1.0f);
        climb.wall_climbing = 1;

        /* Looking somewhere other than level, too: the aim layer picks its pose
           from the camera's pitch, and a body that tracks at zero and nowhere
           else is a body that is not tracking. */
        CharacterVisualState high = strafing(6.0f, 1.0f);
        high.look_pitch_degrees = 40.0f;
        CharacterVisualState low = strafing(6.0f, 1.0f);
        low.look_pitch_degrees = -25.0f;
        /* And the same body with the sight down, which must NOT be held to the
           crosshair: a rifle carried at the ready points where the carrying
           does, and pinning it there would be the stiffness this is trying to
           avoid everywhere it is not wanted. */
        CharacterVisualState carried = strafing(6.0f, 0.0f);
        /* And peeking while aiming, which is the whole point of a peek: the
           head and the rifle come out past the corner, and the barrel had
           better still be on what the player is looking at when they get
           there. The lean rolls the torso about roughly the axis the gun
           points along, so it cants the weapon rather than swinging it - but
           "roughly" is a word worth a check. */
        CharacterVisualState peeking = strafing(6.0f, 1.0f);
        peeking.lean = 1.0f;
        CharacterVisualState lean_still_r = strafing(0.0f, 1.0f);
        lean_still_r.lean = -1.0f;
        CharacterVisualState lean_half = strafing(0.0f, 1.0f);
        lean_half.lean = 0.5f;

        const struct {
            const char* label; const CharacterVisualState* state; float limit;
        } CASES[] = {
            {"standing", &idle, 4.0f},
            {"jogging across the aim", &jog, 4.0f},
            {"crouch walking", &crouch, 4.0f},
            {"airborne", &air, 4.0f},
            {"sliding", &slide, 4.0f},
            {"jogging, looking up 40", &high, 6.0f},
            {"jogging, looking down 25", &low, 6.0f},
            {"jogging, leaning left", &peeking, 4.0f},
            {"standing, leaning right", &lean_still_r, 4.0f},
            {"standing, half lean", &lean_half, 4.0f},
            {"wall climbing", &climb, 0.0f},
            {"jogging, sight down", &carried, 0.0f},
        };
        for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; ++c) {
            character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
            settle_and_aim(&animation, &model, CASES[c].state, CASES[c].label,
                           CASES[c].limit);
        }
    }

    printf("\n7. the muzzle of the rifle the body is holding\n");
    {
        /* Where a tracer starts once the player can see themselves. In first
           person it comes off the viewmodel, which is drawn in camera space and
           is the right answer while the player is looking down it; in third
           person that viewmodel is not drawn at all, and a streak from its
           barrel came out of thin air about half a metre beside the player's
           head and swung around them as they turned. This is the replacement,
           and it is the same transform the body's rifle is DRAWN through. */
        const float MUZZLE_UP = 0.0f, MUZZLE_FORWARD = 0.613f;

        CharacterVisualState state = strafing(6.0f, 1.0f);
        state.x = 3.0f; state.y = 0.5f; state.z = -2.0f;
        character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
        /* An instance by hand rather than through character_instance_init,
           which lives with the drawing and wants a GL context to free. All the
           transform reads off one is the pose, the rig index and the hang
           blend. */
        CharacterInstance instance;
        memset(&instance, 0, sizeof instance);
        instance.ready = 1;
        for (int i = 0; i < 360; ++i)
            character_animation_update(&animation, &model, &state, 1.0f / 120.0f);
        character_animation_pose(&animation, &model, &instance.pose);
        instance.animation = animation;

        float muzzle[3];
        if (!character_weapon_muzzle(&model, &instance, &state, MUZZLE_UP,
                                     MUZZLE_FORWARD, muzzle)) {
            printf("  FAIL no muzzle from a body that has a rig and a pose\n");
            ++failures;
        } else {
            /* Off the body's own feet, which is where the state puts it. */
            printf("       muzzle at feet + (%+.2f, %+.2f, %+.2f)\n",
                   muzzle[0] - state.x, muzzle[1] - state.y, muzzle[2] - state.z);
            check("muzzle height above the feet", muzzle[1] - state.y, 1.55f, 0.60f);
            float flat = sqrtf((muzzle[0] - state.x) * (muzzle[0] - state.x) +
                               (muzzle[2] - state.z) * (muzzle[2] - state.z));
            printf("       %.2fm out from the body's own axis\n", flat);

            /* The check worth making is against the hand, not against the body:
               body proportions are a matter of taste and a grip chain is not.
               The muzzle sits one rifle-length from the fist that holds it,
               where a rifle length is the spec's own barrel scaled by whatever
               stands this rig at gameplay height - the gun is drawn inside that
               scale, so a body two and a half metres tall carries a rifle in
               proportion to itself. The slack is the step into the palm and the
               offset to the pistol grip, which are centimetres. */
            Mat4 body = character_body_transform(&instance, &state, &model);
            Mat4 hand_local;
            if (skinned_model_bone_matrix(&model, &instance.pose, "hand_r",
                                          &hand_local)) {
                Mat4 hand = mat4_multiply(body, hand_local);
                float reach = sqrtf(
                    (muzzle[0] - hand.m[12]) * (muzzle[0] - hand.m[12]) +
                    (muzzle[1] - hand.m[13]) * (muzzle[1] - hand.m[13]) +
                    (muzzle[2] - hand.m[14]) * (muzzle[2] - hand.m[14]));
                check("a barrel's length from the hand holding it", reach,
                      MUZZLE_FORWARD * character_rig_scale(&model), 0.15f);
            } else {
                printf("  FAIL no hand_r to hold a rifle with\n");
                ++failures;
            }

            /* It is a point on a body, so it goes where the body goes. A
               transform that had picked up anything of the camera's would fail
               this the moment the body moved without the camera. */
            CharacterVisualState moved = state;
            moved.x += 5.0f; moved.z -= 3.0f; moved.y += 1.0f;
            float there[3];
            character_weapon_muzzle(&model, &instance, &moved, MUZZLE_UP,
                                    MUZZLE_FORWARD, there);
            check("carried with the body, x", there[0] - muzzle[0], 5.0f, 1e-4f);
            check("carried with the body, y", there[1] - muzzle[1], 1.0f, 1e-4f);
            check("carried with the body, z", there[2] - muzzle[2], -3.0f, 1e-4f);

            /* And it peeks with the body: the gun is in the hands the lean
               takes out past the corner, so its muzzle goes too. */
            CharacterVisualState peek = state;
            peek.lean = 1.0f;
            character_animation_init(&animation, &model,
                             &WEAPON_SPEC_ASSAULT_RIFLE.clips);
            for (int i = 0; i < 360; ++i)
                character_animation_update(&animation, &model, &peek, 1.0f / 120.0f);
            character_animation_pose(&animation, &model, &instance.pose);
            instance.animation = animation;
            float peeked[3];
            character_weapon_muzzle(&model, &instance, &peek, MUZZLE_UP,
                                    MUZZLE_FORWARD, peeked);
            float moved_by = sqrtf((peeked[0] - muzzle[0]) * (peeked[0] - muzzle[0]) +
                                   (peeked[2] - muzzle[2]) * (peeked[2] - muzzle[2]));
            check_min("muzzle moves with a full lean, metres", moved_by, 0.10f);
        }
    }

    check_clip_set(&model, &WEAPON_SPEC_ASSAULT_RIFLE);
    check_clip_set(&model, &WEAPON_SPEC_KATANA);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}

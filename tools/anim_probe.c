/* Headless checks for the death beat and the hit reaction: that a pool
   emptying reaches the rig as a state and an edge, that a corpse holds its last
   frame instead of looping back to its feet, that coming back plays a get-up
   and that a player who wants to move does not have to watch it, and that being
   shot bends a body without taking its legs out from under it.

   `make animprobe` from the project root, on the host compiler rather than the
   cross one - posing a skeleton needs no window and no GL context.

   It exists because every failure here is quiet. A death clip that loops is a
   corpse that stands up and falls over again forever, which reads as a physics
   bug rather than as a missing flag. A LANDED arriving on the respawn frame
   fires a landing over the top of the get-up, and the only symptom is that the
   get-up sometimes does not play. And a reaction that reaches the pelvis stops
   a sprinting player dead every time they are clipped, which is the single
   thing this game's movement is not allowed to do - and which, in the middle of
   a fight, feels exactly like getting shot is supposed to feel. */
#include "gameplay/combatant.h"
#include "gameplay/combatant_visual_state.h"
#include "gameplay/combat_tuning.h"
#include "render/character_animation.h"
#include "render/character_library.h"
#include "render/skinned_model.h"
#include "gameplay/weapon_registry.h"
#include "gameplay/weapon_spec.h"
#include "utils/mat4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
}

static void check(const char* what, float got, float want, float tol) {
    int ok = fabsf(got - want) <= tol;
    if (!ok) ++failures;
    printf("  %s %-46s got %+.4f want %+.4f\n", ok ? "ok  " : "FAIL", what,
           got, want);
}

#define STEP (1.0f / 120.0f)

/* Where a bone sits in the model's own space, for the poses below. */
static int bone_at(const SkinnedModel* model, const Pose* pose,
                   const char* bone, float out[3]) {
    Mat4 m;
    if (!skinned_model_bone_matrix(model, pose, bone, &m)) return 0;
    out[0] = m.m[12]; out[1] = m.m[13]; out[2] = m.m[14];
    return 1;
}

static float distance3(const float a[3], const float b[3]) {
    float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

/* One body on the ground at the origin, out of any set - the shape the player's
   own body has. */
static Combatant lone_body(float max_health) {
    Combatant body;
    memset(&body, 0, sizeof body);
    body.max_health = max_health;
    body.active = 1;
    combatant_respawn(&body, 0.0f, 0.0f, 0.0f, 0.0f);
    return body;
}

/* ---- 1. the pool reaches the rig ---------------------------------------- */

static void check_death_on_the_wire(void) {
    printf("\n1. a pool that empties reaches the rig as a state and an edge\n");

    Combatant body = lone_body(COMBAT_PLAYER_HEALTH);
    CharacterVisualState state;
    combatant_visual_state_fill(&state, NULL, &body);
    expect(!state.dead, "a standing body is not dead");
    expect(!(state.events & CHARACTER_EVENT_DIED),
           "and a body with no previous frame reports no edge - a spawn is not a death");

    combatant_damage(&body, COMBATANT_REGION_CHEST, COMBAT_PLAYER_HEALTH);
    combatant_visual_state_fill(&state, &state, &body);
    expect(state.dead, "an emptied pool publishes dead");
    expect((state.events & CHARACTER_EVENT_DIED) != 0, "and the edge with it");

    /* The bit is an edge and the flag is a state: one crossing, one report, but
       the body stays down. */
    combatant_visual_state_fill(&state, &state, &body);
    expect(state.dead, "the state holds on the next frame");
    expect(!(state.events & CHARACTER_EVENT_DIED),
           "and the edge does not fire twice");

    combatant_respawn(&body, 4.0f, 0.0f, 9.0f, 90.0f);
    combatant_visual_state_fill(&state, &state, &body);
    expect(!state.dead, "standing back up clears the state");
    expect((state.events & CHARACTER_EVENT_RESPAWNED) != 0,
           "and reports the respawn as its own edge");

    /* The whole beat has to fit inside the count, or a corpse is standing up
       before it has finished falling over. */
    printf("   the beat: %.2fs falling + %.2fs getting up in a %.2fs count\n",
           2.40f, 1.53f, COMBAT_RESPAWN_DELAY);
    expect(2.40f + 1.53f < COMBAT_RESPAWN_DELAY,
           "death and get-up both fit inside COMBAT_RESPAWN_DELAY");
}

/* ---- 2. the reaction is carried, and the peek is not overwritten --------- */

static void check_flinch_is_not_the_lean(void) {
    printf("\n2. the reaction has its own channel\n");

    Combatant body = lone_body(COMBAT_DUMMY_HEALTH);
    CombatantSet set;
    memset(&set, 0, sizeof set);
    set.count = 1;
    set.items[0] = body;

    /* A round arriving from the body's front: the normal points back out at
       whoever fired, so it is -Z for a body facing +Z. */
    CombatantHit hit;
    memset(&hit, 0, sizeof hit);
    hit.index = 0;
    hit.region = COMBATANT_REGION_CHEST;
    hit.normal_x = 0.0f; hit.normal_y = 0.0f; hit.normal_z = -1.0f;
    combatant_apply_damage(&set, &hit, 10.0f);

    CharacterVisualState state;
    combatant_visual_state_fill(&state, NULL, &set.items[0]);
    check("the rock is on flinch", state.flinch, COMBAT_FLINCH_AMOUNT, 0.001f);
    check("and points the way the round went, x", state.flinch_x, 0.0f, 0.001f);
    check("                                   z", state.flinch_z, 1.0f, 0.001f);
    check("the peek channel is left alone", state.lean, 0.0f, 0.0001f);
    expect(state.flinch_region == CHARACTER_HIT_CHEST,
           "and the band it landed in comes with it");

    /* Which is the whole point: a player peeking a corner and taking rounds is
       doing both, and before this the second overwrote the first. */
    state.lean = 0.8f;
    expect(state.lean > 0.0f && state.flinch > 0.0f,
           "a peek and a flinch can be true at once");
}

/* ---- 3. the rig ---------------------------------------------------------- */

static void pose_body(CharacterAnimation* animation, const SkinnedModel* model,
                      const CharacterVisualState* state, float seconds,
                      Pose* out) {
    for (float t = 0.0f; t < seconds; t += STEP)
        character_animation_update(animation, model, state, STEP);
    character_animation_pose(animation, model, out);
}

static CharacterVisualState standing_state(void) {
    CharacterVisualState state;
    memset(&state, 0, sizeof state);
    state.grounded = 1;
    /* Heading zero faces +Z and a look yaw of zero looks down -Z, so these two
       are the same facing and leave the rig with no twist to apply. */
    state.heading_degrees = 0.0f;
    state.look_yaw_degrees = -180.0f;
    return state;
}

static void check_death_role(const SkinnedModel* model) {
    printf("\n3. a corpse holds its last frame\n");

    CharacterAnimation animation;
    character_animation_init(&animation, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);

    int death_clip = skinned_model_find_clip(model, "Death01");
    int getup_clip = skinned_model_find_clip(model, "LayToIdle");
    expect(death_clip >= 0, "the rig has Death01");
    expect(getup_clip >= 0, "and LayToIdle");
    if (death_clip < 0 || getup_clip < 0) return;
    float death_length = model->clips[death_clip].duration;
    printf("   Death01 runs %.2fs\n", death_length);

    CharacterVisualState state = standing_state();
    character_animation_update(&animation, model, &state, STEP);

    state.dead = 1;
    state.grounded = 0;
    state.events = CHARACTER_EVENT_DIED;
    character_animation_update(&animation, model, &state, STEP);
    expect(animation.role == CHARACTER_CLIP_DEATH,
           "dying switches the body to the death role at once");
    state.events = 0;

    /* Past the end of the clip, twice over. A role that looped would be back
       near its first frame by now; a clamped one is exactly where it stopped. */
    Pose at_end, well_past;
    pose_body(&animation, model, &state, death_length + 0.05f, &at_end);
    pose_body(&animation, model, &state, death_length, &well_past);

    float a[3], b[3];
    expect(bone_at(model, &at_end, "pelvis", a) &&
           bone_at(model, &well_past, "pelvis", b),
           "the rig has a pelvis to measure");
    check("the corpse has not moved a clip later", distance3(a, b), 0.0f, 0.002f);
    expect(animation.role == CHARACTER_CLIP_DEATH,
           "and it is still the death role, not handed back to idle");

    /* And it is actually lying down rather than standing at attention. */
    float bind_pelvis[3];
    Pose bind;
    skinned_model_bind_pose(model, &bind);
    if (bone_at(model, &bind, "pelvis", bind_pelvis)) {
        printf("   pelvis at %.3f on the bind pose, %.3f on the corpse\n",
               bind_pelvis[1], a[1]);
        expect(a[1] < bind_pelvis[1] * 0.6f,
               "the body is on the floor, not upright");
    }
}

static void check_get_up(const SkinnedModel* model) {
    printf("\n4. coming back plays a get-up, and gives way to a player who moves\n");

    CharacterAnimation animation;
    character_animation_init(&animation, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    CharacterVisualState state = standing_state();
    state.dead = 1;
    state.events = CHARACTER_EVENT_DIED;
    character_animation_update(&animation, model, &state, STEP);
    state.events = 0;
    pose_body(&animation, model, &state, 1.0f, &(Pose){0});

    /* The respawn frame, carrying the LANDED that player_controller_place's
       init-then-pose is about to produce. RESPAWNED has to win. */
    state.dead = 0;
    state.grounded = 1;
    state.events = CHARACTER_EVENT_RESPAWNED | CHARACTER_EVENT_LANDED;
    character_animation_update(&animation, model, &state, STEP);
    state.events = 0;
    expect(animation.role == CHARACTER_CLIP_GET_UP,
           "the get-up wins the frame it shares with a landing");
    expect(animation.hold > 0.0f, "and owns the body for its own length");

    /* Left alone, it runs its course and hands back. */
    for (float t = 0.0f; t < 1.6f; t += STEP)
        character_animation_update(&animation, model, &state, STEP);
    expect(animation.role != CHARACTER_CLIP_GET_UP,
           "left alone it expires back into the locomotion roles");

    /* Asked to move, it is over now. A player who has just come back and is
       already running does not stand there finishing an animation. */
    CharacterAnimation moving;
    character_animation_init(&moving, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    CharacterVisualState down = standing_state();
    down.dead = 1;
    down.events = CHARACTER_EVENT_DIED;
    character_animation_update(&moving, model, &down, STEP);
    down.events = 0;
    for (float t = 0.0f; t < 1.0f; t += STEP)
        character_animation_update(&moving, model, &down, STEP);

    down.dead = 0;
    down.grounded = 1;
    down.events = CHARACTER_EVENT_RESPAWNED;
    character_animation_update(&moving, model, &down, STEP);
    down.events = 0;
    down.has_movement_input = 1;
    down.moving = 1;
    down.velocity_z = 6.0f;
    character_animation_update(&moving, model, &down, STEP);
    check("a player asking to move cuts the hold", moving.hold, 0.0f, 0.0001f);
    character_animation_update(&moving, model, &down, STEP);
    expect(moving.role != CHARACTER_CLIP_GET_UP,
           "and is running on the very next step");
}

static void check_reaction(const SkinnedModel* model) {
    printf("\n5. being shot bends the body and leaves the legs alone\n");

    CharacterVisualState calm = standing_state();
    CharacterVisualState hit = standing_state();
    hit.flinch = COMBAT_FLINCH_AMOUNT;
    hit.flinch_region = CHARACTER_HIT_CHEST;
    /* Straight into the chest of a body facing +Z: the round travels +Z. */
    hit.flinch_x = 0.0f;
    hit.flinch_z = 1.0f;

    CharacterAnimation a, b;
    character_animation_init(&a, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    character_animation_init(&b, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    Pose calm_pose, hit_pose;
    pose_body(&a, model, &calm, 0.5f, &calm_pose);
    pose_body(&b, model, &hit, 0.5f, &hit_pose);

    /* The legs. This is the check the whole layer is shaped around: a reaction
       that reaches the pelvis stops a sprinting player, and stopping a player
       who is moving well is the one thing this game does not do. */
    static const char* LEGS[] = {"pelvis", "thigh_l", "thigh_r",
                                 "calf_l", "calf_r", "foot_l", "foot_r"};
    int leg_count = (int)(sizeof LEGS / sizeof LEGS[0]);
    float worst_leg = 0.0f;
    const char* worst_leg_bone = LEGS[0];
    int measured = 0;
    for (int i = 0; i < leg_count; ++i) {
        float p[3], q[3];
        if (!bone_at(model, &calm_pose, LEGS[i], p)) continue;
        if (!bone_at(model, &hit_pose, LEGS[i], q)) continue;
        ++measured;
        float moved = distance3(p, q);
        if (moved > worst_leg) { worst_leg = moved; worst_leg_bone = LEGS[i]; }
    }
    /* Counted, because the whole check passes trivially if the rig has none of
       these bones under these names and nothing was ever compared. A silent
       zero out of zero measurements is the most expensive kind of green. */
    printf("   %d of %d leg bones measured, worst moved %.5f (%s)\n",
           measured, leg_count, worst_leg, worst_leg_bone);
    expect(measured == leg_count, "every leg bone was actually found and measured");
    expect(worst_leg < 0.001f, "and no bone from the pelvis down moves at all");

    /* The torso. It has to actually go somewhere, or the layer is a no-op that
       passes the check above by doing nothing. */
    float calm_head[3], hit_head[3], calm_chest[3], hit_chest[3];
    if (bone_at(model, &calm_pose, "Head", calm_head) &&
        bone_at(model, &hit_pose, "Head", hit_head) &&
        bone_at(model, &calm_pose, "spine_03", calm_chest) &&
        bone_at(model, &hit_pose, "spine_03", hit_chest)) {
        float head_moved = distance3(calm_head, hit_head);
        float chest_moved = distance3(calm_chest, hit_chest);
        printf("   chest moved %.4f, head moved %.4f\n", chest_moved, head_moved);
        expect(chest_moved > 0.005f, "the chest is rocked");
        expect(head_moved > chest_moved,
               "and the head further than the chest, being further up the bend");
        /* Driven the way the round went: a round travelling +Z pushes the body
           along +Z, which is the direction the rig faces. */
        expect(hit_head[2] > calm_head[2],
               "the body goes the way the round was travelling");
    }

    /* Mirrored. A round from the left and a round from the right have to
       produce the same bend the other way, or the direction is being read
       through something that is not symmetric. */
    CharacterVisualState from_left = standing_state();
    from_left.flinch = COMBAT_FLINCH_AMOUNT;
    from_left.flinch_region = CHARACTER_HIT_CHEST;
    from_left.flinch_x = -1.0f;   /* travelling toward the body's right */
    CharacterVisualState from_right = from_left;
    from_right.flinch_x = 1.0f;

    CharacterAnimation l, r;
    character_animation_init(&l, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    character_animation_init(&r, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    Pose left_pose, right_pose;
    pose_body(&l, model, &from_left, 0.5f, &left_pose);
    pose_body(&r, model, &from_right, 0.5f, &right_pose);

    float lh[3], rh[3], ch[3];
    if (bone_at(model, &left_pose, "Head", lh) &&
        bone_at(model, &right_pose, "Head", rh) &&
        bone_at(model, &calm_pose, "Head", ch)) {
        float went_left = lh[0] - ch[0];
        float went_right = rh[0] - ch[0];
        printf("   head x: %+.4f calm, %+.4f from the left, %+.4f from the right\n",
               ch[0], lh[0], rh[0]);
        expect(went_left * went_right < 0.0f,
               "the two go opposite ways about the calm pose");
        /* Magnitudes rather than a sum, and a percentage rather than a
           distance. The calm pose is NOT symmetric - the body is holding a
           rifle, and its head sits a couple of centimetres off centre before
           anything hits it - so rotating it one way and the other cannot give
           exactly equal and opposite displacements, and a check that demanded
           they did would be measuring the rifle rather than the reaction. */
        float bigger = fmaxf(fabsf(went_left), fabsf(went_right));
        float smaller = fminf(fabsf(went_left), fabsf(went_right));
        printf("   and by %.4f against %.4f, within %.1f%%\n",
               bigger, smaller, 100.0f * (bigger - smaller) / bigger);
        expect(bigger > 0.005f, "and neither of them is nothing");
        expect(bigger - smaller < bigger * 0.05f,
               "by the same amount to within a twentieth");
    }

    /* A headshot is the same layer with a different share: more head, less
       body, which is what the two measured columns are for. */
    CharacterVisualState head_hit = hit;
    head_hit.flinch_region = CHARACTER_HIT_HEAD;
    CharacterAnimation h;
    character_animation_init(&h, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    Pose head_pose;
    pose_body(&h, model, &head_hit, 0.5f, &head_pose);
    float head_chest[3];
    if (bone_at(model, &head_pose, "spine_03", head_chest) &&
        bone_at(model, &calm_pose, "spine_03", calm_chest)) {
        float body_rock = distance3(calm_chest, hit_chest);
        float head_rock = distance3(calm_chest, head_chest);
        printf("   chest moves %.4f on a chest hit, %.4f on a head hit\n",
               body_rock, head_rock);
        expect(head_rock < body_rock,
               "a headshot rocks the chest less and snaps the head instead");
    }
}

static void check_reaction_does_not_replace(const SkinnedModel* model) {
    printf("\n6. the reaction is additive, not a mask\n");

    /* With no round in flight the layer must cost exactly nothing: a body at
       flinch zero has to pose identically to one with the layer never run. */
    CharacterVisualState aiming = standing_state();
    aiming.ads = 1.0f;
    aiming.look_pitch_degrees = 20.0f;

    CharacterAnimation a;
    character_animation_init(&a, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    Pose quiet;
    pose_body(&a, model, &aiming, 0.5f, &quiet);

    CharacterVisualState clipped = aiming;
    clipped.flinch = COMBAT_FLINCH_AMOUNT;
    clipped.flinch_region = CHARACTER_HIT_CHEST;
    clipped.flinch_x = 1.0f;
    CharacterAnimation b;
    character_animation_init(&b, model, &WEAPON_SPEC_ASSAULT_RIFLE.clips);
    Pose rocked;
    pose_body(&b, model, &clipped, 0.5f, &rocked);

    /* The hand still holds the gun where the aim layer put it RELATIVE to the
       chest that carries it: the reaction moves the chest and the arm rides
       along, which is a disturbed aim rather than a broken hold. */
    float quiet_hand[3], quiet_chest[3], rocked_hand[3], rocked_chest[3];
    if (bone_at(model, &quiet, "hand_r", quiet_hand) &&
        bone_at(model, &quiet, "spine_03", quiet_chest) &&
        bone_at(model, &rocked, "hand_r", rocked_hand) &&
        bone_at(model, &rocked, "spine_03", rocked_chest)) {
        /* How far the hand is FROM the chest, and not where that offset points.
           The reaction turns spine_03 itself, so the arm hanging off it turns
           with it and the offset's direction in the model's space is supposed
           to change - that IS the arm riding the chest. What may not change is
           the distance, because that is the hold: an arm that reached further
           from its own shoulder because a round landed would be an arm the
           layer had pulled out of its socket. */
        float quiet_reach = distance3(quiet_hand, quiet_chest);
        float rocked_reach = distance3(rocked_hand, rocked_chest);
        printf("   the gun hand sits %.4f from the chest calm, %.4f rocked\n",
               quiet_reach, rocked_reach);
        check("the hold on the weapon survives the bend", rocked_reach,
              quiet_reach, 0.001f);
        expect(distance3(quiet_hand, rocked_hand) > 0.005f,
               "while the gun itself has genuinely been moved off the aim");
    }
}

int main(void) {
    SkinnedModel model;
    char error[256] = {0};
    if (!skinned_model_load_glb(&model, CHARACTER_RIG_PLAYER_MODEL,
                                error, (int)sizeof error)) {
        printf("could not load the rig: %s\n", error);
        return 1;
    }
    if (!skinned_model_add_clips(&model, CHARACTER_RIG_PLAYER_CLIPS,
                                 error, (int)sizeof error))
        printf("note: extra clips not loaded (%s)\n", error);

    check_death_on_the_wire();
    check_flinch_is_not_the_lean();
    check_death_role(&model);
    check_get_up(&model);
    check_reaction(&model);
    check_reaction_does_not_replace(&model);

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    if (failures) printf("%d failing check(s)\n", failures);
    /* No skinned_model_free: it lives with the GL code, for the texture it has
       to release, and a probe that exits releases everything anyway. */
    return failures ? 1 : 0;
}

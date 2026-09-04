/* What every weapon has, whichever kind it is: the state, the timers, the
   viewmodel's resting pose, and the one dispatch that sends a frame to the gun
   code or the sword code.

   This file used to be all three. Twelve hundred lines that alternated a
   magazine drop, a carry blend, a tracer and a sheathe, so a reader of either
   half read past the other - while the DATA had already split, since a weapon
   carries a FirearmSpec or a MeleeSpec and never both. The behaviour follows
   that line now: see gameplay/weapon_internal.h for the seam, weapon_firearm.c
   for the gun and weapon_melee.c for the sword.

   What stayed is what is genuinely common. A weapon of either kind has an
   aim, a stow, a sprint carry and a set of timers, and weapon_update owns the
   order those happen in before either half is reached. */

#include "gameplay/weapon.h"
#include "gameplay/weapon_internal.h"
#include "gameplay/weapon_spec.h"
#include "gameplay/weapon_tuning.h"
#include "gameplay/action_track.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/traversal.h"
#include "gameplay/arena.h"
#include "gameplay/combatant.h"
/* The neck the viewmodel is worn on. A render header in a gameplay file, and
   deliberately: the module holds no GL for exactly this reason - the tracer's
   origin is one of its three consumers and is computed here. See
   render/viewmodel_neck.h. */
#include "render/viewmodel_neck.h"
#include "utils/math_utils.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

float weapon_sprint_blend(const Weapon* weapon) {
    /* Multiplied out rather than branched. The gates below drive the sprint
       number's TARGET to zero the moment the player aims, but the two ease at
       different rates, so for a few frames both are non-zero and the pose would
       be a blend of two carries that move the gun along the same axes. This is
       what makes the precedence hold at every instant rather than only at rest,
       and it is one expression so nothing can apply half of it. */
    return weapon->sprint * (1.0f - weapon->ads) * (1.0f - weapon->stow);
}

float weapon_reload_blend(const Weapon* weapon) {
    /* The same shape as weapon_sprint_blend, and multiplied out for the same
       reason: the swell and the sight are eased on different clocks, so the
       precedence has to hold at every instant and not only at rest. */
    return reload_sequence_gun_blend(weapon->reload) *
           (1.0f - weapon->ads) * (1.0f - weapon->stow);
}

void weapon_viewmodel_pose(const Weapon* weapon, const PlayerController* player,
                           ViewmodelPose* out) {
    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    const WeaponSpec* spec = weapon->spec;
    float sprint = weapon_sprint_blend(weapon);
    float reload = weapon_reload_blend(weapon);
    /* A reload takes the carry off the gun: the hands are busy with a magazine,
       which is not something done while the weapon is held in against the chest
       at a run. Collapsed here rather than by refusing to sprint, because the
       player keeps running either way - it is the gun that stops being carried,
       not the player that stops moving. */
    sprint *= 1.0f - reload;

    /* Where the gun rests for this blend of the three carries, rotation and
       all. Asked of the spec rather than composed here, because the arms
       calibrate their shoulders against the same function - a second copy of
       this and the hands would reach for a gun that is not the one drawn. */
    /* A melee weapon's base is where the animated hand put it; a gun's is
       where the spec says it rests. One branch, and only over the base: the
       bob, the sway, the wall-run bank, the kick and the stow drop below are
       the same lines for both, because a sword bobs when the player runs for
       exactly the reason a rifle does. A melee weapon whose rig has not been
       sampled yet falls through to the rest pose, which puts it somewhere
       plausible for the frame or two before the arms are ready. */
    float action_seconds = 0.0f;
    const ActionTrack* action = weapon_melee_action_track(weapon, &action_seconds);
    if (action && action->count > 0) {
        /* A draw or a sheathe overrides the clip: the sword is travelling a
           path this spec states, and the hold pose is where that path ends
           rather than something to blend with along the way. */
        ActionSample key;
        action_track_sample(action, weapon->melee_action_progress, &key);
        memset(out, 0, sizeof *out);
        out->x = lerpf(key.from->hand[0], key.to->hand[0], key.t);
        out->y = lerpf(key.from->hand[1], key.to->hand[1], key.t);
        out->z = lerpf(key.from->hand[2], key.to->hand[2], key.t);
    } else if (weapon_spec_melee(spec) && weapon->melee_base_valid)
        /* Where the animated hand put it - the hold, a swing, a draw and a
           sheathe alike, all one clip sampled through the eye. The sword is in
           the hand the third-person body has it in, at the frame that body is
           drawing, because it IS that frame: there is no second authoring of
           the hold to drift from it. */
        *out = weapon->melee_base;
    else
        weapon_spec_rest_pose(spec, sprint, reload, weapon->ads, out);

    /* The knock off a magazine being seated and then tapped home. On the pose
       rather than layered on by the renderer, so the muzzle, the optic's glass
       and the hands all take it - a jolt that moved the mesh alone would put
       the tracer's origin off the barrel for as long as it lasted, and the
       hands would hold a gun that had gone somewhere without them.

       Weighted by the reload blend, so a player who raises the sight through a
       magazine change does not get punched by a gun they have already brought
       back up. */
    if (reload > 0.0f) {
        float jolt = reload_sequence_seat_jolt(
            weapon_spec_reload_sequence(spec), weapon->reload) * reload;
        out->y += WEAPON_VIEW_SEAT_JOLT_UP * jolt;
        out->z += WEAPON_VIEW_SEAT_JOLT_BACK * jolt;
        out->pitch += WEAPON_VIEW_SEAT_JOLT_PITCH * jolt;
    }

    /* The recoil the gun is drawn through. Set here, on the pose, rather than
       reached for separately by the renderer: it sits inside the gun's
       transform, so everything that takes a point off the drawn gun - the
       tracer's origin, the optic's glass, a dropped magazine - needs it, and
       the pose is the one place they all read. */
    out->kick_pitch = weapon->gun_pitch_degrees;
    out->kick_yaw = weapon->gun_yaw_degrees;

    /* Bob and sway are hip-weapon_firearm_fire tells. Through the scope they would read as the
       view shaking rather than the gun moving, so they fade out with it. */
    float loose = 1.0f - weapon->ads;
    /* Bob is a footfall, so it wants a foot leaving the ground. A slide is fast
       and grounded and has neither - the player is carried along on their side,
       not taking strides - so a slide got a full-amplitude run bob and read as
       sprinting in a crouch. The gun goes quiet instead, which is also the more
       useful tell: it says the player has stopped walking and started
       travelling. */
    int striding = player->ground.is_grounded && !player->slide.active;
    float stride = fminf(speed / WEAPON_VIEW_BOB_SPEED, 1.0f) *
                   (striding ? 1.0f : 0.0f);

    /* The footfall bob hands over to the sprint gait as the carry comes in, and
       both run off walk_time so they stay in phase through the crossfade - a
       bob that reset its clock would show a step in the middle of the handover. */
    float bob = sinf(player->walk_time) * WEAPON_VIEW_BOB * stride *
                (1.0f - sprint);

    /* The gait. Two rates, because a run has two: the body swings and rolls
       from side to side once per STRIDE, and rises and falls once per FOOTFALL,
       which is twice as often. walk_time counts footfalls, so the stride is
       half of it.

       Run at one rate this was a gun sliding side to side on a rail. The second
       rate is what makes it a gait: the lateral and the vertical together trace
       a figure of eight, which is the path a carried weapon actually takes.

       The vertical is -cos rather than sin so the gun is at the BOTTOM of its
       travel as each foot lands, and rides up between. A weapon that floated
       upward onto the footfall reads as the player bouncing off the ground
       rather than driving into it. */
    float carry = stride * sprint;
    float stride_phase = player->walk_time * 0.5f;
    float swing = sinf(stride_phase);
    float land = -cosf(player->walk_time);
    float carry_sway = swing * WEAPON_VIEW_SPRINT_SWAY * carry;
    float carry_heave = land * WEAPON_VIEW_SPRINT_HEAVE * carry;

    /* The wall run's bank, which is a different thing entirely: it says which
       side the wall is on, and it survives the sprint carry because a gun held
       in against the chest is still held against a banked body. */
    float wall_sway = (float)player->wall_run.side * WEAPON_VIEW_SWAY *
                      (player->wall_run.active ? 1.0f : 0.0f);

    out->x += (wall_sway + carry_sway) * loose;
    out->y += bob * loose + carry_heave * loose;
    out->z += weapon->view_kick * WEAPON_VIEW_KICK_BACK;

    /* And the two angles, which are what stop the whole thing reading as the
       gun being slid around in front of a still camera.

       The rock leads the swing by a quarter cycle - cos against the sway's sin -
       because a shoulder is already rolling INTO the next step while the gun is
       still at the far side of the last one. In phase they cancel each other
       out and the gun looks stiff; a quarter apart the muzzle traces a loop.

       The nod is on the footfall, with the heave, so the gun tips as the weight
       lands rather than on its own schedule. */
    out->roll += cosf(stride_phase) * WEAPON_VIEW_SPRINT_ROCK * carry * loose;
    out->pitch += land * WEAPON_VIEW_SPRINT_NOD * carry * loose;
    /* Applied last and to the finished pose, so it drops the gun out of frame
       from wherever it happened to be rather than fighting the bob, the carry
       or the scope for the same axis. */
    out->y -= WEAPON_VIEW_STOW_DROP * weapon->stow;
}

void weapon_set_world_muzzle(Weapon* weapon, const float muzzle[3], int valid) {
    if (!weapon) return;
    weapon->world_muzzle_valid = valid && muzzle != 0;
    if (!weapon->world_muzzle_valid) return;
    for (int c = 0; c < 3; ++c) weapon->world_muzzle[c] = muzzle[c];
}

void weapon_init(Weapon* weapon, const WeaponSpec* spec) {
    weapon->spec = spec;
    /* A gun with nowhere to send its damage is a valid gun, and this is what
       makes it the DEFAULT one: nothing has to remember to clear this, and a
       caller that never publishes a set gets geometry-only traces rather than a
       stale pointer into somebody else's world. */
    weapon->targets = NULL;
    /* A weapon with no gun half starts with no rounds and a cone that is
       already shut, which is the resting state it never leaves. */
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    weapon->ammo_in_magazine = gun ? gun->magazine : 0;
    weapon->is_reloading = 0;
    weapon->reload_time_left = 0.0f;
    weapon->fire_cooldown = 0.0f;
    weapon->quiet_time = gun ? gun->bloom_recovery_delay : 0.0f;
    weapon->bloom_degrees = gun ? gun->bloom_min : 0.0f;
    weapon->muzzle_flash = 0.0f;
    weapon->gun_pitch_degrees = 0.0f;
    weapon->gun_yaw_degrees = 0.0f;
    weapon->world_muzzle_valid = 0;
    weapon->view_kick = 0.0f;
    weapon->ads = 0.0f;
    weapon->stow = 0.0f;
    weapon->sprint = 0.0f;
    weapon->reload = 0.0f;
    weapon->trigger_latched = 1;
    /* Idle, and worn rather than held. A melee weapon comes out of its
       scabbard through the draw track rather than starting in the hand, and a
       firearm keeps these at rest for its whole life. */
    melee_swing_end(&weapon->swing);
    weapon->blade_valid = 0;
    weapon->melee_quiet_time = 0.0f;
    weapon->melee_attack_buffered = 0;
    weapon->melee_heavy_buffered = 0;
    weapon->melee_recovering = 0;
    weapon->melee_action = WEAPON_MELEE_ACTION_NONE;
    weapon->melee_action_progress = 0.0f;
    weapon->melee_attack = -1;
    weapon->melee_time = 0.0f;
    weapon->melee_duration = 0.0f;
    weapon->sheathed = weapon_spec_melee(spec) != NULL;
    /* Wholly in the guard. A sword is not carried at all until it is drawn, so
       nothing reads these before the draw lands - but they have to describe a
       hold from the first frame either way, because the one thing they must
       never do is sum to something other than one. */
    for (int c = 0; c < MELEE_CARRY_COUNT; ++c) weapon->melee_carry[c] = 0.0f;
    weapon->melee_carry[MELEE_CARRY_GUARD] = 1.0f;
    weapon->melee_base_valid = 0;
    weapon->random_state = 0x9e3779b9u;
    for (int i = 0; i < WEAPON_TRACER_COUNT; ++i) weapon->tracers[i].life = 0.0f;
    for (int i = 0; i < WEAPON_IMPACT_COUNT; ++i) {
        weapon->impacts[i].life = 0.0f;
        weapon->impacts[i].on_body = 0;
    }
    for (int i = 0; i < WEAPON_DROPPED_MAG_COUNT; ++i)
        weapon->dropped_mags[i].life = 0.0f;
}

void weapon_set_targets(Weapon* weapon, CombatantSet* targets) {
    if (!weapon) return;
    weapon->targets = targets;
}

void weapon_latch_trigger(Weapon* weapon) {
    weapon->trigger_latched = 1;
}

static void advance_timers(Weapon* weapon, float delta_time) {
    weapon_melee_advance_action(weapon, delta_time);
    /* A weapon with no gun half has no cone to recover, so the whole bloom
       half of this is skipped rather than run against zeroes. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    /* The kick springs out of the gun's own aim rather than out of the
       camera's. Decayed here, once, and read by both the shot and the
       reticle - they are the same two numbers, so they cannot disagree. */
    float recoil_decay = expf(-delta_time * WEAPON_RECOIL_DECAY);
    weapon->gun_pitch_degrees *= recoil_decay;
    weapon->gun_yaw_degrees *= recoil_decay;
    weapon->fire_cooldown = fmaxf(0.0f, weapon->fire_cooldown - delta_time);
    /* Only ever compared against the recovery delay, and left to run free it
       would eventually grow large enough that adding a frame to it does
       nothing. Capping it keeps the comparison meaningful forever. */
    weapon->quiet_time = fminf(weapon->quiet_time + delta_time,
                               (gun ? gun->bloom_recovery_delay : 0.0f) + 1.0f);
    weapon->muzzle_flash = fmaxf(0.0f, weapon->muzzle_flash - delta_time);
    weapon->view_kick *= expf(-delta_time * WEAPON_VIEW_KICK_DECAY);

    if (gun && weapon->quiet_time >= gun->bloom_recovery_delay)
        weapon->bloom_degrees = approachf(weapon->bloom_degrees,
                                          gun->bloom_min,
                                          gun->bloom_recovery * delta_time);

    for (int i = 0; i < WEAPON_TRACER_COUNT; ++i)
        weapon->tracers[i].life = fmaxf(0.0f,
                                        weapon->tracers[i].life - delta_time);
    for (int i = 0; i < WEAPON_IMPACT_COUNT; ++i)
        weapon->impacts[i].life = fmaxf(0.0f,
                                        weapon->impacts[i].life - delta_time);
    weapon_firearm_advance_dropped_mags(weapon, delta_time);
}

void weapon_update(Weapon* weapon, const WeaponCommand* command,
                   const PlayerController* player,
                   float aim_yaw_degrees, float aim_pitch_degrees,
                   float camera_roll_degrees,
                   float eye_height, float view_fov_degrees,
                   float delta_time) {
    advance_timers(weapon, delta_time);
    /* Before any of the paths below, all of which can return early: a stance
       that stopped blending because the player happened to be hanging off a
       ledge would resume from wherever it was abandoned. */
    weapon_melee_carry_update(weapon, player, delta_time);

    /* Aiming is never blocked and never blocks anything: it runs ahead of the
       reload and weapon_firearm_fire paths, so the player can raise or drop the scope through
       a reload, a wall run or a slide without the weapon noticing. */
    /* Hanging off a ledge occupies both hands, so it lowers the gun and refuses
       the scope for as long as it lasts - a sight picture from a player who is
       holding on with both arms would undo the read the stow is there to give. */
    int hands_full = player->mantle.ledge_hanging;
    weapon->ads = approachf(weapon->ads,
                            (command->aim_held && !hands_full) ? 1.0f : 0.0f,
                            delta_time / weapon_spec_ads_time(weapon->spec));
    weapon->stow = approachf(weapon->stow, hands_full ? 1.0f : 0.0f,
                             delta_time / WEAPON_VIEW_STOW_TIME);

    /* The reload's clock. Driven straight off the timer rather than approached,
       because a sequence has a beginning and an end and easing into one would
       make the magazine leave the gun at a different point every time. It runs
       to 1 and is left there for the frame the reload finishes on, so the track
       ends at the pose it started from and there is nothing to snap back. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    float was_reloading = weapon->reload;
    weapon->reload = weapon->is_reloading && gun && gun->reload_time > 1e-4f
        ? 1.0f - weapon->reload_time_left / gun->reload_time
        : 0.0f;
    if (weapon->reload < 0.0f) weapon->reload = 0.0f;
    if (weapon->reload > 1.0f) weapon->reload = 1.0f;

    /* The one frame the hand lets go. Detected as a CROSSING rather than as a
       range, so exactly one magazine is dropped however long the frame was and
       however slow the reload is - a test of the form "past the drop point"
       would spawn one every frame from here to the end of the animation. */
    {
        const ActionTrack* track = weapon_spec_reload_sequence(weapon->spec);
        if (weapon->reload >= track->drop_at && was_reloading < track->drop_at &&
            gun && gun->magazine_part)
            weapon_firearm_drop_magazine(weapon, player, aim_yaw_degrees, aim_pitch_degrees,
                          camera_roll_degrees, eye_height);
    }

    /* The sprint carry. `is_sprinting` alone is not the question, and the two
       gates on top of it are the same two the lean already refuses for:

       Not on a wall. is_sprinting is not gated on being grounded, so it stays
       true through a wall run and a wall climb, and a gun pulled in against the
       chest there would fight the bank that tells the player they are on a wall
       - which is the one read they cannot afford to lose while running along
       one.

       Not in a slide. The same argument that just took the footfall bob off a
       slide: a slide is not a stride, it is fast and grounded and has its own
       read, and a carry pose would say the player is running when they are
       being carried.

       Aiming collapses it rather than blending with it. The number is driven to
       zero the moment the scope is asked for, and weapon_sprint_blend holds the
       precedence through the frames where both are still easing.

       Not while shooting either, which is the same argument as aiming and was
       simply missing. The carry pulls the weapon in against the chest and rolls
       it a quarter turn over; a player firing from there is watching rounds
       leave a gun that is plainly not pointed at what they are hitting. Using
       the weapon is a statement that it is in use, and the carry is what a gun
       does when it is not. The cooldown is in the test as well as the trigger
       so a tap counts: it stays set for one shot's interval afterwards, which
       is what stops a single round from being fired out of a gun that is on its
       way down and back up again. */
    int on_wall = player->wall_run.active || player->wall_climb.active;
    /* The cooldown as well as the trigger, so a tap counts: it stays set for one
       shot's interval afterwards, which is what puts the gun up for the round
       rather than letting the carry start back the same frame. */
    int firing = command->fire_held || weapon->fire_cooldown > 0.0f;
    int carrying = player->is_sprinting && !on_wall && !player->slide.active &&
                   !hands_full && !command->aim_held && !firing;
    float carry_time = firing && !carrying
        ? WEAPON_VIEW_SPRINT_RAISE_TIME : WEAPON_VIEW_SPRINT_TIME;
    weapon->sprint = approachf(weapon->sprint, carrying ? 1.0f : 0.0f,
                               delta_time / carry_time);

    if (!command->fire_held) weapon->trigger_latched = 0;

    if (weapon->is_reloading) {
        weapon->reload_time_left -= delta_time;
        if (weapon->reload_time_left <= 0.0f) {
            weapon->is_reloading = 0;
            weapon->reload_time_left = 0.0f;
            weapon->ammo_in_magazine = gun ? gun->magazine : 0;
        }
        return;
    }

    /* A melee weapon takes an entirely different set of inputs from here on:
       no trigger, no cone, no magazine. The sheathe is the one thing the two
       halves share a button for. */
    if (weapon_spec_melee(weapon->spec)) {
        if (command->reload_pressed) { weapon_toggle_sheathe(weapon); return; }
        weapon_melee_update(weapon, command, delta_time);
        return;
    }

    /* Only a reload that started costs the frame; pressing R on a full
       magazine must not interrupt weapon_firearm_fire. */
    if (command->reload_pressed && weapon_firearm_begin_reload(weapon))
        return;

    int wants_to_fire = command->fire_held && !weapon->trigger_latched;
    if (!wants_to_fire) return;

    /* Pulling the trigger on an empty magazine is a request to reload. Making
       the player press a second key to do the only thing left to do costs time
       in a fight and teaches nothing. */
    if (weapon->ammo_in_magazine <= 0) {
        weapon_firearm_begin_reload(weapon);
        return;
    }
    if (weapon->fire_cooldown > 0.0f) return;

    /* The round leaves along where the GUN is pointing, which is the player's
       look plus whatever kick the gun is still carrying. That is the same pair
       of numbers the reticle is drawn along, so a burst that walks the muzzle
       up walks the reticle up with it and the sight never lies about where the
       next round goes. */
    float shot_yaw, shot_pitch;
    weapon_aim_angles(weapon, aim_yaw_degrees, aim_pitch_degrees,
                      &shot_yaw, &shot_pitch);
    weapon_firearm_fire(weapon, player, shot_yaw, shot_pitch, camera_roll_degrees, eye_height,
         view_fov_degrees);
}

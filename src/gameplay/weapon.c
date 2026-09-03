#include "gameplay/weapon.h"
#include "gameplay/weapon_spec.h"
#include "gameplay/weapon_tuning.h"
#include "gameplay/action_track.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/traversal.h"
#include "gameplay/arena.h"
#include "gameplay/combatant.h"
#include "utils/math_utils.h"

#include <string.h>
#include <math.h>
#include <stddef.h>

/* Held fire, flat damage at any range, and a cone that opens only with
   sustained fire. Every number comes off the spec the weapon was initialised
   with; what is here is the order things happen in, which is the same for
   every gun. */

/* xorshift, so the spread pattern is the weapon's own and does not move when
   something else in the process calls rand(). */
static float random_unit(Weapon* weapon) {
    unsigned int x = weapon->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    weapon->random_state = x;
    return (float)(x >> 8) / 16777216.0f;
}

/* Defined below, beside weapon_toggle_sheathe which is what starts one. */
static const ActionTrack* melee_action_track(const Weapon* weapon,
                                             float* seconds);

float weapon_cone_degrees(const Weapon* weapon) {
    /* A weapon with no gun half has no cone at all: its bloom never leaves
       zero, so the scale it would be narrowed by does not matter. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    return weapon->bloom_degrees *
           lerpf(1.0f, gun ? gun->ads_bloom_scale : 1.0f, weapon->ads);
}

float weapon_sight_window_fraction(const Weapon* weapon) {
    const WeaponSpec* spec = weapon->spec;
    /* No gun half, no glass: there is no window to take a share of the
       screen, which is what zero says to every caller of this. */
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    if (!gun || !gun->sight) return 0.0f;
    /* Distance from the eye to the optic: the viewmodel's origin, plus however
       far forward of that the glass sits. */
    float origin = spec->view_forward + gun->view_ads_forward * weapon->ads;
    float distance = fmaxf(0.05f, origin + weapon_spec_sight_forward(spec));
    /* The gun is drawn through its own projection, not the world's, so the
       bore's share of the screen is measured against that one. */
    float view_fov = lerpf(spec->view_fov, weapon_spec_view_ads_fov(spec), weapon->ads);
    /* The glass belongs to the optic, so its width comes off the sight and not
       out of the shared tuning header - which is what let a prism sight's
       radius decide the size of a holographic sight's window. */
    return (gun->sight->window_radius / distance) /
           tanf(view_fov * 0.5f * MATH_DEG_TO_RAD);
}

int weapon_sight_is_magnified(const Weapon* weapon) {
    /* Asked of the magnification rather than of a named sight, so a new optic
       needs no case here to be understood. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    return gun && gun->sight && gun->sight->magnification > 1.0f;
}

void weapon_aim_angles(const Weapon* weapon, float look_yaw_degrees,
                       float look_pitch_degrees,
                       float* yaw_degrees, float* pitch_degrees) {
    *yaw_degrees = look_yaw_degrees + weapon->gun_yaw_degrees;
    *pitch_degrees = look_pitch_degrees + weapon->gun_pitch_degrees;
}

/* Offsets the aim by a uniform sample of the bloom disc. Sampling the radius as
   sqrt(u) rather than u is what keeps the density even instead of piling shots
   into the middle of the cone. */
static void apply_bloom(Weapon* weapon, float* dx, float* dy, float* dz) {
    float radius = tanf(weapon_cone_degrees(weapon) * MATH_DEG_TO_RAD) *
                   sqrtf(random_unit(weapon));
    float angle = random_unit(weapon) * 2.0f * (float)MATH_PI;

    /* Any axis not parallel to the aim will do as a seed for the basis; up
       fails only when the player is looking near-vertically. */
    float seed_x = 0.0f, seed_y = 1.0f, seed_z = 0.0f;
    if (fabsf(*dy) > 0.99f) { seed_x = 1.0f; seed_y = 0.0f; }

    float right_x = seed_y * *dz - seed_z * *dy;
    float right_y = seed_z * *dx - seed_x * *dz;
    float right_z = seed_x * *dy - seed_y * *dx;
    float length = sqrtf(right_x * right_x + right_y * right_y +
                         right_z * right_z);
    if (length < 0.00001f) return;
    right_x /= length; right_y /= length; right_z /= length;

    float up_x = *dy * right_z - *dz * right_y;
    float up_y = *dz * right_x - *dx * right_z;
    float up_z = *dx * right_y - *dy * right_x;

    float across = cosf(angle) * radius;
    float along = sinf(angle) * radius;
    float out_x = *dx + right_x * across + up_x * along;
    float out_y = *dy + right_y * across + up_y * along;
    float out_z = *dz + right_z * across + up_z * along;
    float out_length = sqrtf(out_x * out_x + out_y * out_y + out_z * out_z);
    *dx = out_x / out_length;
    *dy = out_y / out_length;
    *dz = out_z / out_length;
}

/* What one round found. `hit` is where the flight path ends whatever it ended
   against, so a tracer always has a terminus; `body` names the combatant when
   the thing it ended against was one, and `body.index` is -1 when it was not.

   Keeping the body's identity beside the geometric hit rather than folding it
   into TraversalHit is deliberate: a TraversalHit is a point and a face, it is
   what the grapple and the mantle also ask for, and neither of those has any
   business knowing that bodies exist. */
typedef struct {
    TraversalHit hit;
    int hit_anything;
    CombatantHit body;
} WeaponShot;

/* Nearest of the arena's boxes, the ground plane, and the bodies standing on
   them. The ground is not a wall, so the raycast does not know about it and it
   is resolved here; the bodies are not geometry, so they are resolved here too.

   Bodies go LAST, against the distance the geometry has already shortened the
   ray to, which is what makes a target behind a wall a target behind a wall.
   Reversing the two would let a round pass through a tower to reach a dummy on
   the far side of it. */
static void trace_shot(const CombatantSet* targets,
                        float ox, float oy, float oz,
                        float dx, float dy, float dz, float max_range,
                        WeaponShot* shot) {
    TraversalHit* hit = &shot->hit;
    int* hit_anything = &shot->hit_anything;
    shot->body.index = -1;
    /* Filled in with the end of the flight path up front, so a shot that hits
       nothing still has a terminus for the tracer to be drawn to. */
    float distance = max_range;
    hit->x = ox + dx * distance;
    hit->y = oy + dy * distance;
    hit->z = oz + dz * distance;
    hit->normal_x = -dx;
    hit->normal_y = -dy;
    hit->normal_z = -dz;
    hit->wall_index = -1;
    *hit_anything = 0;

    TraversalHit wall_hit;
    if (traversal_raycast_walls(ox, oy, oz, dx, dy, dz,
                                max_range, &wall_hit)) {
        float rx = wall_hit.x - ox, ry = wall_hit.y - oy, rz = wall_hit.z - oz;
        distance = sqrtf(rx * rx + ry * ry + rz * rz);
        *hit = wall_hit;
        *hit_anything = 1;
    }

    if (dy < -0.00001f) {
        float ground = -oy / dy;
        float gx = ox + dx * ground;
        float gz = oz + dz * ground;
        if (ground > 0.0f && ground < distance &&
            fabsf(gx) <= ARENA_HALF_SIZE && fabsf(gz) <= ARENA_HALF_SIZE) {
            distance = ground;
            hit->x = gx; hit->y = 0.0f; hit->z = gz;
            hit->normal_x = 0.0f; hit->normal_y = 1.0f; hit->normal_z = 0.0f;
            hit->wall_index = -1;
            *hit_anything = 1;
        }
    }

    /* No targets is not an error and not a special case - it is a world with
       nothing in it to shoot, which is what the headless probe runs in. */
    CombatantHit body;
    if (combatant_raycast(targets, ox, oy, oz, dx, dy, dz, distance, &body)) {
        hit->x = body.x; hit->y = body.y; hit->z = body.z;
        hit->normal_x = body.normal_x;
        hit->normal_y = body.normal_y;
        hit->normal_z = body.normal_z;
        /* A body is not one of the arena's boxes, whatever was behind it. */
        hit->wall_index = -1;
        *hit_anything = 1;
        shot->body = body;
    }
}

/* Both effect rings overwrite their oldest entry rather than dropping the new
   one, so the streak the player just made is always the one that survives. */
static void spawn_tracer(Weapon* weapon, float sx, float sy, float sz,
                         float ex, float ey, float ez) {
    int oldest = 0;
    for (int i = 1; i < WEAPON_TRACER_COUNT; ++i)
        if (weapon->tracers[i].life < weapon->tracers[oldest].life) oldest = i;
    WeaponTracer* tracer = &weapon->tracers[oldest];
    tracer->start_x = sx; tracer->start_y = sy; tracer->start_z = sz;
    tracer->end_x = ex; tracer->end_y = ey; tracer->end_z = ez;
    tracer->life = WEAPON_TRACER_LIFETIME;
}

static void spawn_impact(Weapon* weapon, const TraversalHit* hit, int on_body) {
    int oldest = 0;
    for (int i = 1; i < WEAPON_IMPACT_COUNT; ++i)
        if (weapon->impacts[i].life < weapon->impacts[oldest].life) oldest = i;
    WeaponImpact* impact = &weapon->impacts[oldest];
    impact->x = hit->x; impact->y = hit->y; impact->z = hit->z;
    impact->normal_x = hit->normal_x;
    impact->normal_y = hit->normal_y;
    impact->normal_z = hit->normal_z;
    impact->life = WEAPON_IMPACT_LIFETIME;
    impact->on_body = (unsigned char)(on_body != 0);
}

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
    const ActionTrack* action = melee_action_track(weapon, &action_seconds);
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

    /* Bob and sway are hip-fire tells. Through the scope they would read as the
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

void weapon_sight_center(const Weapon* weapon, const PlayerController* player,
                         float* x, float* y, float* z) {
    ViewmodelPose pose;
    weapon_viewmodel_pose(weapon, player, &pose);
    /* The glass is a point ON the gun, so it takes the gun's cosmetic rotation
       with it. Offsetting the pose's origin and stopping there put the mask
       where an uncanted gun's optic would have been, and left the magnified
       circle a little off the metal it is a window into. */
    const float local[3] = {0.0f, weapon_spec_sight_height(weapon->spec),
                            -weapon_spec_sight_forward(weapon->spec)};
    float center[3];
    viewmodel_pose_transform_point(&pose, local, center);
    *x = center[0]; *y = center[1]; *z = center[2];
}

/* Where the streak is drawn from. The bullet itself always leaves the eye, so
   what the crosshair covers is what the round hits; only the visible line
   starts at the barrel, which is what stops a shot from looking like it came
   out of the player's forehead - or, while scoped, out of thin air a foot to
   the right of a gun the player can plainly see is centred. */
/* The combo, which is the attack table read as a graph.

   There is no state machine here beyond "which attack is running", because the
   table already says where each one goes: an input inside the live window
   advances to attack->next, an input outside it starts over, and next == -1 is
   the end of the chain. Writing that as a separate machine would be the same
   information twice, and the second copy is the one that goes stale when
   somebody retunes a window. */
/* Whether the blade is live right now. A block states its window backwards,
   which is what says it has none at all rather than a zero-length one at the
   start that a rounding error could open. */
static int melee_live(const MeleeSpec* melee, const Weapon* weapon) {
    if (weapon->melee_attack < 0) return 0;
    /* A recovery is the arm coming back. It is never live, and its clock runs
       against a different duration, so reading the attack's own window
       fractions against it would answer a question about the wrong clip. */
    if (weapon->melee_recovering) return 0;
    const MeleeAttack* attack = &melee->attacks[weapon->melee_attack];
    if (attack->active_to <= attack->active_from) return 0;
    float t = weapon->melee_duration > 0.0f
            ? weapon->melee_time / weapon->melee_duration : 0.0f;
    return t >= attack->active_from && t <= attack->active_to;
}

static void melee_start(Weapon* weapon, int attack, float duration) {
    weapon->melee_attack = attack;
    weapon->melee_recovering = 0;
    weapon->melee_time = 0.0f;
    weapon->melee_duration = duration;
    /* The hit list, which is what makes one body one swing. Cleared per ATTACK
       and not per input: a chain of three is three swings and each may cut the
       same person again, which is what a combo is for. */
    melee_swing_begin(&weapon->swing, attack);
}

static void melee_idle(Weapon* weapon) {
    weapon->melee_attack = -1;
    weapon->melee_recovering = 0;
    weapon->melee_time = 0.0f;
    weapon->melee_duration = 0.0f;
    melee_swing_end(&weapon->swing);
}

/* One step of a melee weapon: the clock, the sweep, and the inputs. */
static void melee_update(Weapon* weapon, const WeaponCommand* command,
                         float delta_time) {
    const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
    if (!melee) return;

    /* A sword in its scabbard, or halfway out of one, swings at nothing. The
       draw has to finish first, which is what makes drawing a commitment
       rather than a free animation the player can attack through. */
    int busy = weapon->sheathed ||
               weapon->melee_action != WEAPON_MELEE_ACTION_NONE;

    if (weapon->melee_attack >= 0) {
        weapon->melee_time += delta_time;

        /* The blade, swept between where it was and where it is. Before the
           chain advance below, so the frame an attack ends still pays out the
           part of its window that ran.

           Never during a recovery: that is the arm coming back through the
           space the cut went through, and a blade live on the way back would
           hit twice for one swing in the one case the hit list cannot catch -
           a second body stepping into the arc. */
        if (!weapon->melee_recovering && weapon->blade_valid &&
            weapon->targets && weapon->melee_duration > 0.0f)
            melee_swing_update(&weapon->swing, weapon->spec,
                               &weapon->blade_previous, &weapon->blade_now,
                               weapon->melee_time / weapon->melee_duration,
                               weapon->targets);

        if (weapon->melee_time >= weapon->melee_duration) {
            /* An attack that nothing followed settles through its recovery
               before the weapon is idle again. Only A and B ship one; C ends
               the chain and recovers into the hold pose, which is what a NULL
               here means rather than an omission. */
            const MeleeAttack* done = &melee->attacks[weapon->melee_attack];
            if (!weapon->melee_recovering && done->recovery) {
                weapon->melee_recovering = 1;
                weapon->melee_time = 0.0f;
                weapon->melee_duration = MELEE_RECOVER_SECONDS;
            } else {
                melee_idle(weapon);
            }
        }
    }

    if (busy) return;

    /* Block: held, and refused while the current attack is live. A block that
       cancelled a swing already in its window would let a player attack and
       defend on the same frame, which is the one thing a block must not be. */
    if (command->block_held) {
        if (weapon->melee_attack < 0)
            melee_start(weapon, melee->block_attack, MELEE_BLOCK_HOLD);
        else if (!melee_live(melee, weapon) &&
                 weapon->melee_attack != melee->block_attack)
            melee_start(weapon, melee->block_attack, MELEE_BLOCK_HOLD);
        return;
    }
    /* Letting go of block drops it immediately - it is a hold, not a swing. */
    if (weapon->melee_attack == melee->block_attack) melee_idle(weapon);

    if (!command->attack_pressed && !command->heavy_pressed) return;

    if (weapon->melee_attack < 0 || weapon->melee_recovering) {
        int entry = command->heavy_pressed ? melee->heavy_attack
                                           : melee->light_attack;
        melee_start(weapon, entry, MELEE_ATTACK_SECONDS);
        return;
    }

    /* Mid-chain. A further light input inside the window advances; a heavy
       does not, because a heavy is an opener and starting one out of a chain
       would make it the cheapest attack in the game rather than the
       committing one. */
    if (!command->attack_pressed) return;
    if (!melee_live(melee, weapon)) return;
    const MeleeAttack* current = &melee->attacks[weapon->melee_attack];
    if (current->next < 0 || current->next >= melee->attack_count) return;
    melee_start(weapon, current->next, MELEE_ATTACK_SECONDS);
}

void weapon_toggle_sheathe(Weapon* weapon) {
    if (!weapon || !weapon_spec_melee(weapon->spec)) return;
    if (weapon->melee_action != WEAPON_MELEE_ACTION_NONE) return;
    weapon->melee_action = weapon->sheathed ? WEAPON_MELEE_ACTION_DRAW
                                            : WEAPON_MELEE_ACTION_SHEATHE;
    weapon->melee_action_progress = 0.0f;
    /* The sword is in the hand for the whole of both motions - a draw has it
       gripped from the first key and a sheathe until the last - so the bit
       flips at the START of a draw and at the END of a sheathe. Flipping it
       the other way round would draw a sword that was still in its scabbard,
       or leave one in mid-air while the hand walked away from it. */
    if (weapon->melee_action == WEAPON_MELEE_ACTION_DRAW) weapon->sheathed = 0;
}

/* The track a melee action is running, and how far through it. NULL when
   nothing is. */
static const ActionTrack* melee_action_track(const Weapon* weapon,
                                             float* seconds) {
    const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
    if (!melee || weapon->melee_action == WEAPON_MELEE_ACTION_NONE) return NULL;
    if (weapon->melee_action == WEAPON_MELEE_ACTION_DRAW) {
        if (seconds) *seconds = melee->draw_time;
        return melee->draw_track;
    }
    if (seconds) *seconds = melee->sheathe_time;
    return melee->sheathe_track;
}

const char* weapon_melee_clip(const Weapon* weapon, float* time_out) {
    if (!weapon) return NULL;
    const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
    if (!melee) return NULL;
    if (weapon->melee_attack >= 0 &&
        weapon->melee_attack < melee->attack_count) {
        const MeleeAttack* attack = &melee->attacks[weapon->melee_attack];
        if (time_out) *time_out = weapon->melee_time;
        if (weapon->melee_recovering && attack->recovery)
            return attack->recovery;
        return attack->clip;
    }
    /* Idle: the hold pose, which is the same clip the third-person body holds
       the sword in. One name, so the two views cannot drift into holding it
       differently. */
    if (time_out) *time_out = 0.0f;
    return weapon->spec->clips.aim[1];
}

void weapon_set_blade_transform(Weapon* weapon, const Mat4* world, int valid) {
    if (!weapon) return;
    if (!valid || !world) { weapon->blade_valid = 0; return; }
    /* The first publish after a gap seeds both ends, so the swing's first step
       sweeps a zero-length interval rather than one that starts wherever the
       body was standing before it teleported. */
    weapon->blade_previous = weapon->blade_valid ? weapon->blade_now : *world;
    weapon->blade_now = *world;
    weapon->blade_valid = 1;
}

void weapon_set_melee_base(Weapon* weapon, const ViewmodelPose* base, int valid) {
    if (!weapon) return;
    weapon->melee_base_valid = valid && base != NULL;
    if (weapon->melee_base_valid) weapon->melee_base = *base;
}

void weapon_set_world_muzzle(Weapon* weapon, const float muzzle[3], int valid) {
    if (!weapon) return;
    weapon->world_muzzle_valid = valid && muzzle != 0;
    if (!weapon->world_muzzle_valid) return;
    for (int c = 0; c < 3; ++c) weapon->world_muzzle[c] = muzzle[c];
}

void weapon_tracer_origin(const Weapon* weapon, const PlayerController* player,
                          float yaw_degrees, float pitch_degrees,
                          float roll_degrees, float eye_height,
                          float view_fov_degrees,
                          float* x, float* y, float* z) {
    /* The body's gun, when there is one on screen. Everything below this line
       is about placing a barrel that is drawn in camera space through its own
       projection; a barrel already in world units needs none of it. */
    if (weapon->world_muzzle_valid) {
        *x = weapon->world_muzzle[0];
        *y = weapon->world_muzzle[1];
        *z = weapon->world_muzzle[2];
        return;
    }

    float forward[3], right[3], up[3];
    traversal_view_basis(yaw_degrees, pitch_degrees, roll_degrees,
                         forward, right, up);

    /* The barrel tip as a point on the model, put through the pose the gun is
       actually drawn at - rotation included. Composing it from the pose's
       origin alone was a latent bug of exactly the kind the pose type exists to
       stop: the tracer left from a muzzle the mesh had already turned away
       from, by 3cm at the hip cant and by the length of the receiver at the
       sprint carry's 25 degrees. */
    ViewmodelPose pose;
    weapon_viewmodel_pose(weapon, player, &pose);
    /* A weapon with no barrel has no tip for a streak to leave from; the
       caller falls back to the eye, which is where it started. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    const float local[3] = {0.0f, gun ? gun->muzzle_up : 0.0f,
                            gun ? -gun->muzzle_forward : 0.0f};
    float tip[3];
    viewmodel_pose_transform_point(&pose, local, tip);
    float ox = tip[0], oy = tip[1], oz = tip[2];

    /* The gun is drawn through its OWN projection and the tracer through the
       world's, and those are not the same angle - 43 degrees against 75 on the
       rifle at the hip. A camera-space offset put straight into the world lands
       about half as far from the middle of the screen as the barrel it came
       from, so the streak left from inside the gun rather than off the end of
       it. Leaning is where it shows worst, because a lean is what swings the
       barrel furthest off centre, but it was never only a lean.

       Matching the two means matching the ANGLE the offset subtends: keep the
       depth and scale the lateral part by the ratio of the tangents. The result
       is further out in metres than the barrel really is, which costs nothing -
       a tracer is a streak whose direction the eye reads, and the round itself
       leaves the eye regardless. */
    float world_tangent = tanf(view_fov_degrees * 0.5f * MATH_DEG_TO_RAD);
    float gun_fov = lerpf(weapon->spec->view_fov, weapon_spec_view_ads_fov(weapon->spec),
                          weapon->ads);
    float gun_tangent = tanf(gun_fov * 0.5f * MATH_DEG_TO_RAD);
    if (gun_tangent > 0.0001f) {
        float widen = world_tangent / gun_tangent;
        ox *= widen;
        oy *= widen;
    }

    /* The renderer cants the complete camera-space weapon during a lean. Keep
       the visible tracer attached to that transformed barrel. */
    float lean_angle = player->lean * VIEWMODEL_LEAN_ROLL * MATH_DEG_TO_RAD;
    float leaned_ox = ox * cosf(lean_angle) - oy * sinf(lean_angle);
    float leaned_oy = ox * sinf(lean_angle) + oy * cosf(lean_angle);
    ox = leaned_ox;
    oy = leaned_oy;

    /* The first-person camera itself shifts during a peek. Start the visible
       barrel from that same lateral origin before applying its local pose, so
       tracers do not appear to leave the unleaned player centre. */
    float yaw = yaw_degrees * MATH_DEG_TO_RAD;
    float lean_x = -cosf(yaw) * player->lean * VIEW_LEAN_OFFSET;
    float lean_z = sinf(yaw) * player->lean * VIEW_LEAN_OFFSET;

    *x = player->x + lean_x + right[0] * ox + up[0] * oy - forward[0] * oz;
    *y = player->y + eye_height + right[1] * ox + up[1] * oy - forward[1] * oz;
    *z = player->z + lean_z + right[2] * ox + up[2] * oy - forward[2] * oz;
}

/* A camera-space point into the world. The same basis muzzle_position builds,
   and deliberately without its field-of-view widening: that widening exists to
   put a TRACER where the eye expects it against a gun drawn through a narrower
   projection, and a dropped magazine is a real object drawn through the world's
   own. Widened, it would land a foot from where it was let go. */
static void camera_to_world(const PlayerController* player,
                            float yaw_degrees, float pitch_degrees,
                            float roll_degrees, float eye_height,
                            const float camera[3], float out[3]) {
    float forward[3], right[3], up[3];
    traversal_view_basis(yaw_degrees, pitch_degrees, roll_degrees,
                         forward, right, up);
    float yaw = yaw_degrees * MATH_DEG_TO_RAD;
    float lean_x = -cosf(yaw) * player->lean * VIEW_LEAN_OFFSET;
    float lean_z = sinf(yaw) * player->lean * VIEW_LEAN_OFFSET;
    for (int c = 0; c < 3; ++c) {
        float base = c == 0 ? player->x + lean_x
                   : c == 1 ? player->y + eye_height
                            : player->z + lean_z;
        out[c] = base + right[c] * camera[0] + up[c] * camera[1]
                      - forward[c] * camera[2];
    }
}

/* Lets go of the spent magazine, into the arena. Overwrites the oldest slot for
   the same reason the tracers and impacts do: the one the player just made is
   the one worth keeping. */
static void drop_magazine(Weapon* weapon, const PlayerController* player,
                          float yaw_degrees, float pitch_degrees,
                          float roll_degrees, float eye_height) {
    int oldest = 0;
    for (int i = 1; i < WEAPON_DROPPED_MAG_COUNT; ++i)
        if (weapon->dropped_mags[i].life < weapon->dropped_mags[oldest].life)
            oldest = i;
    WeaponDroppedMag* mag = &weapon->dropped_mags[oldest];

    /* Where the viewmodel's magazine had got to when the hand let go, so the
       real one starts exactly where the drawn one stopped. */
    ViewmodelPose pose;
    weapon_viewmodel_pose(weapon, player, &pose);
    const ActionTrack* track = weapon_spec_reload_sequence(weapon->spec);
    ActionSample sample;
    action_track_sample(track, track->drop_at, &sample);
    float local[3];
    for (int c = 0; c < 3; ++c)
        local[c] = lerpf(sample.from->mag[c], sample.to->mag[c], sample.t);
    float camera[3];
    viewmodel_pose_transform_point(&pose, local, camera);
    float world[3];
    camera_to_world(player, yaw_degrees, pitch_degrees, roll_degrees,
                    eye_height, camera, world);
    mag->x = world[0]; mag->y = world[1]; mag->z = world[2];

    /* It leaves with the player's own motion, because a magazine dropped at a
       run does not stop dead in the air, plus a push down and out to the left
       of the weapon - which is the way a magazine actually falls out of a rifle
       held in the right hand. */
    float forward[3], right[3], up[3];
    traversal_view_basis(yaw_degrees, pitch_degrees, roll_degrees,
                         forward, right, up);
    for (int c = 0; c < 3; ++c) {
        float push = -up[c] * WEAPON_MAG_DROP_DOWN
                   - right[c] * WEAPON_MAG_DROP_AWAY;
        float carried = c == 0 ? player->velocity_x
                      : c == 1 ? 0.0f : player->velocity_z;
        float v = push + carried;
        if (c == 0) mag->velocity_x = v;
        else if (c == 1) mag->velocity_y = v;
        else mag->velocity_z = v;
    }

    /* Where the floor is under it, asked once. A magazine dropped on a rooftop
       should lie on the rooftop, and casting down from where it left the hand
       is both cheaper and steadier than testing every step of the fall. */
    TraversalHit hit;
    mag->rest_y = 0.0f;
    if (traversal_raycast_walls(mag->x, mag->y, mag->z, 0.0f, -1.0f, 0.0f,
                                40.0f, &hit) && hit.y > 0.0f)
        mag->rest_y = hit.y;

    mag->spin = 0.0f;
    mag->tilt = 0.0f;
    mag->spin_rate = WEAPON_MAG_SPIN * (random_unit(weapon) * 2.0f - 1.0f);
    mag->tilt_rate = WEAPON_MAG_TILT * (0.4f + random_unit(weapon) * 0.6f);
    mag->life = WEAPON_MAG_FALL_LIMIT + WEAPON_MAG_REST_TIME;
    mag->grounded = 0;
}

static void advance_dropped_mags(Weapon* weapon, float delta_time) {
    for (int i = 0; i < WEAPON_DROPPED_MAG_COUNT; ++i) {
        WeaponDroppedMag* mag = &weapon->dropped_mags[i];
        if (mag->life <= 0.0f) continue;
        mag->life -= delta_time;
        if (mag->grounded) continue;

        mag->velocity_y -= WEAPON_MAG_GRAVITY * delta_time;
        mag->x += mag->velocity_x * delta_time;
        mag->y += mag->velocity_y * delta_time;
        mag->z += mag->velocity_z * delta_time;
        mag->spin += mag->spin_rate * delta_time;
        mag->tilt += mag->tilt_rate * delta_time;

        float floor = mag->rest_y + WEAPON_MAG_REST_HEIGHT;
        if (mag->y > floor) continue;
        /* Landed. It stops dead rather than bouncing - a magazine is a box of
           steel and plastic and does not bounce in any way worth drawing - and
           lies flat, because it came to rest and a magazine resting on its
           corner reads as a physics bug rather than as an object. */
        mag->y = floor;
        mag->grounded = 1;
        mag->velocity_x = mag->velocity_y = mag->velocity_z = 0.0f;
        mag->spin_rate = mag->tilt_rate = 0.0f;
        mag->tilt = 90.0f;
        /* However far it fell, it lies still for the same beat. */
        if (mag->life > WEAPON_MAG_REST_TIME) mag->life = WEAPON_MAG_REST_TIME;
    }
}

static void fire(Weapon* weapon, const PlayerController* player,
                 float aim_yaw_degrees, float aim_pitch_degrees,
                 float camera_roll_degrees, float eye_height,
                 float view_fov_degrees) {
    const WeaponSpec* spec = weapon->spec;
    /* Only a gun fires. The caller already gates on ammunition, which a melee
       weapon has none of, so this is belt and braces - but it is the guard
       that makes every gun-only read below safe. */
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    if (!gun) return;
    weapon->ammo_in_magazine--;
    weapon->fire_cooldown = gun->fire_interval;
    weapon->quiet_time = 0.0f;
    weapon->muzzle_flash = WEAPON_MUZZLE_FLASH_TIME;
    weapon->view_kick = 1.0f;

    float origin_x = player->x;
    float origin_y = player->y + eye_height;
    float origin_z = player->z;
    /* Fire from the camera's lateral peek, otherwise the view can clear a
       corner while the authoritative trace still begins behind it. */
    float yaw = aim_yaw_degrees * MATH_DEG_TO_RAD;
    origin_x -= cosf(yaw) * player->lean * VIEW_LEAN_OFFSET;
    origin_z += sinf(yaw) * player->lean * VIEW_LEAN_OFFSET;
    float dx, dy, dz;
    traversal_aim_direction(aim_yaw_degrees, aim_pitch_degrees, &dx, &dy, &dz);
    apply_bloom(weapon, &dx, &dy, &dz);

    WeaponShot shot;
    trace_shot(weapon->targets, origin_x, origin_y, origin_z, dx, dy, dz,
               spec->range, &shot);

    float muzzle_x, muzzle_y, muzzle_z;
    weapon_tracer_origin(weapon, player, aim_yaw_degrees, aim_pitch_degrees,
                         camera_roll_degrees, eye_height, view_fov_degrees,
                         &muzzle_x, &muzzle_y, &muzzle_z);
    spawn_tracer(weapon, muzzle_x, muzzle_y, muzzle_z,
                 shot.hit.x, shot.hit.y, shot.hit.z);
    if (shot.hit_anything)
        spawn_impact(weapon, &shot.hit, shot.body.index >= 0);

    /* spec->damage is the same here as it would be at point blank; this game
       has no falloff by design. What the band it landed in multiplies it by,
       what comes off the pool and the figure thrown for it are all one call, so
       a round cannot take one amount off and show another. */
    if (shot.body.index >= 0)
        combatant_apply_damage(weapon->targets, &shot.body, spec->damage);

    /* Bloom is charged after the shot, so the first round of a burst leaves at
       whatever accuracy the player had recovered to. */
    weapon->bloom_degrees = fminf(gun->bloom_max,
                                  weapon->bloom_degrees + gun->bloom_per_shot);
    float kick = lerpf(1.0f, gun->ads_recoil_scale, weapon->ads);
    weapon->gun_pitch_degrees += gun->recoil_pitch * kick;
    weapon->gun_yaw_degrees += (random_unit(weapon) * 2.0f - 1.0f) *
                               gun->recoil_yaw * kick;
}

/* Non-zero when a reload actually started, so a caller can tell a request that
   did something from one there was nothing to do about. */
static int begin_reload(Weapon* weapon) {
    /* Nothing to reload on a weapon with no magazine, which is not a failure
       to report - it is a request there was never anything to do about. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    if (!gun) return 0;
    if (weapon->is_reloading) return 0;
    if (weapon->ammo_in_magazine >= gun->magazine) return 0;
    weapon->is_reloading = 1;
    weapon->reload_time_left = gun->reload_time;
    return 1;
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
    weapon->melee_action = WEAPON_MELEE_ACTION_NONE;
    weapon->melee_action_progress = 0.0f;
    weapon->melee_attack = -1;
    weapon->melee_time = 0.0f;
    weapon->melee_duration = 0.0f;
    weapon->sheathed = weapon_spec_melee(spec) != NULL;
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

/* The draw or the sheathe, one step on. Ends by handing the body back to the
   hold pose, and by putting the sword away if that is what it was doing. */
static void advance_melee_action(Weapon* weapon, float delta_time) {
    float seconds = 0.0f;
    if (!melee_action_track(weapon, &seconds)) return;
    if (seconds <= 0.0f) seconds = 0.001f;
    weapon->melee_action_progress += delta_time / seconds;
    if (weapon->melee_action_progress < 1.0f) return;
    weapon->melee_action_progress = 0.0f;
    if (weapon->melee_action == WEAPON_MELEE_ACTION_SHEATHE)
        weapon->sheathed = 1;
    melee_swing_end(&weapon->swing);
    weapon->blade_valid = 0;
    weapon->melee_action = WEAPON_MELEE_ACTION_NONE;
}

static void advance_timers(Weapon* weapon, float delta_time) {
    advance_melee_action(weapon, delta_time);
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
    advance_dropped_mags(weapon, delta_time);
}

void weapon_update(Weapon* weapon, const WeaponCommand* command,
                   const PlayerController* player,
                   float aim_yaw_degrees, float aim_pitch_degrees,
                   float camera_roll_degrees,
                   float eye_height, float view_fov_degrees,
                   float delta_time) {
    advance_timers(weapon, delta_time);

    /* Aiming is never blocked and never blocks anything: it runs ahead of the
       reload and fire paths, so the player can raise or drop the scope through
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
            drop_magazine(weapon, player, aim_yaw_degrees, aim_pitch_degrees,
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
        melee_update(weapon, command, delta_time);
        return;
    }

    /* Only a reload that started costs the frame; pressing R on a full
       magazine must not interrupt fire. */
    if (command->reload_pressed && begin_reload(weapon))
        return;

    int wants_to_fire = command->fire_held && !weapon->trigger_latched;
    if (!wants_to_fire) return;

    /* Pulling the trigger on an empty magazine is a request to reload. Making
       the player press a second key to do the only thing left to do costs time
       in a fight and teaches nothing. */
    if (weapon->ammo_in_magazine <= 0) {
        begin_reload(weapon);
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
    fire(weapon, player, shot_yaw, shot_pitch, camera_roll_degrees, eye_height,
         view_fov_degrees);
}

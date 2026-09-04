/* The melee half of a weapon: the carries, the attack chain, the blade's
   transform, and getting the sword out of its scabbard and back into it.

   Split out of weapon.c beside weapon_firearm.c, along the same line the spec
   already drew between a FirearmSpec and a MeleeSpec. Nothing here was
   rewritten. See gameplay/weapon_internal.h.

   Every number comes off the MeleeSpec the weapon was initialised with. What
   is here is the order things happen in - which attack a further input
   advances to, when a swing can be cancelled, when an idle sword puts itself
   away - and that order is the same for every blade. */

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

/* Which stance the player is asking for, this step.

   Movement first: a wall run or a slide is the case the momentum carry exists
   for, and it outranks the idle clock because a player on a wall is plainly
   not idle whatever the clock says. Then the rest, which is the first half of
   the auto-sheathe - the hands come down, and the sword follows them in. Then
   the guard, which is what a sword is held in when none of the above is true. */
static int melee_carry_target(const Weapon* weapon,
                              const PlayerController* player) {
    if (player && (player->wall_run.active || player->slide.active))
        return MELEE_CARRY_MOMENTUM;
    if (weapon->melee_quiet_time >=
        MELEE_AUTO_SHEATHE_SECONDS * MELEE_REST_AFTER)
        return MELEE_CARRY_REST;
    return MELEE_CARRY_GUARD;
}

/* Eases every weight toward the asked-for stance - one toward it, the rest
   toward zero - at MELEE_CARRY_RESPONSE.

   An exponential approach on each weight separately, which is what keeps the
   sum at one without anything having to enforce it: every weight moves the
   same fraction of its own distance to its own target, and those targets
   themselves sum to one, so the total moves the same fraction of its distance
   to one - and it started there. The normalise at the end is against float
   drift over a long idle and nothing else; it is not what makes the invariant
   true.

   Runs whether or not the sword is being carried this step. A swing that ends
   on a wall should come back to the carry it would have had, not to the one it
   left, and a blend that stopped while the hands were busy would do the
   second. */
void weapon_melee_carry_update(Weapon* weapon, const PlayerController* player,
                               float delta_time) {
    if (!weapon || !weapon_spec_melee(weapon->spec) || delta_time <= 0.0f)
        return;

    int target = melee_carry_target(weapon, player);
    float blend = 1.0f - expf(-MELEE_CARRY_RESPONSE * delta_time);
    float total = 0.0f;
    for (int c = 0; c < MELEE_CARRY_COUNT; ++c) {
        float wanted = c == target ? 1.0f : 0.0f;
        weapon->melee_carry[c] += (wanted - weapon->melee_carry[c]) * blend;
        if (weapon->melee_carry[c] < 0.0f) weapon->melee_carry[c] = 0.0f;
        total += weapon->melee_carry[c];
    }
    if (total > 0.0f)
        for (int c = 0; c < MELEE_CARRY_COUNT; ++c)
            weapon->melee_carry[c] /= total;
}

int weapon_melee_carry(const Weapon* weapon, ViewmodelPose* held,
                       float weights[MELEE_CARRY_COUNT]) {
    if (!weapon) return 0;
    const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
    if (!melee) return 0;
    /* Worn, or halfway in or out of the scabbard, or swinging. All three are
       hands that belong to something else - an action track or an attack clip -
       and a stance offered alongside one is a second opinion about where the
       sword is. */
    if (weapon->sheathed) return 0;
    if (weapon->melee_action != WEAPON_MELEE_ACTION_NONE) return 0;
    if (weapon->melee_attack >= 0) return 0;

    if (weights)
        for (int c = 0; c < MELEE_CARRY_COUNT; ++c)
            weights[c] = weapon->melee_carry[c];

    if (held) {
        /* Folded in against the running total, which for two poses is the
           weighted average - the same shape viewmodel_arms uses to blend the
           shoulder placements these poses are reached from, so the sword and
           the hands holding it are never a stance out of step. */
        ViewmodelPose out = melee->carries[0];
        float total = weapon->melee_carry[0];
        for (int c = 1; c < MELEE_CARRY_COUNT; ++c) {
            total += weapon->melee_carry[c];
            if (total > 0.0f)
                viewmodel_pose_lerp(&out, &melee->carries[c],
                                    weapon->melee_carry[c] / total, &out);
        }
        *held = out;
    }
    return 1;
}

/* One step of a melee weapon: the clock, the sweep, and the inputs. */
void weapon_melee_update(Weapon* weapon, const WeaponCommand* command,
                         float delta_time) {
    const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
    if (!melee) return;

    int wants_attack = command->attack_pressed || command->heavy_pressed;

    /* Attacking with the sword away draws it. The player asked to swing, and
       answering "you were sheathed" is the game arguing with an input it
       understood perfectly well - so the draw starts and the swing is
       remembered until it lands. A sheathe already running is reversed on the
       spot for the same reason. */
    if (wants_attack &&
        (weapon->sheathed ||
         weapon->melee_action == WEAPON_MELEE_ACTION_SHEATHE)) {
        if (weapon->melee_action == WEAPON_MELEE_ACTION_SHEATHE) {
            /* Turn round rather than finish putting it away and take it out
               again. Entering the draw where the sheathe had got to keeps the
               sword where it is on the frame the player changed their mind. */
            weapon->melee_action_progress =
                1.0f - weapon->melee_action_progress;
            weapon->melee_action = WEAPON_MELEE_ACTION_DRAW;
            weapon->sheathed = 0;
        } else {
            weapon_toggle_sheathe(weapon);
        }
        weapon->melee_attack_buffered = 1;
        weapon->melee_heavy_buffered = command->heavy_pressed ? 1 : 0;
    }

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

    /* The draw has landed, so the swing that started it happens now. */
    if (weapon->melee_attack_buffered) {
        int entry = weapon->melee_heavy_buffered ? melee->heavy_attack
                                                 : melee->light_attack;
        weapon->melee_attack_buffered = 0;
        weapon->melee_heavy_buffered = 0;
        weapon->melee_quiet_time = 0.0f;
        melee_start(weapon, entry, MELEE_ATTACK_SECONDS);
        return;
    }

    /* And a sword nobody has used for long enough puts itself away. Counted
       only while it is genuinely idle - an attack, a recovery, a block or a
       held input all reset it - so a chain never races the clock, and a player
       standing guard does not have the sword taken out of their hands. */
    if (weapon->melee_attack >= 0 || wants_attack || command->block_held) {
        weapon->melee_quiet_time = 0.0f;
    } else if (!weapon->sheathed) {
        weapon->melee_quiet_time += delta_time;
        if (weapon->melee_quiet_time >= MELEE_AUTO_SHEATHE_SECONDS) {
            weapon->melee_quiet_time = 0.0f;
            weapon_toggle_sheathe(weapon);
            return;
        }
    }

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
const ActionTrack* weapon_melee_action_track(const Weapon* weapon,
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

/* The draw or the sheathe, one step on. Ends by handing the body back to the
   hold pose, and by putting the sword away if that is what it was doing. */
void weapon_melee_advance_action(Weapon* weapon, float delta_time) {
    float seconds = 0.0f;
    if (!weapon_melee_action_track(weapon, &seconds)) return;
    if (seconds <= 0.0f) seconds = 0.001f;
    weapon->melee_action_progress += delta_time / seconds;
    if (weapon->melee_action_progress < 1.0f) return;
    weapon->melee_action_progress = 0.0f;
    if (weapon->melee_action == WEAPON_MELEE_ACTION_SHEATHE)
        weapon->sheathed = 1;
    /* The action, and nothing else. A draw that finishes must not clear the
       swing the player buffered to start it - that input is the whole reason
       the draw is running. */
    weapon->melee_action = WEAPON_MELEE_ACTION_NONE;
}

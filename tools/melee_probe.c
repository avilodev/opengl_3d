/* What a swing hits, checked without a window - see include/gameplay/melee_swing.h.

   `make meleeprobe` from the project root.

   Three things here cannot be found by playing. Whether a swing pays twice is
   invisible at any frame rate you happen to be running: it looks like the
   sword being strong. Whether the blade is live during the wind-up is
   invisible because the wind-up passes through the same space the cut does.
   And whether a fast swing tunnels is invisible in exactly the case that
   matters, because the swings that tunnel are the ones too fast to watch.

   The swing is built here from two matrices a frame apart rather than by
   running the game, so each of the three is a arithmetic rather than a
   playtest. */
#include "gameplay/melee_swing.h"
#include "gameplay/combatant.h"
#include "gameplay/combat_tuning.h"
#include "gameplay/weapon_spec.h"
#include "gameplay/weapon.h"
#include "gameplay/weapon_tuning.h"
#include "utils/mat4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char* what, float got, float want, float tol) {
    int ok = fabsf(got - want) <= tol;
    if (!ok) ++failures;
    printf("  %s %-44s got %+.3f want %+.3f\n", ok ? "ok  " : "FAIL", what,
           got, want);
}

/* One body standing at the origin and nothing else in the world, so every
   count below is about the one swing rather than about the arena. */
static void one_body(CombatantSet* set, float x, float z) {
    memset(set, 0, sizeof *set);
    set->count = 1;
    /* combatant_set_init would stand up the arena's whole spawn table, which
       is a dozen bodies and a map. These two fields are what it sets that a
       respawn does not, and they are what a trace looks at first. */
    set->items[0].active = 1;
    set->items[0].max_health = COMBAT_DUMMY_HEALTH;
    combatant_respawn(&set->items[0], x, 0.0f, z, 0.0f);
}

/* The weapon's transform: the grip at a point, blade pointing down -z, which
   is the frame the spec's blade_base and blade_tip are stated in.

   Held at chest height, because that is where a cut goes and because a blade
   swept along the floor hits nothing on a body standing on it - which would
   have made every check below pass for the wrong reason. */
#define SWING_HEIGHT 1.25f

static Mat4 at(float x, float z) {
    return mat4_translation(x, SWING_HEIGHT, z);
}

/* Drives a katana through weapon_update with nothing else in the world, so the
   only thing that can move the chain is the input. */
static void step(Weapon* weapon, const PlayerController* player,
                 int attack, int block, float delta_time) {
    WeaponCommand command;
    memset(&command, 0, sizeof command);
    command.attack_pressed = attack;
    command.block_held = block;
    command.heavy_pressed = attack && block;
    weapon_update(weapon, &command, player, 0.0f, 0.0f, 0.0f, 1.6f, 75.0f,
                  delta_time);
}

/* The combo, the cancel and the block - three rules about WHEN an input is
   taken, none of which leaves a mark anywhere a player can see. A chain that
   silently refuses to advance looks like the player mistiming it; one that
   advances from outside the window looks like the game being generous. */
static void check_chain(void) {
    const MeleeSpec* melee = weapon_spec_melee(&WEAPON_SPEC_KATANA);
    PlayerController player;
    memset(&player, 0, sizeof player);
    player.ground.is_grounded = 1;

    printf("\nthe chain advances inside the window\n");
    {
        Weapon weapon;
        weapon_init(&weapon, &WEAPON_SPEC_KATANA);
        weapon.sheathed = 0;

        step(&weapon, &player, 1, 0, 0.0f);
        check("the first input opens the chain", (float)weapon.melee_attack,
              (float)melee->light_attack, 0.0f);

        /* Into the middle of the window, then a second input. */
        const MeleeAttack* a = &melee->attacks[melee->light_attack];
        float mid = (a->active_from + a->active_to) * 0.5f;
        step(&weapon, &player, 0, 0, MELEE_ATTACK_SECONDS * mid);
        step(&weapon, &player, 1, 0, 0.0f);
        check("a second input advances to B", (float)weapon.melee_attack,
              (float)a->next, 0.0f);

        const MeleeAttack* b = &melee->attacks[a->next];
        float mid_b = (b->active_from + b->active_to) * 0.5f;
        step(&weapon, &player, 0, 0, MELEE_ATTACK_SECONDS * mid_b);
        step(&weapon, &player, 1, 0, 0.0f);
        check("a third advances to C", (float)weapon.melee_attack,
              (float)b->next, 0.0f);

        /* C ends the chain: next is -1, so a fourth input does nothing. */
        const MeleeAttack* c = &melee->attacks[b->next];
        float mid_c = (c->active_from + c->active_to) * 0.5f;
        step(&weapon, &player, 0, 0, MELEE_ATTACK_SECONDS * mid_c);
        int was = weapon.melee_attack;
        step(&weapon, &player, 1, 0, 0.0f);
        check("and stops at C", (float)weapon.melee_attack, (float)was, 0.0f);
    }

    printf("\nan unchained swing settles through its recovery\n");
    {
        Weapon weapon;
        weapon_init(&weapon, &WEAPON_SPEC_KATANA);
        weapon.sheathed = 0;
        step(&weapon, &player, 1, 0, 0.0f);
        step(&weapon, &player, 0, 0, MELEE_ATTACK_SECONDS * 1.01f);
        check("A recovers rather than going idle",
              (float)weapon.melee_recovering, 1.0f, 0.0f);
        check("and is still its own attack", (float)weapon.melee_attack,
              (float)melee->light_attack, 0.0f);
        /* The recovery is cancellable - that is what a recovery is for. */
        step(&weapon, &player, 1, 0, 0.0f);
        check("an input cancels it into a fresh swing",
              (float)weapon.melee_recovering, 0.0f, 0.0f);

        /* And left alone it runs out into nothing. */
        Weapon settled;
        weapon_init(&settled, &WEAPON_SPEC_KATANA);
        settled.sheathed = 0;
        step(&settled, &player, 1, 0, 0.0f);
        step(&settled, &player, 0, 0, MELEE_ATTACK_SECONDS * 1.01f);
        step(&settled, &player, 0, 0, MELEE_RECOVER_SECONDS * 1.01f);
        check("left alone it goes idle", (float)settled.melee_attack, -1.0f,
              0.0f);

        /* C ends the chain and has no recovery clip, so it goes straight to
           idle rather than settling through one that does not exist. */
        Weapon ended;
        weapon_init(&ended, &WEAPON_SPEC_KATANA);
        ended.sheathed = 0;
        const MeleeAttack* a0 = &melee->attacks[melee->light_attack];
        float m0 = (a0->active_from + a0->active_to) * 0.5f;
        step(&ended, &player, 1, 0, 0.0f);
        step(&ended, &player, 0, 0, MELEE_ATTACK_SECONDS * m0);
        step(&ended, &player, 1, 0, 0.0f);
        const MeleeAttack* b0 = &melee->attacks[a0->next];
        float m1 = (b0->active_from + b0->active_to) * 0.5f;
        step(&ended, &player, 0, 0, MELEE_ATTACK_SECONDS * m1);
        step(&ended, &player, 1, 0, 0.0f);
        step(&ended, &player, 0, 0, MELEE_ATTACK_SECONDS * 1.01f);
        check("C has no recovery and goes idle", (float)ended.melee_attack,
              -1.0f, 0.0f);
    }

    printf("\nan input outside the window starts over\n");
    {
        Weapon weapon;
        weapon_init(&weapon, &WEAPON_SPEC_KATANA);
        weapon.sheathed = 0;
        step(&weapon, &player, 1, 0, 0.0f);
        /* Past the end of the attack and its recovery both, so the weapon is
           idle rather than mid-settle. */
        step(&weapon, &player, 0, 0, MELEE_ATTACK_SECONDS * 1.1f);
        step(&weapon, &player, 0, 0, MELEE_RECOVER_SECONDS * 1.1f);
        check("the attack expires", (float)weapon.melee_attack, -1.0f, 0.0f);
        step(&weapon, &player, 1, 0, 0.0f);
        check("and the next input opens the chain again",
              (float)weapon.melee_attack, (float)melee->light_attack, 0.0f);
    }

    printf("\nblock is refused while a swing is live\n");
    {
        Weapon weapon;
        weapon_init(&weapon, &WEAPON_SPEC_KATANA);
        weapon.sheathed = 0;
        step(&weapon, &player, 1, 0, 0.0f);
        const MeleeAttack* a = &melee->attacks[melee->light_attack];
        float mid = (a->active_from + a->active_to) * 0.5f;
        step(&weapon, &player, 0, 0, MELEE_ATTACK_SECONDS * mid);
        step(&weapon, &player, 0, 1, 0.0f);
        check("a live swing is not cancelled by block",
              (float)weapon.melee_attack, (float)melee->light_attack, 0.0f);

        /* Past the window but still inside the attack: now it may block. */
        float after = a->active_to + (1.0f - a->active_to) * 0.5f;
        step(&weapon, &player, 0, 1,
             MELEE_ATTACK_SECONDS * (after - mid));
        check("and taken once the window has passed",
              (float)weapon.melee_attack, (float)melee->block_attack, 0.0f);
    }

    printf("\na sheathed sword swings at nothing\n");
    {
        Weapon weapon;
        weapon_init(&weapon, &WEAPON_SPEC_KATANA);
        check("a katana starts sheathed", (float)weapon.sheathed, 1.0f, 0.0f);
        step(&weapon, &player, 1, 0, 0.0f);
        check("and does not swing", (float)weapon.melee_attack, -1.0f, 0.0f);
    }
}

int main(void) {
    const WeaponSpec* spec = &WEAPON_SPEC_KATANA;
    const MeleeSpec* melee = weapon_spec_melee(spec);
    const MeleeAttack* attack = &melee->attacks[melee->light_attack];
    float live = (attack->active_from + attack->active_to) * 0.5f;

    /* A body a metre and a half in front of the origin, which the blade
       reaches: the katana's tip is 0.907m out and the body is 0.6m from the
       swing's own path at its closest. */
    CombatantSet set;

    printf("one body, one swing\n");
    {
        one_body(&set, 0.0f, -0.6f);
        MeleeSwing swing;
        melee_swing_begin(&swing, melee->light_attack);
        /* The blade crosses the body and then keeps going, three frames of
           active window. Without a hit list this pays three times. */
        int hits = 0;
        Mat4 prev = at(-0.4f, 0.0f);
        for (int f = 0; f < 3; ++f) {
            Mat4 now = at(-0.4f + 0.4f * (f + 1), 0.0f);
            hits += melee_swing_update(&swing, spec, &prev, &now, live, &set);
            prev = now;
        }
        check("one body, one swing", (float)hits, 1.0f, 0.0f);
    }

    printf("\nnothing lands outside the window\n");
    {
        one_body(&set, 0.0f, -0.6f);
        MeleeSwing swing;
        melee_swing_begin(&swing, melee->light_attack);
        Mat4 prev = at(-0.4f, 0.0f);
        Mat4 now = at(0.4f, 0.0f);
        /* Halfway through the wind-up: the blade sweeps the same space and is
           not cutting with it. */
        float early = attack->active_from * 0.5f;
        int hits = melee_swing_update(&swing, spec, &prev, &now, early, &set);
        check("hits before active_from", (float)hits, 0.0f, 0.0f);

        /* And after the follow-through. */
        float late = attack->active_to + (1.0f - attack->active_to) * 0.5f;
        hits = melee_swing_update(&swing, spec, &prev, &now, late, &set);
        check("hits after active_to", (float)hits, 0.0f, 0.0f);
    }

    printf("\na fast swing does not tunnel\n");
    {
        one_body(&set, 0.0f, -0.6f);
        MeleeSwing swing;
        melee_swing_begin(&swing, melee->light_attack);
        /* Both ENDS clear of the body, which is what makes this a real check
           rather than one that passes because the blade happened to stop
           inside somebody. COMBAT_BODY_RADIUS on this rig is 0.405m - the rig
           is 2.53m tall - so a blade at x +-0.5 misses at both ends and only
           the steps between them can connect. Tested only where the blade
           ends up, this finds nothing at all. */
        Mat4 prev = at(-0.5f, 0.0f);
        Mat4 now = at(0.5f, 0.0f);
        int hits = melee_swing_update(&swing, spec, &prev, &now, live, &set);
        check("fast swing past a body", (float)hits, 1.0f, 0.0f);
    }

    printf("\nand it costs the body something\n");
    {
        one_body(&set, 0.0f, -0.6f);
        float before = set.items[0].health;
        MeleeSwing swing;
        melee_swing_begin(&swing, melee->light_attack);
        Mat4 prev = at(-0.4f, 0.0f);
        Mat4 now = at(0.4f, 0.0f);
        melee_swing_update(&swing, spec, &prev, &now, live, &set);
        float taken = before - set.items[0].health;
        /* Banded by region like every other hit, so the exact figure depends
           on where the blade crossed - what matters is that it is the spec's
           damage scaled by the attack and not some other number. */
        check("damage is at least the attack's own", taken,
              spec->damage * attack->damage_scale,
              spec->damage * attack->damage_scale);
        if (taken <= 0.0f) { ++failures; printf("  FAIL nothing was taken\n"); }
    }

    check_chain();

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}

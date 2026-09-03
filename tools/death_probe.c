/* Headless checks for health, death and respawn: that a pool empties into a
   state and not past it, that a body which is down is gone from the trace, that
   the count is the length it was tuned to, and that coming back leaves nothing
   running.

   `make deathprobe` from the project root, on the host compiler rather than the
   cross one - none of this needs a window, a GL context or Windows.

   It exists because none of it can be checked by looking at the game. Nothing
   in the world can shoot the player yet, so the only way this loop runs at all
   today is a debug key and this file; and every one of the failures it is
   guarding against is invisible rather than loud. A corpse that still stops
   rounds is cover the player cannot see. A regeneration that ignores death is a
   body that heals to full while lying down and stands up at the wrong time. A
   respawn that clears only what somebody remembered to clear is a player who
   comes back still wall running, and they will blame the movement code. */
#include "gameplay/arena.h"
#include "gameplay/combatant.h"
#include "gameplay/combat_tuning.h"
#include "gameplay/player_combatant.h"
#include "gameplay/player_controller.h"
#include "gameplay/player_internal.h"
#include "gameplay/spawn_select.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char* what, float got, float want, float tol) {
    int ok = fabsf(got - want) <= tol;
    if (!ok) ++failures;
    printf("  %s %-46s got %+.6f want %+.6f\n", ok ? "ok  " : "FAIL", what,
           got, want);
}

static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
}

/* The simulation's own step, so a count measured here is the count the game
   runs. Stated rather than included because main.c owns it and main.c has a
   window in it. */
#define STEP (1.0f / 120.0f)

/* One body on the ground at the origin, out of any set - which is the shape the
   player's own body has, and the shape every check below wants. */
static Combatant lone_body(float max_health) {
    Combatant body;
    memset(&body, 0, sizeof body);
    body.max_health = max_health;
    body.active = 1;
    combatant_respawn(&body, 0.0f, 0.0f, 0.0f, 0.0f);
    return body;
}

static void check_damage_and_death(void) {
    printf("damage and death\n");

    Combatant body = lone_body(100.0f);
    check("chest hit takes its face value",
          combatant_damage(&body, COMBATANT_REGION_CHEST, 25.0f), 25.0f, 1e-4f);
    check("head hit takes the multiplier",
          combatant_damage(&body, COMBATANT_REGION_HEAD, 25.0f),
          25.0f * COMBAT_DAMAGE_HEAD, 1e-4f);
    check("pool is what is left", body.health, 25.0f, 1e-4f);
    expect(body.alive, "a body with health left is still standing");

    /* The figure paid is clamped to what was there, so what the player reads is
       what came off rather than what the round was worth. */
    check("the killing blow pays only what was left",
          combatant_damage(&body, COMBATANT_REGION_CHEST, 500.0f), 25.0f, 1e-4f);
    expect(!body.alive, "an emptied pool takes the body down");
    check("health lands on zero, not below", body.health, 0.0f, 1e-4f);
    check("the count starts at the tuned delay", body.respawn_timer,
          COMBAT_RESPAWN_DELAY, 1e-4f);
    check("the flinch is cleared with the body", body.flinch, 0.0f, 1e-4f);

    check("a body already down takes nothing more",
          combatant_damage(&body, COMBATANT_REGION_HEAD, 500.0f), 0.0f, 1e-4f);
    check("and the count is not restarted by it", body.respawn_timer,
          COMBAT_RESPAWN_DELAY, 1e-4f);
}

static void check_no_regeneration_while_down(void) {
    printf("regeneration\n");

    Combatant body = lone_body(100.0f);
    combatant_damage(&body, COMBATANT_REGION_CHEST, 40.0f);
    /* Past the delay, so anything that was going to heal has had the chance. */
    for (int i = 0; i < (int)((COMBAT_REGEN_DELAY + 1.0f) / STEP); ++i)
        combatant_tick(&body, STEP);
    expect(body.health > 60.0f, "a standing body heals once it is left alone");

    combatant_damage(&body, COMBATANT_REGION_CHEST, 500.0f);
    float down_at = body.health;
    for (int i = 0; i < (int)((COMBAT_REGEN_DELAY + 1.0f) / STEP); ++i)
        combatant_tick(&body, STEP);
    check("a body that is down does not heal", body.health, down_at, 1e-4f);
    expect(!body.alive, "and does not stand back up on its own");
}

/* The count, measured the way the game runs it: a step at a time, watching for
   the one step that reports it has run out. */
static void check_respawn_timing(void) {
    printf("the count\n");

    Combatant body = lone_body(100.0f);
    combatant_damage(&body, COMBATANT_REGION_CHEST, 500.0f);

    int steps = 0;
    int reports = 0;
    int reported_at = -1;
    /* Two full delays' worth, so a second report would be seen. */
    int limit = (int)(COMBAT_RESPAWN_DELAY * 2.0f / STEP);
    for (int i = 0; i < limit; ++i) {
        ++steps;
        if (combatant_tick(&body, STEP)) {
            ++reports;
            if (reported_at < 0) reported_at = steps;
        }
    }

    expect(reports == 1, "the count runs out exactly once");
    check("and it runs out at the tuned delay", (float)reported_at * STEP,
          COMBAT_RESPAWN_DELAY, STEP * 1.5f);
    expect(!body.alive, "a body nobody respawns stays down");

    combatant_respawn(&body, 3.0f, 4.0f, 5.0f, 90.0f);
    expect(body.alive, "respawning stands it back up");
    check("with the whole pool", body.health, body.max_health, 1e-4f);
    check("no count left", body.respawn_timer, 0.0f, 1e-4f);
    check("no flinch carried over", body.flinch, 0.0f, 1e-4f);
    check("at the place it was given, x", body.x, 3.0f, 1e-4f);
    check("at the place it was given, y", body.y, 4.0f, 1e-4f);
    check("at the place it was given, z", body.z, 5.0f, 1e-4f);
    check("facing the way it was given", body.yaw_degrees, 90.0f, 1e-4f);
}

/* A round through the space a body is standing in, before and after it dies. */
static void check_dead_bodies_are_not_traced(void) {
    printf("the trace\n");

    CombatantSet set;
    combatant_set_init(&set);
    expect(set.count > 0, "the arena stands its bodies up");

    Combatant* target = &set.items[0];
    /* Down the +X axis into the middle of the chest, from four metres out and
       one body-radius clear of the cylinder so the ray does not begin inside
       it - which is a miss by design. */
    float chest = target->y + COMBAT_BODY_HEIGHT * 0.6f;
    float ox = target->x - 4.0f;

    CombatantHit hit;
    expect(combatant_raycast(&set, ox, chest, target->z, 1.0f, 0.0f, 0.0f,
                             8.0f, &hit), "a standing body is hit");
    expect(hit.index == 0, "and it is the body that was aimed at");

    combatant_damage(target, COMBATANT_REGION_CHEST, COMBAT_DUMMY_HEALTH * 4.0f);
    expect(!target->alive, "the same body, emptied, is down");
    expect(!combatant_raycast(&set, ox, chest, target->z, 1.0f, 0.0f, 0.0f,
                              8.0f, &hit),
           "rounds pass through it once it is down");

    /* And it comes back where the arena authored it, on its own, without
       anybody outside combatant.c naming a coordinate. */
    float authored_x = target->x, authored_z = target->z;
    for (int i = 0; i < (int)((COMBAT_RESPAWN_DELAY + 1.0f) / STEP); ++i)
        combatant_set_update(&set, 0.0f, 0.0f, STEP);
    expect(target->alive, "the set stands its own bodies back up");
    check("where it was authored, x", target->x, authored_x, 1e-4f);
    check("where it was authored, z", target->z, authored_z, 1e-4f);
    check("with the whole pool", target->health, target->max_health, 1e-4f);
}

/* The rule the selector is meant to follow, restated as arithmetic: the slot it
   returns is the one whose nearest live body is furthest away. */
static float clearance(const ArenaSpawn* spawn, const CombatantSet* set) {
    float nearest = INFINITY;
    for (int i = 0; i < set->count; ++i) {
        const Combatant* body = &set->items[i];
        if (!body->active || !body->alive) continue;
        float dx = body->x - spawn->x;
        float dz = body->z - spawn->z;
        float distance = dx * dx + dz * dz;
        if (distance < nearest) nearest = distance;
    }
    return nearest;
}

static void check_spawn_selection(void) {
    printf("spawn selection\n");

    CombatantSet set;
    combatant_set_init(&set);

    for (int team = 0; team < ARENA_TEAM_COUNT; ++team) {
        int count = 0;
        const ArenaSpawn* slots = arena_team_spawns((ArenaTeam)team, &count);
        expect(slots && count == ARENA_TEAM_SIZE,
               "the map publishes a full team's slots");

        ArenaSpawn chosen;
        expect(spawn_select((ArenaTeam)team, set.items, set.count, &chosen),
               "the selector answers");

        /* It has to be one the map published, not a coordinate of its own. */
        int is_a_published_slot = 0;
        float best = -1.0f;
        for (int i = 0; i < count; ++i) {
            float c = clearance(&slots[i], &set);
            if (c > best) best = c;
            if (fabsf(slots[i].x - chosen.x) < 1e-4f &&
                fabsf(slots[i].z - chosen.z) < 1e-4f)
                is_a_published_slot = 1;
        }
        expect(is_a_published_slot, "and it is one of the map's own slots");
        check("with the furthest nearest body", clearance(&chosen, &set), best,
              1e-3f);
    }

    /* A world with nothing live in it: every slot ties, and the first one - the
       one the map authored first - is what comes back. */
    for (int i = 0; i < set.count; ++i)
        combatant_damage(&set.items[i], COMBATANT_REGION_CHEST,
                         COMBAT_DUMMY_HEALTH * 4.0f);
    int count = 0;
    const ArenaSpawn* slots = arena_team_spawns(ARENA_TEAM_BASTION, &count);
    ArenaSpawn chosen;
    spawn_select(ARENA_TEAM_BASTION, set.items, set.count, &chosen);
    check("with nothing live, the first slot, x", chosen.x, slots[0].x, 1e-4f);
    check("with nothing live, the first slot, z", chosen.z, slots[0].z, 1e-4f);

    /* Dead bodies are not avoided. If they were, the selector would push the
       player away from the safest ground on the map. */
    ArenaSpawn none;
    spawn_select(ARENA_TEAM_BASTION, NULL, 0, &none);
    check("and no bodies at all is the same answer, x", none.x, chosen.x, 1e-4f);
    check("and no bodies at all is the same answer, z", none.z, chosen.z, 1e-4f);
}

/* The failure this is guarding against is a player who respawns still wall
   running, or on a slide's forward vector, or dragged by a grapple anchored
   across the map - and who blames the movement code for it. */
static void check_place_clears_everything(void) {
    printf("placing the player\n");

    PlayerController player;
    player_controller_init(&player);

    /* Every mechanic switched on at once. None of these is a reachable state
       together; that is the point - what is being checked is that placing the
       player clears them all rather than the ones somebody remembered. */
    player.is_wall_running = 1;
    player.wall_run_time = 3.0f;
    player.wall_run_speed = 22.0f;
    player.wall_run_wall_index = 4;
    player.wall_run_side = -1;
    player.is_wall_climbing = 1;
    player.wall_climb_time = 1.0f;
    player.is_sliding = 1;
    player.slide_time = 2.0f;
    player.slide_forward_x = 1.0f;
    player.is_grappling = 1;
    player.grapple_length = 30.0f;
    player.is_mantling = 1;
    player.is_ledge_hanging = 1;
    player.mantle_time = 0.5f;
    player.velocity_x = 14.0f;
    player.velocity_z = -9.0f;
    player.vertical_velocity = 25.0f;
    player.air_jumps_used = 2;
    player.air_time = 6.0f;
    player.lean = 0.8f;
    player.lean_target = 1;

    player_controller_place(&player, 11.0f, 2.5f, -7.0f, 135.0f);

    check("placed, x", player.x, 11.0f, 1e-4f);
    check("placed, y", player.y, 2.5f, 1e-4f);
    check("placed, z", player.z, -7.0f, 1e-4f);
    check("facing", player.heading_degrees, 135.0f, 1e-4f);
    expect(!player.is_wall_running, "not wall running");
    expect(!player.is_wall_climbing, "not wall climbing");
    expect(!player.is_sliding, "not sliding");
    expect(!player.is_grappling, "not grappling");
    expect(!player.is_mantling, "not mantling");
    expect(!player.is_ledge_hanging, "not hanging off a ledge");
    check("no lateral velocity, x", player.velocity_x, 0.0f, 1e-4f);
    check("no lateral velocity, z", player.velocity_z, 0.0f, 1e-4f);
    check("no vertical velocity", player.vertical_velocity, 0.0f, 1e-4f);
    check("no air jumps spent", (float)player.air_jumps_used, 0.0f, 1e-4f);
    check("no lean held", player.lean, 0.0f, 1e-4f);
    check("no lean committed to", (float)player.lean_target, 0.0f, 1e-4f);
}

/* The seam between the two: the controller moves, the body follows, and health
   is never written from the controller's side. */
static void check_player_body_follows_the_controller(void) {
    printf("the player as a body\n");

    PlayerController player;
    player_controller_place(&player, 5.0f, 1.0f, -3.0f, 45.0f);

    Combatant body;
    memset(&body, 0, sizeof body);
    player_combatant_init(&body, &player);
    check("the player's own pool", body.max_health, COMBAT_PLAYER_HEALTH, 1e-4f);
    check("full on entry", body.health, COMBAT_PLAYER_HEALTH, 1e-4f);
    expect(body.alive && body.active, "standing and shootable");
    check("standing where the controller is, x", body.x, 5.0f, 1e-4f);
    check("standing where the controller is, z", body.z, -3.0f, 1e-4f);
    check("facing the way the controller does", body.yaw_degrees, 45.0f, 1e-4f);

    player.x = 20.0f;
    player.z = 8.0f;
    player.heading_degrees = -90.0f;
    player_combatant_fill(&body, &player);
    check("the body follows, x", body.x, 20.0f, 1e-4f);
    check("the body follows, z", body.z, 8.0f, 1e-4f);
    check("and the facing", body.yaw_degrees, -90.0f, 1e-4f);
    check("without touching the pool", body.health, COMBAT_PLAYER_HEALTH, 1e-4f);

    /* Four presses of the debug key is a death, which is what the tuning
       comment claims and what makes the key worth having. */
    int presses = 0;
    while (body.alive && presses < 32) {
        combatant_damage(&body, COMBATANT_REGION_CHEST, COMBAT_DEBUG_DAMAGE);
        ++presses;
    }
    check("the debug key kills in the stated number of presses",
          (float)presses, COMBAT_PLAYER_HEALTH / COMBAT_DEBUG_DAMAGE, 0.0f);

    /* A corpse stays where it fell rather than being dragged by a controller
       that is no longer being stepped. */
    player.x = 60.0f;
    player_combatant_fill(&body, &player);
    check("a body that is down is not moved", body.x, 20.0f, 1e-4f);
}

int main(void) {
    printf("death probe\n\n");
    check_damage_and_death();
    check_no_regeneration_while_down();
    check_respawn_timing();
    check_dead_bodies_are_not_traced();
    check_spawn_selection();
    check_place_clears_everything();
    check_player_body_follows_the_controller();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

#include "gameplay/combatant.h"
#include "gameplay/arena.h"
#include "utils/math_utils.h"

#include <math.h>
#include <string.h>

/* Calibration mannequins for the current map, as two coordinates and a facing.

   The height is not here on purpose. arena_support_height finds whatever is
   under each of these, so a body authored on a roof stands on the roof and a
   body authored in a channel stands on its floor, and neither entry has to
   restate a number the arena already owns. Move a tower and the body on it
   moves with it.

   They are placed by ROUTE rather than by spread, which is the only placement
   worth having in a game whose thesis is that shooting is expressed through
   movement. They sample the Gatehouse, Turbines, cross-link roofs and Spillway
   rather than pretending to be a PvE population. A ring of targets on flat
   ground would test aim and nothing this game is about. */
typedef struct {
    float x, z;
    float yaw_degrees;
} CombatantSpawn;

static const CombatantSpawn COMBATANT_SPAWNS[] = {
    /* Ground read on the Bastion approach. */
    {   0.0f, -48.0f,   0.0f},
    /* Gatehouse floor and its two roof transitions. */
    { -18.0f,  -7.0f,  90.0f},
    { -10.0f,   0.0f, 270.0f},
    {  10.0f,   0.0f,  90.0f},
    /* Turbine route: floor exit and the low outer plant roof. */
    { -52.0f, -18.0f, 180.0f},
    { -68.0f,  17.0f,  90.0f},
    /* Cross-link roof and inside the southern spillway. */
    { -30.0f,  26.0f, 135.0f},
    {  45.0f,  32.0f, 180.0f},
};

#define COMBATANT_SPAWN_COUNT \
    ((int)(sizeof COMBATANT_SPAWNS / sizeof COMBATANT_SPAWNS[0]))

_Static_assert(COMBATANT_SPAWN_COUNT <= COMBATANT_MAX,
               "COMBATANT_MAX must be at least the number of spawns");

/* Into (-180, 180]. Local because the two other copies in this codebase are
   both inside renderers and this must not link one to turn a body around. */
static float wrap_degrees(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

float combatant_region_multiplier(int region) {
    switch (region) {
    case COMBATANT_REGION_HEAD: return COMBAT_DAMAGE_HEAD;
    case COMBATANT_REGION_LEGS: return COMBAT_DAMAGE_LEGS;
    default: return COMBAT_DAMAGE_CHEST;
    }
}

float combatant_health_fraction(const Combatant* combatant) {
    if (!combatant || combatant->max_health <= 0.0f) return 0.0f;
    return clampf(combatant->health / combatant->max_health, 0.0f, 1.0f);
}

void combatant_set_init(CombatantSet* set) {
    if (!set) return;
    memset(set, 0, sizeof *set);
    damage_numbers_init(&set->numbers);

    for (int i = 0; i < COMBATANT_SPAWN_COUNT; ++i) {
        const CombatantSpawn* spawn = &COMBATANT_SPAWNS[i];
        Combatant* combatant = &set->items[i];
        combatant->x = spawn->x;
        combatant->z = spawn->z;
        /* Whatever is under it, with no ceiling on how high that may be: a body
           authored on a 14m roof is meant to be on the roof, and a max_height
           of anything less would quietly drop it to the street. The disc is the
           hitbox's own radius, so a body on the lip of a roof is supported by
           the same overlap the player would be. */
        combatant->y = arena_support_height(spawn->x, spawn->z,
                                            COMBAT_BODY_RADIUS, 1.0e9f);
        combatant->max_health = COMBAT_DUMMY_HEALTH;
        combatant->active = 1;
        combatant_respawn(combatant, combatant->x, combatant->y, combatant->z,
                          spawn->yaw_degrees);
    }
    set->count = COMBATANT_SPAWN_COUNT;
}

int combatant_tick(Combatant* combatant, float delta_time) {
    if (!combatant || !combatant->active || delta_time <= 0.0f) return 0;

    combatant->flinch = approachf(combatant->flinch, 0.0f,
                                  COMBAT_FLINCH_DECAY * delta_time);

    if (!combatant->alive) {
        /* Already at or below zero means the count has run out and nobody has
           put this body anywhere. It stays down and the caller is not told a
           second time - one crossing, one report. */
        if (combatant->respawn_timer <= 0.0f) return 0;
        combatant->respawn_timer -= delta_time;
        return combatant->respawn_timer <= 0.0f;
    }

    combatant->quiet_time += delta_time;
    if (combatant->quiet_time >= COMBAT_REGEN_DELAY &&
        combatant->health < combatant->max_health) {
        combatant->health = fminf(combatant->max_health,
                                  combatant->health +
                                  COMBAT_REGEN_RATE * delta_time);
    }
    return 0;
}

void combatant_respawn(Combatant* combatant, float x, float y, float z,
                       float yaw_degrees) {
    if (!combatant) return;
    combatant->x = x;
    combatant->y = y;
    combatant->z = z;
    combatant->yaw_degrees = yaw_degrees;
    combatant->health = combatant->max_health;
    /* Full and quiet. Starting the regeneration clock already elapsed costs
       nothing at full health and is what stops a body that respawns mid-fight
       from having a delay it did not earn. */
    combatant->quiet_time = COMBAT_REGEN_DELAY;
    combatant->respawn_timer = 0.0f;
    combatant->flinch = 0.0f;
    combatant->alive = 1;
}

void combatant_set_update(CombatantSet* set, float player_x, float player_z,
                          float delta_time) {
    if (!set || delta_time <= 0.0f) return;

    for (int i = 0; i < set->count; ++i) {
        Combatant* combatant = &set->items[i];
        if (!combatant->active) continue;

        /* Face whoever is shooting. There is no decision in this - it is a
           facing and not a behaviour - but it is what makes the head and chest
           bands mean anything: a body that never turns can only be shot from
           the one angle it was authored at. */
        float to_x = player_x - combatant->x;
        float to_z = player_z - combatant->z;
        if (combatant->alive && to_x * to_x + to_z * to_z > 0.0001f) {
            float target = atan2f(to_x, to_z) / MATH_DEG_TO_RAD;
            float turn = wrap_degrees(target - combatant->yaw_degrees);
            if (fabsf(turn) > COMBAT_TURN_DEADZONE) {
                float step = COMBAT_TURN_RATE * delta_time;
                if (turn > step) turn = step;
                else if (turn < -step) turn = -step;
                combatant->yaw_degrees =
                    wrap_degrees(combatant->yaw_degrees + turn);
            }
        }

        if (combatant_tick(combatant, delta_time) &&
            i < COMBATANT_SPAWN_COUNT) {
            /* Back where it was authored. A mannequin's place in the map is the
               whole of what it is for - it samples one route from one angle -
               so anywhere else would quietly un-author the calibration. The
               height is found again rather than remembered, for the same reason
               it was found in the first place: move the roof it stands on and
               the body moves with it. */
            const CombatantSpawn* spawn = &COMBATANT_SPAWNS[i];
            float y = arena_support_height(spawn->x, spawn->z,
                                           COMBAT_BODY_RADIUS, 1.0e9f);
            combatant_respawn(combatant, spawn->x, y, spawn->z,
                              spawn->yaw_degrees);
        }
    }

    damage_numbers_update(&set->numbers, delta_time);
}

/* Ray against one body's cylinder: the infinite tube in xz intersected with the
   slab between its feet and its head. Written as one interval on each and their
   overlap, which is the same shape as a slab test against a box and gets the
   caps for free - a round coming down onto a body standing on a roof enters
   through the top of the cylinder and this finds it without a case of its own.

   Returns the distance along the ray to the entry point, or a negative number
   for a miss. A ray that starts inside the body is a miss, which is the rule
   traversal_raycast_walls follows and the reason a muzzle inside a body does
   not resolve against it. */
static float ray_body(float ox, float oy, float oz,
                      float dx, float dy, float dz,
                      float bx, float by, float bz,
                      float radius, float height, float max_distance,
                      int* radial) {
    const float epsilon = 1.0e-6f;
    float rx = ox - bx;
    float ry = oy - by;
    float rz = oz - bz;

    /* The tube. */
    float radial_near, radial_far;
    float a = dx * dx + dz * dz;
    if (a < epsilon) {
        /* Straight up or down: either the ray is inside the tube for its whole
           length or it never enters it. */
        if (rx * rx + rz * rz > radius * radius) return -1.0f;
        radial_near = -INFINITY;
        radial_far = INFINITY;
    } else {
        float b = 2.0f * (rx * dx + rz * dz);
        float c = rx * rx + rz * rz - radius * radius;
        float discriminant = b * b - 4.0f * a * c;
        if (discriminant < 0.0f) return -1.0f;
        float root = sqrtf(discriminant);
        radial_near = (-b - root) / (2.0f * a);
        radial_far = (-b + root) / (2.0f * a);
    }

    /* The slab between the feet and the top of the head. */
    float slab_near, slab_far;
    if (fabsf(dy) < epsilon) {
        if (ry < 0.0f || ry > height) return -1.0f;
        slab_near = -INFINITY;
        slab_far = INFINITY;
    } else {
        slab_near = -ry / dy;
        slab_far = (height - ry) / dy;
        if (slab_near > slab_far) {
            float swap = slab_near;
            slab_near = slab_far;
            slab_far = swap;
        }
    }

    float enter = radial_near > slab_near ? radial_near : slab_near;
    float exit = radial_far < slab_far ? radial_far : slab_far;
    if (enter > exit) return -1.0f;
    /* At or behind the origin means the ray began inside, or the body is
       behind the shooter. Neither is a hit. */
    if (enter <= 0.0f) return -1.0f;
    if (enter > max_distance) return -1.0f;

    /* Which surface it came in through, for the normal. The later of the two
       near bounds is the one that was crossed last, and so the one the entry
       point sits on. */
    if (radial) *radial = a >= epsilon && radial_near >= slab_near;
    return enter;
}

static int region_at(float height_above_feet) {
    float fraction = height_above_feet / COMBAT_BODY_HEIGHT;
    if (fraction >= COMBAT_HEAD_BOTTOM) return COMBATANT_REGION_HEAD;
    if (fraction >= COMBAT_LEGS_TOP) return COMBATANT_REGION_CHEST;
    return COMBATANT_REGION_LEGS;
}

int combatant_raycast(const CombatantSet* set,
                      float origin_x, float origin_y, float origin_z,
                      float direction_x, float direction_y, float direction_z,
                      float max_distance, CombatantHit* hit) {
    if (!set || !hit) return 0;

    int nearest = -1;
    int nearest_radial = 1;
    float nearest_distance = max_distance;

    for (int i = 0; i < set->count; ++i) {
        const Combatant* combatant = &set->items[i];
        /* Rounds pass through the dead. Without this a body lying where it fell
           is cover, and cover the player cannot see - the renderer has already
           stopped drawing it. */
        if (!combatant->active || !combatant->alive) continue;
        int radial = 1;
        float distance = ray_body(origin_x, origin_y, origin_z,
                                  direction_x, direction_y, direction_z,
                                  combatant->x, combatant->y, combatant->z,
                                  COMBAT_BODY_RADIUS, COMBAT_BODY_HEIGHT,
                                  nearest_distance, &radial);
        if (distance < 0.0f) continue;
        nearest = i;
        nearest_radial = radial;
        nearest_distance = distance;
    }

    if (nearest < 0) return 0;

    const Combatant* combatant = &set->items[nearest];
    hit->index = nearest;
    hit->distance = nearest_distance;
    hit->x = origin_x + direction_x * nearest_distance;
    hit->y = origin_y + direction_y * nearest_distance;
    hit->z = origin_z + direction_z * nearest_distance;
    hit->region = region_at(hit->y - combatant->y);

    if (nearest_radial) {
        float nx = hit->x - combatant->x;
        float nz = hit->z - combatant->z;
        float length = sqrtf(nx * nx + nz * nz);
        if (length < 1.0e-5f) { nx = 0.0f; nz = 1.0f; length = 1.0f; }
        hit->normal_x = nx / length;
        hit->normal_y = 0.0f;
        hit->normal_z = nz / length;
    } else {
        hit->normal_x = 0.0f;
        hit->normal_y = direction_y > 0.0f ? -1.0f : 1.0f;
        hit->normal_z = 0.0f;
    }
    return 1;
}

float combatant_damage(Combatant* combatant, int region, float damage) {
    if (!combatant || !combatant->active || !combatant->alive) return 0.0f;

    float taken = damage * combatant_region_multiplier(region);
    /* Never more than is left, so the figure the player reads is what came off
       the pool rather than what the round was worth. */
    if (taken > combatant->health) taken = combatant->health;
    if (taken <= 0.0f) return 0.0f;
    combatant->health -= taken;
    combatant->quiet_time = 0.0f;

    if (combatant->health <= 0.0f) {
        combatant->health = 0.0f;
        combatant->alive = 0;
        combatant->respawn_timer = COMBAT_RESPAWN_DELAY;
        /* A body that is not drawn cannot be seen flinching, and leaving the
           rock on would have it inherited whole by whoever stands up next. */
        combatant->flinch = 0.0f;
    }
    return taken;
}

float combatant_apply_damage(CombatantSet* set, const CombatantHit* hit,
                             float damage) {
    if (!set || !hit) return 0.0f;
    if (hit->index < 0 || hit->index >= set->count) return 0.0f;

    Combatant* combatant = &set->items[hit->index];
    float taken = combatant_damage(combatant, hit->region, damage);

    /* Rocked AWAY from the round. The hit normal points back out of the body
       at whoever fired, and the rig's own left is up cross forward, which for a
       body facing +Z at zero heading is (cos yaw, 0, -sin yaw) - so this is how
       much of the shot arrived on the body's left side. Negated, because a
       round from the left throws the body to its right, and the rig reads
       positive lean as a bend to its own left.

       Straight down the cylinder's cap this comes out at zero, which is right:
       a shot from directly above does not rock anybody sideways. */
    if (combatant->alive) {
        float from_left =
            hit->normal_x * cosf(combatant->yaw_degrees * MATH_DEG_TO_RAD) -
            hit->normal_z * sinf(combatant->yaw_degrees * MATH_DEG_TO_RAD);
        combatant->flinch = -clampf(from_left, -1.0f, 1.0f) * COMBAT_FLINCH_AMOUNT;
    }

    /* No figure for a round that took nothing off, which is what every round
       into a drained body does. The impact mark still lands and the pool bar is
       still empty, so the player can see they connected - what they are not
       shown is a number that would sit there being re-popped by every round for
       as long as the trigger was held, reporting a hit that did nothing. The
       hit marker rides the same figure, so it goes quiet with it. */
    if (taken > 0.0f)
        damage_numbers_add(&set->numbers, hit->index, hit->region, taken,
                           hit->x, hit->y, hit->z);
    return taken;
}

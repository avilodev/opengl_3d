/* The gun half of a weapon: the cone, the kick, the trace, the tracers, the
   magazine and the reload.

   Split out of weapon.c, which alternated this with the sword code down its
   whole length. Nothing here was rewritten - the arithmetic is what it was -
   and nothing here asks whether the weapon is a gun, because weapon_update
   answers that once before calling in. See gameplay/weapon_internal.h for the
   seam and why it is drawn where it is.

   Every number comes off the FirearmSpec the weapon was initialised with; what
   is here is the order things happen in, which is the same for every gun. */

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

/* Held weapon_firearm_fire, flat damage at any range, and a cone that opens only with
   sustained weapon_firearm_fire. Every number comes off the spec the weapon was initialised
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
    float view_fov = lerpf(VIEWMODEL_FOV, weapon_spec_view_ads_fov(spec), weapon->ads);
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

    /* And the neck the gun is worn on. This is the third of the three consumers
       render/viewmodel_neck.h names - the GL draw, the stencil cut for a
       magnified optic, and this - and it was the one still composing the
       rotation by hand, which is to say composing HALF of it: the lean's cant
       and nothing else. So the drawn gun turned about the skull on every look
       up or down and the streak went on leaving a barrel welded to the eye.

       The error is exactly zero at a level look, which is the one pitch a still
       frame is ever taken at, and better than half the width of the screen by
       the time the player is looking sixty degrees up. See the probe.

       Before the widening below, because the widening is what matches the gun's
       projection to the world's and the neck happens on the gun's side of it -
       and because a neck's pitch moves the muzzle in depth as well as across.

       The VIEW's pitch, not the aim's: a neck is worn by the head, and the
       recoil the aim carries is already in the pose above as kick_pitch. The
       caller adds the two together to point the round - see weapon_aim_angles -
       so taking the gun's own kick back off is what recovers where the head is
       actually looking. */
    ViewmodelNeck neck;
    viewmodel_neck_build(&neck, weapon->spec,
                         pitch_degrees - weapon->gun_pitch_degrees,
                         player->lean, weapon->ads);
    viewmodel_neck_transform_point(&neck, tip, tip);

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
    float gun_fov = lerpf(VIEWMODEL_FOV, weapon_spec_view_ads_fov(weapon->spec),
                          weapon->ads);
    float gun_tangent = tanf(gun_fov * 0.5f * MATH_DEG_TO_RAD);
    if (gun_tangent > 0.0001f) {
        float widen = world_tangent / gun_tangent;
        ox *= widen;
        oy *= widen;
    }

    /* The lean's cant used to be applied here, by hand, as the second of what
       viewmodel_neck.h calls the three copies. It is inside the neck now, with
       the pitch and the yaw, which is the whole point of that module: one
       rotation, built once, and no way for the three of them to drift apart. */

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
void weapon_firearm_drop_magazine(Weapon* weapon, const PlayerController* player,
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

void weapon_firearm_advance_dropped_mags(Weapon* weapon, float delta_time) {
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

void weapon_firearm_fire(Weapon* weapon, const PlayerController* player,
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
int weapon_firearm_begin_reload(Weapon* weapon) {
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

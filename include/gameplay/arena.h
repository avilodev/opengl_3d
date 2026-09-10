#ifndef ARENA_H
#define ARENA_H

#define ARENA_HALF_SIZE 105.0f
#define ARENA_WALL_COUNT 283
#define ARENA_TEAM_SIZE 6

/* Open sky: no box overhead. Far above anything the movement code can reach. */
#define ARENA_NO_CEILING 1.0e9f

typedef enum {
    ARENA_MATERIAL_NEUTRAL = 0,
    ARENA_MATERIAL_BASTION,
    ARENA_MATERIAL_AXIOM,
    ARENA_MATERIAL_WATER,
    ARENA_MATERIAL_HAZARD
} ArenaMaterial;

typedef enum {
    ARENA_TEAM_BASTION = 0,
    ARENA_TEAM_AXIOM,
    ARENA_TEAM_COUNT
} ArenaTeam;

typedef struct {
    float x, y, z;
    float yaw_degrees;
} ArenaSpawn;

/* An axis-aligned box spanning `base`..`top` on the vertical axis. Its top face
   is solid, so every box is both an obstacle and a platform.

   `base` is what makes a room a room. A box resting on the ground has base 0
   and behaves exactly as it always did; one with base above 0 has air beneath
   it, which is a roof, a catwalk, an overpass, or the lintel over a doorway.
   The player passes under it and stands on it, and both of those are the same
   two numbers read by different code.

   It is base/top rather than base/height on purpose: an author writing a floor
   slab at six metres thinks in the elevations it sits between, not in a
   thickness, and the rename made the compiler point at all twenty-three places
   the old field was read. */
typedef struct {
    float x;
    float z;
    float width;
    float depth;
    float base;
    float top;
    ArenaMaterial material;
} ArenaWall;

/* Shared by rendering, movement, and raycasting so visible geometry is also
   solid and solid geometry is also visible. */
extern const ArenaWall g_arena_walls[ARENA_WALL_COUNT];

const ArenaSpawn* arena_team_spawns(ArenaTeam team, int* count);

/* Highest surface under a disc of `radius` at (x, z), considering only tops at
   or below `max_height`. Returns 0 (the ground plane) when nothing qualifies. */
float arena_support_height(float x, float z, float radius, float max_height);

/* The mirror of the above: the lowest UNDERSIDE over a disc of `radius` at
   (x, z), considering only bases at or above `min_height`. Returns
   ARENA_NO_CEILING when nothing is overhead, which is most of the map.

   Without this a player jumps through the floor above them, so it is what
   makes a second storey a storey rather than a decal. */
float arena_ceiling_height(float x, float z, float radius, float min_height);

#endif

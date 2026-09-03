#ifndef ARENA_H
#define ARENA_H

#define ARENA_HALF_SIZE 75.0f
#define ARENA_WALL_COUNT 66
#define ARENA_TEAM_SIZE 6

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

/* An axis-aligned box standing on the ground plane. Its top face is solid, so
   every box is both an obstacle and a platform. */
typedef struct {
    float x;
    float z;
    float width;
    float depth;
    float height;
    ArenaMaterial material;
} ArenaWall;

/* Shared by rendering, movement, and raycasting so visible geometry is also
   solid and solid geometry is also visible. */
extern const ArenaWall g_arena_walls[ARENA_WALL_COUNT];

const ArenaSpawn* arena_team_spawns(ArenaTeam team, int* count);

/* Highest surface under a disc of `radius` at (x, z), considering only tops at
   or below `max_height`. Returns 0 (the ground plane) when nothing qualifies. */
float arena_support_height(float x, float z, float radius, float max_height);

#endif

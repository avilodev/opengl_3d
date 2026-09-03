#include "gameplay/arena.h"
#include "utils/math_utils.h"

/* FLOODGATE K-12, VAROS CORRIDOR

   A flood-control station grew into a freight interchange, then became a
   battlefield when Axiom installed an emergency biological-filtration annex
   opposite Bastion's armored railhead. The compact 6v6 footprint has three
   routes: close wall-jump turbine rooms west, a readable gatehouse fight in
   mid, and a slide/wall-run/grapple spillway east.

   The plan is rotationally fair: equal distances and route access after a
   180-degree turn, but distinct materials and silhouettes. The full design is
   in story/maps/FLOODGATE_K12.md. */
const ArenaWall g_arena_walls[ARENA_WALL_COUNT] = {
    /* Perimeter flood walls; their tops form a risky traversal circuit. */
    {-75, 0, 1,150,14,ARENA_MATERIAL_NEUTRAL},
    { 75, 0, 1,150,14,ARENA_MATERIAL_NEUTRAL},
    {0,-75,150,1,14,ARENA_MATERIAL_BASTION},
    {0, 75,150,1,14,ARENA_MATERIAL_AXIOM},

    /* Bastion railhead, north entry. */
    {0,-65,30,16,.20f,ARENA_MATERIAL_BASTION},
    {0,-71,34,6,7,ARENA_MATERIAL_BASTION},
    {-17,-62,1,20,6,ARENA_MATERIAL_BASTION},
    { 17,-62,1,20,6,ARENA_MATERIAL_BASTION},
    {0,-55,10,3,3,ARENA_MATERIAL_BASTION},

    /* Axiom triage annex, the rotated combat footprint. */
    {0,65,30,16,.20f,ARENA_MATERIAL_AXIOM},
    {0,71,34,6,7,ARENA_MATERIAL_AXIOM},
    { 17,62,1,20,6,ARENA_MATERIAL_AXIOM},
    {-17,62,1,20,6,ARENA_MATERIAL_AXIOM},
    {0,55,10,3,3,ARENA_MATERIAL_AXIOM},

    /* Gatehouse: compact middle, four exposed faces on the high control block. */
    {-10,0,9,17,5,ARENA_MATERIAL_NEUTRAL},
    { 10,0,9,17,5,ARENA_MATERIAL_NEUTRAL},
    {0,0,5,8,9,ARENA_MATERIAL_HAZARD},
    {-10,-11,5,3,2.2f,ARENA_MATERIAL_NEUTRAL},
    { 10, 11,5,3,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-19,-7,7,1,1.3f,ARENA_MATERIAL_NEUTRAL},
    { 19, 7,7,1,1.3f,ARENA_MATERIAL_NEUTRAL},
    {-4,13,1,7,1.7f,ARENA_MATERIAL_NEUTRAL},
    { 4,-13,1,7,1.7f,ARENA_MATERIAL_NEUTRAL},
    {0,0,29,26,.08f,ARENA_MATERIAL_WATER},

    /* Turbines: broken five-metre wall-jump shafts and lateral run surfaces. */
    {-52,-42,30,1,11,ARENA_MATERIAL_NEUTRAL},
    {-52,-36,22,1,11,ARENA_MATERIAL_NEUTRAL},
    {-61,-36,1,13,11,ARENA_MATERIAL_NEUTRAL},
    {-43,-36,1,13,11,ARENA_MATERIAL_NEUTRAL},
    {-52,0,38,1,13,ARENA_MATERIAL_NEUTRAL},
    {-52,6,28,1,13,ARENA_MATERIAL_NEUTRAL},
    {-52,36,22,1,11,ARENA_MATERIAL_NEUTRAL},
    {-52,42,30,1,11,ARENA_MATERIAL_NEUTRAL},
    {-61,36,1,13,11,ARENA_MATERIAL_NEUTRAL},
    {-43,36,1,13,11,ARENA_MATERIAL_NEUTRAL},
    {-68,-17,10,12,4,ARENA_MATERIAL_BASTION},
    {-68, 17,10,12,4,ARENA_MATERIAL_AXIOM},

    /* Spillway: twelve-metre channel, low slide divider, grapple pylons. */
    {39,-43,1,39,7,ARENA_MATERIAL_NEUTRAL},
    {51,-43,1,39,7,ARENA_MATERIAL_NEUTRAL},
    {39, 43,1,39,7,ARENA_MATERIAL_NEUTRAL},
    {51, 43,1,39,7,ARENA_MATERIAL_NEUTRAL},
    {45,0,1,52,.45f,ARENA_MATERIAL_WATER},
    {45,-28,4,4,21,ARENA_MATERIAL_BASTION},
    {45,  0,4,4,25,ARENA_MATERIAL_HAZARD},
    {45, 28,4,4,21,ARENA_MATERIAL_AXIOM},
    {45,-12,13,4,5,ARENA_MATERIAL_NEUTRAL},
    {45, 12,13,4,5,ARENA_MATERIAL_NEUTRAL},

    /* Cross-links: low mantles near mid and faster roofs farther out. */
    {-30,-26,12,12,4.5f,ARENA_MATERIAL_BASTION},
    { 30, 26,12,12,4.5f,ARENA_MATERIAL_AXIOM},
    { 30,-26,12,12,6.5f,ARENA_MATERIAL_BASTION},
    {-30, 26,12,12,6.5f,ARENA_MATERIAL_AXIOM},
    {-27,-7,4,8,2,ARENA_MATERIAL_NEUTRAL},
    { 27, 7,4,8,2,ARENA_MATERIAL_NEUTRAL},
    { 27,-11,3,3,14,ARENA_MATERIAL_NEUTRAL},
    {-27, 11,3,3,14,ARENA_MATERIAL_NEUTRAL},
    {-24,-45,1,15,6,ARENA_MATERIAL_BASTION},
    { 24, 45,1,15,6,ARENA_MATERIAL_AXIOM},

    /* Staggered approach cover removes the base-to-base rifle lane. */
    {-10,-43,8,5,2.8f,ARENA_MATERIAL_BASTION},
    { 11,-39,6,8,4,ARENA_MATERIAL_BASTION},
    { 10, 43,8,5,2.8f,ARENA_MATERIAL_AXIOM},
    {-11, 39,6,8,4,ARENA_MATERIAL_AXIOM},

    /* Landmarks: flood sirens, neutral gauge towers, and gate blades. */
    {-22,-55,3,3,18,ARENA_MATERIAL_BASTION},
    { 22, 55,3,3,18,ARENA_MATERIAL_AXIOM},
    { 22,-18,2,2,17,ARENA_MATERIAL_NEUTRAL},
    {-22, 18,2,2,17,ARENA_MATERIAL_NEUTRAL},
    {0,-24,18,1,6,ARENA_MATERIAL_NEUTRAL},
    {0, 24,18,1,6,ARENA_MATERIAL_NEUTRAL},
};

_Static_assert(sizeof(g_arena_walls) / sizeof(g_arena_walls[0]) ==
               ARENA_WALL_COUNT,
               "ARENA_WALL_COUNT must match the number of walls defined here");

static const ArenaSpawn TEAM_SPAWNS[ARENA_TEAM_COUNT][ARENA_TEAM_SIZE] = {
    {
        {-10,.20f,-66,180}, {-6,.20f,-64,180}, {-2,.20f,-66,180},
        {  2,.20f,-64,180}, { 6,.20f,-66,180}, {10,.20f,-64,180},
    },
    {
        { 10,.20f,66,0}, { 6,.20f,64,0}, { 2,.20f,66,0},
        { -2,.20f,64,0}, {-6,.20f,66,0}, {-10,.20f,64,0},
    },
};

const ArenaSpawn* arena_team_spawns(ArenaTeam team, int* count) {
    if (count) *count = 0;
    if (team < 0 || team >= ARENA_TEAM_COUNT) return 0;
    if (count) *count = ARENA_TEAM_SIZE;
    return TEAM_SPAWNS[team];
}

float arena_support_height(float x, float z, float radius, float max_height) {
    float best = 0.0f;
    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        if (wall->height > max_height || wall->height <= best) continue;
        float hw = wall->width * .5f, hd = wall->depth * .5f;
        float nx = clampf(x, wall->x - hw, wall->x + hw);
        float nz = clampf(z, wall->z - hd, wall->z + hd);
        float dx = x - nx, dz = z - nz;
        if (dx * dx + dz * dz >= radius * radius) continue;
        best = wall->height;
    }
    return best;
}

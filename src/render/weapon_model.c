#include "render/weapon_model.h"
#include "render/static_model.h"

#include <stdio.h>
#include <string.h>

/* Loaded on the first frame that draws one rather than at startup, the same
   bargain the character rig makes: the renderer keeps its habit of owning no
   init step, and a missing or unreadable file costs the look rather than the
   session, because every caller still has boxes to fall back to. */

enum { MODEL_UNTRIED, MODEL_LOADED, MODEL_MISSING };

/* A cache keyed by the path itself rather than a table with one enum per mesh.
   An optic states its own file on its SightSpec, so adding the next sight is a
   line in sights.c and a glb - not a line here, a case in a switch, and a new
   enumerator, which is what it used to cost. */
#define WEAPON_MESH_SLOTS 8

/* The mesh the rifle slot draws, in one place so swapping what is in the
   player's hands is one line. Pointed at the katana while it is being looked
   at; the carbine is a comment away. */
#define RIFLE_MESH "models/Swords/Katana/katana.glb"
/* #define RIFLE_MESH "models/Guns/M4/m4_carbine.glb" */

typedef struct {
    const char* path;                /* null while the slot is free */
    StaticModel model;
    int state;
} WeaponMesh;

static WeaponMesh g_meshes[WEAPON_MESH_SLOTS];

static const StaticModel* mesh(const char* path) {
    if (!path) return NULL;

    WeaponMesh* entry = NULL;
    for (int i = 0; i < WEAPON_MESH_SLOTS; ++i) {
        if (g_meshes[i].path && strcmp(g_meshes[i].path, path) == 0) {
            entry = &g_meshes[i];
            break;
        }
        if (!g_meshes[i].path && !entry) entry = &g_meshes[i];
    }
    /* Every slot taken by a different mesh. Costs the look of one optic rather
       than trampling another's, which is the same bargain as a missing file. */
    if (!entry) return NULL;

    if (!entry->path) {
        entry->path = path;
        entry->state = MODEL_UNTRIED;
    }

    if (entry->state == MODEL_UNTRIED) {
        char error[256];
        if (static_model_load_glb(&entry->model, path, error, sizeof error)) {
            static_model_upload(&entry->model);
            entry->state = MODEL_LOADED;
            int triangles = 0;
            for (int i = 0; i < entry->model.mesh_count; ++i)
                triangles += entry->model.meshes[i].index_count / 3;
            printf("%s: %d triangles across %d materials, %.3fm long\n",
                   path, triangles, entry->model.mesh_count,
                   entry->model.max[2] - entry->model.min[2]);
            /* The parts list, so a spec naming one to leave off has something
               to check itself against. An asset that comes back from a
               re-export with its materials renamed is otherwise silent: the
               name matches nothing, everything draws, and the gun simply grows
               its furniture back. */
            for (int i = 0; i < entry->model.mesh_count; ++i)
                printf("    part '%s': %d triangles\n",
                       entry->model.meshes[i].material,
                       entry->model.meshes[i].index_count / 3);
        } else {
            entry->state = MODEL_MISSING;
            printf("model not loaded (%s): %s\n", path, error);
        }
        /* Same reasoning as the startup banner in main: a diagnostic still
           sitting in the buffer when something crashes is no diagnostic. */
        fflush(stdout);
    }
    return entry->state == MODEL_LOADED ? &entry->model : NULL;
}

void weapon_model_shutdown(void) {
    for (int i = 0; i < WEAPON_MESH_SLOTS; ++i) {
        if (g_meshes[i].state == MODEL_LOADED)
            static_model_free(&g_meshes[i].model);
        g_meshes[i].path = NULL;
        g_meshes[i].state = MODEL_UNTRIED;
    }
}

int weapon_model_draw(WeaponModelKind kind, const char* const* hidden_parts) {
    /* The sidearm has no mesh yet. It keeps the boxes, which is why the
       fallback is a return value rather than a silent nothing. */
    if (kind != WEAPON_MODEL_RIFLE) return 0;
    const StaticModel* model = mesh(RIFLE_MESH);
    if (!model) return 0;
    static_model_draw_except(model, hidden_parts);
    return 1;
}

int weapon_model_draw_only(WeaponModelKind kind, const char* const* parts) {
    if (kind != WEAPON_MODEL_RIFLE) return 0;
    const StaticModel* model = mesh(RIFLE_MESH);
    if (!model) return 0;
    static_model_draw_only(model, parts);
    return 1;
}

int weapon_model_draw_sight(const SightSpec* sight) {
    const StaticModel* model = sight ? mesh(sight->mesh_path) : NULL;
    if (!model) return 0;
    static_model_draw(model);
    return 1;
}

int weapon_model_sight_glass(const SightSpec* sight) {
    const StaticModel* model = sight ? mesh(sight->mesh_path) : NULL;
    if (!model) return 0;
    return static_model_draw_translucent(model);
}

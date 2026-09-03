#include "render/character_library.h"

#include <stdio.h>
#include <string.h>

/* Loading rigs and handing them out. Everything here is bookkeeping around
   skinned_model_load_glb; the interesting part is what it does not do, which
   is decide anything about a character. A rig is a mesh that can hold a pose,
   and which pose is character_animation's business. */

enum { RIG_UNTRIED, RIG_LOADED, RIG_MISSING };

/* Copies a path into a fixed field, refusing one that would not fit rather
   than storing a truncated path that fails to open with a confusing name. */
static int copy_path(char* out, const char* in) {
    if (!in) { out[0] = '\0'; return 1; }
    size_t length = strlen(in);
    if (length >= CHARACTER_PATH_MAX) return 0;
    memcpy(out, in, length + 1);
    return 1;
}

int character_library_add(CharacterLibrary* library, const char* model_path,
                          const char* clips_path) {
    if (!library || !model_path || library->rig_count >= CHARACTER_MAX_RIGS)
        return CHARACTER_RIG_NONE;

    CharacterRig* rig = &library->rigs[library->rig_count];
    memset(rig, 0, sizeof *rig);
    if (!copy_path(rig->model_path, model_path) ||
        !copy_path(rig->clips_path, clips_path))
        return CHARACTER_RIG_NONE;
    rig->state = RIG_UNTRIED;
    return library->rig_count++;
}

/* Bounds-checked lookup, so an instance carrying a stale or never-set rig
   index draws the box fallback rather than reading past the array. */
static CharacterRig* find(CharacterLibrary* library, int index) {
    if (!library || index < 0 || index >= library->rig_count) return NULL;
    return &library->rigs[index];
}

SkinnedModel* character_library_load(CharacterLibrary* library, int index) {
    CharacterRig* rig = find(library, index);
    if (!rig) return NULL;

    if (rig->state == RIG_UNTRIED) {
        char error[256];
        if (skinned_model_load_glb(&rig->model, rig->model_path, error, sizeof error)) {
            rig->state = RIG_LOADED;
            /* Clips from a second library are a bonus, not a requirement:
               without them the slide and climb roles simply find nothing and
               fall back to the bind pose for those states. */
            if (rig->clips_path[0] &&
                !skinned_model_add_clips(&rig->model, rig->clips_path,
                                         error, sizeof error))
                printf("Extra animation clips not loaded (%s): %s\n",
                       rig->clips_path, error);
            /* The caller reached here from an update, which runs with a
               current context - which is what upload needs. */
            skinned_model_upload(&rig->model);
            printf("Character rig %d: %d clips on %d bones, %d meshes (%s)\n",
                   index, rig->model.clip_count, rig->model.skeleton.joint_count,
                   rig->model.mesh_count, rig->model_path);
        } else {
            rig->state = RIG_MISSING;
            printf("Character model not loaded (%s): %s\n", rig->model_path, error);
        }
        /* Same reasoning as the startup banner in main: a diagnostic that is
           still sitting in the buffer when something crashes is no
           diagnostic. */
        fflush(stdout);
    }
    return rig->state == RIG_LOADED ? &rig->model : NULL;
}

const SkinnedModel* character_library_model(const CharacterLibrary* library, int index) {
    if (!library || index < 0 || index >= library->rig_count) return NULL;
    const CharacterRig* rig = &library->rigs[index];
    return rig->state == RIG_LOADED ? &rig->model : NULL;
}

void character_library_free(CharacterLibrary* library) {
    if (!library) return;
    for (int i = 0; i < library->rig_count; ++i)
        if (library->rigs[i].state == RIG_LOADED)
            skinned_model_free(&library->rigs[i].model);
    memset(library, 0, sizeof *library);
}

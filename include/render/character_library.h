#ifndef RENDER_CHARACTER_LIBRARY_H
#define RENDER_CHARACTER_LIBRARY_H

#include "render/skinned_model.h"

/* The rigs a world has bodies wearing, and who owns them.

   A rig - mesh, skeleton, clips, textures - is read-only from the moment it
   finishes loading, so every body wearing it shares one copy. That was already
   true; what was not is that there could be more than one of them. The rig
   used to be a file-static SkinnedModel inside character_renderer with two
   hardcoded paths, which meant one character type per process, a load nobody
   asked for and nobody could unload, and a shutdown that reached into a static
   to free it.

   This is that same sharing with an owner. The caller holds the library, says
   which rigs are in it, and frees it; a CharacterInstance names the rig it
   wears by index, so a second character type is a second entry rather than a
   second import. Loading is still lazy, on the first update that needs a rig,
   because a rig nobody draws should not cost eight megabytes of file reading.

   Not thread safe, and not trying to be: one library is loaded from whichever
   thread runs the frame. */

#define CHARACTER_MAX_RIGS 4
#define CHARACTER_PATH_MAX 128

/* No rig. An instance holding this draws the box fallback. */
#define CHARACTER_RIG_NONE (-1)

/* The rig the player wears. The mesh, the skeleton and forty-three clips come
   out of the first file; the second is opened only for its clips, which its own
   copy of the same skeleton lets the rig play.

   The _RM files beside them bake root motion into every clip. These are the
   other two, because movement is the controller's to decide: a clip that walked
   the body forward by itself would fight it. */
#define CHARACTER_RIG_PLAYER_MODEL "models/animations/UAL1_Standard.glb"
#define CHARACTER_RIG_PLAYER_CLIPS "models/animations/UAL2_Standard.glb"

typedef struct {
    SkinnedModel model;
    char model_path[CHARACTER_PATH_MAX];
    /* The second file, opened for its clips alone. Empty for a rig that wants
       only what its own file carries. */
    char clips_path[CHARACTER_PATH_MAX];
    int state;
} CharacterRig;

typedef struct {
    CharacterRig rigs[CHARACTER_MAX_RIGS];
    int rig_count;
} CharacterLibrary;

/* Registers a rig and returns its index, or CHARACTER_RIG_NONE if the library
   is full or the paths do not fit. Reads no files: the load happens on the
   first update that draws a body wearing it, so a rig registered and never
   worn costs an entry.

   `clips_path` may be NULL. Zero-initialise the library before the first
   call. */
int character_library_add(CharacterLibrary* library, const char* model_path,
                          const char* clips_path);

/* The rig, loading it if this is the first ask. Needs a current GL context,
   since a freshly loaded rig uploads its textures and buffers. NULL if the
   index is not one this library handed out, or if the file would not load -
   which costs the look and not the session, because the caller still has the
   box fallback. */
SkinnedModel* character_library_load(CharacterLibrary* library, int rig);

/* The rig if it is already loaded, and NULL otherwise - this never reads a
   file. It is what the draw uses, which is the point: a draw that could load an
   asset is a draw that can stall a frame on disk, and taking a const library
   makes that a compile error rather than a rule to remember. */
const SkinnedModel* character_library_model(const CharacterLibrary* library, int rig);

/* Frees every loaded rig. Needs a current GL context, since a rig holds
   textures and buffers. Instances built against these rigs are not touched;
   free those first. */
void character_library_free(CharacterLibrary* library);

#endif

#include "gameplay/weapon_placement.h"
#include "gameplay/weapon_registry.h"

#include <stdio.h>
#include <string.h>

#ifndef WEAPON_TUNING

/* Without the overlay this file is four empty answers. Declared in the header
   either way so that no caller needs an #ifdef of its own - the calls are
   still there in a release build, they simply do nothing, and the compiler
   removes them. A header that has to be guarded at every use is a header that
   gets used wrong once. */
int weapon_placement_load(WeaponSpec* spec) { (void)spec; return 0; }
int weapon_placement_load_all(void) { return 0; }
void weapon_placement_emit(const WeaponSpec* spec, void* out) {
    (void)spec; (void)out;
}
int weapon_placement_save(const WeaponSpec* spec) { (void)spec; return 0; }
int weapon_placement_unbaked_count(void) { return 0; }

#else

#include "core/json.h"

#include <stdlib.h>

/* Where a weapon's own folder is: the directory its mesh sits in.

   Derived rather than stated as a field, because a second path on the spec is
   a second thing that can disagree with the first, and there is nothing a
   weapon's folder could be except where its mesh came from.

   A weapon with no mesh has no folder and cannot be tuned this way, which is
   the right answer rather than a gap: the auto pistol is still drawn from
   boxes, and there is nothing on screen to tune a grip against. */
static int asset_folder(const WeaponSpec* spec, char* out, size_t capacity) {
    if (!spec || !spec->mesh_path) return 0;
    const char* slash = strrchr(spec->mesh_path, '/');
    if (!slash) return 0;
    size_t length = (size_t)(slash - spec->mesh_path);
    if (length + 1 >= capacity) return 0;
    memcpy(out, spec->mesh_path, length);
    out[length] = '\0';
    return 1;
}

static int placement_path(const WeaponSpec* spec, char* out, size_t capacity) {
    char folder[192];
    if (!asset_folder(spec, folder, sizeof folder)) return 0;
    return snprintf(out, capacity, "%s/placement.json", folder) > 0;
}

/* --- reading ------------------------------------------------------------ */

static int read_f32(JsonSlice object, const char* key, float* out) {
    JsonSlice value;
    if (!json_member(object, key, &value)) return 0;
    return json_float(value, out);
}

/* Three numbers, all or nothing: a vector with two of its components stated is
   a typo, not a partial intent, and taking the two would move the weapon
   somewhere nobody asked for while looking like it worked. */
static int read_vec3(JsonSlice object, const char* key, float out[3]) {
    JsonSlice array, element;
    if (!json_member(object, key, &array)) return 0;
    if (json_array_count(array) != 3) return 0;
    float scratch[3];
    for (int c = 0; c < 3; ++c) {
        if (!json_array_at(array, c, &element)) return 0;
        if (!json_float(element, &scratch[c])) return 0;
    }
    memcpy(out, scratch, sizeof scratch);
    return 1;
}

/* A pose, field by field, and any subset of them. Unlike a vector this IS a
   partial intent: a stance is eight numbers and nudging one of them is the
   normal thing to do, so a file that states only `roll` states only roll. */
static int read_pose(JsonSlice object, const char* key, ViewmodelPose* out) {
    JsonSlice pose;
    if (!json_member(object, key, &pose)) return 0;
    int changed = 0;
    changed += read_f32(pose, "x", &out->x);
    changed += read_f32(pose, "y", &out->y);
    changed += read_f32(pose, "z", &out->z);
    changed += read_f32(pose, "pitch", &out->pitch);
    changed += read_f32(pose, "yaw", &out->yaw);
    changed += read_f32(pose, "roll", &out->roll);
    return changed > 0;
}

int weapon_placement_load(WeaponSpec* spec) {
    char path[256];
    if (!spec || !placement_path(spec, path, sizeof path)) return 0;

    char error[256];
    char* text = json_read_file(path, error, sizeof error);
    if (!text) return 0;          /* absent is normal: a baked weapon has none */

    JsonSlice root = json_slice(text);
    int changed = 0;

    /* The three halves a row can name. A row for a half this weapon does not
       carry is skipped rather than written into a zeroed struct - which is
       what makes a rifle's placement.json harmless if somebody pastes a
       sword's field into it. */
    WeaponSpec* WEAPON = spec;
    FirearmSpec* FIREARM = (FirearmSpec*)spec->firearm;
    MeleeSpec* MELEE = (MeleeSpec*)spec->melee;

#define PLACEMENT_F32(owner, field, doc) \
    if (owner) changed += read_f32(root, #field, &owner->field);
#define PLACEMENT_VEC3(owner, field, doc) \
    if (owner) changed += read_vec3(root, #field, owner->field);
#define PLACEMENT_POSE(name, stance, doc) \
    if (MELEE) changed += read_pose(root, #name, &MELEE->carries[stance]);
#include "gameplay/weapon_placement_fields.h"

    free(text);
    if (changed)
        printf("placement: %s <- %s (%d fields)\n", spec->name, path, changed);
    return changed;
}

int weapon_placement_load_all(void) {
    int weapons = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        WeaponSpec* spec = weapon_registry_tunable(i);
        if (spec && weapon_placement_load(spec) > 0) ++weapons;
    }
    return weapons;
}

int weapon_placement_unbaked_count(void) {
    int unbaked = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        char path[256];
        if (!placement_path(weapon_registry_get(i), path, sizeof path)) continue;
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); ++unbaked; }
    }
    return unbaked;
}

/* --- writing ------------------------------------------------------------ */

/* %.4f throughout, which is a tenth of a millimetre.

   Not %g and not more digits, for two different reasons. These numbers are
   metres off a mesh the player sees at arm's length: the fourth decimal is
   already below what a nudge can be seen to do, and printing the float's full
   decimal expansion would put seventeen digits of noise into a file somebody
   has to read and a literal somebody has to paste. And a fixed width means the
   emitted C lines up in a column, which is how weapon_specs.c is written. */
static void emit_vec3(FILE* f, const char* field, const float v[3]) {
    fprintf(f, "    .%-18s = {%+.4ff, %+.4ff, %+.4ff},\n", field,
            (double)v[0], (double)v[1], (double)v[2]);
}

static void emit_pose(FILE* f, const char* name, int stance,
                      const ViewmodelPose* p) {
    (void)stance;
    fprintf(f, "    /* %s */\n", name);
    fprintf(f, "    {%+.4ff, %+.4ff, %+.4ff, %+.4ff, %+.4ff, %+.4ff},\n",
            (double)p->x, (double)p->y, (double)p->z,
            (double)p->pitch, (double)p->yaw, (double)p->roll);
}

void weapon_placement_emit(const WeaponSpec* spec, void* out) {
    FILE* f = out ? (FILE*)out : stdout;
    if (!spec) return;

    const WeaponSpec* WEAPON = spec;
    const FirearmSpec* FIREARM = spec->firearm;
    const MeleeSpec* MELEE = spec->melee;

    fprintf(f, "\n/* %s: paste into weapon_specs.c, then delete the "
               "placement.json this came from. */\n", spec->name);

#define PLACEMENT_F32(owner, field, doc) \
    if (owner) fprintf(f, "    .%-18s = %+.4ff,\n", #field, (double)owner->field);
#define PLACEMENT_VEC3(owner, field, doc) \
    if (owner) emit_vec3(f, #field, owner->field);
#define PLACEMENT_POSE(name, stance, doc) \
    if (MELEE) emit_pose(f, #name, stance, &MELEE->carries[stance]);
#include "gameplay/weapon_placement_fields.h"

    fflush(f);
}

static void save_vec3(FILE* f, const char* field, const float v[3],
                      const char* comma) {
    fprintf(f, "  \"%s\": [%+.4f, %+.4f, %+.4f]%s\n", field,
            (double)v[0], (double)v[1], (double)v[2], comma);
}

int weapon_placement_save(const WeaponSpec* spec) {
    char path[256];
    if (!spec || !placement_path(spec, path, sizeof path)) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;

    const WeaponSpec* WEAPON = spec;
    const FirearmSpec* FIREARM = spec->firearm;
    const MeleeSpec* MELEE = spec->melee;

    /* A trailing "_" member so that every real one can end in a comma without
       the last needing to know it is last. json.c does not mind either way;
       what this is for is the person editing the file, who should be able to
       reorder lines and delete them without counting commas. */
    fprintf(f, "{\n");
#define PLACEMENT_F32(owner, field, doc) \
    if (owner) fprintf(f, "  \"%s\": %+.4f,\n", #field, (double)owner->field);
#define PLACEMENT_VEC3(owner, field, doc) \
    if (owner) save_vec3(f, #field, owner->field, ",");
#define PLACEMENT_POSE(name, stance, doc)                                    \
    if (MELEE) fprintf(f, "  \"%s\": {\"x\": %+.4f, \"y\": %+.4f, "          \
                          "\"z\": %+.4f, \"pitch\": %+.4f, \"yaw\": %+.4f, " \
                          "\"roll\": %+.4f},\n", #name,                      \
                       (double)MELEE->carries[stance].x,                     \
                       (double)MELEE->carries[stance].y,                     \
                       (double)MELEE->carries[stance].z,                     \
                       (double)MELEE->carries[stance].pitch,                 \
                       (double)MELEE->carries[stance].yaw,                   \
                       (double)MELEE->carries[stance].roll);
#include "gameplay/weapon_placement_fields.h"
    fprintf(f, "  \"_\": 0\n}\n");

    fclose(f);
    printf("placement: %s -> %s\n", spec->name, path);
    return 1;
}

#endif

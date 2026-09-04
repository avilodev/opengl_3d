/* Headless checks on the weapon specs themselves: that every weapon says what
   kind of weapon it is and carries exactly the one class half that kind means,
   that the mesh it names is a file that loads, and that every part name it
   states matches a material on that mesh.

   `make specprobe` from the project root, on the host compiler rather than the
   cross one - none of this needs a window or a GL context.

   It exists because all four of these failures are silent. A spec with no
   class half is a null dereference at the first shot, and one with two is a
   weapon that is a gun and a sword depending on which reader you ask. A mesh
   path that does not load costs the look and says so once, into a console
   nobody is reading. And a part name that matches no material is the quietest
   of the four: nothing errors, everything draws, and the gun simply grows back
   the furniture somebody took off it - which is exactly what a re-export that
   renamed a material does, and exactly what nobody notices until a screenshot
   has an iron sight poking through an optic. */
#include "gameplay/weapon_registry.h"
#include "gameplay/weapon_spec.h"
#include "render/static_model.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
}

/* Exactly one class half, and the one the kind names. Checked as a count
   rather than as two ifs so that a third class added later fails here loudly
   instead of passing by not being looked at. */
static void check_class_halves(const WeaponSpec* spec) {
    int halves = (spec->firearm != NULL) + (spec->melee != NULL);
    printf("%s\n", spec->name);
    expect(halves == 1, "exactly one class half is non-NULL");
    expect(spec->kind != WEAPON_CLASS_FIREARM || spec->firearm != NULL,
           "a firearm carries a FirearmSpec");
    expect(spec->kind != WEAPON_CLASS_MELEE || spec->melee != NULL,
           "a melee weapon carries a MeleeSpec");
}

static int has_material(const StaticModel* model, const char* name) {
    for (int m = 0; m < model->mesh_count; ++m)
        if (strcmp(model->meshes[m].material, name) == 0) return 1;
    return 0;
}

/* The mesh a spec names, and every part name it states against that mesh.

   A part name that matches no material is the quietest failure in the whole
   weapon system: nothing errors, everything draws, and the gun simply grows
   back the furniture somebody took off it. The usual cause is an asset that
   came back from a re-export renamed, and the usual way it is noticed is a
   screenshot with an iron sight poking through an optic. */
static void check_mesh(const WeaponSpec* spec) {
    if (!spec->mesh_path) {
        printf("%s: no mesh yet\n", spec->name);
        return;
    }
    printf("%s: %s\n", spec->name, spec->mesh_path);

    StaticModel model;
    char error[256];
    if (!static_model_load_glb(&model, spec->mesh_path, error, sizeof error)) {
        expect(0, error);
        return;
    }
    expect(1, "mesh loads");

    for (int i = 0; i < WEAPON_MAX_HIDDEN_PARTS && spec->hidden_parts[i]; ++i)
        expect(has_material(&model, spec->hidden_parts[i]), spec->hidden_parts[i]);
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    if (gun && gun->magazine_part)
        expect(has_material(&model, gun->magazine_part), gun->magazine_part);
    if (gun && gun->rounds_part)
        expect(has_material(&model, gun->rounds_part), gun->rounds_part);

    /* Not freed. static_model_free lives beside the GL calls in
       static_model.c, and linking that here would drag a context into a probe
       whose whole point is not needing one. A process that is about to exit
       leaks three models; that is a fair price for staying headless. */
}

/* The blade segment, which is the whole of what a swing means geometrically.
   A tip behind its base, or level with it, is a sword that traces a segment of
   no length forward of the hand: every swing misses, and the miss looks like a
   hit detection bug rather than a spec typo. */
static void check_blade(const WeaponSpec* spec) {
    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee) return;
    printf("%s blade\n", spec->name);
    float dz = melee->blade_tip[2] - melee->blade_base[2];
    expect(dz < -0.2f, "the blade runs forward of its base");

    /* Being worn is a FEATURE, not something every melee weapon has: a baton
       carried in the hand has nowhere to go and states no scabbard. So this is
       not "the scabbard names a mesh" but "the parts of being worn agree with
       each other". A weapon with a scabbard and no draw track cannot get the
       blade out of it; one with a draw track and no scabbard draws from thin
       air. Half-configured is the failure worth catching, and it is the one a
       new weapon copied from an old one actually makes.

       Checked as a count for the reason check_class_halves is: a fourth part
       of being worn, added later, fails here loudly rather than passing by not
       being looked at. */
    int worn = (melee->scabbard_path != NULL) + (melee->draw_track != NULL) +
               (melee->sheathe_track != NULL);
    expect(worn == 0 || worn == 3,
           "worn on the hip: scabbard, draw and sheathe are all stated or none are");

    /* The scabbard is a second file, split out of the same FBX, and it is the
       one the sword is not drawn with - so a rename or a stale path there goes
       unnoticed for exactly as long as nobody sheathes. */
    if (!melee->scabbard_path) {
        printf("   ---- carried in the hand, no scabbard\n");
        return;
    }
    StaticModel saya;
    char error[256];
    if (static_model_load_glb(&saya, melee->scabbard_path, error, sizeof error))
        expect(1, melee->scabbard_path);
    else
        expect(0, error);
}

/* Every weapon answers to its own name, and no two answer to the same one.

   weapon_registry_find compares against the name the player is shown, so two
   weapons sharing one is a lookup that silently returns whichever was listed
   first - and the usual cause is a new weapon copied from an old one whose
   name never got changed. */
static void check_names(void) {
    printf("names\n");
    for (int i = 0; i < weapon_registry_count(); ++i) {
        const WeaponSpec* spec = weapon_registry_get(i);
        expect(spec->name != NULL && spec->name[0] != '\0', "states a name");
        if (!spec->name) continue;
        expect(weapon_registry_find(spec->name) == spec, spec->name);
    }
}

int main(void) {
    /* Every weapon there is, and not a list of the ones somebody remembered.
       See include/gameplay/weapon_registry.h for what that used to cost. */
    printf("%d weapons in the registry\n\n", weapon_registry_count());
    check_names();
    for (int i = 0; i < weapon_registry_count(); ++i)
        check_class_halves(weapon_registry_get(i));
    for (int i = 0; i < weapon_registry_count(); ++i)
        check_mesh(weapon_registry_get(i));
    for (int i = 0; i < weapon_registry_count(); ++i)
        check_blade(weapon_registry_get(i));
    printf("\n%d failures\n", failures);
    return failures != 0;
}

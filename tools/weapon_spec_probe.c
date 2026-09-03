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

int main(void) {
    check_class_halves(&WEAPON_SPEC_AUTO_PISTOL);
    check_class_halves(&WEAPON_SPEC_ASSAULT_RIFLE);
    check_mesh(&WEAPON_SPEC_AUTO_PISTOL);
    check_mesh(&WEAPON_SPEC_ASSAULT_RIFLE);
    printf("\n%d failures\n", failures);
    return failures != 0;
}

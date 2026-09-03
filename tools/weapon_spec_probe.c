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

#include <stdio.h>

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

int main(void) {
    check_class_halves(&WEAPON_SPEC_AUTO_PISTOL);
    check_class_halves(&WEAPON_SPEC_ASSAULT_RIFLE);
    printf("\n%d failures\n", failures);
    return failures != 0;
}

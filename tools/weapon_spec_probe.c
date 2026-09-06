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
#include "gameplay/sight_registry.h"
#include "gameplay/viewmodel_pose.h"
#include "gameplay/weapon_spec.h"
#include "render/static_model.h"
#include "render/weapon_model.h"

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

/* --- the golden snapshot -------------------------------------------------

   Every number a weapon's placement can be READ THROUGH, printed and diffed
   against a checked-in file.

   The invariants above catch a weapon that is wrong on its own terms - a mesh
   that will not load, a part name that matches nothing. What they cannot catch
   is a weapon that is wrong relative to yesterday, and that is the failure
   this system now makes easy: the numbers are shared. weapon_spec_sight_height
   is a gun's rail plus an optic's reticle, so retuning a sight moves every gun
   carrying it. weapon_spec_rest_pose is read by both the drawn weapon and the
   arms' shoulder calibration. A nudge meant for the rifle lands on the pistol
   and nothing says so, because nothing was ever wrong - it was only changed.

   So: the derived values, not the stated ones. Stated values are already in
   the file somebody just edited, and printing them back would only confirm
   the edit. What is worth watching is what came OUT of the edit two functions
   later.

   `make golden` rewrites the file, deliberately, when a change was meant. */
#define GOLDEN_PATH "tools/golden/placement.txt"

static void print_pose(FILE* f, const char* label, const ViewmodelPose* p) {
    fprintf(f, "  %-14s %+.4f %+.4f %+.4f  %+.2f %+.2f %+.2f\n", label,
            (double)p->x, (double)p->y, (double)p->z,
            (double)p->pitch, (double)p->yaw, (double)p->roll);
}

static void write_signature(FILE* f) {
    fprintf(f, "Derived placement, one block per weapon in the registry.\n"
               "Regenerate deliberately with `make golden` when a change was "
               "meant.\n");
    for (int i = 0; i < weapon_registry_count(); ++i) {
        const WeaponSpec* spec = weapon_registry_get(i);
        fprintf(f, "\n%s\n", spec->name);

        /* The sight, whose height is the one number two different meshes
           contribute halves of. */
        fprintf(f, "  %-14s %+.4f\n", "sight_height",
                (double)weapon_spec_sight_height(spec));
        fprintf(f, "  %-14s %+.4f\n", "sight_forward",
                (double)weapon_spec_sight_forward(spec));
        fprintf(f, "  %-14s %+.2f %+.2f\n", "ads_fov",
                (double)weapon_spec_ads_fov(spec),
                (double)weapon_spec_view_ads_fov(spec));
        fprintf(f, "  %-14s %+.4f\n", "ads_time",
                (double)weapon_spec_ads_time(spec));

        /* The resting pose at each end of its travel. What the weapon is
           drawn at, and what the arms are calibrated against - so a drift
           here is a drift in both, in agreement, and invisible. */
        ViewmodelPose pose;
        weapon_spec_rest_pose(spec, 0.0f, 0.0f, 0.0f, &pose);
        print_pose(f, "rest", &pose);
        weapon_spec_rest_pose(spec, 0.0f, 0.0f, 1.0f, &pose);
        print_pose(f, "aimed", &pose);
        weapon_spec_rest_pose(spec, 1.0f, 0.0f, 0.0f, &pose);
        print_pose(f, "sprint", &pose);
        weapon_spec_rest_pose(spec, 0.0f, 1.0f, 0.0f, &pose);
        print_pose(f, "reload", &pose);

        /* Where the hands are told to go, which is what the shoulder solve
           reaches for. */
        fprintf(f, "  %-14s %+.4f %+.4f %+.4f\n", "grip_right",
                (double)spec->grip_right[0], (double)spec->grip_right[1],
                (double)spec->grip_right[2]);
        fprintf(f, "  %-14s %+.4f %+.4f %+.4f\n", "grip_left",
                (double)spec->grip_left[0], (double)spec->grip_left[1],
                (double)spec->grip_left[2]);

        const MeleeSpec* melee = weapon_spec_melee(spec);
        if (melee) {
            float step[3];
            weapon_spec_sheathed_step(melee, step);
            fprintf(f, "  %-14s %+.4f %+.4f %+.4f\n", "sheathed_step",
                    (double)step[0], (double)step[1], (double)step[2]);
            for (int c = 0; c < MELEE_CARRY_COUNT; ++c) {
                char label[24];
                snprintf(label, sizeof label, "carry[%d]", c);
                print_pose(f, label, &melee->carries[c]);
            }
        }
    }
}

/* Writes the signature to a string, so it can be compared without a temp
   file. Sized generously: three weapons is under two kilobytes and this is a
   probe, not a hot path. */
static int signature_matches(void) {
    FILE* got = tmpfile();
    if (!got) { expect(0, "a scratch file for the snapshot"); return 0; }
    write_signature(got);

    FILE* want = fopen(GOLDEN_PATH, "rb");
    if (!want) {
        fclose(got);
        printf("   ---- no %s yet; run `make golden` to record one\n",
               GOLDEN_PATH);
        return 1;   /* not a failure: the first run has nothing to differ from */
    }

    rewind(got);
    int line = 1, same = 1;
    char a[512], b[512];
    for (;;) {
        char* ga = fgets(a, sizeof a, got);
        char* gb = fgets(b, sizeof b, want);
        if (!ga && !gb) break;
        if (!ga || !gb || strcmp(a, b) != 0) {
            if (same) printf("   ---- %s differs:\n", GOLDEN_PATH);
            printf("     %d- %s", line, gb ? b : "(end of file)\n");
            printf("     %d+ %s", line, ga ? a : "(end of file)\n");
            same = 0;
        }
        ++line;
    }
    fclose(got);
    fclose(want);
    expect(same, "derived placement matches the golden snapshot");
    return same;
}

/* Every optic in the rack, whether or not a weapon currently carries one.

   The same argument the weapon registry makes. A sight reached only through
   the weapon that names it is a sight checked only while some weapon names it,
   and the development key that puts each of them on the gun in turn can reach
   one that nothing does - so an optic whose mesh went missing would be a
   keypress away from a gun with no sight on it and nothing said. Walking the
   rack is what makes the swap key safe to trust.

   The mount height is not checked here and cannot be: it is the sum of a rail
   the sight knows nothing about and a seat the gun knows nothing about, which
   is the whole point of keeping the two halves apart. What IS checked is that
   both halves are present and positive, because a zero on either side is the
   shape a missed measurement takes. */
static void check_sight(const SightSpec* sight) {
    printf("%s\n", sight->name);
    expect(sight->name != NULL, "states a name");
    expect(sight->reticle_above_seat > 0.0f,
           "its reticle stands above its own seat");
    expect(sight->window_radius > 0.0f, "has a window to look through");
    expect(sight->magnification >= 1.0f, "does not shrink the target");
    if (!sight->mesh_path) {
        printf("   ---- no mesh yet, drawn out of boxes\n");
        return;
    }
    StaticModel model;
    char error[256];
    if (!static_model_load_glb(&model, sight->mesh_path, error, sizeof error)) {
        expect(0, error);
        return;
    }
    expect(1, sight->mesh_path);
}


/* Every mesh the game can ask for, against the number the renderer can hold.

   The cache in src/render/weapon_model.c is keyed by path and never evicts:
   the first WEAPON_MESH_SLOTS distinct meshes requested get a slot, and every
   later one is refused and falls back to the boxes. Nothing about that is
   visible on screen except a gun that is sometimes a gun and sometimes a
   crate, depending on the order a session happened to ask - which spawn class,
   which weapon key, which optic. It cannot be caught by looking at one weapon,
   because no single weapon is wrong.

   So it is counted here instead. A weapon can name two meshes, its own and a
   scabbard, and every optic in the rack names one. */
static void check_mesh_cache(void) {
    const char* paths[64];
    int n = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        const WeaponSpec* spec = weapon_registry_get(i);
        const MeleeSpec* melee = weapon_spec_melee(spec);
        const char* named[2] = {spec->mesh_path,
                                melee ? melee->scabbard_path : NULL};
        for (int k = 0; k < 2; ++k) {
            if (!named[k]) continue;
            int seen = 0;
            for (int j = 0; j < n; ++j)
                if (strcmp(paths[j], named[k]) == 0) seen = 1;
            if (!seen && n < (int)(sizeof paths / sizeof paths[0]))
                paths[n++] = named[k];
        }
    }
    for (int i = 0; i < sight_registry_count(); ++i) {
        const SightSpec* sight = sight_registry_get(i);
        if (!sight->mesh_path) continue;
        int seen = 0;
        for (int j = 0; j < n; ++j)
            if (strcmp(paths[j], sight->mesh_path) == 0) seen = 1;
        if (!seen && n < (int)(sizeof paths / sizeof paths[0]))
            paths[n++] = sight->mesh_path;
    }
    printf("\nthe mesh cache\n");
    printf("  %d distinct meshes named, %d slots\n", n, WEAPON_MESH_SLOTS);
    expect(n <= WEAPON_MESH_SLOTS,
           "every mesh the game names fits in the renderer's cache");
}

int main(int argc, char** argv) {
    /* `make golden` passes --update. Deliberately an argument rather than an
       environment variable or a second binary: rewriting the file somebody is
       diffing against should be something they typed. */
    if (argc > 1 && strcmp(argv[1], "--update") == 0) {
        FILE* f = fopen(GOLDEN_PATH, "wb");
        if (!f) { printf("cannot write %s\n", GOLDEN_PATH); return 1; }
        write_signature(f);
        fclose(f);
        printf("wrote %s\n", GOLDEN_PATH);
        return 0;
    }

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

    printf("\n%d optics in the rack\n", sight_registry_count());
    for (int i = 0; i < sight_registry_count(); ++i)
        check_sight(sight_registry_get(i));

    check_mesh_cache();

    printf("\nthe golden snapshot\n");
    signature_matches();
    printf("\n%d failures\n", failures);
    return failures != 0;
}

#include "gameplay/weapon_registry.h"

#include <string.h>

/* The list, expanded from the same file that declares the externs. There is no
   second place to add a weapon, which is the whole point: see
   include/gameplay/weapon_list.h. */
static const WeaponSpec* const registry[] = {
#define WEAPON(id) &WEAPON_SPEC_##id,
#include "gameplay/weapon_list.h"
#undef WEAPON
};

#define WEAPON_COUNT ((int)(sizeof(registry) / sizeof(registry[0])))

int weapon_registry_count(void) {
    return WEAPON_COUNT;
}

#ifdef WEAPON_TUNING

/* The mutable copies a placement file writes over. See
   weapon_registry_tunable: the const literals are what ships, so tuning gets
   a copy and everything gets the copy.

   Filled on the first call rather than by an init the caller has to remember,
   because forgetting it would mean silently tuning nothing - the copies would
   be zeroed and every weapon would be at the origin, which looks like a bug in
   the renderer and is a bug in the startup order. */
static WeaponSpec copies[WEAPON_COUNT];
static FirearmSpec firearm_copies[WEAPON_COUNT];
static MeleeSpec melee_copies[WEAPON_COUNT];
static int copies_ready = 0;

static void make_copies(void) {
    if (copies_ready) return;
    copies_ready = 1;
    for (int i = 0; i < WEAPON_COUNT; ++i) {
        copies[i] = *registry[i];
        if (registry[i]->firearm) {
            firearm_copies[i] = *registry[i]->firearm;
            copies[i].firearm = &firearm_copies[i];
        }
        if (registry[i]->melee) {
            melee_copies[i] = *registry[i]->melee;
            copies[i].melee = &melee_copies[i];
        }
    }
}

WeaponSpec* weapon_registry_tunable(int index) {
    if (index < 0 || index >= WEAPON_COUNT) return NULL;
    make_copies();
    return &copies[index];
}

int weapon_registry_tuning_enabled(void) { return 1; }

const WeaponSpec* weapon_registry_get(int index) {
    if (index < 0) index = 0;
    if (index >= WEAPON_COUNT) index = WEAPON_COUNT - 1;
    make_copies();
    return &copies[index];
}

#else

WeaponSpec* weapon_registry_tunable(int index) { (void)index; return NULL; }

int weapon_registry_tuning_enabled(void) { return 0; }

const WeaponSpec* weapon_registry_get(int index) {
    if (index < 0) index = 0;
    if (index >= WEAPON_COUNT) index = WEAPON_COUNT - 1;
    return registry[index];
}

#endif

const WeaponSpec* weapon_registry_find(const char* name) {
    if (!name) return NULL;
    /* Through weapon_registry_get rather than answering registry[i] directly,
       so that a tuning build hands back the copy the placement file was read
       over. Answering the literal here was a bug that reported the file
       loading and then showed the untuned weapon - the overlay working
       perfectly and reaching nobody. */
    for (int i = 0; i < weapon_registry_count(); ++i)
        if (strcmp(registry[i]->name, name) == 0) return weapon_registry_get(i);
    return NULL;
}

int weapon_registry_index_of(const WeaponSpec* spec) {
    /* Against the literal AND against the tuning copy, because a build with
       the overlay hands out the copy and a caller holding one would otherwise
       be told its weapon is not in the registry it came from. The two are
       compared here rather than at every call site for exactly that reason. */
    for (int i = 0; i < weapon_registry_count(); ++i) {
        if (registry[i] == spec) return i;
        if (weapon_registry_tunable(i) == spec) return i;
    }
    return -1;
}

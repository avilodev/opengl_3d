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

/* The mutable copies everything is handed. See weapon_registry_mutable: the
   const literals are what ships, so anything that writes to a weapon writes to
   a copy, and everything reads the copy.

   Filled on the first call rather than by an init the caller has to remember,
   because forgetting it would mean silently reading nothing - the copies would
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

WeaponSpec* weapon_registry_mutable(int index) {
    if (index < 0 || index >= WEAPON_COUNT) return NULL;
    make_copies();
    return &copies[index];
}

const WeaponSpec* weapon_registry_get(int index) {
    if (index < 0) index = 0;
    if (index >= WEAPON_COUNT) index = WEAPON_COUNT - 1;
    make_copies();
    return &copies[index];
}

int weapon_registry_tuning_enabled(void) {
#ifdef WEAPON_TUNING
    return 1;
#else
    return 0;
#endif
}

const WeaponSpec* weapon_registry_find(const char* name) {
    if (!name) return NULL;
    /* Through weapon_registry_get rather than answering registry[i] directly,
       so that the caller gets the copy anything written to a weapon was
       written over. Answering the literal here was a bug that reported a
       placement file loading and then showed the untuned weapon - the overlay
       working perfectly and reaching nobody. */
    for (int i = 0; i < weapon_registry_count(); ++i)
        if (strcmp(registry[i]->name, name) == 0) return weapon_registry_get(i);
    return NULL;
}

int weapon_registry_index_of(const WeaponSpec* spec) {
    /* Against the literal AND against the copy, because what is handed out is
       the copy and a caller holding one would otherwise be told its weapon is
       not in the registry it came from. The two are compared here rather than
       at every call site for exactly that reason. */
    for (int i = 0; i < weapon_registry_count(); ++i) {
        if (registry[i] == spec) return i;
        if (weapon_registry_mutable(i) == spec) return i;
    }
    return -1;
}

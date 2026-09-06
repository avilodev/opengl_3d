#include "gameplay/player_loadout.h"
#include "gameplay/weapon_registry.h"

/* The class list. One row is one weapon, and adding a class is adding a row:
   nothing else in the build counts them, names them or decides what order they
   come in. The pause menu draws a button per row and the game arms whatever
   weapon the chosen row names.

   A row is a weapon and nothing else, and the button is labelled with that
   weapon's own name. It used to carry a label of its own beside the pointer -
   Rifleman, Marksman, Breacher, Scout - and the label is gone rather than
   rewritten, because once a class is called what its gun is called, a string
   here is the same string twice. Two of them is one that can go stale: rename
   a weapon in src/gameplay/weapons/ and the menu would still say the old
   thing, silently and only in the menu. So the name comes off the weapon, and
   the only way to relabel a class is to rename its gun.

   What that gives up is a class whose name is NOT its weapon's, and it is
   worth knowing the door is closed rather than discovering it: a class that
   ever means more than the gun it hands out - stats, abilities, a loadout of
   several things - wants its own name back, and that is this array growing a
   field again rather than anything else changing. */
static const WeaponSpec* const rows[] = {
    &WEAPON_SPEC_KATANA,
    &WEAPON_SPEC_ASSAULT_RIFLE,

    /* The same cartridge as the rifle, fired slower and landing harder: the
       rifle wins a close fight on rate and this one wins a long one on the
       cone. See WEAPON_SPEC_CARBINE for the arithmetic.

       Worth knowing before this class is tuned further: a flat 100-point pool
       and a flat chest multiplier mean the damage figure only changes anything
       at a shot-count boundary. Both rifles kill a chest in three, and they do
       so anywhere from 34 to 49 damage; at 50 the carbine becomes a two-shot
       weapon and stops being a sibling of the rifle at all. So a request to
       make this class hit harder is answered by 49 up to that line and by a
       much lower fire rate past it, and never by a number in between. */
    &WEAPON_SPEC_CARBINE,

    /* The four that arrived together. Each is here because it is a different
       question to the player rather than a different model of the same one -
       which is the bar a row has to clear, since a class the player cannot
       tell apart from another one is a button that wastes their time.

       The M9 and the Magnum are the two halves of the same idea: a shot at a
       time, aimed, over irons. What separates them is the boundary at 50
       damage - the M9 takes two rounds to a chest and the Magnum takes one -
       and everything else about the revolver is the price of being over it.

       The MP7 and the PMX are the two automatics between the sidearms and the
       rifles. The MP7 is the fastest gun here on the loosest cone; the PMX is
       a third steadier and a third slower, and reaches accordingly. See each
       weapon's file in src/gameplay/weapons/ for the arithmetic. */
    &WEAPON_SPEC_M9,
    &WEAPON_SPEC_MAGNUM_357,
    &WEAPON_SPEC_MP7,
    &WEAPON_SPEC_BERETTA_PMX
};

int player_loadout_count(void) {
    return (int)(sizeof(rows) / sizeof(rows[0]));
}

const PlayerLoadout* player_loadout_get(int index) {
    if (index < 0 || index >= player_loadout_count()) index = 0;

    /* The weapon comes back through the registry rather than as the pointer
       the row states.

       They are the same weapon, and in a release build they are the same
       pointer. In a tuning build they are not: the registry hands out a
       mutable copy that placement.json has been read over, and the literal in
       the row above is the untuned original. A player armed straight off the
       row would be holding the one weapon in the process that the tuning
       overlay had not reached - which looks exactly like the overlay not
       working, and is instead the loadout going around it. */
    static PlayerLoadout answer;
    int in_registry = weapon_registry_index_of(rows[index]);
    answer.weapon = in_registry >= 0 ? weapon_registry_get(in_registry)
                                     : rows[index];
    /* Off the weapon the player will actually hold, which in a tuning build is
       the copy - so a gun renamed in an overlay is renamed on the button too,
       rather than the menu and the gun disagreeing. */
    answer.name = answer.weapon->name;
    return &answer;
}

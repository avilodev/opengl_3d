#include "player/player_class.h"

#include <string.h>

/* Class balance lives here. These are intentionally plain authored values so
   tuning a class never requires changing any derived-stat formula. */
static const PlayerClassProfile profiles[PLAYER_CLASS_COUNT] = {
    {
        PLAYER_CLASS_VANGUARD, "VANGUARD", "Balanced repeater", 1, {0.90f, 0.72f, 0.25f},
        /* STR DEX WILL FOCUS LUCK VIGOR POTENCY FINESSE FORTUNE */
        {0, 9, 14, 10, 8, 12, 7, 0, 8}
    },
    {
        PLAYER_CLASS_HUNTER, "HUNTER", "One-shot heavy crossbow", 9, {0.30f, 0.78f, 0.42f},
        {0, 13, 8, 15, 11, 10, 6, 0, 9}
    },
    {
        PLAYER_CLASS_SUMMONER, "SUMMONER", "Weak focus + 3 attack orbs", 10, {0.62f, 0.38f, 0.92f},
        {0, 10, 10, 9, 9, 8, 15, 0, 12}
    },
    {
        /* The only class built around Strength and Finesse, which every other
           profile leaves at zero. Focus is near-useless to a sword, so it pays
           for the melee stats. */
        PLAYER_CLASS_BLOODHUNTER, "BLOODHUNTER", "Sweeping sword + damaging dash", 17, {0.82f, 0.16f, 0.22f},
        {14, 12, 13, 4, 8, 13, 5, 11, 7}
    }, 
    {
        PLAYER_CLASS_ASH_KNIGHT, "ASH KNIGHT", "Armored firebrand with a railspike", 12,
        {0.88f, 0.36f, 0.12f},
        {11, 7, 12, 10, 6, 16, 9, 5, 5}
    }
};

PlayerClass player_class_from_key(const char*key){
    if(key&&strcmp(key,"ash_knight")==0)return PLAYER_CLASS_ASH_KNIGHT;
    return PLAYER_CLASS_COUNT;
}

const PlayerClassProfile* player_class_get(PlayerClass selected_class) {
    if (selected_class < 0 || selected_class >= PLAYER_CLASS_COUNT)
        selected_class = PLAYER_CLASS_VANGUARD;
    return &profiles[selected_class];
}

void player_class_apply_stats(PlayerStats* stats, PlayerClass selected_class) {
    if (!stats) return;
    const PlayerClassProfile* profile = player_class_get(selected_class);
    memcpy(stats->values, profile->stats, sizeof(profile->stats));
}

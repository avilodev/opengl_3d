#ifndef PLAYER_CLASS_H
#define PLAYER_CLASS_H

#include "player/player_stats.h"

typedef enum {
    PLAYER_CLASS_VANGUARD = 0,
    PLAYER_CLASS_HUNTER,
    PLAYER_CLASS_SUMMONER,
    PLAYER_CLASS_BLOODHUNTER,
    PLAYER_CLASS_ASH_KNIGHT,
    PLAYER_CLASS_COUNT
} PlayerClass;

typedef struct {
    PlayerClass id;
    const char* name;
    const char* description;
    unsigned int starting_weapon_id;
    float color[3];
    float stats[PLAYER_STAT_COUNT];
} PlayerClassProfile;

const PlayerClassProfile* player_class_get(PlayerClass selected_class);
void player_class_apply_stats(PlayerStats* stats, PlayerClass selected_class);
PlayerClass player_class_from_key(const char*key);

#endif

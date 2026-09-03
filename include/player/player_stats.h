#ifndef PLAYER_STATS_H
#define PLAYER_STATS_H

typedef enum {
    PLAYER_STAT_STRENGTH,
    PLAYER_STAT_DEXTERITY,
    PLAYER_STAT_WILL,
    PLAYER_STAT_FOCUS,
    PLAYER_STAT_LUCK,
    PLAYER_STAT_VIGOR,
    PLAYER_STAT_POTENCY,
    PLAYER_STAT_FINESSE,
    PLAYER_STAT_FORTUNE,
    PLAYER_STAT_COUNT
} PlayerStat;

typedef struct {
    const char* id;
    const char* display_name;
    /* Abbreviation for readouts that show all nine stats on one line. Lives
       here so those readouts can loop the table instead of hardcoding a list
       and quietly omitting whichever stats nobody used yet. */
    const char* short_name;
    const char* description;
} PlayerStatDefinition;

typedef struct {
    float values[PLAYER_STAT_COUNT];
} PlayerStats;

extern const PlayerStatDefinition g_player_stat_definitions[PLAYER_STAT_COUNT];

void player_stats_init(PlayerStats* stats);
float player_stats_get(const PlayerStats* stats, PlayerStat stat);
void player_stats_set(PlayerStats* stats, PlayerStat stat, float value);
void player_stats_add(PlayerStats* stats, PlayerStat stat, float amount);
int player_stat_from_id(const char* id, PlayerStat* stat);

/* Derived values live here so future items and passives share one formula. */
float player_stats_movement_multiplier(const PlayerStats* stats);
float player_stats_roll_iframe_multiplier(const PlayerStats* stats);
float player_stats_max_health(const PlayerStats* stats, float base_health);
float player_stats_damage_taken_multiplier(const PlayerStats* stats);
float player_stats_reload_speed_multiplier(const PlayerStats* stats);
float player_stats_accuracy_multiplier(const PlayerStats* stats);
float player_stats_status_duration_multiplier(const PlayerStats* stats);
float player_stats_status_magnitude_multiplier(const PlayerStats* stats);
float player_stats_status_chance_multiplier(const PlayerStats* stats);

/* Stable hooks for systems that are intentionally not built yet. */
float player_stats_melee_damage_multiplier(const PlayerStats* stats);
float player_stats_melee_speed_multiplier(const PlayerStats* stats);
float player_stats_item_quality_multiplier(const PlayerStats* stats);
float player_stats_currency_multiplier(const PlayerStats* stats);
float player_stats_shop_discount(const PlayerStats* stats);

#endif

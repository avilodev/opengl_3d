#include "player/player_stats.h"

#include <string.h>

const PlayerStatDefinition g_player_stat_definitions[PLAYER_STAT_COUNT] = {
    {"strength",  "Strength",  "STR", "Melee damage"},
    {"dexterity", "Dexterity", "DEX", "Movement speed"},
    {"will",      "Will",      "WILL", "Maximum health and damage resistance"},
    {"focus",     "Focus",     "FOCUS", "Reload speed and accuracy"},
    {"luck",      "Luck",      "LUCK", "Chance for better item and chest quality"},
    {"vigor",     "Vigor",     "VIGOR", "Dodge-roll invulnerability duration"},
    {"potency",   "Potency",   "POT", "Status-effect duration and chance"},
    {"finesse",   "Finesse",   "FIN", "Melee swing speed"},
    {"fortune",   "Fortune",   "FORT", "Currency find rate and shop discount"}
};

static int valid_stat(PlayerStat stat) {
    return stat >= 0 && stat < PLAYER_STAT_COUNT;
}

void player_stats_init(PlayerStats* stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

float player_stats_get(const PlayerStats* stats, PlayerStat stat) {
    return stats && valid_stat(stat) ? stats->values[stat] : 0.0f;
}

void player_stats_set(PlayerStats* stats, PlayerStat stat, float value) {
    if (stats && valid_stat(stat)) stats->values[stat] = value;
}

void player_stats_add(PlayerStats* stats, PlayerStat stat, float amount) {
    if (stats && valid_stat(stat)) stats->values[stat] += amount;
}

int player_stat_from_id(const char* id, PlayerStat* stat) {
    if (!id) return 0;
    for (int i = 0; i < PLAYER_STAT_COUNT; i++) {
        if (strcmp(id, g_player_stat_definitions[i].id) == 0) {
            if (stat) *stat = (PlayerStat)i;
            return 1;
        }
    }
    return 0;
}

float player_stats_movement_multiplier(const PlayerStats* stats) {
    float multiplier = 1.0f + player_stats_get(stats, PLAYER_STAT_DEXTERITY) * 0.02f;
    return multiplier < 0.1f ? 0.1f : multiplier;
}

float player_stats_roll_iframe_multiplier(const PlayerStats* stats) {
    float multiplier = 1.0f + player_stats_get(stats, PLAYER_STAT_VIGOR) * 0.05f;
    return multiplier < 0.0f ? 0.0f : multiplier;
}

float player_stats_max_health(const PlayerStats* stats, float base_health) {
    return base_health + player_stats_get(stats, PLAYER_STAT_WILL) * 5.0f;
}

float player_stats_damage_taken_multiplier(const PlayerStats* stats) {
    float will = player_stats_get(stats, PLAYER_STAT_WILL);
    return 1.0f / (1.0f + (will > 0.0f ? will : 0.0f) * 0.025f);
}

float player_stats_reload_speed_multiplier(const PlayerStats* stats) {
    float result = 1.0f + player_stats_get(stats, PLAYER_STAT_FOCUS) * 0.03f;
    return result < 0.1f ? 0.1f : result;
}

float player_stats_accuracy_multiplier(const PlayerStats* stats) {
    float focus = player_stats_get(stats, PLAYER_STAT_FOCUS);
    return 1.0f / (1.0f + (focus > 0.0f ? focus : 0.0f) * 0.04f);
}

float player_stats_status_duration_multiplier(const PlayerStats* stats) {
    float result = 1.0f + player_stats_get(stats, PLAYER_STAT_POTENCY) * 0.04f;
    return result < 0.0f ? 0.0f : result;
}

float player_stats_status_magnitude_multiplier(const PlayerStats* stats) {
    float result = 1.0f + player_stats_get(stats, PLAYER_STAT_POTENCY) * 0.025f;
    return result < 0.0f ? 0.0f : result;
}

float player_stats_status_chance_multiplier(const PlayerStats* stats) {
    float result = 1.0f + player_stats_get(stats, PLAYER_STAT_POTENCY) * 0.03f;
    return result < 0.0f ? 0.0f : result;
}

float player_stats_melee_damage_multiplier(const PlayerStats* stats) {
    return 1.0f + player_stats_get(stats, PLAYER_STAT_STRENGTH) * 0.05f;
}

float player_stats_melee_speed_multiplier(const PlayerStats* stats) {
    return 1.0f + player_stats_get(stats, PLAYER_STAT_FINESSE) * 0.03f;
}

float player_stats_item_quality_multiplier(const PlayerStats* stats) {
    return 1.0f + player_stats_get(stats, PLAYER_STAT_LUCK) * 0.03f;
}

float player_stats_currency_multiplier(const PlayerStats* stats) {
    return 1.0f + player_stats_get(stats, PLAYER_STAT_FORTUNE) * 0.04f;
}

float player_stats_shop_discount(const PlayerStats* stats) {
    float discount = player_stats_get(stats, PLAYER_STAT_FORTUNE) * 0.01f;
    if (discount < 0.0f) return 0.0f;
    return discount > 0.5f ? 0.5f : discount;
}

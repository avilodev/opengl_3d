#ifndef CORE_SAVE_STATE_H
#define CORE_SAVE_STATE_H

#include <stdint.h>

#include "player/player_class.h"
#include "core/item_inventory.h"

#define SAVE_STATE_FORMAT_VERSION 4u
#define SAVE_PROGRESSION_CAPACITY 128
#define SAVE_CHARACTER_CAPACITY 64
#define SAVE_CHARACTER_KEY_LENGTH 32

/* One durable character record. The string key is the stable save identity;
   array position is deliberately not part of the file format. */
typedef struct {
    char key[SAVE_CHARACTER_KEY_LENGTH];
    uint8_t discovered;
    uint8_t dialogue_stage;
    uint8_t playable_unlocked;
} SavedCharacterState;

typedef struct {
    SavedCharacterState characters[SAVE_CHARACTER_CAPACITY];
    uint16_t character_count;
} SavedWorldState;

typedef struct {
    uint8_t playable_classes[PLAYER_CLASS_COUNT];
    uint8_t starting_weapon_selection;
    uint8_t extra_inventory_slots;
    uint8_t offering_bounties;
    uint32_t preferred_starting_weapon;
} SavedUnlockState;
typedef struct { uint32_t definition_id;uint32_t completion_count; } SavedWishRecord;
typedef struct {
    uint32_t discovered_recipes[SAVE_PROGRESSION_CAPACITY];
    uint16_t discovered_recipe_count;
    SavedWishRecord wishes[SAVE_PROGRESSION_CAPACITY];
    uint16_t wish_count;
} SavedProgressionState;

/* Aggregate root for durable game progress. Add future persistent subsystems
   as nested structs here; temporary activity state remains in RunState. */
typedef struct SaveState {
    uint32_t format_version;
    uint32_t next_item_instance_id;
    ItemInventory inventory;
    SavedWorldState world;
    SavedUnlockState unlocks;
    SavedProgressionState progression;
} SaveState;

void save_state_init(SaveState* save);
int save_state_load(SaveState* save, const char* path,
                    char* error, int error_size);
int save_state_write(const SaveState* save, const char* path,
                     char* error, int error_size);

const SavedCharacterState* save_state_character(const SaveState* save,
                                                 const char* key);
SavedCharacterState* save_state_ensure_character(SaveState* save,
                                                 const char* key);
int save_state_discover_character(SaveState* save, const char* key);
int save_state_set_dialogue_stage(SaveState* save, const char* key,
                                  uint8_t stage);
int save_state_unlock_playable_character(SaveState* save, const char* key);
uint32_t save_state_allocate_item_instance(SaveState* save);
int save_state_discover_recipe(SaveState*save,uint32_t definition_id);
int save_state_recipe_discovered(const SaveState*save,uint32_t definition_id);
int save_state_record_wish(SaveState*save,uint32_t definition_id);
uint32_t save_state_wish_count(const SaveState*save,uint32_t definition_id);

#endif

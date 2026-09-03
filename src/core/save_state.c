#include "core/save_state.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void set_error(char* error, int error_size, const char* message) {
    if (error && error_size > 0)
        snprintf(error, (size_t)error_size, "%s", message ? message : "save error");
}

static int valid_key(const char* key) {
    if (!key || !key[0] || strlen(key) >= SAVE_CHARACTER_KEY_LENGTH) return 0;
    for (const unsigned char* p = (const unsigned char*)key; *p; ++p)
        if (!isalnum(*p) && *p != '_' && *p != '-') return 0;
    return 1;
}

void save_state_init(SaveState* save) {
    if (!save) return;
    memset(save, 0, sizeof(*save));
    save->format_version = SAVE_STATE_FORMAT_VERSION;
    save->next_item_instance_id = 1;
    /* The original four classes predate persistence. Wish-added classes begin
       locked on a fresh save. */
    for (int i = 0; i < PLAYER_CLASS_ASH_KNIGHT; ++i)
        save->unlocks.playable_classes[i] = 1;
}

const SavedCharacterState* save_state_character(const SaveState* save,
                                                 const char* key) {
    if (!save || !valid_key(key)) return NULL;
    for (uint16_t i = 0; i < save->world.character_count; ++i)
        if (strcmp(save->world.characters[i].key, key) == 0)
            return &save->world.characters[i];
    return NULL;
}

SavedCharacterState* save_state_ensure_character(SaveState* save,
                                                 const char* key) {
    if (!save || !valid_key(key)) return NULL;
    const SavedCharacterState* existing = save_state_character(save, key);
    if (existing) return &save->world.characters[existing - save->world.characters];
    if (save->world.character_count >= SAVE_CHARACTER_CAPACITY) return NULL;
    SavedCharacterState* character =
        &save->world.characters[save->world.character_count++];
    memset(character, 0, sizeof(*character));
    snprintf(character->key, sizeof(character->key), "%s", key);
    return character;
}

int save_state_discover_character(SaveState* save, const char* key) {
    SavedCharacterState* character = save_state_ensure_character(save, key);
    if (!character) return 0;
    character->discovered = 1;
    return 1;
}

int save_state_set_dialogue_stage(SaveState* save, const char* key,
                                  uint8_t stage) {
    SavedCharacterState* character = save_state_ensure_character(save, key);
    if (!character) return 0;
    character->dialogue_stage = stage;
    return 1;
}

int save_state_unlock_playable_character(SaveState* save, const char* key) {
    SavedCharacterState* character = save_state_ensure_character(save, key);
    if (!character) return 0;
    character->discovered = 1;
    character->playable_unlocked = 1;
    return 1;
}

uint32_t save_state_allocate_item_instance(SaveState* save) {
    if (!save) return 0;
    if (!save->next_item_instance_id) save->next_item_instance_id = 1;
    uint32_t start = save->next_item_instance_id;
    do {
        uint32_t candidate = save->next_item_instance_id++;
        if (!save->next_item_instance_id) save->next_item_instance_id = 1;
        if (!item_inventory_instance(&save->inventory, candidate)) return candidate;
    } while (save->next_item_instance_id != start);
    return 0;
}

int save_state_recipe_discovered(const SaveState*s,uint32_t id){if(!s||!id)return 0;for(uint16_t i=0;i<s->progression.discovered_recipe_count;i++)if(s->progression.discovered_recipes[i]==id)return 1;return 0;}
int save_state_discover_recipe(SaveState*s,uint32_t id){if(!s||!id)return 0;if(save_state_recipe_discovered(s,id))return 1;if(s->progression.discovered_recipe_count>=SAVE_PROGRESSION_CAPACITY)return 0;s->progression.discovered_recipes[s->progression.discovered_recipe_count++]=id;return 1;}
uint32_t save_state_wish_count(const SaveState*s,uint32_t id){if(!s)return 0;for(uint16_t i=0;i<s->progression.wish_count;i++)if(s->progression.wishes[i].definition_id==id)return s->progression.wishes[i].completion_count;return 0;}
int save_state_record_wish(SaveState*s,uint32_t id){if(!s||!id)return 0;for(uint16_t i=0;i<s->progression.wish_count;i++)if(s->progression.wishes[i].definition_id==id){if(s->progression.wishes[i].completion_count==UINT32_MAX)return 0;s->progression.wishes[i].completion_count++;return 1;}if(s->progression.wish_count>=SAVE_PROGRESSION_CAPACITY)return 0;s->progression.wishes[s->progression.wish_count++]=(SavedWishRecord){id,1};return 1;}

int save_state_load(SaveState* save, const char* path,
                    char* error, int error_size) {
    if (!save || !path) { set_error(error, error_size, "invalid save load arguments"); return 0; }
    FILE* file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) { save_state_init(save); if (error && error_size > 0) error[0] = 0; return 1; }
        set_error(error, error_size, "could not open save file"); return 0;
    }
    SaveState loaded;
    save_state_init(&loaded);
    char line[256];
    int line_number = 0, saw_header = 0;unsigned source_version=0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char command[32];
        if (sscanf(line, "%31s", command) != 1 || command[0] == '#') continue;
        if (strcmp(command, "save") == 0) {
            unsigned version;
            if (saw_header || sscanf(line, "save %u", &version) != 1 ||
                version < 2 || version > SAVE_STATE_FORMAT_VERSION) goto malformed;
            source_version=version;loaded.format_version = SAVE_STATE_FORMAT_VERSION; saw_header = 1;
        } else if (strcmp(command, "class") == 0) {
            int class_id, unlocked;
            if (!saw_header || sscanf(line, "class %d %d", &class_id, &unlocked) != 2 ||
                class_id < 0 || class_id >= PLAYER_CLASS_COUNT ||
                (unlocked != 0 && unlocked != 1)) goto malformed;
            loaded.unlocks.playable_classes[class_id] = (uint8_t)unlocked;
        } else if (strcmp(command, "character") == 0) {
            char key[SAVE_CHARACTER_KEY_LENGTH]; int discovered, stage, playable;
            if (!saw_header || sscanf(line, "character %31s %d %d %d", key,
                    &discovered, &stage, &playable) != 4 || !valid_key(key) ||
                discovered < 0 || discovered > 1 || stage < 0 || stage > 255 ||
                playable < 0 || playable > 1 || save_state_character(&loaded, key))
                goto malformed;
            SavedCharacterState* character = save_state_ensure_character(&loaded, key);
            if (!character) goto malformed;
            character->discovered = (uint8_t)discovered;
            character->dialogue_stage = (uint8_t)stage;
            character->playable_unlocked = (uint8_t)playable;
        } else if (strcmp(command,"recipe")==0){unsigned id;if(!saw_header||source_version<3||sscanf(line,"recipe %u",&id)!=1||!save_state_discover_recipe(&loaded,id))goto malformed;
        } else if (strcmp(command,"wish")==0){unsigned id,count;if(!saw_header||source_version<3||sscanf(line,"wish %u %u",&id,&count)!=2||!id||!count||loaded.progression.wish_count>=SAVE_PROGRESSION_CAPACITY)goto malformed;loaded.progression.wishes[loaded.progression.wish_count++]=(SavedWishRecord){id,count};
        } else if(strcmp(command,"perks")==0){unsigned weapon;int selection,slots,bounties;if(!saw_header||source_version<4||sscanf(line,"perks %d %d %d %u",&selection,&slots,&bounties,&weapon)!=4||selection<0||selection>1||slots<0||slots>4||bounties<0||bounties>1)goto malformed;loaded.unlocks.starting_weapon_selection=(uint8_t)selection;loaded.unlocks.extra_inventory_slots=(uint8_t)slots;loaded.unlocks.offering_bounties=(uint8_t)bounties;loaded.unlocks.preferred_starting_weapon=weapon;
        } else if (strcmp(command, "next_item") == 0) {
            unsigned next;
            if (!saw_header || sscanf(line, "next_item %u", &next) != 1 ||
                !next) goto malformed;
            loaded.next_item_instance_id = next;
        } else if (strcmp(command, "item") == 0) {
            int type; unsigned definition, quantity, instance;
            if (!saw_header || sscanf(line, "item %d %u %u %u", &type,
                    &definition, &quantity, &instance) != 4 ||
                !item_inventory_type_valid((InventoryItemType)type) ||
                !definition || !quantity) goto malformed;
            int added = item_inventory_type_stackable((InventoryItemType)type)
                ? (!instance && item_inventory_add_components(&loaded.inventory,
                                                              definition, quantity))
                : (quantity == 1 && instance && item_inventory_add_unique(
                    &loaded.inventory, (InventoryItemType)type,
                    definition, instance));
            if (!added) goto malformed;
        } else goto malformed;
    }
    fclose(file);
    if (!saw_header) { set_error(error, error_size, "save file has no header"); return 0; }
    *save = loaded;
    if (error && error_size > 0) error[0] = 0;
    return 1;

malformed:
    fclose(file);
    if (error && error_size > 0)
        snprintf(error, (size_t)error_size, "malformed save file line %d", line_number);
    return 0;
}

static int replace_file(const char* temporary, const char* path) {
#ifdef _WIN32
    return MoveFileExA(temporary, path,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, path) == 0;
#endif
}

int save_state_write(const SaveState* save, const char* path,
                     char* error, int error_size) {
    if (!save || !path || save->format_version != SAVE_STATE_FORMAT_VERSION ||
        save->world.character_count > SAVE_CHARACTER_CAPACITY) {
        set_error(error, error_size, "invalid save write arguments"); return 0;
    }
    for (int i = 0; i < PLAYER_CLASS_COUNT; ++i)
        if (save->unlocks.playable_classes[i] > 1) {
            set_error(error, error_size, "invalid playable-class state"); return 0;
        }
    if(save->unlocks.starting_weapon_selection>1||
       save->unlocks.extra_inventory_slots>4||save->unlocks.offering_bounties>1){
        set_error(error,error_size,"invalid permanent perk state");return 0;
    }
    if (!save->next_item_instance_id ||
        save->inventory.entry_count > ITEM_INVENTORY_CAPACITY) {
        set_error(error, error_size, "invalid saved inventory"); return 0;
    }
    char temporary[512];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) {
        set_error(error, error_size, "save path is too long"); return 0;
    }
    FILE* file = fopen(temporary, "w");
    if (!file) { set_error(error, error_size, "could not create temporary save file"); return 0; }
    int ok = fprintf(file, "save %u\n", save->format_version) >= 0;
    if (ok) ok = fprintf(file, "next_item %u\n",
                         save->next_item_instance_id) >= 0;
    for (int i = 0; ok && i < PLAYER_CLASS_COUNT; ++i)
        ok = fprintf(file, "class %d %u\n", i,
            (unsigned)save->unlocks.playable_classes[i]) >= 0;
    if(ok)ok=fprintf(file,"perks %u %u %u %u\n",
        (unsigned)save->unlocks.starting_weapon_selection,
        (unsigned)save->unlocks.extra_inventory_slots,
        (unsigned)save->unlocks.offering_bounties,
        save->unlocks.preferred_starting_weapon)>=0;
    for (uint16_t i = 0; ok && i < save->inventory.entry_count; ++i) {
        const InventoryItem* item = &save->inventory.entries[i];
        int stackable = item_inventory_type_stackable(item->type);
        if (!item_inventory_type_valid(item->type) || !item->definition_id ||
            !item->quantity || (stackable && item->instance_id) ||
            (!stackable && (!item->instance_id || item->quantity != 1))) {
            ok = 0; break;
        }
        ok = fprintf(file, "item %d %u %u %u\n", (int)item->type,
            item->definition_id, item->quantity, item->instance_id) >= 0;
    }
    for (uint16_t i = 0; ok && i < save->world.character_count; ++i) {
        const SavedCharacterState* character = &save->world.characters[i];
        if (!valid_key(character->key)) { ok = 0; break; }
        ok = fprintf(file, "character %s %u %u %u\n", character->key,
            (unsigned)character->discovered,
            (unsigned)character->dialogue_stage,
            (unsigned)character->playable_unlocked) >= 0;
    }
    for(uint16_t i=0;ok&&i<save->progression.discovered_recipe_count;i++)
        ok=fprintf(file,"recipe %u\n",save->progression.discovered_recipes[i])>=0;
    for(uint16_t i=0;ok&&i<save->progression.wish_count;i++)
        ok=fprintf(file,"wish %u %u\n",save->progression.wishes[i].definition_id,
            save->progression.wishes[i].completion_count)>=0;
    if (fflush(file) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok || !replace_file(temporary, path)) {
        remove(temporary); set_error(error, error_size, "could not commit save file"); return 0;
    }
    if (error && error_size > 0) error[0] = 0;
    return 1;
}

#ifndef CORE_ITEM_INVENTORY_H
#define CORE_ITEM_INVENTORY_H

#include <stdint.h>

#define ITEM_INVENTORY_CAPACITY 128

typedef enum {
    INVENTORY_ITEM_WEAPON = 0,
    INVENTORY_ITEM_COMBAT_ITEM,
    INVENTORY_ITEM_OFFERING_COMPONENT,
    INVENTORY_ITEM_OFFERING,
    INVENTORY_ITEM_TYPE_COUNT
} InventoryItemType;

typedef struct {
    uint32_t instance_id;   /* Zero only for a stackable component entry. */
    uint32_t definition_id;
    uint32_t quantity;      /* Always one for unique entries. */
    InventoryItemType type;
} InventoryItem;

typedef struct {
    InventoryItem entries[ITEM_INVENTORY_CAPACITY];
    uint16_t entry_count;
} ItemInventory;

void item_inventory_init(ItemInventory* inventory);
int item_inventory_type_valid(InventoryItemType type);
int item_inventory_type_stackable(InventoryItemType type);
const InventoryItem* item_inventory_instance(const ItemInventory* inventory,
                                             uint32_t instance_id);
uint32_t item_inventory_quantity(const ItemInventory* inventory,
                                 InventoryItemType type,
                                 uint32_t definition_id);
int item_inventory_add_unique(ItemInventory* inventory, InventoryItemType type,
                              uint32_t definition_id, uint32_t instance_id);
int item_inventory_remove_instance(ItemInventory* inventory,
                                   uint32_t instance_id);
int item_inventory_add_components(ItemInventory* inventory,
                                  uint32_t definition_id, uint32_t quantity);
int item_inventory_remove_components(ItemInventory* inventory,
                                     uint32_t definition_id,
                                     uint32_t quantity);
int item_inventory_transfer_instance(ItemInventory* destination,
                                     ItemInventory* source,
                                     uint32_t instance_id);
int item_inventory_transfer_components(ItemInventory* destination,
                                       ItemInventory* source,
                                       uint32_t definition_id,
                                       uint32_t quantity);
int item_inventory_merge(ItemInventory* destination,
                         const ItemInventory* source);
void item_inventory_clear(ItemInventory* inventory);

#endif

#include "core/item_inventory.h"

#include <limits.h>
#include <string.h>

void item_inventory_init(ItemInventory* inventory) {
    if (inventory) memset(inventory, 0, sizeof(*inventory));
}

int item_inventory_type_valid(InventoryItemType type) {
    return type >= 0 && type < INVENTORY_ITEM_TYPE_COUNT;
}

int item_inventory_type_stackable(InventoryItemType type) {
    return type == INVENTORY_ITEM_OFFERING_COMPONENT;
}

static int find_instance(const ItemInventory* inventory, uint32_t instance_id) {
    if (!inventory || !instance_id) return -1;
    for (uint16_t i = 0; i < inventory->entry_count; ++i)
        if (inventory->entries[i].instance_id == instance_id) return (int)i;
    return -1;
}

static int find_components(const ItemInventory* inventory,
                           uint32_t definition_id) {
    if (!inventory || !definition_id) return -1;
    for (uint16_t i = 0; i < inventory->entry_count; ++i)
        if (inventory->entries[i].type == INVENTORY_ITEM_OFFERING_COMPONENT &&
            inventory->entries[i].definition_id == definition_id) return (int)i;
    return -1;
}

const InventoryItem* item_inventory_instance(const ItemInventory* inventory,
                                             uint32_t instance_id) {
    int index = find_instance(inventory, instance_id);
    return index >= 0 ? &inventory->entries[index] : NULL;
}

uint32_t item_inventory_quantity(const ItemInventory* inventory,
                                 InventoryItemType type,
                                 uint32_t definition_id) {
    if (!inventory || !item_inventory_type_valid(type) || !definition_id) return 0;
    uint32_t quantity = 0;
    for (uint16_t i = 0; i < inventory->entry_count; ++i) {
        const InventoryItem* item = &inventory->entries[i];
        if (item->type == type && item->definition_id == definition_id) {
            if (UINT32_MAX - quantity < item->quantity) return UINT32_MAX;
            quantity += item->quantity;
        }
    }
    return quantity;
}

int item_inventory_add_unique(ItemInventory* inventory, InventoryItemType type,
                              uint32_t definition_id, uint32_t instance_id) {
    if (!inventory || !item_inventory_type_valid(type) ||
        item_inventory_type_stackable(type) || !definition_id || !instance_id ||
        find_instance(inventory, instance_id) >= 0 ||
        inventory->entry_count >= ITEM_INVENTORY_CAPACITY) return 0;
    inventory->entries[inventory->entry_count++] =
        (InventoryItem){instance_id, definition_id, 1, type};
    return 1;
}

int item_inventory_add_components(ItemInventory* inventory,
                                  uint32_t definition_id, uint32_t quantity) {
    if (!inventory || !definition_id || !quantity) return 0;
    int index = find_components(inventory, definition_id);
    if (index >= 0) {
        if (UINT32_MAX - inventory->entries[index].quantity < quantity) return 0;
        inventory->entries[index].quantity += quantity;
        return 1;
    }
    if (inventory->entry_count >= ITEM_INVENTORY_CAPACITY) return 0;
    inventory->entries[inventory->entry_count++] = (InventoryItem){
        0, definition_id, quantity, INVENTORY_ITEM_OFFERING_COMPONENT};
    return 1;
}

static void remove_index(ItemInventory* inventory, int index) {
    inventory->entries[index] = inventory->entries[inventory->entry_count - 1];
    inventory->entry_count--;
}

int item_inventory_remove_instance(ItemInventory* inventory,
                                   uint32_t instance_id) {
    int index = find_instance(inventory, instance_id);
    if (index < 0) return 0;
    remove_index(inventory, index);
    return 1;
}

int item_inventory_remove_components(ItemInventory* inventory,
                                     uint32_t definition_id,
                                     uint32_t quantity) {
    if (!inventory || !quantity) return 0;
    int index = find_components(inventory, definition_id);
    if (index < 0 || inventory->entries[index].quantity < quantity) return 0;
    inventory->entries[index].quantity -= quantity;
    if (!inventory->entries[index].quantity) remove_index(inventory, index);
    return 1;
}

int item_inventory_transfer_instance(ItemInventory* destination,
                                     ItemInventory* source,
                                     uint32_t instance_id) {
    if (!destination || !source || destination == source) return 0;
    int index = find_instance(source, instance_id);
    if (index < 0) return 0;
    InventoryItem item = source->entries[index];
    if (!item_inventory_add_unique(destination, item.type, item.definition_id,
                                   item.instance_id)) return 0;
    remove_index(source, index);
    return 1;
}

int item_inventory_transfer_components(ItemInventory* destination,
                                       ItemInventory* source,
                                       uint32_t definition_id,
                                       uint32_t quantity) {
    if (!destination || !source || destination == source ||
        item_inventory_quantity(source, INVENTORY_ITEM_OFFERING_COMPONENT,
                                definition_id) < quantity) return 0;
    if (!item_inventory_add_components(destination, definition_id, quantity)) return 0;
    if (item_inventory_remove_components(source, definition_id, quantity)) return 1;
    item_inventory_remove_components(destination, definition_id, quantity);
    return 0;
}

int item_inventory_merge(ItemInventory* destination,
                         const ItemInventory* source) {
    if (!destination || !source || destination == source) return 0;
    ItemInventory merged = *destination;
    for (uint16_t i = 0; i < source->entry_count; ++i) {
        const InventoryItem* item = &source->entries[i];
        int added = item_inventory_type_stackable(item->type)
            ? item_inventory_add_components(&merged, item->definition_id,
                                             item->quantity)
            : item_inventory_add_unique(&merged, item->type,
                                        item->definition_id, item->instance_id);
        if (!added) return 0;
    }
    *destination = merged;
    return 1;
}

void item_inventory_clear(ItemInventory* inventory) {
    item_inventory_init(inventory);
}

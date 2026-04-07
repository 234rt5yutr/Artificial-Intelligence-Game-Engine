#pragma once

// Inventory System
// Inventory component and manager for entity item storage

#include "Item.h"
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace Core {
namespace Gameplay {

    //=========================================================================
    // Inventory Slot
    //=========================================================================

    struct InventorySlot {
        ItemInstance Item;
        bool IsLocked = false;            // Slot cannot be modified

        bool IsEmpty() const { 
            return Item.DefinitionId.empty() || Item.Quantity == 0; 
        }
    };

    //=========================================================================
    // Inventory Result
    //=========================================================================

    enum class InventoryResult {
        Success,
        InventoryFull,
        ItemNotFound,
        InsufficientQuantity,
        SlotLocked,
        InvalidItem,
        CannotStack,
        WeightLimitExceeded
    };

    //=========================================================================
    // Inventory Component (ECS Component)
    //=========================================================================

    struct InventoryComponent {
        std::vector<InventorySlot> Slots;
        std::unordered_map<EquipmentSlot, ItemInstance> EquippedItems;

        uint32_t MaxSlots = 20;
        float MaxWeight = 100.0f;
        float CurrentWeight = 0.0f;

        bool EnableWeightLimit = false;

        // Initialize slots
        void Initialize(uint32_t slotCount = 20) {
            MaxSlots = slotCount;
            Slots.resize(slotCount);
        }

        // Get total items across all slots
        uint32_t GetTotalItemCount() const {
            uint32_t count = 0;
            for (const auto& slot : Slots) {
                if (!slot.IsEmpty()) {
                    count += slot.Item.Quantity;
                }
            }
            return count;
        }

        // Get used slot count
        uint32_t GetUsedSlotCount() const {
            uint32_t count = 0;
            for (const auto& slot : Slots) {
                if (!slot.IsEmpty()) count++;
            }
            return count;
        }

        // Find first empty slot
        int32_t FindEmptySlot() const {
            for (size_t i = 0; i < Slots.size(); ++i) {
                if (Slots[i].IsEmpty() && !Slots[i].IsLocked) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        // Find slot containing specific item type
        int32_t FindItemSlot(const std::string& definitionId) const {
            for (size_t i = 0; i < Slots.size(); ++i) {
                if (Slots[i].Item.DefinitionId == definitionId) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        // Find slot with stackable item that has room
        int32_t FindStackableSlot(const std::string& definitionId, 
                                  uint32_t maxStackSize) const {
            for (size_t i = 0; i < Slots.size(); ++i) {
                if (Slots[i].Item.DefinitionId == definitionId &&
                    Slots[i].Item.Quantity < maxStackSize &&
                    !Slots[i].IsLocked) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        // Calculate total quantity of an item
        uint32_t GetItemQuantity(const std::string& definitionId) const {
            uint32_t total = 0;
            for (const auto& slot : Slots) {
                if (slot.Item.DefinitionId == definitionId) {
                    total += slot.Item.Quantity;
                }
            }
            return total;
        }

        // Check if item is equipped
        bool IsItemEquipped(EquipmentSlot slot) const {
            return EquippedItems.contains(slot) && 
                   !EquippedItems.at(slot).DefinitionId.empty();
        }

        // Serialization
        Json ToJson() const {
            Json j;
            j["maxSlots"] = MaxSlots;
            j["maxWeight"] = MaxWeight;
            j["enableWeightLimit"] = EnableWeightLimit;

            Json slotsJson = Json::array();
            for (const auto& slot : Slots) {
                Json slotJson;
                slotJson["item"] = slot.Item.ToJson();
                slotJson["locked"] = slot.IsLocked;
                slotsJson.push_back(slotJson);
            }
            j["slots"] = slotsJson;

            Json equippedJson;
            for (const auto& [slot, item] : EquippedItems) {
                equippedJson[std::to_string(static_cast<int>(slot))] = item.ToJson();
            }
            j["equipped"] = equippedJson;

            return j;
        }

        void FromJson(const Json& j) {
            MaxSlots = j.value("maxSlots", 20);
            MaxWeight = j.value("maxWeight", 100.0f);
            EnableWeightLimit = j.value("enableWeightLimit", false);

            Slots.clear();
            if (j.contains("slots")) {
                for (const auto& slotJson : j["slots"]) {
                    InventorySlot slot;
                    if (slotJson.contains("item")) {
                        slot.Item.FromJson(slotJson["item"]);
                    }
                    slot.IsLocked = slotJson.value("locked", false);
                    Slots.push_back(slot);
                }
            }

            EquippedItems.clear();
            if (j.contains("equipped")) {
                for (auto& [key, itemJson] : j["equipped"].items()) {
                    EquipmentSlot slot = static_cast<EquipmentSlot>(std::stoi(key));
                    ItemInstance item;
                    item.FromJson(itemJson);
                    EquippedItems[slot] = item;
                }
            }
        }
    };

    //=========================================================================
    // Inventory Manager
    //=========================================================================

    class InventoryManager {
    public:
        static InventoryManager& Get() {
            static InventoryManager instance;
            return instance;
        }

        // ================================================================
        // Item Operations
        // ================================================================

        /// Add an item to inventory
        InventoryResult AddItem(InventoryComponent& inventory, 
                               const std::string& definitionId,
                               uint32_t quantity = 1) {
            const ItemDefinition* def = ItemDatabase::Get().GetItem(definitionId);
            if (!def) return InventoryResult::InvalidItem;

            // Check weight limit
            if (inventory.EnableWeightLimit) {
                float addedWeight = def->Weight * quantity;
                if (inventory.CurrentWeight + addedWeight > inventory.MaxWeight) {
                    return InventoryResult::WeightLimitExceeded;
                }
            }

            uint32_t remaining = quantity;

            // Try to stack with existing items
            if (def->MaxStackSize > 1) {
                while (remaining > 0) {
                    int32_t stackSlot = inventory.FindStackableSlot(
                        definitionId, def->MaxStackSize);
                    
                    if (stackSlot < 0) break;

                    auto& slot = inventory.Slots[stackSlot];
                    uint32_t canAdd = def->MaxStackSize - slot.Item.Quantity;
                    uint32_t toAdd = std::min(canAdd, remaining);
                    
                    slot.Item.Quantity += toAdd;
                    remaining -= toAdd;
                }
            }

            // Add to new slots
            while (remaining > 0) {
                int32_t emptySlot = inventory.FindEmptySlot();
                if (emptySlot < 0) {
                    // Rollback partial add? For now, we keep what we added
                    return InventoryResult::InventoryFull;
                }

                auto& slot = inventory.Slots[emptySlot];
                slot.Item.InstanceId = GenerateInstanceId();
                slot.Item.DefinitionId = definitionId;
                slot.Item.Quantity = std::min(remaining, def->MaxStackSize);
                remaining -= slot.Item.Quantity;
            }

            // Update weight
            if (inventory.EnableWeightLimit) {
                inventory.CurrentWeight += def->Weight * quantity;
            }

            return InventoryResult::Success;
        }

        /// Remove an item from inventory
        InventoryResult RemoveItem(InventoryComponent& inventory,
                                   const std::string& definitionId,
                                   uint32_t quantity = 1) {
            uint32_t available = inventory.GetItemQuantity(definitionId);
            if (available < quantity) {
                return InventoryResult::InsufficientQuantity;
            }

            uint32_t remaining = quantity;

            // Remove from slots (last first to preserve stacking)
            for (int i = static_cast<int>(inventory.Slots.size()) - 1; 
                 i >= 0 && remaining > 0; --i) {
                auto& slot = inventory.Slots[i];
                if (slot.Item.DefinitionId == definitionId && !slot.IsLocked) {
                    uint32_t toRemove = std::min(slot.Item.Quantity, remaining);
                    slot.Item.Quantity -= toRemove;
                    remaining -= toRemove;

                    if (slot.Item.Quantity == 0) {
                        slot.Item = ItemInstance(); // Clear slot
                    }
                }
            }

            // Update weight
            if (inventory.EnableWeightLimit) {
                const ItemDefinition* def = ItemDatabase::Get().GetItem(definitionId);
                if (def) {
                    inventory.CurrentWeight -= def->Weight * quantity;
                    inventory.CurrentWeight = std::max(0.0f, inventory.CurrentWeight);
                }
            }

            return InventoryResult::Success;
        }

        /// Move item between slots
        InventoryResult MoveItem(InventoryComponent& inventory,
                                 uint32_t fromSlot, uint32_t toSlot,
                                 uint32_t quantity = 0) {
            if (fromSlot >= inventory.Slots.size() || 
                toSlot >= inventory.Slots.size()) {
                return InventoryResult::InvalidItem;
            }

            auto& from = inventory.Slots[fromSlot];
            auto& to = inventory.Slots[toSlot];

            if (from.IsEmpty()) return InventoryResult::ItemNotFound;
            if (from.IsLocked || to.IsLocked) return InventoryResult::SlotLocked;

            if (quantity == 0) quantity = from.Item.Quantity;

            // If target is empty, move directly
            if (to.IsEmpty()) {
                if (quantity >= from.Item.Quantity) {
                    to.Item = std::move(from.Item);
                    from.Item = ItemInstance();
                } else {
                    to.Item = from.Item;
                    to.Item.InstanceId = GenerateInstanceId();
                    to.Item.Quantity = quantity;
                    from.Item.Quantity -= quantity;
                }
                return InventoryResult::Success;
            }

            // Try to stack if same item
            if (from.Item.DefinitionId == to.Item.DefinitionId) {
                const ItemDefinition* def = ItemDatabase::Get().GetItem(
                    from.Item.DefinitionId);
                if (!def || def->MaxStackSize <= 1) {
                    return InventoryResult::CannotStack;
                }

                uint32_t canAdd = def->MaxStackSize - to.Item.Quantity;
                uint32_t toMove = std::min(canAdd, quantity);
                
                to.Item.Quantity += toMove;
                from.Item.Quantity -= toMove;
                
                if (from.Item.Quantity == 0) {
                    from.Item = ItemInstance();
                }

                return InventoryResult::Success;
            }

            // Swap if different items and moving full stack
            if (quantity >= from.Item.Quantity) {
                std::swap(from.Item, to.Item);
                return InventoryResult::Success;
            }

            return InventoryResult::CannotStack;
        }

        /// Equip an item from inventory
        InventoryResult EquipItem(InventoryComponent& inventory,
                                  uint32_t slotIndex) {
            if (slotIndex >= inventory.Slots.size()) {
                return InventoryResult::InvalidItem;
            }

            auto& slot = inventory.Slots[slotIndex];
            if (slot.IsEmpty()) return InventoryResult::ItemNotFound;

            const ItemDefinition* def = ItemDatabase::Get().GetItem(
                slot.Item.DefinitionId);
            if (!def || def->Slot == EquipmentSlot::None) {
                return InventoryResult::InvalidItem;
            }

            // Unequip existing item first
            if (inventory.IsItemEquipped(def->Slot)) {
                auto existing = inventory.EquippedItems[def->Slot];
                inventory.EquippedItems.erase(def->Slot);
                
                // Put in inventory
                int32_t emptySlot = inventory.FindEmptySlot();
                if (emptySlot >= 0) {
                    inventory.Slots[emptySlot].Item = existing;
                }
            }

            // Equip new item
            inventory.EquippedItems[def->Slot] = slot.Item;
            slot.Item = ItemInstance();

            return InventoryResult::Success;
        }

        /// Unequip an item
        InventoryResult UnequipItem(InventoryComponent& inventory,
                                    EquipmentSlot equipSlot) {
            if (!inventory.IsItemEquipped(equipSlot)) {
                return InventoryResult::ItemNotFound;
            }

            int32_t emptySlot = inventory.FindEmptySlot();
            if (emptySlot < 0) {
                return InventoryResult::InventoryFull;
            }

            inventory.Slots[emptySlot].Item = inventory.EquippedItems[equipSlot];
            inventory.EquippedItems.erase(equipSlot);

            return InventoryResult::Success;
        }

        /// Check if inventory has item
        bool HasItem(const InventoryComponent& inventory,
                    const std::string& definitionId,
                    uint32_t quantity = 1) const {
            return inventory.GetItemQuantity(definitionId) >= quantity;
        }

    private:
        InventoryManager() = default;

        uint64_t GenerateInstanceId() {
            return m_NextInstanceId.fetch_add(1);
        }

        std::atomic<uint64_t> m_NextInstanceId{1};
    };

    //=========================================================================
    // Inventory Result Helpers
    //=========================================================================

    inline const char* InventoryResultToString(InventoryResult result) {
        switch (result) {
            case InventoryResult::Success: return "Success";
            case InventoryResult::InventoryFull: return "Inventory Full";
            case InventoryResult::ItemNotFound: return "Item Not Found";
            case InventoryResult::InsufficientQuantity: return "Insufficient Quantity";
            case InventoryResult::SlotLocked: return "Slot Locked";
            case InventoryResult::InvalidItem: return "Invalid Item";
            case InventoryResult::CannotStack: return "Cannot Stack";
            case InventoryResult::WeightLimitExceeded: return "Weight Limit Exceeded";
            default: return "Unknown";
        }
    }

} // namespace Gameplay
} // namespace Core

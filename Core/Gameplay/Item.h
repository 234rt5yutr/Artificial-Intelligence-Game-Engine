#pragma once

// Item System
// Item definitions, rarity, and database for the inventory system

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

namespace Core {
namespace Gameplay {

    using Json = nlohmann::json;

    //=========================================================================
    // Item Enums
    //=========================================================================

    /// Item quality/rarity levels
    enum class ItemRarity : uint8_t {
        Common = 0,
        Uncommon = 1,
        Rare = 2,
        Epic = 3,
        Legendary = 4,
        Unique = 5
    };

    /// Item types/categories
    enum class ItemType : uint8_t {
        None = 0,
        Weapon,
        Armor,
        Consumable,
        Quest,
        Key,
        Material,
        Currency,
        Miscellaneous
    };

    /// Equipment slots
    enum class EquipmentSlot : uint8_t {
        None = 0,
        Head,
        Chest,
        Hands,
        Legs,
        Feet,
        MainHand,
        OffHand,
        Accessory1,
        Accessory2
    };

    //=========================================================================
    // Item Stats
    //=========================================================================

    struct ItemStats {
        float Damage = 0.0f;
        float Defense = 0.0f;
        float Health = 0.0f;
        float Mana = 0.0f;
        float Speed = 0.0f;
        float CriticalChance = 0.0f;
        std::unordered_map<std::string, float> CustomStats;

        Json ToJson() const {
            Json j;
            j["damage"] = Damage;
            j["defense"] = Defense;
            j["health"] = Health;
            j["mana"] = Mana;
            j["speed"] = Speed;
            j["criticalChance"] = CriticalChance;
            j["custom"] = CustomStats;
            return j;
        }

        void FromJson(const Json& j) {
            Damage = j.value("damage", 0.0f);
            Defense = j.value("defense", 0.0f);
            Health = j.value("health", 0.0f);
            Mana = j.value("mana", 0.0f);
            Speed = j.value("speed", 0.0f);
            CriticalChance = j.value("criticalChance", 0.0f);
            if (j.contains("custom")) {
                CustomStats = j["custom"].get<std::unordered_map<std::string, float>>();
            }
        }
    };

    //=========================================================================
    // Item Definition
    //=========================================================================

    struct ItemDefinition {
        std::string Id;                    // Unique identifier
        std::string Name;                  // Display name
        std::string Description;           // Item description
        std::string IconPath;              // Path to icon texture
        std::string MeshPath;              // Path to 3D mesh (for drops/equipment)

        ItemType Type = ItemType::Miscellaneous;
        ItemRarity Rarity = ItemRarity::Common;
        EquipmentSlot Slot = EquipmentSlot::None;

        uint32_t MaxStackSize = 1;         // Max items per stack
        uint32_t Value = 0;                // Base currency value
        float Weight = 0.0f;               // Weight for encumbrance systems

        ItemStats Stats;                   // Combat/gameplay stats

        bool IsConsumable = false;
        bool IsQuestItem = false;
        bool IsDroppable = true;
        bool IsTradable = true;

        std::vector<std::string> Tags;     // Custom tags for filtering
        std::unordered_map<std::string, std::string> CustomData;

        Json ToJson() const {
            Json j;
            j["id"] = Id;
            j["name"] = Name;
            j["description"] = Description;
            j["iconPath"] = IconPath;
            j["meshPath"] = MeshPath;
            j["type"] = static_cast<int>(Type);
            j["rarity"] = static_cast<int>(Rarity);
            j["slot"] = static_cast<int>(Slot);
            j["maxStackSize"] = MaxStackSize;
            j["value"] = Value;
            j["weight"] = Weight;
            j["stats"] = Stats.ToJson();
            j["isConsumable"] = IsConsumable;
            j["isQuestItem"] = IsQuestItem;
            j["isDroppable"] = IsDroppable;
            j["isTradable"] = IsTradable;
            j["tags"] = Tags;
            j["customData"] = CustomData;
            return j;
        }

        void FromJson(const Json& j) {
            Id = j.value("id", "");
            Name = j.value("name", "");
            Description = j.value("description", "");
            IconPath = j.value("iconPath", "");
            MeshPath = j.value("meshPath", "");
            Type = static_cast<ItemType>(j.value("type", 0));
            Rarity = static_cast<ItemRarity>(j.value("rarity", 0));
            Slot = static_cast<EquipmentSlot>(j.value("slot", 0));
            MaxStackSize = j.value("maxStackSize", 1);
            Value = j.value("value", 0);
            Weight = j.value("weight", 0.0f);
            if (j.contains("stats")) Stats.FromJson(j["stats"]);
            IsConsumable = j.value("isConsumable", false);
            IsQuestItem = j.value("isQuestItem", false);
            IsDroppable = j.value("isDroppable", true);
            IsTradable = j.value("isTradable", true);
            if (j.contains("tags")) Tags = j["tags"].get<std::vector<std::string>>();
            if (j.contains("customData")) {
                CustomData = j["customData"].get<std::unordered_map<std::string, std::string>>();
            }
        }
    };

    //=========================================================================
    // Item Instance (runtime instance with unique properties)
    //=========================================================================

    struct ItemInstance {
        uint64_t InstanceId = 0;           // Unique runtime ID
        std::string DefinitionId;          // Reference to ItemDefinition
        uint32_t Quantity = 1;             // Stack count
        
        // Instance-specific overrides
        std::optional<std::string> CustomName;
        std::optional<ItemStats> CustomStats;
        std::optional<uint32_t> CustomValue;
        
        // Durability system
        float Durability = 100.0f;
        float MaxDurability = 100.0f;

        // Ownership
        uint32_t OwnerEntityId = 0;

        Json ToJson() const {
            Json j;
            j["instanceId"] = InstanceId;
            j["definitionId"] = DefinitionId;
            j["quantity"] = Quantity;
            if (CustomName) j["customName"] = *CustomName;
            if (CustomStats) j["customStats"] = CustomStats->ToJson();
            if (CustomValue) j["customValue"] = *CustomValue;
            j["durability"] = Durability;
            j["maxDurability"] = MaxDurability;
            j["ownerId"] = OwnerEntityId;
            return j;
        }

        void FromJson(const Json& j) {
            InstanceId = j.value("instanceId", 0ULL);
            DefinitionId = j.value("definitionId", "");
            Quantity = j.value("quantity", 1);
            if (j.contains("customName")) CustomName = j["customName"].get<std::string>();
            if (j.contains("customStats")) {
                ItemStats stats;
                stats.FromJson(j["customStats"]);
                CustomStats = stats;
            }
            if (j.contains("customValue")) CustomValue = j["customValue"].get<uint32_t>();
            Durability = j.value("durability", 100.0f);
            MaxDurability = j.value("maxDurability", 100.0f);
            OwnerEntityId = j.value("ownerId", 0);
        }
    };

    //=========================================================================
    // Item Database
    //=========================================================================

    class ItemDatabase {
    public:
        static ItemDatabase& Get() {
            static ItemDatabase instance;
            return instance;
        }

        // ================================================================
        // Registration
        // ================================================================

        void RegisterItem(const ItemDefinition& item) {
            m_Items[item.Id] = item;
        }

        void UnregisterItem(const std::string& id) {
            m_Items.erase(id);
        }

        // ================================================================
        // Lookup
        // ================================================================

        const ItemDefinition* GetItem(const std::string& id) const {
            auto it = m_Items.find(id);
            return it != m_Items.end() ? &it->second : nullptr;
        }

        bool HasItem(const std::string& id) const {
            return m_Items.contains(id);
        }

        std::vector<std::string> GetAllItemIds() const {
            std::vector<std::string> ids;
            ids.reserve(m_Items.size());
            for (const auto& [id, _] : m_Items) {
                ids.push_back(id);
            }
            return ids;
        }

        // ================================================================
        // Filtering
        // ================================================================

        std::vector<const ItemDefinition*> GetItemsByType(ItemType type) const {
            std::vector<const ItemDefinition*> results;
            for (const auto& [_, item] : m_Items) {
                if (item.Type == type) {
                    results.push_back(&item);
                }
            }
            return results;
        }

        std::vector<const ItemDefinition*> GetItemsByRarity(ItemRarity rarity) const {
            std::vector<const ItemDefinition*> results;
            for (const auto& [_, item] : m_Items) {
                if (item.Rarity == rarity) {
                    results.push_back(&item);
                }
            }
            return results;
        }

        std::vector<const ItemDefinition*> GetItemsByTag(const std::string& tag) const {
            std::vector<const ItemDefinition*> results;
            for (const auto& [_, item] : m_Items) {
                for (const auto& t : item.Tags) {
                    if (t == tag) {
                        results.push_back(&item);
                        break;
                    }
                }
            }
            return results;
        }

        // ================================================================
        // Serialization
        // ================================================================

        Json ToJson() const {
            Json j = Json::array();
            for (const auto& [_, item] : m_Items) {
                j.push_back(item.ToJson());
            }
            return j;
        }

        void FromJson(const Json& j) {
            m_Items.clear();
            if (j.is_array()) {
                for (const auto& itemJson : j) {
                    ItemDefinition item;
                    item.FromJson(itemJson);
                    m_Items[item.Id] = item;
                }
            }
        }

        void Clear() {
            m_Items.clear();
        }

        size_t GetItemCount() const { return m_Items.size(); }

    private:
        ItemDatabase() = default;
        std::unordered_map<std::string, ItemDefinition> m_Items;
    };

    //=========================================================================
    // Utility Functions
    //=========================================================================

    inline const char* RarityToString(ItemRarity rarity) {
        switch (rarity) {
            case ItemRarity::Common: return "Common";
            case ItemRarity::Uncommon: return "Uncommon";
            case ItemRarity::Rare: return "Rare";
            case ItemRarity::Epic: return "Epic";
            case ItemRarity::Legendary: return "Legendary";
            case ItemRarity::Unique: return "Unique";
            default: return "Unknown";
        }
    }

    inline const char* ItemTypeToString(ItemType type) {
        switch (type) {
            case ItemType::None: return "None";
            case ItemType::Weapon: return "Weapon";
            case ItemType::Armor: return "Armor";
            case ItemType::Consumable: return "Consumable";
            case ItemType::Quest: return "Quest";
            case ItemType::Key: return "Key";
            case ItemType::Material: return "Material";
            case ItemType::Currency: return "Currency";
            case ItemType::Miscellaneous: return "Miscellaneous";
            default: return "Unknown";
        }
    }

} // namespace Gameplay
} // namespace Core

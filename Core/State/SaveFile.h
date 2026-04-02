#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace Core {
namespace State {

    /// @brief Magic number for save file identification
    constexpr uint32_t SAVE_FILE_MAGIC = 0x53415645;  // "SAVE" in ASCII

    /// @brief Current save file format version
    constexpr uint16_t SAVE_FILE_VERSION = 1;

    /// @brief Save file format type
    enum class SaveFormat : uint8_t {
        JSON = 0,       ///< Human-readable JSON format (for debugging)
        Binary = 1      ///< Compressed binary format (for production)
    };

    /// @brief Save file header structure
    /// 
    /// This header is written at the beginning of all save files for
    /// identification and versioning purposes.
    struct SaveHeader {
        uint32_t magic = SAVE_FILE_MAGIC;       ///< File identification magic
        uint16_t version = SAVE_FILE_VERSION;   ///< Format version for compatibility
        uint8_t format = static_cast<uint8_t>(SaveFormat::Binary);  ///< JSON or Binary
        uint8_t flags = 0;                      ///< Reserved for future use
        uint32_t dataSize = 0;                  ///< Size of payload data in bytes
        uint32_t checksum = 0;                  ///< CRC32 checksum of payload

        /// @brief Check if header is valid
        bool IsValid() const {
            return magic == SAVE_FILE_MAGIC && version <= SAVE_FILE_VERSION;
        }

        /// @brief Get format as enum
        SaveFormat GetFormat() const {
            return static_cast<SaveFormat>(format);
        }
    };

    /// @brief Save file metadata
    /// 
    /// Contains human-readable information about the save file.
    struct SaveMetadata {
        std::string saveName;               ///< User-given save name
        std::string sceneName;              ///< Current scene/level name
        std::string timestamp;              ///< ISO 8601 timestamp when saved
        uint32_t playtimeSeconds = 0;       ///< Total play time in seconds
        std::string thumbnailPath;          ///< Path to save screenshot (optional)
        std::string engineVersion;          ///< Engine version string
        uint32_t entityCount = 0;           ///< Number of entities in save
        
        /// @brief Get current timestamp as ISO 8601 string
        static std::string GetCurrentTimestamp();

        /// @brief Parse timestamp to time point
        std::chrono::system_clock::time_point GetTimestampAsTime() const;
    };

    /// @brief Player-specific state data
    /// 
    /// Contains player progress that persists across scenes.
    struct PlayerState {
        // Health and vitals
        int32_t health = 100;
        int32_t maxHealth = 100;
        int32_t stamina = 100;
        int32_t maxStamina = 100;

        // Position (checkpoint)
        glm::vec3 checkpointPosition = {0.0f, 0.0f, 0.0f};
        glm::vec3 checkpointRotation = {0.0f, 0.0f, 0.0f};
        std::string checkpointScene;

        // Inventory (serialized as JSON array)
        nlohmann::json inventory = nlohmann::json::array();

        // Quest progress (serialized as JSON object)
        nlohmann::json questProgress = nlohmann::json::object();

        // Player settings (keybindings, graphics, audio preferences)
        nlohmann::json settings = nlohmann::json::object();

        // Custom game-specific data
        nlohmann::json customData = nlohmann::json::object();
    };

    /// @brief Complete save file data structure
    /// 
    /// Contains all data needed to restore game state.
    struct SaveData {
        SaveHeader header;
        SaveMetadata metadata;
        PlayerState playerState;
        nlohmann::json sceneData;       ///< Serialized scene entities
    };

    // =========================================================================
    // JSON Serialization for Save Types
    // =========================================================================

    inline void to_json(nlohmann::json& j, const SaveMetadata& m) {
        j = nlohmann::json{
            {"saveName", m.saveName},
            {"sceneName", m.sceneName},
            {"timestamp", m.timestamp},
            {"playtimeSeconds", m.playtimeSeconds},
            {"thumbnailPath", m.thumbnailPath},
            {"engineVersion", m.engineVersion},
            {"entityCount", m.entityCount}
        };
    }

    inline void from_json(const nlohmann::json& j, SaveMetadata& m) {
        m.saveName = j.value("saveName", "");
        m.sceneName = j.value("sceneName", "");
        m.timestamp = j.value("timestamp", "");
        m.playtimeSeconds = j.value("playtimeSeconds", 0u);
        m.thumbnailPath = j.value("thumbnailPath", "");
        m.engineVersion = j.value("engineVersion", "");
        m.entityCount = j.value("entityCount", 0u);
    }

    inline void to_json(nlohmann::json& j, const PlayerState& p) {
        j = nlohmann::json{
            {"health", p.health},
            {"maxHealth", p.maxHealth},
            {"stamina", p.stamina},
            {"maxStamina", p.maxStamina},
            {"checkpointPosition", {p.checkpointPosition.x, p.checkpointPosition.y, p.checkpointPosition.z}},
            {"checkpointRotation", {p.checkpointRotation.x, p.checkpointRotation.y, p.checkpointRotation.z}},
            {"checkpointScene", p.checkpointScene},
            {"inventory", p.inventory},
            {"questProgress", p.questProgress},
            {"settings", p.settings},
            {"customData", p.customData}
        };
    }

    inline void from_json(const nlohmann::json& j, PlayerState& p) {
        p.health = j.value("health", 100);
        p.maxHealth = j.value("maxHealth", 100);
        p.stamina = j.value("stamina", 100);
        p.maxStamina = j.value("maxStamina", 100);
        
        if (j.contains("checkpointPosition") && j["checkpointPosition"].is_array()) {
            auto& pos = j["checkpointPosition"];
            p.checkpointPosition = {pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};
        }
        
        if (j.contains("checkpointRotation") && j["checkpointRotation"].is_array()) {
            auto& rot = j["checkpointRotation"];
            p.checkpointRotation = {rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>()};
        }
        
        p.checkpointScene = j.value("checkpointScene", "");
        p.inventory = j.value("inventory", nlohmann::json::array());
        p.questProgress = j.value("questProgress", nlohmann::json::object());
        p.settings = j.value("settings", nlohmann::json::object());
        p.customData = j.value("customData", nlohmann::json::object());
    }

    // =========================================================================
    // Save File Utilities
    // =========================================================================

    /// @brief Calculate CRC32 checksum for data validation
    /// @param data Pointer to data buffer
    /// @param size Size of data in bytes
    /// @return CRC32 checksum
    uint32_t CalculateCRC32(const void* data, size_t size);

    /// @brief Validate save file integrity
    /// @param filepath Path to save file
    /// @return true if file is valid
    bool ValidateSaveFile(const std::string& filepath);

    /// @brief Get save file metadata without loading full data
    /// @param filepath Path to save file
    /// @param outMetadata Output metadata structure
    /// @return true if metadata was read successfully
    bool ReadSaveMetadata(const std::string& filepath, SaveMetadata& outMetadata);

    /// @brief Generate save slot name from current time
    /// @return Auto-generated slot name (e.g., "autosave_20240101_120000")
    std::string GenerateSaveSlotName(const std::string& prefix = "save");

    /// @brief Get default saves directory path
    /// @return Platform-specific saves directory
    std::string GetSavesDirectory();

    /// @brief Get full path for a save slot
    /// @param slotName Save slot name
    /// @param format Save format (determines file extension)
    /// @return Full file path
    std::string GetSaveFilePath(const std::string& slotName, SaveFormat format);

} // namespace State
} // namespace Core

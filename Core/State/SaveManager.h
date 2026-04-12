#pragma once

#include "SaveFile.h"
#include <future>
#include <mutex>
#include <filesystem>

namespace Core {

// Forward declarations
namespace ECS {
    class Scene;
}

namespace State {

    /// @brief Save/Load manager for game state persistence
    /// 
    /// SaveManager handles:
    /// - Saving and loading game state to/from disk
    /// - Quick save/quick load functionality
    /// - Auto-save with configurable intervals
    /// - Save file enumeration and management
    /// - Async save/load operations
    /// 
    /// Usage:
    /// @code
    /// // Save current state
    /// SaveManager::Get().Save("slot1", scene, playerState);
    /// 
    /// // Load state
    /// PlayerState playerState;
    /// SaveManager::Get().Load("slot1", scene, playerState);
    /// 
    /// // Quick save/load
    /// SaveManager::Get().QuickSave(scene, playerState);
    /// SaveManager::Get().QuickLoad(scene, playerState);
    /// @endcode
    class SaveManager {
    public:
        static SaveManager& Get();

        // =====================================================================
        // Initialization
        // =====================================================================

        /// @brief Initialize save manager with custom save directory
        /// @param saveDirectory Path to saves directory (uses default if empty)
        void Initialize(const std::string& saveDirectory = "");

        /// @brief Shutdown and cleanup
        void Shutdown();

        /// @brief Set the scene serialization callback
        using SceneSerializeFunc = std::function<nlohmann::json(ECS::Scene*)>;
        using SceneDeserializeFunc = std::function<bool(ECS::Scene*, const nlohmann::json&)>;
        
        void SetSceneSerializeCallback(SceneSerializeFunc func) { m_SerializeScene = std::move(func); }
        void SetSceneDeserializeCallback(SceneDeserializeFunc func) { m_DeserializeScene = std::move(func); }

        // =====================================================================
        // Save Operations
        // =====================================================================

        /// @brief Save game state to named slot
        /// @param slotName Save slot name
        /// @param scene Current scene to serialize
        /// @param playerState Player-specific state
        /// @param format Save format (JSON for debugging, Binary for production)
        /// @return true if save succeeded
        bool Save(const std::string& slotName, ECS::Scene* scene,
                  const PlayerState& playerState, SaveFormat format = SaveFormat::Binary);

        /// @brief Quick save to default quick save slot
        bool QuickSave(ECS::Scene* scene, const PlayerState& playerState);

        /// @brief Auto-save (called automatically based on interval)
        bool AutoSave(ECS::Scene* scene, const PlayerState& playerState);

        // =====================================================================
        // Load Operations
        // =====================================================================

        /// @brief Load game state from named slot
        /// @param slotName Save slot name
        /// @param scene Scene to populate
        /// @param outPlayerState Output player state
        /// @return true if load succeeded
        bool Load(const std::string& slotName, ECS::Scene* scene,
                  PlayerState& outPlayerState);

        /// @brief Quick load from default quick save slot
        bool QuickLoad(ECS::Scene* scene, PlayerState& outPlayerState);

        // =====================================================================
        // Scene Asset Round-Trip
        // =====================================================================

        /// @brief Serialize an ECS scene into a standalone scene asset file
        bool SerializeSceneToAsset(const std::string& assetPath, ECS::Scene* scene);

        /// @brief Deserialize a scene asset file into an ECS scene
        bool DeserializeSceneFromAsset(const std::string& assetPath, ECS::Scene* scene);

        // =====================================================================
        // Async Operations
        // =====================================================================

        /// @brief Async save operation
        std::future<bool> SaveAsync(const std::string& slotName, ECS::Scene* scene,
                                     const PlayerState& playerState,
                                     SaveFormat format = SaveFormat::Binary);

        /// @brief Async load operation
        std::future<bool> LoadAsync(const std::string& slotName, ECS::Scene* scene,
                                     PlayerState& outPlayerState);

        /// @brief Check if async operation is in progress
        bool IsAsyncOperationInProgress() const { return m_AsyncInProgress; }

        // =====================================================================
        // Save Management
        // =====================================================================

        /// @brief Get list of all save slots
        std::vector<SaveMetadata> GetSaveSlots();

        /// @brief Delete a save slot
        bool DeleteSave(const std::string& slotName);

        /// @brief Validate save file integrity
        bool ValidateSave(const std::string& slotName);

        /// @brief Check if save slot exists
        bool SaveExists(const std::string& slotName);

        /// @brief Get metadata for a save slot
        bool GetSaveMetadata(const std::string& slotName, SaveMetadata& outMetadata);

        // =====================================================================
        // Auto-Save Configuration
        // =====================================================================

        /// @brief Set auto-save interval in seconds
        void SetAutoSaveInterval(float seconds) { m_AutoSaveInterval = seconds; }
        float GetAutoSaveInterval() const { return m_AutoSaveInterval; }

        /// @brief Enable/disable auto-save
        void EnableAutoSave(bool enable) { m_AutoSaveEnabled = enable; }
        bool IsAutoSaveEnabled() const { return m_AutoSaveEnabled; }

        /// @brief Set maximum number of auto-saves to keep (rotation)
        void SetMaxAutoSaves(uint32_t max) { m_MaxAutoSaves = max; }
        uint32_t GetMaxAutoSaves() const { return m_MaxAutoSaves; }

        /// @brief Update auto-save timer (call each frame)
        /// @param deltaTime Frame delta time
        /// @param scene Current scene
        /// @param playerState Current player state
        /// @return true if auto-save was triggered
        bool UpdateAutoSave(float deltaTime, ECS::Scene* scene, const PlayerState& playerState);

        // =====================================================================
        // Configuration
        // =====================================================================

        /// @brief Get saves directory path
        const std::string& GetSavesDirectory() const { return m_SaveDirectory; }

        /// @brief Set engine version string (included in saves)
        void SetEngineVersion(const std::string& version) { m_EngineVersion = version; }

    private:
        SaveManager() = default;
        ~SaveManager() = default;

        // Delete copy/move
        SaveManager(const SaveManager&) = delete;
        SaveManager& operator=(const SaveManager&) = delete;

        /// @brief Internal save implementation
        bool SaveInternal(const std::string& filepath, ECS::Scene* scene,
                          const PlayerState& playerState, const SaveMetadata& metadata,
                          SaveFormat format);

        /// @brief Internal load implementation
        bool LoadInternal(const std::string& filepath, ECS::Scene* scene,
                          PlayerState& outPlayerState);

        /// @brief Write JSON format save
        bool WriteJsonSave(const std::string& filepath, const SaveData& data);

        /// @brief Write binary format save
        bool WriteBinarySave(const std::string& filepath, const SaveData& data);

        /// @brief Read JSON format save
        bool ReadJsonSave(const std::string& filepath, SaveData& outData);

        /// @brief Read binary format save
        bool ReadBinarySave(const std::string& filepath, SaveData& outData);

        /// @brief Backup existing save before overwrite
        bool BackupSave(const std::string& filepath);

        /// @brief Rotate auto-saves (delete oldest if over limit)
        void RotateAutoSaves();

    private:
        bool m_Initialized = false;
        std::string m_SaveDirectory;
        std::string m_EngineVersion = "1.0.0";

        // Auto-save configuration
        float m_AutoSaveInterval = 300.0f;  // 5 minutes
        bool m_AutoSaveEnabled = true;
        float m_AutoSaveTimer = 0.0f;
        uint32_t m_MaxAutoSaves = 3;
        uint32_t m_AutoSaveCounter = 0;

        // Quick save slot name
        static constexpr const char* QUICK_SAVE_SLOT = "quicksave";
        static constexpr const char* AUTO_SAVE_PREFIX = "autosave_";

        // Async operation tracking
        std::atomic<bool> m_AsyncInProgress{false};
        std::mutex m_SaveMutex;

        // Scene serialization callbacks
        SceneSerializeFunc m_SerializeScene;
        SceneDeserializeFunc m_DeserializeScene;
    };

} // namespace State
} // namespace Core

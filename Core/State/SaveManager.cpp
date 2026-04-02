#include "SaveManager.h"
#include "Core/Log.h"
#include "Core/ECS/Scene.h"

#include <fstream>
#include <algorithm>

namespace Core {
namespace State {

// ============================================================================
// Singleton Instance
// ============================================================================

SaveManager& SaveManager::Get() {
    static SaveManager instance;
    return instance;
}

// ============================================================================
// Initialization
// ============================================================================

void SaveManager::Initialize(const std::string& saveDirectory) {
    if (m_Initialized) {
        ENGINE_CORE_WARN("SaveManager::Initialize - Already initialized");
        return;
    }

    m_SaveDirectory = saveDirectory.empty() ? GetSavesDirectory() : saveDirectory;

    // Ensure directory exists
    if (!std::filesystem::exists(m_SaveDirectory)) {
        std::filesystem::create_directories(m_SaveDirectory);
    }

    m_Initialized = true;
    ENGINE_CORE_INFO("SaveManager initialized (saves: {})", m_SaveDirectory);
}

void SaveManager::Shutdown() {
    m_Initialized = false;
    ENGINE_CORE_INFO("SaveManager shut down");
}

// ============================================================================
// Save Operations
// ============================================================================

bool SaveManager::Save(const std::string& slotName, ECS::Scene* scene,
                       const PlayerState& playerState, SaveFormat format) {
    if (!m_Initialized) {
        ENGINE_CORE_ERROR("SaveManager::Save - Not initialized");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_SaveMutex);

    std::string filepath = GetSaveFilePath(slotName, format);
    
    SaveMetadata metadata;
    metadata.saveName = slotName;
    metadata.sceneName = scene ? "CurrentScene" : "Unknown";
    metadata.timestamp = SaveMetadata::GetCurrentTimestamp();
    metadata.engineVersion = m_EngineVersion;

    return SaveInternal(filepath, scene, playerState, metadata, format);
}

bool SaveManager::QuickSave(ECS::Scene* scene, const PlayerState& playerState) {
    ENGINE_CORE_INFO("SaveManager: Quick save...");
    return Save(QUICK_SAVE_SLOT, scene, playerState, SaveFormat::Binary);
}

bool SaveManager::AutoSave(ECS::Scene* scene, const PlayerState& playerState) {
    // Generate auto-save slot name
    std::string slotName = AUTO_SAVE_PREFIX + std::to_string(m_AutoSaveCounter % m_MaxAutoSaves);
    m_AutoSaveCounter++;

    ENGINE_CORE_INFO("SaveManager: Auto-save to slot '{}'", slotName);
    return Save(slotName, scene, playerState, SaveFormat::Binary);
}

// ============================================================================
// Load Operations
// ============================================================================

bool SaveManager::Load(const std::string& slotName, ECS::Scene* scene,
                       PlayerState& outPlayerState) {
    if (!m_Initialized) {
        ENGINE_CORE_ERROR("SaveManager::Load - Not initialized");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_SaveMutex);

    // Try binary format first, then JSON
    std::string binaryPath = GetSaveFilePath(slotName, SaveFormat::Binary);
    std::string jsonPath = GetSaveFilePath(slotName, SaveFormat::JSON);

    if (std::filesystem::exists(binaryPath)) {
        return LoadInternal(binaryPath, scene, outPlayerState);
    } else if (std::filesystem::exists(jsonPath)) {
        return LoadInternal(jsonPath, scene, outPlayerState);
    }

    ENGINE_CORE_ERROR("SaveManager::Load - Save not found: {}", slotName);
    return false;
}

bool SaveManager::QuickLoad(ECS::Scene* scene, PlayerState& outPlayerState) {
    ENGINE_CORE_INFO("SaveManager: Quick load...");
    return Load(QUICK_SAVE_SLOT, scene, outPlayerState);
}

// ============================================================================
// Async Operations
// ============================================================================

std::future<bool> SaveManager::SaveAsync(const std::string& slotName, ECS::Scene* scene,
                                          const PlayerState& playerState, SaveFormat format) {
    m_AsyncInProgress = true;
    
    return std::async(std::launch::async, [this, slotName, scene, playerState, format]() {
        bool result = Save(slotName, scene, playerState, format);
        m_AsyncInProgress = false;
        return result;
    });
}

std::future<bool> SaveManager::LoadAsync(const std::string& slotName, ECS::Scene* scene,
                                          PlayerState& outPlayerState) {
    m_AsyncInProgress = true;
    
    return std::async(std::launch::async, [this, slotName, scene, &outPlayerState]() {
        bool result = Load(slotName, scene, outPlayerState);
        m_AsyncInProgress = false;
        return result;
    });
}

// ============================================================================
// Save Management
// ============================================================================

std::vector<SaveMetadata> SaveManager::GetSaveSlots() {
    std::vector<SaveMetadata> saves;

    if (!m_Initialized || !std::filesystem::exists(m_SaveDirectory)) {
        return saves;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_SaveDirectory)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".sav" && ext != ".json") continue;

        SaveMetadata metadata;
        if (ReadSaveMetadata(entry.path().string(), metadata)) {
            saves.push_back(metadata);
        }
    }

    // Sort by timestamp (newest first)
    std::sort(saves.begin(), saves.end(), [](const SaveMetadata& a, const SaveMetadata& b) {
        return a.timestamp > b.timestamp;
    });

    return saves;
}

bool SaveManager::DeleteSave(const std::string& slotName) {
    std::string binaryPath = GetSaveFilePath(slotName, SaveFormat::Binary);
    std::string jsonPath = GetSaveFilePath(slotName, SaveFormat::JSON);

    bool deleted = false;
    if (std::filesystem::exists(binaryPath)) {
        std::filesystem::remove(binaryPath);
        deleted = true;
    }
    if (std::filesystem::exists(jsonPath)) {
        std::filesystem::remove(jsonPath);
        deleted = true;
    }

    if (deleted) {
        ENGINE_CORE_INFO("SaveManager: Deleted save '{}'", slotName);
    }
    return deleted;
}

bool SaveManager::ValidateSave(const std::string& slotName) {
    std::string binaryPath = GetSaveFilePath(slotName, SaveFormat::Binary);
    std::string jsonPath = GetSaveFilePath(slotName, SaveFormat::JSON);

    if (std::filesystem::exists(binaryPath)) {
        return ValidateSaveFile(binaryPath);
    } else if (std::filesystem::exists(jsonPath)) {
        return ValidateSaveFile(jsonPath);
    }
    return false;
}

bool SaveManager::SaveExists(const std::string& slotName) {
    return std::filesystem::exists(GetSaveFilePath(slotName, SaveFormat::Binary)) ||
           std::filesystem::exists(GetSaveFilePath(slotName, SaveFormat::JSON));
}

bool SaveManager::GetSaveMetadata(const std::string& slotName, SaveMetadata& outMetadata) {
    std::string binaryPath = GetSaveFilePath(slotName, SaveFormat::Binary);
    std::string jsonPath = GetSaveFilePath(slotName, SaveFormat::JSON);

    if (std::filesystem::exists(binaryPath)) {
        return ReadSaveMetadata(binaryPath, outMetadata);
    } else if (std::filesystem::exists(jsonPath)) {
        return ReadSaveMetadata(jsonPath, outMetadata);
    }
    return false;
}

// ============================================================================
// Auto-Save
// ============================================================================

bool SaveManager::UpdateAutoSave(float deltaTime, ECS::Scene* scene, const PlayerState& playerState) {
    if (!m_AutoSaveEnabled || !m_Initialized) {
        return false;
    }

    m_AutoSaveTimer += deltaTime;
    
    if (m_AutoSaveTimer >= m_AutoSaveInterval) {
        m_AutoSaveTimer = 0.0f;
        return AutoSave(scene, playerState);
    }

    return false;
}

// ============================================================================
// Internal Implementation
// ============================================================================

bool SaveManager::SaveInternal(const std::string& filepath, ECS::Scene* scene,
                                const PlayerState& playerState, const SaveMetadata& metadata,
                                SaveFormat format) {
    // Backup existing save
    if (std::filesystem::exists(filepath)) {
        BackupSave(filepath);
    }

    SaveData data;
    data.metadata = metadata;
    data.playerState = playerState;

    // Serialize scene if callback is set
    if (m_SerializeScene && scene) {
        data.sceneData = m_SerializeScene(scene);
        data.metadata.entityCount = static_cast<uint32_t>(data.sceneData.size());
    } else {
        data.sceneData = nlohmann::json::object();
    }

    bool success = false;
    if (format == SaveFormat::JSON) {
        success = WriteJsonSave(filepath, data);
    } else {
        success = WriteBinarySave(filepath, data);
    }

    if (success) {
        ENGINE_CORE_INFO("SaveManager: Saved to '{}'", filepath);
    } else {
        ENGINE_CORE_ERROR("SaveManager: Failed to save to '{}'", filepath);
    }

    return success;
}

bool SaveManager::LoadInternal(const std::string& filepath, ECS::Scene* scene,
                                PlayerState& outPlayerState) {
    SaveData data;
    bool success = false;

    // Determine format from extension
    std::string ext = std::filesystem::path(filepath).extension().string();
    if (ext == ".json") {
        success = ReadJsonSave(filepath, data);
    } else {
        success = ReadBinarySave(filepath, data);
    }

    if (!success) {
        ENGINE_CORE_ERROR("SaveManager: Failed to read save '{}'", filepath);
        return false;
    }

    outPlayerState = data.playerState;

    // Deserialize scene if callback is set
    if (m_DeserializeScene && scene) {
        if (!m_DeserializeScene(scene, data.sceneData)) {
            ENGINE_CORE_WARN("SaveManager: Scene deserialization had warnings");
        }
    }

    ENGINE_CORE_INFO("SaveManager: Loaded from '{}'", filepath);
    return true;
}

bool SaveManager::WriteJsonSave(const std::string& filepath, const SaveData& data) {
    try {
        nlohmann::json saveJson;
        saveJson["metadata"] = data.metadata;
        saveJson["playerState"] = data.playerState;
        saveJson["sceneData"] = data.sceneData;

        std::string jsonStr = saveJson.dump(2);  // Pretty print

        // Build save with header
        SaveHeader header;
        header.format = static_cast<uint8_t>(SaveFormat::JSON);
        header.dataSize = static_cast<uint32_t>(jsonStr.size());
        header.checksum = CalculateCRC32(jsonStr.data(), jsonStr.size());

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(jsonStr.data(), jsonStr.size());
        
        return file.good();
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SaveManager::WriteJsonSave - Error: {}", e.what());
        return false;
    }
}

bool SaveManager::WriteBinarySave(const std::string& filepath, const SaveData& data) {
    try {
        // Serialize to JSON first, then write as binary
        nlohmann::json saveJson;
        saveJson["metadata"] = data.metadata;
        saveJson["playerState"] = data.playerState;
        saveJson["sceneData"] = data.sceneData;

        // Use MessagePack for compact binary format
        std::vector<uint8_t> binaryData = nlohmann::json::to_msgpack(saveJson);

        // Build save with header
        SaveHeader header;
        header.format = static_cast<uint8_t>(SaveFormat::Binary);
        header.dataSize = static_cast<uint32_t>(binaryData.size());
        header.checksum = CalculateCRC32(binaryData.data(), binaryData.size());

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(binaryData.data()), binaryData.size());
        
        return file.good();
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SaveManager::WriteBinarySave - Error: {}", e.what());
        return false;
    }
}

bool SaveManager::ReadJsonSave(const std::string& filepath, SaveData& outData) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        SaveHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (!header.IsValid()) {
            return false;
        }

        std::vector<char> jsonData(header.dataSize);
        file.read(jsonData.data(), header.dataSize);

        // Verify checksum
        uint32_t checksum = CalculateCRC32(jsonData.data(), jsonData.size());
        if (checksum != header.checksum) {
            ENGINE_CORE_WARN("SaveManager::ReadJsonSave - Checksum mismatch");
        }

        nlohmann::json saveJson = nlohmann::json::parse(jsonData.begin(), jsonData.end());
        
        outData.header = header;
        outData.metadata = saveJson["metadata"].get<SaveMetadata>();
        outData.playerState = saveJson["playerState"].get<PlayerState>();
        outData.sceneData = saveJson.value("sceneData", nlohmann::json::object());

        return true;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SaveManager::ReadJsonSave - Error: {}", e.what());
        return false;
    }
}

bool SaveManager::ReadBinarySave(const std::string& filepath, SaveData& outData) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        SaveHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (!header.IsValid()) {
            return false;
        }

        std::vector<uint8_t> binaryData(header.dataSize);
        file.read(reinterpret_cast<char*>(binaryData.data()), header.dataSize);

        // Verify checksum
        uint32_t checksum = CalculateCRC32(binaryData.data(), binaryData.size());
        if (checksum != header.checksum) {
            ENGINE_CORE_WARN("SaveManager::ReadBinarySave - Checksum mismatch");
        }

        nlohmann::json saveJson = nlohmann::json::from_msgpack(binaryData);
        
        outData.header = header;
        outData.metadata = saveJson["metadata"].get<SaveMetadata>();
        outData.playerState = saveJson["playerState"].get<PlayerState>();
        outData.sceneData = saveJson.value("sceneData", nlohmann::json::object());

        return true;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SaveManager::ReadBinarySave - Error: {}", e.what());
        return false;
    }
}

bool SaveManager::BackupSave(const std::string& filepath) {
    std::string backupPath = filepath + ".bak";
    
    try {
        if (std::filesystem::exists(backupPath)) {
            std::filesystem::remove(backupPath);
        }
        std::filesystem::rename(filepath, backupPath);
        return true;
    } catch (const std::exception& e) {
        ENGINE_CORE_WARN("SaveManager::BackupSave - Failed: {}", e.what());
        return false;
    }
}

void SaveManager::RotateAutoSaves() {
    // Find and delete oldest auto-saves if over limit
    std::vector<std::pair<std::string, std::filesystem::file_time_type>> autoSaves;

    for (const auto& entry : std::filesystem::directory_iterator(m_SaveDirectory)) {
        std::string filename = entry.path().stem().string();
        if (filename.find(AUTO_SAVE_PREFIX) == 0) {
            autoSaves.push_back({entry.path().string(), entry.last_write_time()});
        }
    }

    // Sort by age (oldest first)
    std::sort(autoSaves.begin(), autoSaves.end(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Delete oldest if over limit
    while (autoSaves.size() > m_MaxAutoSaves) {
        std::filesystem::remove(autoSaves.front().first);
        autoSaves.erase(autoSaves.begin());
    }
}

} // namespace State
} // namespace Core

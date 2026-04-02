#include "SaveFile.h"
#include "Core/Log.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace Core {
namespace State {

// ============================================================================
// SaveMetadata Implementation
// ============================================================================

std::string SaveMetadata::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

std::chrono::system_clock::time_point SaveMetadata::GetTimestampAsTime() const {
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// ============================================================================
// CRC32 Implementation (standard polynomial)
// ============================================================================

static uint32_t s_CRC32Table[256] = {0};
static bool s_CRC32TableInitialized = false;

static void InitCRC32Table() {
    if (s_CRC32TableInitialized) return;
    
    const uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
        s_CRC32Table[i] = crc;
    }
    s_CRC32TableInitialized = true;
}

uint32_t CalculateCRC32(const void* data, size_t size) {
    InitCRC32Table();
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < size; ++i) {
        crc = s_CRC32Table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// Save File Utilities
// ============================================================================

bool ValidateSaveFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        ENGINE_CORE_WARN("ValidateSaveFile - Cannot open file: {}", filepath);
        return false;
    }

    // Read header
    SaveHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(SaveHeader));
    
    if (!header.IsValid()) {
        ENGINE_CORE_WARN("ValidateSaveFile - Invalid header in: {}", filepath);
        return false;
    }

    // Read payload for checksum verification
    std::vector<char> payload(header.dataSize);
    file.read(payload.data(), header.dataSize);
    
    if (static_cast<size_t>(file.gcount()) != header.dataSize) {
        ENGINE_CORE_WARN("ValidateSaveFile - Truncated file: {}", filepath);
        return false;
    }

    // Verify checksum
    uint32_t calculatedChecksum = CalculateCRC32(payload.data(), payload.size());
    if (calculatedChecksum != header.checksum) {
        ENGINE_CORE_WARN("ValidateSaveFile - Checksum mismatch in: {}", filepath);
        return false;
    }

    return true;
}

bool ReadSaveMetadata(const std::string& filepath, SaveMetadata& outMetadata) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read header
    SaveHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(SaveHeader));
    
    if (!header.IsValid()) {
        return false;
    }

    // Read payload
    std::vector<char> payload(header.dataSize);
    file.read(payload.data(), header.dataSize);

    try {
        // For JSON format, parse directly
        if (header.GetFormat() == SaveFormat::JSON) {
            nlohmann::json saveJson = nlohmann::json::parse(payload.begin(), payload.end());
            if (saveJson.contains("metadata")) {
                outMetadata = saveJson["metadata"].get<SaveMetadata>();
                return true;
            }
        }
        // For binary format, decompress first, then parse metadata section
        else {
            // Binary format starts with metadata size (uint32_t), then JSON metadata
            if (payload.size() < sizeof(uint32_t)) {
                return false;
            }
            
            uint32_t metadataSize = *reinterpret_cast<const uint32_t*>(payload.data());
            if (metadataSize > payload.size() - sizeof(uint32_t)) {
                return false;
            }
            
            std::string metadataJson(payload.data() + sizeof(uint32_t), metadataSize);
            nlohmann::json j = nlohmann::json::parse(metadataJson);
            outMetadata = j.get<SaveMetadata>();
            return true;
        }
    } catch (const std::exception& e) {
        ENGINE_CORE_WARN("ReadSaveMetadata - Parse error: {}", e.what());
    }

    return false;
}

std::string GenerateSaveSlotName(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << prefix << "_" << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S");
    return ss.str();
}

std::string GetSavesDirectory() {
    // Get user-specific saves directory
    std::filesystem::path savesPath;

#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        savesPath = std::filesystem::path(appdata) / "AIGameEngine" / "Saves";
    } else {
        savesPath = std::filesystem::current_path() / "Saves";
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        savesPath = std::filesystem::path(home) / ".aigameengine" / "saves";
    } else {
        savesPath = std::filesystem::current_path() / "Saves";
    }
#endif

    // Ensure directory exists
    if (!std::filesystem::exists(savesPath)) {
        std::filesystem::create_directories(savesPath);
    }

    return savesPath.string();
}

std::string GetSaveFilePath(const std::string& slotName, SaveFormat format) {
    std::string extension = (format == SaveFormat::JSON) ? ".json" : ".sav";
    std::filesystem::path savePath = std::filesystem::path(GetSavesDirectory()) / (slotName + extension);
    return savePath.string();
}

} // namespace State
} // namespace Core

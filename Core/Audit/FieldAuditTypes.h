#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Audit {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

struct FieldSourceTraceMetadata {
    std::string SourceFile;
    std::string SourceSymbol;
    std::string CollectorId;
    uint32_t SourceLine = 0;
};

struct FieldInventoryEntry {
    std::string FieldId;
    std::string Domain;
    std::string OwnerSubsystem;
    std::string TypeName;
    std::string FieldPath;
    bool Required = true;
    std::vector<std::string> AliasFieldIds;
    std::vector<std::string> VersionLineage;
    FieldSourceTraceMetadata SourceTrace;
};

struct FieldInventoryRequest {
    std::string Scope = "runtime";
    std::filesystem::path OutputDirectory;
};

struct FieldInventorySnapshot {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::vector<std::string> Domains;
    std::vector<FieldInventoryEntry> Entries;
    std::string DeterministicDigest;
};

struct MergeFieldInventoryRequest {
    std::string Scope = "merged";
    std::filesystem::path OutputDirectory;
    FieldInventorySnapshot RuntimeSnapshot;
    FieldInventorySnapshot SerializedSnapshot;
    FieldInventorySnapshot ProtocolSnapshot;
};

} // namespace Core::Audit

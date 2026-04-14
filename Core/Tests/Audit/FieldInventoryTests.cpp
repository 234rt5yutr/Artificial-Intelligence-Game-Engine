#include "Core/Audit/FieldInventoryService.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

Core::Audit::FieldInventoryRequest BuildValidRequest(const std::filesystem::path& outputRoot,
                                                     const std::string_view scope = "runtime") {
    Core::Audit::FieldInventoryRequest request{};
    request.OutputDirectory = outputRoot;
    request.Scope = scope;
    return request;
}

std::set<std::string> CollectDomains(const Core::Audit::FieldInventorySnapshot& snapshot) {
    std::set<std::string> domains;
    for (const Core::Audit::FieldInventoryEntry& entry : snapshot.Entries) {
        domains.insert(entry.Domain);
    }
    return domains;
}

bool AreFieldEntriesSorted(const Core::Audit::FieldInventorySnapshot& snapshot) {
    return std::is_sorted(snapshot.Entries.begin(),
                          snapshot.Entries.end(),
                          [](const Core::Audit::FieldInventoryEntry& left, const Core::Audit::FieldInventoryEntry& right) {
                              if (left.FieldId != right.FieldId) {
                                  return left.FieldId < right.FieldId;
                              }
                              if (left.Domain != right.Domain) {
                                  return left.Domain < right.Domain;
                              }
                              if (left.TypeName != right.TypeName) {
                                  return left.TypeName < right.TypeName;
                              }
                              return left.FieldPath < right.FieldPath;
                          });
}

std::string ComposeFieldId(const std::string_view domain, const std::string_view typeName, const std::string_view fieldPath) {
    return std::string(domain) + "::" + std::string(typeName) + "::" + std::string(fieldPath);
}

Core::Audit::FieldInventoryEntry BuildFieldEntry(const std::string_view domain,
                                                 const std::string_view ownerSubsystem,
                                                 const std::string_view typeName,
                                                 const std::string_view fieldPath,
                                                 const std::string_view collectorId) {
    Core::Audit::FieldInventoryEntry entry{};
    entry.Domain = domain;
    entry.OwnerSubsystem = ownerSubsystem;
    entry.TypeName = typeName;
    entry.FieldPath = fieldPath;
    entry.FieldId = ComposeFieldId(domain, typeName, fieldPath);
    entry.Required = true;
    entry.SourceTrace.SourceFile = "Core/Tests/Audit/FieldInventoryTests.cpp";
    entry.SourceTrace.SourceSymbol = "BuildFieldEntry";
    entry.SourceTrace.SourceLine = 1u;
    entry.SourceTrace.CollectorId = collectorId;
    return entry;
}

const Core::Audit::FieldInventoryEntry* FindEntryByStableKey(const Core::Audit::FieldInventorySnapshot& snapshot,
                                                             const std::string_view typeName,
                                                             const std::string_view fieldPath) {
    for (const Core::Audit::FieldInventoryEntry& entry : snapshot.Entries) {
        if (entry.TypeName == typeName && entry.FieldPath == fieldPath) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    using namespace Core::Audit;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-inventory-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldInventoryRequest invalidRequest{};
        const Result<FieldInventorySnapshot> result = GenerateRuntimeFieldInventory(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldInventoryRequest invalidRequest = BuildValidRequest(root / "unsupported-scope");
        invalidRequest.Scope = "serialized";
        const Result<FieldInventorySnapshot> result = GenerateRuntimeFieldInventory(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        const std::filesystem::path blockedPath = root / "blocked-output";
        std::filesystem::create_directories(root, errorCode);
        assert(!errorCode);
        std::ofstream blockedFile(blockedPath, std::ios::trunc);
        assert(blockedFile.is_open());
        blockedFile << "not-a-directory";
        blockedFile.flush();
        blockedFile.close();

        FieldInventoryRequest invalidRequest = BuildValidRequest(blockedPath);
        const Result<FieldInventorySnapshot> result = GenerateRuntimeFieldInventory(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    {
        const FieldInventoryRequest request = BuildValidRequest(root / "runtime-output");
        const Result<FieldInventorySnapshot> first = GenerateRuntimeFieldInventory(request);
        assert(first.Ok);
        assert(!first.Value.Entries.empty());
        assert(first.Value.Scope == "runtime");
        assert(first.Value.OutputDirectory == request.OutputDirectory);
        assert(!first.Value.DeterministicDigest.empty());
        assert(AreFieldEntriesSorted(first.Value));

        const std::set<std::string> domains = CollectDomains(first.Value);
        const std::array<std::string, 5> requiredDomains = {"ecs", "ui", "network", "diagnostics", "build"};
        for (const std::string& requiredDomain : requiredDomains) {
            if (!domains.contains(requiredDomain)) {
                return 1;
            }
        }

        for (const FieldInventoryEntry& entry : first.Value.Entries) {
            const bool hasCanonicalId = entry.FieldId == entry.Domain + "::" + entry.TypeName + "::" + entry.FieldPath;
            const bool hasTraceMetadata = !entry.SourceTrace.SourceFile.empty() && !entry.SourceTrace.CollectorId.empty();
            const bool hasInventoryMetadata =
                !entry.FieldId.empty() && !entry.Domain.empty() && !entry.TypeName.empty() && !entry.FieldPath.empty();
            if (!hasCanonicalId || !hasTraceMetadata || !hasInventoryMetadata) {
                return 1;
            }
        }

        const Result<FieldInventorySnapshot> second = GenerateRuntimeFieldInventory(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.Entries.size() == first.Value.Entries.size());
        for (std::size_t index = 0; index < first.Value.Entries.size(); ++index) {
            assert(second.Value.Entries[index].FieldId == first.Value.Entries[index].FieldId);
        }
    }

    {
        const FieldInventoryRequest request = BuildValidRequest(root / "serialized-output", "serialized");
        const Result<FieldInventorySnapshot> first = GenerateSerializedFieldInventory(request);
        assert(first.Ok);
        assert(first.Value.Scope == "serialized");
        assert(!first.Value.Entries.empty());
        assert(!first.Value.DeterministicDigest.empty());
        assert(AreFieldEntriesSorted(first.Value));

        const std::set<std::string> domains = CollectDomains(first.Value);
        const std::array<std::string, 8> requiredDomains = {
            "scene", "prefab", "save", "widget", "localization", "addressable", "bundle", "build-manifest"};
        for (const std::string& requiredDomain : requiredDomains) {
            if (!domains.contains(requiredDomain)) {
                return 1;
            }
        }

        bool foundRequiredField = false;
        bool foundOptionalField = false;
        for (const FieldInventoryEntry& entry : first.Value.Entries) {
            if (entry.Required) {
                foundRequiredField = true;
            } else {
                foundOptionalField = true;
            }

            const bool hasCanonicalId = entry.FieldId == entry.Domain + "::" + entry.TypeName + "::" + entry.FieldPath;
            const bool hasTraceMetadata = !entry.SourceTrace.SourceFile.empty() && !entry.SourceTrace.SourceSymbol.empty() &&
                                          !entry.SourceTrace.CollectorId.empty() && entry.SourceTrace.SourceLine > 0;
            const bool hasInventoryMetadata =
                !entry.Domain.empty() && !entry.TypeName.empty() && !entry.FieldPath.empty();
            if (!hasCanonicalId || !hasTraceMetadata || !hasInventoryMetadata) {
                return 1;
            }
        }
        assert(foundRequiredField);
        assert(foundOptionalField);

        const Result<FieldInventorySnapshot> second = GenerateSerializedFieldInventory(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.Entries.size() == first.Value.Entries.size());
        for (std::size_t index = 0; index < first.Value.Entries.size(); ++index) {
            assert(second.Value.Entries[index].FieldId == first.Value.Entries[index].FieldId);
            assert(second.Value.Entries[index].Required == first.Value.Entries[index].Required);
        }
    }

    {
        const FieldInventoryRequest invalidScope = BuildValidRequest(root / "serialized-scope-error", "runtime");
        const Result<FieldInventorySnapshot> result = GenerateSerializedFieldInventory(invalidScope);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldInventoryRequest invalidRequest{};
        const Result<FieldInventorySnapshot> result = GenerateProtocolFieldInventory(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        const FieldInventoryRequest request = BuildValidRequest(root / "protocol-output", "protocol");
        const Result<FieldInventorySnapshot> first = GenerateProtocolFieldInventory(request);
        assert(first.Ok);
        assert(first.Value.Scope == "protocol");
        assert(!first.Value.Entries.empty());
        assert(!first.Value.DeterministicDigest.empty());
        assert(AreFieldEntriesSorted(first.Value));

        const std::set<std::string> domains = CollectDomains(first.Value);
        const std::array<std::string, 5> requiredDomains = {"packets", "rpc", "replay", "mcp-request", "mcp-response"};
        for (const std::string& requiredDomain : requiredDomains) {
            if (!domains.contains(requiredDomain)) {
                return 1;
            }
        }

        bool foundRequiredField = false;
        bool foundOptionalField = false;
        for (const FieldInventoryEntry& entry : first.Value.Entries) {
            if (entry.Required) {
                foundRequiredField = true;
            } else {
                foundOptionalField = true;
            }

            const bool hasCanonicalId = entry.FieldId == entry.Domain + "::" + entry.TypeName + "::" + entry.FieldPath;
            const bool hasTraceMetadata = !entry.SourceTrace.SourceFile.empty() && !entry.SourceTrace.SourceSymbol.empty() &&
                                          !entry.SourceTrace.CollectorId.empty() && entry.SourceTrace.SourceLine > 0;
            const bool hasInventoryMetadata =
                !entry.Domain.empty() && !entry.TypeName.empty() && !entry.FieldPath.empty();
            if (!hasCanonicalId || !hasTraceMetadata || !hasInventoryMetadata) {
                return 1;
            }
        }
        assert(foundRequiredField);
        assert(foundOptionalField);

        const Result<FieldInventorySnapshot> second = GenerateProtocolFieldInventory(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.Entries.size() == first.Value.Entries.size());
        for (std::size_t index = 0; index < first.Value.Entries.size(); ++index) {
            assert(second.Value.Entries[index].FieldId == first.Value.Entries[index].FieldId);
            assert(second.Value.Entries[index].Required == first.Value.Entries[index].Required);
        }
    }

    {
        const FieldInventoryRequest invalidScope = BuildValidRequest(root / "protocol-scope-error", "runtime");
        const Result<FieldInventorySnapshot> result = GenerateProtocolFieldInventory(invalidScope);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        MergeFieldInventoryRequest invalidRequest{};
        const Result<FieldInventorySnapshot> result = MergeFieldInventorySnapshots(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        const Result<FieldInventorySnapshot> runtime =
            GenerateRuntimeFieldInventory(BuildValidRequest(root / "merge-runtime", "runtime"));
        const Result<FieldInventorySnapshot> serialized =
            GenerateSerializedFieldInventory(BuildValidRequest(root / "merge-serialized", "serialized"));
        const Result<FieldInventorySnapshot> protocol =
            GenerateProtocolFieldInventory(BuildValidRequest(root / "merge-protocol", "protocol"));
        assert(runtime.Ok);
        assert(serialized.Ok);
        assert(protocol.Ok);

        MergeFieldInventoryRequest request{};
        request.Scope = "merged";
        request.OutputDirectory = root / "merge-output";
        request.RuntimeSnapshot = runtime.Value;
        request.SerializedSnapshot = serialized.Value;
        request.ProtocolSnapshot = protocol.Value;

        const Result<FieldInventorySnapshot> merged = MergeFieldInventorySnapshots(request);
        assert(merged.Ok);
        assert(merged.Value.Scope == "merged");
        assert(merged.Value.OutputDirectory == request.OutputDirectory);
        assert(!merged.Value.Entries.empty());
        assert(!merged.Value.DeterministicDigest.empty());
        assert(AreFieldEntriesSorted(merged.Value));
        assert(merged.Value.Entries.size() <=
               (runtime.Value.Entries.size() + serialized.Value.Entries.size() + protocol.Value.Entries.size()));

        const std::set<std::string> domains = CollectDomains(merged.Value);
        assert(domains.contains("ecs"));
        assert(domains.contains("scene"));
        assert(domains.contains("packets"));
    }

    {
        FieldInventorySnapshot runtimeSnapshot{};
        runtimeSnapshot.Scope = "runtime";
        runtimeSnapshot.OutputDirectory = root / "merge-overlap-runtime";
        runtimeSnapshot.Entries = {
            BuildFieldEntry("shared-runtime", "runtime-owner", "SharedType", "SharedPath", "runtime-collector"),
            BuildFieldEntry("runtime-only", "runtime-owner", "RuntimeType", "OnlyPath", "runtime-collector")};

        FieldInventorySnapshot serializedSnapshot{};
        serializedSnapshot.Scope = "serialized";
        serializedSnapshot.OutputDirectory = root / "merge-overlap-serialized";
        serializedSnapshot.Entries = {
            BuildFieldEntry("shared-serialized", "serialized-owner", "SharedType", "SharedPath", "serialized-collector")};

        FieldInventorySnapshot protocolSnapshot{};
        protocolSnapshot.Scope = "protocol";
        protocolSnapshot.OutputDirectory = root / "merge-overlap-protocol";
        protocolSnapshot.Entries = {
            BuildFieldEntry("protocol-only", "protocol-owner", "ProtocolType", "OnlyPath", "protocol-collector")};

        MergeFieldInventoryRequest request{};
        request.Scope = "merged";
        request.OutputDirectory = root / "merge-overlap-output";
        request.RuntimeSnapshot = runtimeSnapshot;
        request.SerializedSnapshot = serializedSnapshot;
        request.ProtocolSnapshot = protocolSnapshot;

        const Result<FieldInventorySnapshot> merged = MergeFieldInventorySnapshots(request);
        assert(merged.Ok);
        assert(merged.Value.Entries.size() == 3u);

        const FieldInventoryEntry* sharedEntry = FindEntryByStableKey(merged.Value, "SharedType", "SharedPath");
        if (sharedEntry == nullptr || sharedEntry->FieldId != ComposeFieldId("shared-runtime", "SharedType", "SharedPath") ||
            sharedEntry->AliasFieldIds.size() != 2u ||
            sharedEntry->AliasFieldIds[0] != ComposeFieldId("shared-runtime", "SharedType", "SharedPath") ||
            sharedEntry->AliasFieldIds[1] != ComposeFieldId("shared-serialized", "SharedType", "SharedPath") ||
            sharedEntry->VersionLineage.size() != 2u || sharedEntry->VersionLineage[0] != "runtime" ||
            sharedEntry->VersionLineage[1] != "serialized") {
            return 1;
        }
    }

    {
        const Result<FieldInventorySnapshot> runtime =
            GenerateRuntimeFieldInventory(BuildValidRequest(root / "merge-repeat-runtime", "runtime"));
        const Result<FieldInventorySnapshot> serialized =
            GenerateSerializedFieldInventory(BuildValidRequest(root / "merge-repeat-serialized", "serialized"));
        const Result<FieldInventorySnapshot> protocol =
            GenerateProtocolFieldInventory(BuildValidRequest(root / "merge-repeat-protocol", "protocol"));
        assert(runtime.Ok);
        assert(serialized.Ok);
        assert(protocol.Ok);

        MergeFieldInventoryRequest request{};
        request.Scope = "merged-repeat";
        request.OutputDirectory = root / "merge-repeat-output";
        request.RuntimeSnapshot = runtime.Value;
        request.SerializedSnapshot = serialized.Value;
        request.ProtocolSnapshot = protocol.Value;

        const Result<FieldInventorySnapshot> first = MergeFieldInventorySnapshots(request);
        const Result<FieldInventorySnapshot> second = MergeFieldInventorySnapshots(request);
        assert(first.Ok);
        assert(second.Ok);
        assert(first.Value.DeterministicDigest == second.Value.DeterministicDigest);
        assert(first.Value.Entries.size() == second.Value.Entries.size());
    }

    return 0;
}

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

    return 0;
}

#include "Core/Audit/FieldAuditRunner.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <string>
#include <system_error>

int main() {
    using namespace Core::Audit;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-audit-runner-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldAuditRunRequest invalidRequest{};
        const Result<FieldAuditRunReport> result = RunRuntimeStateFieldAudit(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldAuditRunRequest unsupportedRequest{};
        unsupportedRequest.Scope = "runtime-state-audit-all";
        unsupportedRequest.OutputDirectory = root / "unsupported";
        const Result<FieldAuditRunReport> result = RunRuntimeStateFieldAudit(unsupportedRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldAuditRunRequest request{};
        request.Scope = "runtime-state";
        request.OutputDirectory = root / "runtime-state";

        const Result<FieldAuditRunReport> first = RunRuntimeStateFieldAudit(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.TotalPhases == 3u);
        assert(first.Value.PhaseStamps.size() == 3u);
        assert(!first.Value.DeterministicDigest.empty());

        constexpr std::array<const char*, 3> expectedPhaseIds = {"startup", "loaded-scene", "teardown"};
        uint32_t aggregatedFindingCount = 0;
        for (std::size_t index = 0; index < first.Value.PhaseStamps.size(); ++index) {
            const FieldAuditPhaseStamp& phaseStamp = first.Value.PhaseStamps[index];
            assert(phaseStamp.PhaseId == expectedPhaseIds[index]);
            assert(phaseStamp.PhaseOrdinal == static_cast<uint32_t>(index + 1u));
            assert(!phaseStamp.PhaseLabel.empty());
            assert(!phaseStamp.InventoryDigest.empty());
            assert(!phaseStamp.ValidationDigest.empty());
            assert(!phaseStamp.DeterministicPhaseDigest.empty());
            aggregatedFindingCount += phaseStamp.TotalFindingCount;
        }
        assert(aggregatedFindingCount == first.Value.TotalFindingCount);

        const Result<FieldAuditRunReport> second = RunRuntimeStateFieldAudit(request);
        assert(second.Ok);
        assert(second.Value.Scope == first.Value.Scope);
        assert(second.Value.TotalPhases == first.Value.TotalPhases);
        assert(second.Value.TotalFindingCount == first.Value.TotalFindingCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.PhaseStamps.size() == first.Value.PhaseStamps.size());

        for (std::size_t index = 0; index < first.Value.PhaseStamps.size(); ++index) {
            assert(second.Value.PhaseStamps[index].PhaseId == first.Value.PhaseStamps[index].PhaseId);
            assert(second.Value.PhaseStamps[index].PhaseOrdinal == first.Value.PhaseStamps[index].PhaseOrdinal);
            assert(second.Value.PhaseStamps[index].InventoryDigest == first.Value.PhaseStamps[index].InventoryDigest);
            assert(second.Value.PhaseStamps[index].ValidationDigest == first.Value.PhaseStamps[index].ValidationDigest);
            assert(second.Value.PhaseStamps[index].TotalFindingCount ==
                   first.Value.PhaseStamps[index].TotalFindingCount);
            assert(second.Value.PhaseStamps[index].DeterministicPhaseDigest ==
                   first.Value.PhaseStamps[index].DeterministicPhaseDigest);
        }
    }

    {
        FieldAuditRunRequest invalidRequest{};
        const Result<FieldAuditRunReport> result = RunCookedAndPackagedArtifactFieldAudit(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldAuditRunRequest unsupportedRequest{};
        unsupportedRequest.Scope = "artifact-runtime-all";
        unsupportedRequest.OutputDirectory = root / "unsupported-artifacts";
        const Result<FieldAuditRunReport> result = RunCookedAndPackagedArtifactFieldAudit(unsupportedRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldAuditRunRequest request{};
        request.Scope = "cooked-packaged-artifacts";
        request.OutputDirectory = root / "cooked-packaged-artifacts";

        const Result<FieldAuditRunReport> first = RunCookedAndPackagedArtifactFieldAudit(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.TotalPhases == 4u);
        assert(first.Value.PhaseStamps.size() == 4u);
        assert(!first.Value.DeterministicDigest.empty());

        constexpr std::array<const char*, 4> expectedPhaseIds = {
            "cooked-assets",
            "build-manifests",
            "store-bundles",
            "dedicated-server-artifacts"};
        uint32_t aggregatedFindingCount = 0;
        for (std::size_t index = 0; index < first.Value.PhaseStamps.size(); ++index) {
            const FieldAuditPhaseStamp& phaseStamp = first.Value.PhaseStamps[index];
            assert(phaseStamp.PhaseId == expectedPhaseIds[index]);
            assert(phaseStamp.PhaseOrdinal == static_cast<uint32_t>(index + 1u));
            assert(!phaseStamp.PhaseLabel.empty());
            assert(!phaseStamp.InventoryDigest.empty());
            assert(!phaseStamp.ValidationDigest.empty());
            assert(!phaseStamp.DeterministicPhaseDigest.empty());
            aggregatedFindingCount += phaseStamp.TotalFindingCount;
        }
        assert(aggregatedFindingCount == first.Value.TotalFindingCount);

        const Result<FieldAuditRunReport> second = RunCookedAndPackagedArtifactFieldAudit(request);
        assert(second.Ok);
        assert(second.Value.Scope == first.Value.Scope);
        assert(second.Value.TotalPhases == first.Value.TotalPhases);
        assert(second.Value.TotalFindingCount == first.Value.TotalFindingCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.PhaseStamps.size() == first.Value.PhaseStamps.size());

        for (std::size_t index = 0; index < first.Value.PhaseStamps.size(); ++index) {
            assert(second.Value.PhaseStamps[index].PhaseId == first.Value.PhaseStamps[index].PhaseId);
            assert(second.Value.PhaseStamps[index].PhaseOrdinal == first.Value.PhaseStamps[index].PhaseOrdinal);
            assert(second.Value.PhaseStamps[index].InventoryDigest == first.Value.PhaseStamps[index].InventoryDigest);
            assert(second.Value.PhaseStamps[index].ValidationDigest == first.Value.PhaseStamps[index].ValidationDigest);
            assert(second.Value.PhaseStamps[index].TotalFindingCount ==
                   first.Value.PhaseStamps[index].TotalFindingCount);
            assert(second.Value.PhaseStamps[index].DeterministicPhaseDigest ==
                   first.Value.PhaseStamps[index].DeterministicPhaseDigest);
        }
    }

    {
        FieldAuditRunRequest invalidRequest{};
        const Result<FieldAuditRunReport> result = RunNetworkAndReplayFieldAudit(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldAuditRunRequest unsupportedRequest{};
        unsupportedRequest.Scope = "network-parity-all";
        unsupportedRequest.OutputDirectory = root / "unsupported-network";
        const Result<FieldAuditRunReport> result = RunNetworkAndReplayFieldAudit(unsupportedRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldAuditRunRequest request{};
        request.Scope = "network-replay";
        request.OutputDirectory = root / "network-replay";

        const Result<FieldAuditRunReport> first = RunNetworkAndReplayFieldAudit(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.TotalPhases == 4u);
        assert(first.Value.PhaseStamps.size() == 4u);
        assert(!first.Value.DeterministicDigest.empty());

        constexpr std::array<const char*, 4> expectedPhaseIds = {
            "replication-parity",
            "rpc-fidelity",
            "replay-integrity",
            "rollback-consistency"};
        uint32_t aggregatedFindingCount = 0;
        for (std::size_t index = 0; index < first.Value.PhaseStamps.size(); ++index) {
            const FieldAuditPhaseStamp& phaseStamp = first.Value.PhaseStamps[index];
            assert(phaseStamp.PhaseId == expectedPhaseIds[index]);
            assert(phaseStamp.PhaseOrdinal == static_cast<uint32_t>(index + 1u));
            assert(!phaseStamp.PhaseLabel.empty());
            assert(!phaseStamp.InventoryDigest.empty());
            assert(!phaseStamp.ValidationDigest.empty());
            assert(!phaseStamp.DeterministicPhaseDigest.empty());
            aggregatedFindingCount += phaseStamp.TotalFindingCount;
        }
        assert(aggregatedFindingCount == first.Value.TotalFindingCount);

        const Result<FieldAuditRunReport> second = RunNetworkAndReplayFieldAudit(request);
        assert(second.Ok);
        assert(second.Value.Scope == first.Value.Scope);
        assert(second.Value.TotalPhases == first.Value.TotalPhases);
        assert(second.Value.TotalFindingCount == first.Value.TotalFindingCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.PhaseStamps.size() == first.Value.PhaseStamps.size());

        for (std::size_t index = 0; index < first.Value.PhaseStamps.size(); ++index) {
            assert(second.Value.PhaseStamps[index].PhaseId == first.Value.PhaseStamps[index].PhaseId);
            assert(second.Value.PhaseStamps[index].PhaseOrdinal == first.Value.PhaseStamps[index].PhaseOrdinal);
            assert(second.Value.PhaseStamps[index].InventoryDigest == first.Value.PhaseStamps[index].InventoryDigest);
            assert(second.Value.PhaseStamps[index].ValidationDigest == first.Value.PhaseStamps[index].ValidationDigest);
            assert(second.Value.PhaseStamps[index].TotalFindingCount ==
                   first.Value.PhaseStamps[index].TotalFindingCount);
            assert(second.Value.PhaseStamps[index].DeterministicPhaseDigest ==
                   first.Value.PhaseStamps[index].DeterministicPhaseDigest);
        }
    }

    return 0;
}

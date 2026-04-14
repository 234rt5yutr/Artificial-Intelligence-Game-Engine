#include "Core/Remediation/RuntimeFieldFixService.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

Core::Remediation::RuntimeFieldFixEntry BuildEntry(const Core::Remediation::RuntimeFieldDomain domain,
                                                   const std::string& stableFieldKey,
                                                   const std::string& domainPair,
                                                   const std::string& targetFieldId,
                                                   const std::string& findingId,
                                                   const std::string& owner) {
    Core::Remediation::RuntimeFieldFixEntry entry{};
    entry.Domain = domain;
    entry.StableFieldKey = stableFieldKey;
    entry.DomainPair = domainPair;
    entry.TargetFieldId = targetFieldId;
    entry.Provenance.FindingId = findingId;
    entry.Provenance.RuleId = "FIELD_AUDIT_RULE_RUNTIME_FIELD_ROUTE_DRIFT";
    entry.Provenance.Owner = owner;
    return entry;
}

} // namespace

#define REQUIRE_OR_FAIL(condition) \
    do {                           \
        if (!(condition)) {        \
            return __LINE__;       \
        }                          \
    } while (false)

int main() {
    using namespace Core::Remediation;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "runtime-field-fix-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        RuntimeFieldFixRequest invalidRequest{};
        const Result<RuntimeFieldFixResult> result = FixRuntimeFieldBindingAndReflectionRoutes(invalidRequest);
        REQUIRE_OR_FAIL(!result.Ok);
        REQUIRE_OR_FAIL(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        RuntimeFieldFixRequest invalidRequest{};
        const Result<RuntimeFieldFixResult> result = FixReplicationRPCAndRollbackFieldParity(invalidRequest);
        REQUIRE_OR_FAIL(!result.Ok);
        REQUIRE_OR_FAIL(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        RuntimeFieldFixRequest unsupportedScopeRequest{};
        unsupportedScopeRequest.Scope = "runtime-field-routes-v2";
        unsupportedScopeRequest.OutputDirectory = root / "unsupported";
        unsupportedScopeRequest.RemediationBatchId = "batch-030301";
        unsupportedScopeRequest.RollbackManifestPath = unsupportedScopeRequest.OutputDirectory / "rollback.json";

        RuntimeFieldFixEntry entry = BuildEntry(RuntimeFieldDomain::ECS,
                                                "ECS::Player::Health",
                                                "ecs<->ui<->reflection",
                                                "ecs::Player::Health",
                                                "runtime-finding-a",
                                                "ecs-runtime");
        entry.BindingPath = "/ecs/player/health-old";
        entry.ExpectedBindingPath = "/ecs/player/health";
        unsupportedScopeRequest.Entries = {entry};

        const Result<RuntimeFieldFixResult> result = FixRuntimeFieldBindingAndReflectionRoutes(unsupportedScopeRequest);
        REQUIRE_OR_FAIL(!result.Ok);
        REQUIRE_OR_FAIL(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        RuntimeFieldFixRequest request{};
        request.Scope = "runtime-field-routes";
        request.OutputDirectory = root / "success";
        request.RemediationBatchId = "batch-030301";
        request.RollbackManifestPath = request.OutputDirectory / "rollback-runtime-routes.json";

        RuntimeFieldFixEntry ecsEntry = BuildEntry(RuntimeFieldDomain::ECS,
                                                   "ECS::Player::Health",
                                                   "ecs<->ui<->reflection",
                                                   "ecs::Player::Health",
                                                   "runtime-finding-ecs",
                                                   "ecs-runtime");
        ecsEntry.BindingPath = "/ecs/player/health-old";
        ecsEntry.ExpectedBindingPath = "/ecs/player/health";
        ecsEntry.ReflectionRoute = "ECS.Player.Health.GetOld";
        ecsEntry.ExpectedReflectionRoute = "ECS.Player.Health.Get";
        ecsEntry.InterfaceAliases = {"IHealthReadable", "IHealth"};
        ecsEntry.ExpectedInterfaceAliases = {"IInspectable", "IHealth", "IHealthReadable"};

        RuntimeFieldFixEntry uiEntry = BuildEntry(RuntimeFieldDomain::UIBinding,
                                                  "UI::HUD::HealthBar::Value",
                                                  "ui<->binding-runtime",
                                                  "ui::HUD::HealthBar::Value",
                                                  "runtime-finding-ui",
                                                  "ui-runtime");
        uiEntry.BindingPath = "/ui/hud/health/value_legacy";
        uiEntry.ExpectedBindingPath = "/ui/hud/health/value";
        uiEntry.ReflectionRoute = "UI.HUD.HealthBar.Value";
        uiEntry.ExpectedReflectionRoute = "UI.HUD.HealthBar.Value";
        uiEntry.InterfaceAliases = {"IUIBindable"};
        uiEntry.ExpectedInterfaceAliases = {"IUIBindable"};

        RuntimeFieldFixEntry animationEntry = BuildEntry(RuntimeFieldDomain::Animation,
                                                         "Animation::Locomotion::BlendTree::Speed",
                                                         "animation<->runtime-reflection",
                                                         "animation::Locomotion::BlendTree::Speed",
                                                         "runtime-finding-animation",
                                                         "animation-runtime");
        animationEntry.BindingPath = "/anim/locomotion/speed";
        animationEntry.ExpectedBindingPath = "/anim/locomotion/speed";
        animationEntry.ReflectionRoute = "Animation.Locomotion.Speed.Legacy";
        animationEntry.ExpectedReflectionRoute = "Animation.Locomotion.Speed";
        animationEntry.InterfaceAliases = {"IAnimSpeedReadable", "IAnimSpeedReadable"};
        animationEntry.ExpectedInterfaceAliases = {"IAnimSpeedReadable"};

        RuntimeFieldFixEntry toolingEntry = BuildEntry(RuntimeFieldDomain::Tooling,
                                                       "Tooling::Inspector::Player::Health",
                                                       "tooling<->reflection-interface",
                                                       "tooling::Inspector::Player::Health",
                                                       "runtime-finding-tooling",
                                                       "tool-runtime");
        toolingEntry.BindingPath = "/tool/inspector/player/health";
        toolingEntry.ExpectedBindingPath = "/tool/inspector/player/health";
        toolingEntry.ReflectionRoute = "Tooling.Inspector.Player.Health";
        toolingEntry.ExpectedReflectionRoute = "Tooling.Inspector.Player.Health";
        toolingEntry.InterfaceAliases = {"IInspectableHealth"};
        toolingEntry.ExpectedInterfaceAliases = {"IHealthInspectable", "IInspectableHealth"};

        RuntimeFieldFixEntry duplicateUIEntry = uiEntry;
        request.Entries = {toolingEntry, ecsEntry, duplicateUIEntry, animationEntry, uiEntry};

        const Result<RuntimeFieldFixResult> first = FixRuntimeFieldBindingAndReflectionRoutes(request);
        REQUIRE_OR_FAIL(first.Ok);
        REQUIRE_OR_FAIL(first.Value.Scope == request.Scope);
        REQUIRE_OR_FAIL(first.Value.RemediationBatchId == request.RemediationBatchId);
        REQUIRE_OR_FAIL(first.Value.RollbackManifestPath == request.RollbackManifestPath);
        REQUIRE_OR_FAIL(first.Value.Summary.ECSFixCount == 3u);
        REQUIRE_OR_FAIL(first.Value.Summary.UIBindingFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.AnimationFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.ToolingFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.BindingPathFixCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.ReflectionRouteFixCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.ReflectionInterfaceAliasFixCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.RollbackSafeFixCount == 6u);
        REQUIRE_OR_FAIL(first.Value.Summary.TotalFixCount == 6u);
        REQUIRE_OR_FAIL(first.Value.FixRecords.size() == 6u);
        REQUIRE_OR_FAIL(!first.Value.RollbackManifestDigest.empty());
        REQUIRE_OR_FAIL(!first.Value.DeterministicDigest.empty());

        bool sawEcsBindingFix = false;
        bool sawAnimationReflectionFix = false;
        bool sawToolingAliasFix = false;
        for (const RuntimeFieldFixRecord& record : first.Value.FixRecords) {
            REQUIRE_OR_FAIL(!record.FixId.empty());
            REQUIRE_OR_FAIL(!record.StableFieldKey.empty());
            REQUIRE_OR_FAIL(!record.TargetFieldId.empty());
            REQUIRE_OR_FAIL(!record.PropertyPath.empty());
            REQUIRE_OR_FAIL(!record.ExistingValue.empty());
            REQUIRE_OR_FAIL(!record.ReplacementValue.empty());
            REQUIRE_OR_FAIL(!record.Rationale.empty());
            REQUIRE_OR_FAIL(!record.DeterministicDigest.empty());
            REQUIRE_OR_FAIL(record.Provenance.RemediationBatchId == request.RemediationBatchId);
            REQUIRE_OR_FAIL(record.Rollback.RollbackRequired);
            REQUIRE_OR_FAIL(!record.Rollback.RollbackCheckpointId.empty());
            REQUIRE_OR_FAIL(!record.Rollback.RollbackPropertyPath.empty());
            REQUIRE_OR_FAIL(!record.Rollback.RollbackManifestPath.empty());

            if (record.TargetFieldId == "ecs::Player::Health" && record.FixKind == RuntimeFieldFixKind::BindingPathCorrection) {
                sawEcsBindingFix = true;
                REQUIRE_OR_FAIL(record.PropertyPath == "binding.path");
                REQUIRE_OR_FAIL(record.ExistingValue == "/ecs/player/health-old");
                REQUIRE_OR_FAIL(record.ReplacementValue == "/ecs/player/health");
            }

            if (record.TargetFieldId == "animation::Locomotion::BlendTree::Speed" &&
                record.FixKind == RuntimeFieldFixKind::ReflectionRouteCorrection) {
                sawAnimationReflectionFix = true;
                REQUIRE_OR_FAIL(record.PropertyPath == "reflection.route");
                REQUIRE_OR_FAIL(record.ExistingValue == "Animation.Locomotion.Speed.Legacy");
                REQUIRE_OR_FAIL(record.ReplacementValue == "Animation.Locomotion.Speed");
            }

            if (record.TargetFieldId == "tooling::Inspector::Player::Health" &&
                record.FixKind == RuntimeFieldFixKind::ReflectionInterfaceAliasCorrection) {
                sawToolingAliasFix = true;
                REQUIRE_OR_FAIL(record.PropertyPath == "reflection.interfaceAliases");
                REQUIRE_OR_FAIL(record.ExistingValue == "IInspectableHealth");
                REQUIRE_OR_FAIL(record.ReplacementValue == "IHealthInspectable,IInspectableHealth");
            }
        }
        REQUIRE_OR_FAIL(sawEcsBindingFix);
        REQUIRE_OR_FAIL(sawAnimationReflectionFix);
        REQUIRE_OR_FAIL(sawToolingAliasFix);

        const Result<RuntimeFieldFixResult> second = FixRuntimeFieldBindingAndReflectionRoutes(request);
        REQUIRE_OR_FAIL(second.Ok);
        REQUIRE_OR_FAIL(second.Value.RollbackManifestDigest == first.Value.RollbackManifestDigest);
        REQUIRE_OR_FAIL(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        REQUIRE_OR_FAIL(second.Value.FixRecords.size() == first.Value.FixRecords.size());
        for (std::size_t index = 0; index < first.Value.FixRecords.size(); ++index) {
            REQUIRE_OR_FAIL(second.Value.FixRecords[index].FixId == first.Value.FixRecords[index].FixId);
            REQUIRE_OR_FAIL(second.Value.FixRecords[index].DeterministicDigest ==
                            first.Value.FixRecords[index].DeterministicDigest);
            REQUIRE_OR_FAIL(second.Value.FixRecords[index].Rollback.RollbackCheckpointId ==
                            first.Value.FixRecords[index].Rollback.RollbackCheckpointId);
        }
    }

    {
        RuntimeFieldFixRequest request{};
        request.Scope = "runtime-netcode-parity";
        request.OutputDirectory = root / "parity-success";
        request.RemediationBatchId = "batch-030302";
        request.RollbackManifestPath = request.OutputDirectory / "rollback-runtime-netcode-parity.json";

        RuntimeFieldFixEntry replicationEntry = BuildEntry(RuntimeFieldDomain::Replication,
                                                           "Net::Replication::PlayerState",
                                                           "replication<->client",
                                                           "net::replication::PlayerState",
                                                           "runtime-finding-replication",
                                                           "network-runtime");
        replicationEntry.ReplicationAuthoritativeSchema = "PlayerState[v3]{hp:int,shield:int}";
        replicationEntry.ReplicationClientSchema = "PlayerState[v2]{hp:int}";

        RuntimeFieldFixEntry rpcEntry = BuildEntry(RuntimeFieldDomain::RPC,
                                                   "Net::RPC::ApplyDamage",
                                                   "rpc<->payload",
                                                   "net::rpc::ApplyDamage",
                                                   "runtime-finding-rpc",
                                                   "network-runtime");
        rpcEntry.RPCCanonicalPayloadSchema = "ApplyDamage{targetId:u64,amount:i32,source:u32}";
        rpcEntry.RPCRequestPayloadSchema = "ApplyDamage{targetId:u64,amount:i32}";
        rpcEntry.RPCResponsePayloadSchema = "ApplyDamageAck{ok:bool}";

        RuntimeFieldFixEntry replayRollbackEntry = BuildEntry(RuntimeFieldDomain::ReplayRollback,
                                                              "Net::Replay::FrameState",
                                                              "replay<->rollback",
                                                              "net::replay::FrameState",
                                                              "runtime-finding-replay",
                                                              "network-runtime");
        replayRollbackEntry.ReplaySchema = "FrameState{tick:u32,entities:vector<EntityState>}";
        replayRollbackEntry.RollbackSchema = "FrameState{tick:u32}";

        RuntimeFieldFixEntry duplicateRpcEntry = rpcEntry;
        request.Entries = {rpcEntry, replayRollbackEntry, replicationEntry, duplicateRpcEntry};

        const Result<RuntimeFieldFixResult> first = FixReplicationRPCAndRollbackFieldParity(request);
        REQUIRE_OR_FAIL(first.Ok);
        REQUIRE_OR_FAIL(first.Value.Scope == request.Scope);
        REQUIRE_OR_FAIL(first.Value.RemediationBatchId == request.RemediationBatchId);
        REQUIRE_OR_FAIL(first.Value.RollbackManifestPath == request.RollbackManifestPath);
        REQUIRE_OR_FAIL(first.Value.Summary.ReplicationFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.RPCFixCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.ReplayRollbackFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.ReplicationSchemaParityFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.RPCPayloadParityFixCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.ReplayRollbackSchemaParityFixCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.RollbackSafeFixCount == 4u);
        REQUIRE_OR_FAIL(first.Value.Summary.TotalFixCount == 4u);
        REQUIRE_OR_FAIL(first.Value.FixRecords.size() == 4u);
        REQUIRE_OR_FAIL(!first.Value.RollbackManifestDigest.empty());
        REQUIRE_OR_FAIL(!first.Value.DeterministicDigest.empty());

        bool sawReplicationFix = false;
        bool sawRpcRequestFix = false;
        bool sawRpcResponseFix = false;
        bool sawReplayRollbackFix = false;
        for (const RuntimeFieldFixRecord& record : first.Value.FixRecords) {
            REQUIRE_OR_FAIL(!record.FixId.empty());
            REQUIRE_OR_FAIL(!record.StableFieldKey.empty());
            REQUIRE_OR_FAIL(!record.TargetFieldId.empty());
            REQUIRE_OR_FAIL(!record.PropertyPath.empty());
            REQUIRE_OR_FAIL(!record.ExistingValue.empty());
            REQUIRE_OR_FAIL(!record.ReplacementValue.empty());
            REQUIRE_OR_FAIL(!record.Rationale.empty());
            REQUIRE_OR_FAIL(!record.DeterministicDigest.empty());
            REQUIRE_OR_FAIL(record.Provenance.RemediationBatchId == request.RemediationBatchId);
            REQUIRE_OR_FAIL(record.Rollback.RollbackRequired);
            REQUIRE_OR_FAIL(!record.Rollback.RollbackCheckpointId.empty());
            REQUIRE_OR_FAIL(!record.Rollback.RollbackPropertyPath.empty());
            REQUIRE_OR_FAIL(!record.Rollback.RollbackManifestPath.empty());

            if (record.TargetFieldId == "net::replication::PlayerState" &&
                record.FixKind == RuntimeFieldFixKind::ReplicationSchemaParityCorrection) {
                sawReplicationFix = true;
                REQUIRE_OR_FAIL(record.PropertyPath == "replication.clientSchema");
                REQUIRE_OR_FAIL(record.ExistingValue == "PlayerState[v2]{hp:int}");
                REQUIRE_OR_FAIL(record.ReplacementValue == "PlayerState[v3]{hp:int,shield:int}");
            }

            if (record.TargetFieldId == "net::rpc::ApplyDamage" &&
                record.FixKind == RuntimeFieldFixKind::RPCPayloadParityCorrection &&
                record.PropertyPath == "rpc.requestPayloadSchema") {
                sawRpcRequestFix = true;
                REQUIRE_OR_FAIL(record.ExistingValue == "ApplyDamage{targetId:u64,amount:i32}");
                REQUIRE_OR_FAIL(record.ReplacementValue == "ApplyDamage{targetId:u64,amount:i32,source:u32}");
            }

            if (record.TargetFieldId == "net::rpc::ApplyDamage" &&
                record.FixKind == RuntimeFieldFixKind::RPCPayloadParityCorrection &&
                record.PropertyPath == "rpc.responsePayloadSchema") {
                sawRpcResponseFix = true;
                REQUIRE_OR_FAIL(record.ExistingValue == "ApplyDamageAck{ok:bool}");
                REQUIRE_OR_FAIL(record.ReplacementValue == "ApplyDamage{targetId:u64,amount:i32,source:u32}");
            }

            if (record.TargetFieldId == "net::replay::FrameState" &&
                record.FixKind == RuntimeFieldFixKind::ReplayRollbackSchemaParityCorrection) {
                sawReplayRollbackFix = true;
                REQUIRE_OR_FAIL(record.PropertyPath == "rollback.schema");
                REQUIRE_OR_FAIL(record.ExistingValue == "FrameState{tick:u32}");
                REQUIRE_OR_FAIL(record.ReplacementValue == "FrameState{tick:u32,entities:vector<EntityState>}");
            }
        }
        REQUIRE_OR_FAIL(sawReplicationFix);
        REQUIRE_OR_FAIL(sawRpcRequestFix);
        REQUIRE_OR_FAIL(sawRpcResponseFix);
        REQUIRE_OR_FAIL(sawReplayRollbackFix);

        const Result<RuntimeFieldFixResult> second = FixReplicationRPCAndRollbackFieldParity(request);
        REQUIRE_OR_FAIL(second.Ok);
        REQUIRE_OR_FAIL(second.Value.RollbackManifestDigest == first.Value.RollbackManifestDigest);
        REQUIRE_OR_FAIL(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        REQUIRE_OR_FAIL(second.Value.FixRecords.size() == first.Value.FixRecords.size());
        for (std::size_t index = 0; index < first.Value.FixRecords.size(); ++index) {
            REQUIRE_OR_FAIL(second.Value.FixRecords[index].FixId == first.Value.FixRecords[index].FixId);
            REQUIRE_OR_FAIL(second.Value.FixRecords[index].DeterministicDigest ==
                            first.Value.FixRecords[index].DeterministicDigest);
            REQUIRE_OR_FAIL(second.Value.FixRecords[index].Rollback.RollbackCheckpointId ==
                            first.Value.FixRecords[index].Rollback.RollbackCheckpointId);
        }
    }

    return 0;
}

#undef REQUIRE_OR_FAIL

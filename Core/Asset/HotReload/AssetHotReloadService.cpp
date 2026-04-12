#include "AssetHotReloadService.h"

#include "Core/Asset/Addressables/AddressableRuntime.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/Components/AnimatorComponent.h"
#include "Core/ECS/Components/AudioSourceComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/Scene.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace Core {
namespace Asset {
namespace HotReload {

    AssetHotReloadService& AssetHotReloadService::Get() {
        static AssetHotReloadService instance;
        return instance;
    }

    Result<HotReloadAssetResult> AssetHotReloadService::HotReloadAssetAtRuntime(
        const HotReloadAssetRequest& request) {
        if (request.AddressKeys.empty()) {
            return Result<HotReloadAssetResult>::Failure("HotReloadAssetAtRuntime requires at least one address key");
        }

        std::vector<std::string> deduplicatedKeys = request.AddressKeys;
        std::sort(deduplicatedKeys.begin(), deduplicatedKeys.end());
        deduplicatedKeys.erase(std::unique(deduplicatedKeys.begin(), deduplicatedKeys.end()), deduplicatedKeys.end());

        TrackAssetDependencyGraphRequest graphRequest;
        graphRequest.RootAddressKeys = deduplicatedKeys;
        graphRequest.IncludeReverseDependents = false;
        graphRequest.IncludeTransitive = true;
        graphRequest.DetectCycles = true;

        auto graphResult = TrackAssetDependencyGraph(graphRequest);
        if (!graphResult.Ok) {
            return Result<HotReloadAssetResult>::Failure(graphResult.Error);
        }

        HotReloadAssetResult hotReloadResult;
        std::unordered_set<std::string> impactedKeys;
        for (const auto& node : graphResult.Value.Nodes) {
            impactedKeys.insert(node.AddressKey);
            hotReloadResult.ImpactedAddressKeys.push_back(node.AddressKey);
        }
        std::sort(hotReloadResult.ImpactedAddressKeys.begin(), hotReloadResult.ImpactedAddressKeys.end());

        if (graphResult.Value.HasCycle && request.StrictMode) {
            hotReloadResult.Success = false;
            hotReloadResult.RolledBack = true;
            hotReloadResult.Diagnostics = "Dependency cycle detected in strict mode";
            hotReloadResult.FailedAddressKeys = hotReloadResult.ImpactedAddressKeys;

            std::lock_guard lock(m_Mutex);
            hotReloadResult.EventId = m_NextEventId++;
            hotReloadResult.AppliedFrameIndex = m_FrameIndex;
            HotReloadEventRecord eventRecord;
            eventRecord.EventId = hotReloadResult.EventId;
            eventRecord.AppliedFrameIndex = hotReloadResult.AppliedFrameIndex;
            eventRecord.Trigger = request.Trigger;
            eventRecord.Success = hotReloadResult.Success;
            eventRecord.RolledBack = hotReloadResult.RolledBack;
            eventRecord.ImpactedAddressKeys = hotReloadResult.ImpactedAddressKeys;
            eventRecord.FailedAddressKeys = hotReloadResult.FailedAddressKeys;
            eventRecord.Diagnostics = hotReloadResult.Diagnostics;
            PushEventLocked(std::move(eventRecord));

            return Result<HotReloadAssetResult>::Success(std::move(hotReloadResult));
        }

        std::vector<std::string> preloadedKeys;
        preloadedKeys.reserve(impactedKeys.size());

        for (const std::string& addressKey : hotReloadResult.ImpactedAddressKeys) {
            Addressables::LoadAddressableAssetRequest loadRequest;
            loadRequest.AddressKey = addressKey;
            loadRequest.IncludeTransitiveDependencies = false;

            Addressables::AddressableLoadTicket loadTicket =
                Addressables::AddressableRuntimeService::Get().LoadAddressableAssetAsync(loadRequest);
            Addressables::AddressableLoadResult loadResult = loadTicket.Future.get();
            if (!loadResult.IsValid()) {
                hotReloadResult.FailedAddressKeys.push_back(addressKey);
                continue;
            }

            preloadedKeys.push_back(addressKey);
            hotReloadResult.PreloadedAssetCount++;
        }

        if (request.StrictMode && !hotReloadResult.FailedAddressKeys.empty()) {
            for (const std::string& preloadedKey : preloadedKeys) {
                Addressables::ReleaseAddressableAssetRequest releaseRequest;
                releaseRequest.AddressKey = preloadedKey;
                releaseRequest.Policy = Addressables::AddressableReleasePolicy::Deferred;
                Addressables::AddressableRuntimeService::Get().ReleaseAddressableAsset(releaseRequest);
            }

            hotReloadResult.Success = false;
            hotReloadResult.RolledBack = true;
            hotReloadResult.Diagnostics =
                "Strict hot reload aborted because one or more assets failed to preload";

            std::lock_guard lock(m_Mutex);
            hotReloadResult.EventId = m_NextEventId++;
            hotReloadResult.AppliedFrameIndex = m_FrameIndex;
            HotReloadEventRecord eventRecord;
            eventRecord.EventId = hotReloadResult.EventId;
            eventRecord.AppliedFrameIndex = hotReloadResult.AppliedFrameIndex;
            eventRecord.Trigger = request.Trigger;
            eventRecord.Success = hotReloadResult.Success;
            eventRecord.RolledBack = hotReloadResult.RolledBack;
            eventRecord.ImpactedAddressKeys = hotReloadResult.ImpactedAddressKeys;
            eventRecord.FailedAddressKeys = hotReloadResult.FailedAddressKeys;
            eventRecord.Diagnostics = hotReloadResult.Diagnostics;
            PushEventLocked(std::move(eventRecord));

            return Result<HotReloadAssetResult>::Success(std::move(hotReloadResult));
        }

        std::unordered_set<std::string> normalizedImpactedPaths;
        normalizedImpactedPaths.reserve(hotReloadResult.ImpactedAddressKeys.size() * 2);
        for (const std::string& addressKey : hotReloadResult.ImpactedAddressKeys) {
            auto entry = Addressables::AddressableRuntimeService::Get().FindCatalogEntry(addressKey);
            if (entry.has_value()) {
                normalizedImpactedPaths.insert(NormalizePathKey(entry->SourcePath));
                normalizedImpactedPaths.insert(NormalizePathKey(entry->CookedPath));
            }
        }

        if (!request.DryRun && request.TargetScene != nullptr) {
            auto& registry = request.TargetScene->GetRegistry();

            auto meshView = registry.view<ECS::MeshComponent>();
            for (auto entity : meshView) {
                auto& mesh = meshView.get<ECS::MeshComponent>(entity);
                if (IsPathMatch(mesh.MeshPath, normalizedImpactedPaths)) {
                    ++mesh.AssetGeneration;
                    ++hotReloadResult.ReboundMeshComponentCount;
                }
            }

            auto skeletalView = registry.view<ECS::SkeletalMeshComponent>();
            for (auto entity : skeletalView) {
                auto& skeletal = skeletalView.get<ECS::SkeletalMeshComponent>(entity);
                if (IsPathMatch(skeletal.MeshPath, normalizedImpactedPaths)) {
                    ++skeletal.AssetGeneration;
                    ++skeletal.AnimationClipGeneration;
                    ++hotReloadResult.ReboundSkeletalComponentCount;
                }
            }

            auto& audioSystem = Audio::AudioSystem::Get();
            auto audioSourceView = registry.view<ECS::AudioSourceComponent>();
            for (auto entity : audioSourceView) {
                auto& source = audioSourceView.get<ECS::AudioSourceComponent>(entity);
                if (!IsPathMatch(source.AudioClipPath, normalizedImpactedPaths)) {
                    continue;
                }

                ++source.ClipGeneration;
                ++hotReloadResult.ReboundAudioSourceCount;

                if (source.CurrentHandle != Audio::InvalidSoundHandle) {
                    const bool wasPaused = source.State == ECS::AudioPlaybackState::Paused;
                    const bool wasPlaying =
                        source.State == ECS::AudioPlaybackState::Playing ||
                        source.State == ECS::AudioPlaybackState::FadingIn ||
                        source.State == ECS::AudioPlaybackState::FadingOut;

                    if (request.PreserveAudioPlaybackPosition) {
                        source.StartTime = audioSystem.GetSoundCurrentTime(source.CurrentHandle);
                    } else {
                        source.StartTime = 0.0f;
                    }

                    audioSystem.StopSound(source.CurrentHandle);
                    source.CurrentHandle = Audio::InvalidSoundHandle;

                    if (wasPlaying) {
                        source.State = ECS::AudioPlaybackState::Playing;
                    } else if (wasPaused) {
                        source.State = ECS::AudioPlaybackState::Paused;
                    }
                    source.IsDirty = true;
                }
            }

            auto animatorView = registry.view<ECS::AnimatorComponent, ECS::SkeletalMeshComponent>();
            for (auto entity : animatorView) {
                auto& animator = animatorView.get<ECS::AnimatorComponent>(entity);
                auto& skeletal = animatorView.get<ECS::SkeletalMeshComponent>(entity);
                if (skeletal.AnimationClipGeneration != skeletal.LastAnimationClipGeneration) {
                    skeletal.LastAnimationClipGeneration = skeletal.AnimationClipGeneration;
                    animator.RuntimeState.CurrentTransition.Reset();
                    animator.RuntimeState.PreviousStateName.clear();
                    animator.RuntimeState.PreviousStateTime = 0.0f;
                    animator.RuntimeState.CurrentStateTime = 0.0f;
                    animator.RuntimeState.NormalizedTime = 0.0f;
                }
            }
        }

        for (const std::string& preloadedKey : preloadedKeys) {
            Addressables::ReleaseAddressableAssetRequest releaseRequest;
            releaseRequest.AddressKey = preloadedKey;
            releaseRequest.Policy = Addressables::AddressableReleasePolicy::Deferred;
            Addressables::AddressableRuntimeService::Get().ReleaseAddressableAsset(releaseRequest);
        }

        hotReloadResult.Success = hotReloadResult.FailedAddressKeys.empty() || !request.StrictMode;
        hotReloadResult.RolledBack = request.StrictMode && !hotReloadResult.FailedAddressKeys.empty();
        if (hotReloadResult.Success && hotReloadResult.FailedAddressKeys.empty()) {
            hotReloadResult.Diagnostics = "Hot reload completed";
        } else if (hotReloadResult.Success) {
            hotReloadResult.Diagnostics = "Hot reload completed with partial failures (best-effort mode)";
        } else {
            hotReloadResult.Diagnostics = "Hot reload failed in strict mode";
        }

        {
            std::lock_guard lock(m_Mutex);
            hotReloadResult.EventId = m_NextEventId++;
            hotReloadResult.AppliedFrameIndex = m_FrameIndex;

            HotReloadEventRecord eventRecord;
            eventRecord.EventId = hotReloadResult.EventId;
            eventRecord.AppliedFrameIndex = hotReloadResult.AppliedFrameIndex;
            eventRecord.Trigger = request.Trigger;
            eventRecord.Success = hotReloadResult.Success;
            eventRecord.RolledBack = hotReloadResult.RolledBack;
            eventRecord.ImpactedAddressKeys = hotReloadResult.ImpactedAddressKeys;
            eventRecord.FailedAddressKeys = hotReloadResult.FailedAddressKeys;
            eventRecord.Diagnostics = hotReloadResult.Diagnostics;
            PushEventLocked(std::move(eventRecord));
        }

        return Result<HotReloadAssetResult>::Success(std::move(hotReloadResult));
    }

    std::vector<HotReloadEventRecord> AssetHotReloadService::GetRecentEvents() const {
        std::lock_guard lock(m_Mutex);
        return std::vector<HotReloadEventRecord>(m_EventHistory.begin(), m_EventHistory.end());
    }

    void AssetHotReloadService::ClearEvents() {
        std::lock_guard lock(m_Mutex);
        m_EventHistory.clear();
    }

    void AssetHotReloadService::PumpFrameSafePoint() {
        std::lock_guard lock(m_Mutex);
        ++m_FrameIndex;
    }

    uint64_t AssetHotReloadService::GetCurrentFrameIndex() const {
        std::lock_guard lock(m_Mutex);
        return m_FrameIndex;
    }

    std::string AssetHotReloadService::NormalizePathKey(const std::string& rawPath) {
        std::string normalized = rawPath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized;
    }

    bool AssetHotReloadService::IsPathMatch(
        const std::string& componentPath,
        const std::unordered_set<std::string>& normalizedImpactedPaths) {
        if (componentPath.empty()) {
            return false;
        }
        return normalizedImpactedPaths.find(NormalizePathKey(componentPath)) != normalizedImpactedPaths.end();
    }

    void AssetHotReloadService::PushEventLocked(HotReloadEventRecord eventRecord) {
        if (m_EventHistory.size() >= m_MaxEventHistory) {
            m_EventHistory.pop_front();
        }
        m_EventHistory.push_back(std::move(eventRecord));
    }

    Result<HotReloadAssetResult> HotReloadAssetAtRuntime(const HotReloadAssetRequest& request) {
        return AssetHotReloadService::Get().HotReloadAssetAtRuntime(request);
    }

} // namespace HotReload
} // namespace Asset
} // namespace Core


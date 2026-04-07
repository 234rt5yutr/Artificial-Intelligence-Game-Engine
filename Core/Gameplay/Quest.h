#pragma once

// Quest System
// Quest definitions, objectives, progress tracking, and manager

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <variant>
#include <nlohmann/json.hpp>

namespace Core {
namespace Gameplay {

    using Json = nlohmann::json;

    //=========================================================================
    // Quest Enums
    //=========================================================================

    /// Quest status
    enum class QuestStatus : uint8_t {
        NotStarted = 0,
        InProgress,
        Completed,
        Failed,
        Abandoned
    };

    /// Objective type
    enum class ObjectiveType : uint8_t {
        Kill,              // Kill X enemies
        Collect,           // Collect X items
        Talk,              // Talk to NPC
        GoTo,              // Reach location
        Escort,            // Escort NPC to location
        Interact,          // Interact with object
        Survive,           // Survive for time
        Custom             // Custom objective
    };

    //=========================================================================
    // Quest Objective
    //=========================================================================

    struct QuestObjective {
        std::string Id;                    // Unique objective ID within quest
        std::string Description;           // Objective description text
        ObjectiveType Type = ObjectiveType::Custom;

        // Target info (depends on type)
        std::string TargetId;              // Entity/item/location ID
        std::string TargetName;            // Display name
        
        uint32_t RequiredCount = 1;        // Amount needed
        uint32_t CurrentCount = 0;         // Current progress
        
        bool IsOptional = false;           // Optional objectives don't block completion
        bool IsHidden = false;             // Hidden until triggered
        bool IsComplete = false;

        // Location (for GoTo/Escort)
        float TargetX = 0.0f, TargetY = 0.0f, TargetZ = 0.0f;
        float TargetRadius = 5.0f;         // Radius to count as "reached"

        // Time limit (for Survive)
        float TimeLimit = 0.0f;
        float TimeElapsed = 0.0f;

        float GetProgress() const {
            if (RequiredCount == 0) return IsComplete ? 1.0f : 0.0f;
            return static_cast<float>(CurrentCount) / 
                   static_cast<float>(RequiredCount);
        }

        bool CheckComplete() {
            IsComplete = CurrentCount >= RequiredCount;
            return IsComplete;
        }

        Json ToJson() const {
            Json j;
            j["id"] = Id;
            j["description"] = Description;
            j["type"] = static_cast<int>(Type);
            j["targetId"] = TargetId;
            j["targetName"] = TargetName;
            j["requiredCount"] = RequiredCount;
            j["currentCount"] = CurrentCount;
            j["isOptional"] = IsOptional;
            j["isHidden"] = IsHidden;
            j["isComplete"] = IsComplete;
            j["position"] = {TargetX, TargetY, TargetZ};
            j["radius"] = TargetRadius;
            j["timeLimit"] = TimeLimit;
            j["timeElapsed"] = TimeElapsed;
            return j;
        }

        void FromJson(const Json& j) {
            Id = j.value("id", "");
            Description = j.value("description", "");
            Type = static_cast<ObjectiveType>(j.value("type", 0));
            TargetId = j.value("targetId", "");
            TargetName = j.value("targetName", "");
            RequiredCount = j.value("requiredCount", 1);
            CurrentCount = j.value("currentCount", 0);
            IsOptional = j.value("isOptional", false);
            IsHidden = j.value("isHidden", false);
            IsComplete = j.value("isComplete", false);
            if (j.contains("position") && j["position"].is_array()) {
                TargetX = j["position"][0].get<float>();
                TargetY = j["position"][1].get<float>();
                TargetZ = j["position"][2].get<float>();
            }
            TargetRadius = j.value("radius", 5.0f);
            TimeLimit = j.value("timeLimit", 0.0f);
            TimeElapsed = j.value("timeElapsed", 0.0f);
        }
    };

    //=========================================================================
    // Quest Reward
    //=========================================================================

    struct QuestReward {
        std::string ItemId;                // Item to give
        uint32_t Quantity = 1;
        uint32_t Experience = 0;
        uint32_t Currency = 0;
        std::string UnlockQuestId;         // Quest to unlock on completion
        std::unordered_map<std::string, int32_t> CustomRewards;

        Json ToJson() const {
            Json j;
            j["itemId"] = ItemId;
            j["quantity"] = Quantity;
            j["experience"] = Experience;
            j["currency"] = Currency;
            j["unlockQuest"] = UnlockQuestId;
            j["custom"] = CustomRewards;
            return j;
        }

        void FromJson(const Json& j) {
            ItemId = j.value("itemId", "");
            Quantity = j.value("quantity", 1);
            Experience = j.value("experience", 0);
            Currency = j.value("currency", 0);
            UnlockQuestId = j.value("unlockQuest", "");
            if (j.contains("custom")) {
                CustomRewards = j["custom"].get<std::unordered_map<std::string, int32_t>>();
            }
        }
    };

    //=========================================================================
    // Quest Definition
    //=========================================================================

    struct QuestDefinition {
        std::string Id;                    // Unique quest ID
        std::string Name;                  // Display name
        std::string Description;           // Quest description
        std::string Category;              // Quest category (main, side, etc.)
        
        uint32_t MinLevel = 0;             // Required player level
        std::vector<std::string> Prerequisites; // Required completed quests
        
        std::vector<QuestObjective> Objectives;
        std::vector<QuestReward> Rewards;
        
        bool IsRepeatable = false;
        bool AutoComplete = true;          // Complete when all objectives done
        bool IsTracked = true;             // Show in quest tracker

        // Time limit for entire quest (0 = no limit)
        float TimeLimit = 0.0f;

        // Get total required objective count (non-optional)
        uint32_t GetRequiredObjectiveCount() const {
            uint32_t count = 0;
            for (const auto& obj : Objectives) {
                if (!obj.IsOptional) count++;
            }
            return count;
        }

        Json ToJson() const {
            Json j;
            j["id"] = Id;
            j["name"] = Name;
            j["description"] = Description;
            j["category"] = Category;
            j["minLevel"] = MinLevel;
            j["prerequisites"] = Prerequisites;
            j["isRepeatable"] = IsRepeatable;
            j["autoComplete"] = AutoComplete;
            j["isTracked"] = IsTracked;
            j["timeLimit"] = TimeLimit;

            Json objectivesJson = Json::array();
            for (const auto& obj : Objectives) {
                objectivesJson.push_back(obj.ToJson());
            }
            j["objectives"] = objectivesJson;

            Json rewardsJson = Json::array();
            for (const auto& reward : Rewards) {
                rewardsJson.push_back(reward.ToJson());
            }
            j["rewards"] = rewardsJson;

            return j;
        }

        void FromJson(const Json& j) {
            Id = j.value("id", "");
            Name = j.value("name", "");
            Description = j.value("description", "");
            Category = j.value("category", "");
            MinLevel = j.value("minLevel", 0);
            if (j.contains("prerequisites")) {
                Prerequisites = j["prerequisites"].get<std::vector<std::string>>();
            }
            IsRepeatable = j.value("isRepeatable", false);
            AutoComplete = j.value("autoComplete", true);
            IsTracked = j.value("isTracked", true);
            TimeLimit = j.value("timeLimit", 0.0f);

            Objectives.clear();
            if (j.contains("objectives")) {
                for (const auto& objJson : j["objectives"]) {
                    QuestObjective obj;
                    obj.FromJson(objJson);
                    Objectives.push_back(obj);
                }
            }

            Rewards.clear();
            if (j.contains("rewards")) {
                for (const auto& rewardJson : j["rewards"]) {
                    QuestReward reward;
                    reward.FromJson(rewardJson);
                    Rewards.push_back(reward);
                }
            }
        }
    };

    //=========================================================================
    // Quest Progress (runtime instance)
    //=========================================================================

    struct QuestProgress {
        std::string QuestId;
        QuestStatus Status = QuestStatus::NotStarted;
        std::vector<QuestObjective> Objectives;  // Copy with runtime progress
        float TimeElapsed = 0.0f;
        uint64_t StartTime = 0;
        uint64_t CompletionTime = 0;

        float GetTotalProgress() const {
            if (Objectives.empty()) return Status == QuestStatus::Completed ? 1.0f : 0.0f;
            
            uint32_t totalRequired = 0;
            uint32_t totalProgress = 0;
            
            for (const auto& obj : Objectives) {
                if (!obj.IsOptional) {
                    totalRequired += obj.RequiredCount;
                    totalProgress += std::min(obj.CurrentCount, obj.RequiredCount);
                }
            }
            
            return totalRequired > 0 ? 
                   static_cast<float>(totalProgress) / static_cast<float>(totalRequired) : 1.0f;
        }

        bool AreAllRequiredObjectivesComplete() const {
            for (const auto& obj : Objectives) {
                if (!obj.IsOptional && !obj.IsComplete) {
                    return false;
                }
            }
            return true;
        }

        QuestObjective* GetObjective(const std::string& objectiveId) {
            for (auto& obj : Objectives) {
                if (obj.Id == objectiveId) return &obj;
            }
            return nullptr;
        }

        Json ToJson() const {
            Json j;
            j["questId"] = QuestId;
            j["status"] = static_cast<int>(Status);
            j["timeElapsed"] = TimeElapsed;
            j["startTime"] = StartTime;
            j["completionTime"] = CompletionTime;

            Json objectivesJson = Json::array();
            for (const auto& obj : Objectives) {
                objectivesJson.push_back(obj.ToJson());
            }
            j["objectives"] = objectivesJson;

            return j;
        }

        void FromJson(const Json& j) {
            QuestId = j.value("questId", "");
            Status = static_cast<QuestStatus>(j.value("status", 0));
            TimeElapsed = j.value("timeElapsed", 0.0f);
            StartTime = j.value("startTime", 0ULL);
            CompletionTime = j.value("completionTime", 0ULL);

            Objectives.clear();
            if (j.contains("objectives")) {
                for (const auto& objJson : j["objectives"]) {
                    QuestObjective obj;
                    obj.FromJson(objJson);
                    Objectives.push_back(obj);
                }
            }
        }
    };

    //=========================================================================
    // Quest Database
    //=========================================================================

    class QuestDatabase {
    public:
        static QuestDatabase& Get() {
            static QuestDatabase instance;
            return instance;
        }

        void RegisterQuest(const QuestDefinition& quest) {
            m_Quests[quest.Id] = quest;
        }

        void UnregisterQuest(const std::string& id) {
            m_Quests.erase(id);
        }

        const QuestDefinition* GetQuest(const std::string& id) const {
            auto it = m_Quests.find(id);
            return it != m_Quests.end() ? &it->second : nullptr;
        }

        bool HasQuest(const std::string& id) const {
            return m_Quests.contains(id);
        }

        std::vector<std::string> GetAllQuestIds() const {
            std::vector<std::string> ids;
            ids.reserve(m_Quests.size());
            for (const auto& [id, _] : m_Quests) {
                ids.push_back(id);
            }
            return ids;
        }

        std::vector<const QuestDefinition*> GetQuestsByCategory(
            const std::string& category) const {
            std::vector<const QuestDefinition*> results;
            for (const auto& [_, quest] : m_Quests) {
                if (quest.Category == category) {
                    results.push_back(&quest);
                }
            }
            return results;
        }

        Json ToJson() const {
            Json j = Json::array();
            for (const auto& [_, quest] : m_Quests) {
                j.push_back(quest.ToJson());
            }
            return j;
        }

        void FromJson(const Json& j) {
            m_Quests.clear();
            if (j.is_array()) {
                for (const auto& questJson : j) {
                    QuestDefinition quest;
                    quest.FromJson(questJson);
                    m_Quests[quest.Id] = quest;
                }
            }
        }

        void Clear() { m_Quests.clear(); }
        size_t GetQuestCount() const { return m_Quests.size(); }

    private:
        QuestDatabase() = default;
        std::unordered_map<std::string, QuestDefinition> m_Quests;
    };

    //=========================================================================
    // Quest Component (ECS Component)
    //=========================================================================

    struct QuestComponent {
        std::unordered_map<std::string, QuestProgress> ActiveQuests;
        std::vector<std::string> CompletedQuests;
        std::vector<std::string> FailedQuests;
        uint32_t TrackedQuestLimit = 5;

        bool HasQuest(const std::string& questId) const {
            return ActiveQuests.contains(questId);
        }

        bool HasCompletedQuest(const std::string& questId) const {
            return std::find(CompletedQuests.begin(), CompletedQuests.end(), 
                            questId) != CompletedQuests.end();
        }

        QuestProgress* GetQuestProgress(const std::string& questId) {
            auto it = ActiveQuests.find(questId);
            return it != ActiveQuests.end() ? &it->second : nullptr;
        }

        std::vector<std::string> GetActiveQuestIds() const {
            std::vector<std::string> ids;
            for (const auto& [id, _] : ActiveQuests) {
                ids.push_back(id);
            }
            return ids;
        }

        Json ToJson() const {
            Json j;
            
            Json activeJson;
            for (const auto& [id, progress] : ActiveQuests) {
                activeJson[id] = progress.ToJson();
            }
            j["active"] = activeJson;
            j["completed"] = CompletedQuests;
            j["failed"] = FailedQuests;
            j["trackedLimit"] = TrackedQuestLimit;

            return j;
        }

        void FromJson(const Json& j) {
            ActiveQuests.clear();
            if (j.contains("active")) {
                for (auto& [id, progressJson] : j["active"].items()) {
                    QuestProgress progress;
                    progress.FromJson(progressJson);
                    ActiveQuests[id] = progress;
                }
            }
            
            CompletedQuests.clear();
            if (j.contains("completed")) {
                CompletedQuests = j["completed"].get<std::vector<std::string>>();
            }
            
            FailedQuests.clear();
            if (j.contains("failed")) {
                FailedQuests = j["failed"].get<std::vector<std::string>>();
            }
            
            TrackedQuestLimit = j.value("trackedLimit", 5);
        }
    };

    //=========================================================================
    // Quest Manager
    //=========================================================================

    class QuestManager {
    public:
        using QuestEventCallback = std::function<void(const std::string& questId, 
                                                      QuestStatus newStatus)>;
        using ObjectiveEventCallback = std::function<void(const std::string& questId,
                                                          const std::string& objectiveId,
                                                          uint32_t newProgress)>;

        static QuestManager& Get() {
            static QuestManager instance;
            return instance;
        }

        // ================================================================
        // Quest Operations
        // ================================================================

        /// Start a quest for an entity
        bool StartQuest(QuestComponent& questComp, const std::string& questId) {
            // Check if already has quest
            if (questComp.HasQuest(questId)) return false;
            
            // Check if repeatable or already completed
            if (!questComp.HasCompletedQuest(questId)) {
                // OK to start
            } else {
                const QuestDefinition* def = QuestDatabase::Get().GetQuest(questId);
                if (!def || !def->IsRepeatable) return false;
            }

            const QuestDefinition* def = QuestDatabase::Get().GetQuest(questId);
            if (!def) return false;

            // Check prerequisites
            for (const auto& prereq : def->Prerequisites) {
                if (!questComp.HasCompletedQuest(prereq)) {
                    return false;
                }
            }

            // Create progress entry
            QuestProgress progress;
            progress.QuestId = questId;
            progress.Status = QuestStatus::InProgress;
            progress.Objectives = def->Objectives;
            progress.StartTime = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count());

            questComp.ActiveQuests[questId] = progress;

            // Fire event
            if (m_OnQuestStatusChanged) {
                m_OnQuestStatusChanged(questId, QuestStatus::InProgress);
            }

            return true;
        }

        /// Update objective progress
        bool UpdateObjective(QuestComponent& questComp, 
                            const std::string& questId,
                            const std::string& objectiveId,
                            uint32_t progressDelta = 1) {
            auto* progress = questComp.GetQuestProgress(questId);
            if (!progress || progress->Status != QuestStatus::InProgress) {
                return false;
            }

            auto* objective = progress->GetObjective(objectiveId);
            if (!objective || objective->IsComplete) return false;

            objective->CurrentCount = std::min(
                objective->CurrentCount + progressDelta,
                objective->RequiredCount);
            
            // Fire objective event
            if (m_OnObjectiveProgress) {
                m_OnObjectiveProgress(questId, objectiveId, objective->CurrentCount);
            }

            // Check completion
            if (objective->CheckComplete()) {
                CheckQuestCompletion(questComp, questId);
            }

            return true;
        }

        /// Complete a quest manually
        void CompleteQuest(QuestComponent& questComp, const std::string& questId) {
            auto* progress = questComp.GetQuestProgress(questId);
            if (!progress) return;

            progress->Status = QuestStatus::Completed;
            progress->CompletionTime = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count());

            questComp.CompletedQuests.push_back(questId);
            questComp.ActiveQuests.erase(questId);

            if (m_OnQuestStatusChanged) {
                m_OnQuestStatusChanged(questId, QuestStatus::Completed);
            }
        }

        /// Fail a quest
        void FailQuest(QuestComponent& questComp, const std::string& questId) {
            auto* progress = questComp.GetQuestProgress(questId);
            if (!progress) return;

            progress->Status = QuestStatus::Failed;
            questComp.FailedQuests.push_back(questId);
            questComp.ActiveQuests.erase(questId);

            if (m_OnQuestStatusChanged) {
                m_OnQuestStatusChanged(questId, QuestStatus::Failed);
            }
        }

        /// Abandon a quest
        void AbandonQuest(QuestComponent& questComp, const std::string& questId) {
            if (!questComp.HasQuest(questId)) return;

            questComp.ActiveQuests.erase(questId);

            if (m_OnQuestStatusChanged) {
                m_OnQuestStatusChanged(questId, QuestStatus::Abandoned);
            }
        }

        // ================================================================
        // Event Callbacks
        // ================================================================

        void SetOnQuestStatusChanged(QuestEventCallback callback) {
            m_OnQuestStatusChanged = std::move(callback);
        }

        void SetOnObjectiveProgress(ObjectiveEventCallback callback) {
            m_OnObjectiveProgress = std::move(callback);
        }

    private:
        QuestManager() = default;

        void CheckQuestCompletion(QuestComponent& questComp, 
                                  const std::string& questId) {
            auto* progress = questComp.GetQuestProgress(questId);
            if (!progress) return;

            const QuestDefinition* def = QuestDatabase::Get().GetQuest(questId);
            if (!def || !def->AutoComplete) return;

            if (progress->AreAllRequiredObjectivesComplete()) {
                CompleteQuest(questComp, questId);
            }
        }

        QuestEventCallback m_OnQuestStatusChanged;
        ObjectiveEventCallback m_OnObjectiveProgress;
    };

    //=========================================================================
    // Utility Functions
    //=========================================================================

    inline const char* QuestStatusToString(QuestStatus status) {
        switch (status) {
            case QuestStatus::NotStarted: return "Not Started";
            case QuestStatus::InProgress: return "In Progress";
            case QuestStatus::Completed: return "Completed";
            case QuestStatus::Failed: return "Failed";
            case QuestStatus::Abandoned: return "Abandoned";
            default: return "Unknown";
        }
    }

    inline const char* ObjectiveTypeToString(ObjectiveType type) {
        switch (type) {
            case ObjectiveType::Kill: return "Kill";
            case ObjectiveType::Collect: return "Collect";
            case ObjectiveType::Talk: return "Talk";
            case ObjectiveType::GoTo: return "Go To";
            case ObjectiveType::Escort: return "Escort";
            case ObjectiveType::Interact: return "Interact";
            case ObjectiveType::Survive: return "Survive";
            case ObjectiveType::Custom: return "Custom";
            default: return "Unknown";
        }
    }

} // namespace Gameplay
} // namespace Core

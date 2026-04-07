#pragma once

// Dialogue System
// Node-based dialogue trees with conditions, actions, and branching

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace Core {
namespace Dialogue {

    using Json = nlohmann::json;

    //=========================================================================
    // Dialogue Node Types
    //=========================================================================

    enum class DialogueNodeType : uint8_t {
        Text,              // Simple text display
        Choice,            // Player choice selection
        Branch,            // Conditional branching
        Action,            // Execute game action
        Random,            // Random selection
        Start,             // Entry point
        End                // Exit point
    };

    //=========================================================================
    // Dialogue Condition
    //=========================================================================

    struct DialogueCondition {
        std::string Type;              // "quest", "item", "variable", "custom"
        std::string Key;               // Quest ID, item ID, variable name
        std::string Operator;          // "==", "!=", ">", "<", ">=", "<=", "has"
        std::string Value;             // Value to compare

        bool IsNegated = false;

        Json ToJson() const {
            Json j;
            j["type"] = Type;
            j["key"] = Key;
            j["op"] = Operator;
            j["value"] = Value;
            j["negated"] = IsNegated;
            return j;
        }

        void FromJson(const Json& j) {
            Type = j.value("type", "");
            Key = j.value("key", "");
            Operator = j.value("op", "==");
            Value = j.value("value", "");
            IsNegated = j.value("negated", false);
        }
    };

    //=========================================================================
    // Dialogue Action
    //=========================================================================

    struct DialogueAction {
        std::string Type;              // "give_item", "start_quest", "set_variable", etc.
        std::string Target;            // Target ID or name
        std::string Value;             // Action value
        std::unordered_map<std::string, std::string> Parameters;

        Json ToJson() const {
            Json j;
            j["type"] = Type;
            j["target"] = Target;
            j["value"] = Value;
            j["params"] = Parameters;
            return j;
        }

        void FromJson(const Json& j) {
            Type = j.value("type", "");
            Target = j.value("target", "");
            Value = j.value("value", "");
            if (j.contains("params")) {
                Parameters = j["params"].get<std::unordered_map<std::string, std::string>>();
            }
        }
    };

    //=========================================================================
    // Dialogue Choice
    //=========================================================================

    struct DialogueChoice {
        std::string Text;              // Choice text to display
        std::string NextNodeId;        // Node to go to when selected
        std::vector<DialogueCondition> Conditions;  // Conditions to show choice
        std::vector<DialogueAction> Actions;        // Actions on selection
        bool IsHiddenIfUnavailable = true;
        int32_t Priority = 0;          // Higher = shown first

        bool IsDefaultChoice = false;  // Auto-select after timeout

        Json ToJson() const {
            Json j;
            j["text"] = Text;
            j["nextNodeId"] = NextNodeId;
            j["hideIfUnavailable"] = IsHiddenIfUnavailable;
            j["priority"] = Priority;
            j["isDefault"] = IsDefaultChoice;

            Json conditionsJson = Json::array();
            for (const auto& cond : Conditions) {
                conditionsJson.push_back(cond.ToJson());
            }
            j["conditions"] = conditionsJson;

            Json actionsJson = Json::array();
            for (const auto& action : Actions) {
                actionsJson.push_back(action.ToJson());
            }
            j["actions"] = actionsJson;

            return j;
        }

        void FromJson(const Json& j) {
            Text = j.value("text", "");
            NextNodeId = j.value("nextNodeId", "");
            IsHiddenIfUnavailable = j.value("hideIfUnavailable", true);
            Priority = j.value("priority", 0);
            IsDefaultChoice = j.value("isDefault", false);

            Conditions.clear();
            if (j.contains("conditions")) {
                for (const auto& condJson : j["conditions"]) {
                    DialogueCondition cond;
                    cond.FromJson(condJson);
                    Conditions.push_back(cond);
                }
            }

            Actions.clear();
            if (j.contains("actions")) {
                for (const auto& actionJson : j["actions"]) {
                    DialogueAction action;
                    action.FromJson(actionJson);
                    Actions.push_back(action);
                }
            }
        }
    };

    //=========================================================================
    // Dialogue Node
    //=========================================================================

    struct DialogueNode {
        std::string Id;                           // Unique node ID
        DialogueNodeType Type = DialogueNodeType::Text;

        // Text node data
        std::string SpeakerId;                    // Who is speaking
        std::string SpeakerName;                  // Display name
        std::string Text;                         // Dialogue text
        std::string NextNodeId;                   // Next node (for Text/Action)

        // Choice node data
        std::vector<DialogueChoice> Choices;

        // Branch node data
        std::vector<std::pair<std::vector<DialogueCondition>, std::string>> Branches;

        // Random node data
        std::vector<std::string> RandomNodes;     // Possible next nodes

        // Actions to execute when entering this node
        std::vector<DialogueAction> EntryActions;

        // Audio/Visual
        std::string VoiceClipPath;                // Path to voice audio
        std::string AnimationId;                  // Animation to play
        std::string CameraAngleId;                // Camera preset
        float TextSpeed = 1.0f;                   // Text reveal speed multiplier

        // Timing
        float AutoAdvanceDelay = 0.0f;            // Auto-advance after seconds (0=disabled)
        bool WaitForInput = true;                 // Wait for player input

        // Metadata
        std::unordered_map<std::string, std::string> Tags;

        Json ToJson() const {
            Json j;
            j["id"] = Id;
            j["type"] = static_cast<int>(Type);
            j["speakerId"] = SpeakerId;
            j["speakerName"] = SpeakerName;
            j["text"] = Text;
            j["nextNodeId"] = NextNodeId;
            j["voiceClip"] = VoiceClipPath;
            j["animation"] = AnimationId;
            j["camera"] = CameraAngleId;
            j["textSpeed"] = TextSpeed;
            j["autoAdvance"] = AutoAdvanceDelay;
            j["waitForInput"] = WaitForInput;
            j["tags"] = Tags;

            Json choicesJson = Json::array();
            for (const auto& choice : Choices) {
                choicesJson.push_back(choice.ToJson());
            }
            j["choices"] = choicesJson;

            Json entryActionsJson = Json::array();
            for (const auto& action : EntryActions) {
                entryActionsJson.push_back(action.ToJson());
            }
            j["entryActions"] = entryActionsJson;

            Json branchesJson = Json::array();
            for (const auto& [conditions, nodeId] : Branches) {
                Json branchJson;
                branchJson["nodeId"] = nodeId;
                Json conditionsJson = Json::array();
                for (const auto& cond : conditions) {
                    conditionsJson.push_back(cond.ToJson());
                }
                branchJson["conditions"] = conditionsJson;
                branchesJson.push_back(branchJson);
            }
            j["branches"] = branchesJson;

            j["randomNodes"] = RandomNodes;

            return j;
        }

        void FromJson(const Json& j) {
            Id = j.value("id", "");
            Type = static_cast<DialogueNodeType>(j.value("type", 0));
            SpeakerId = j.value("speakerId", "");
            SpeakerName = j.value("speakerName", "");
            Text = j.value("text", "");
            NextNodeId = j.value("nextNodeId", "");
            VoiceClipPath = j.value("voiceClip", "");
            AnimationId = j.value("animation", "");
            CameraAngleId = j.value("camera", "");
            TextSpeed = j.value("textSpeed", 1.0f);
            AutoAdvanceDelay = j.value("autoAdvance", 0.0f);
            WaitForInput = j.value("waitForInput", true);
            
            if (j.contains("tags")) {
                Tags = j["tags"].get<std::unordered_map<std::string, std::string>>();
            }

            Choices.clear();
            if (j.contains("choices")) {
                for (const auto& choiceJson : j["choices"]) {
                    DialogueChoice choice;
                    choice.FromJson(choiceJson);
                    Choices.push_back(choice);
                }
            }

            EntryActions.clear();
            if (j.contains("entryActions")) {
                for (const auto& actionJson : j["entryActions"]) {
                    DialogueAction action;
                    action.FromJson(actionJson);
                    EntryActions.push_back(action);
                }
            }

            Branches.clear();
            if (j.contains("branches")) {
                for (const auto& branchJson : j["branches"]) {
                    std::vector<DialogueCondition> conditions;
                    if (branchJson.contains("conditions")) {
                        for (const auto& condJson : branchJson["conditions"]) {
                            DialogueCondition cond;
                            cond.FromJson(condJson);
                            conditions.push_back(cond);
                        }
                    }
                    std::string nodeId = branchJson.value("nodeId", "");
                    Branches.emplace_back(conditions, nodeId);
                }
            }

            if (j.contains("randomNodes")) {
                RandomNodes = j["randomNodes"].get<std::vector<std::string>>();
            }
        }
    };

    //=========================================================================
    // Dialogue Tree
    //=========================================================================

    struct DialogueTree {
        std::string Id;
        std::string Name;
        std::string Description;
        std::string StartNodeId;
        
        std::unordered_map<std::string, DialogueNode> Nodes;
        
        // Metadata
        std::vector<std::string> ParticipantIds;      // Required speakers
        std::unordered_map<std::string, std::string> Variables;  // Local variables

        const DialogueNode* GetNode(const std::string& nodeId) const {
            auto it = Nodes.find(nodeId);
            return it != Nodes.end() ? &it->second : nullptr;
        }

        DialogueNode* GetNode(const std::string& nodeId) {
            auto it = Nodes.find(nodeId);
            return it != Nodes.end() ? &it->second : nullptr;
        }

        const DialogueNode* GetStartNode() const {
            return GetNode(StartNodeId);
        }

        Json ToJson() const {
            Json j;
            j["id"] = Id;
            j["name"] = Name;
            j["description"] = Description;
            j["startNodeId"] = StartNodeId;
            j["participants"] = ParticipantIds;
            j["variables"] = Variables;

            Json nodesJson;
            for (const auto& [nodeId, node] : Nodes) {
                nodesJson[nodeId] = node.ToJson();
            }
            j["nodes"] = nodesJson;

            return j;
        }

        void FromJson(const Json& j) {
            Id = j.value("id", "");
            Name = j.value("name", "");
            Description = j.value("description", "");
            StartNodeId = j.value("startNodeId", "");
            
            if (j.contains("participants")) {
                ParticipantIds = j["participants"].get<std::vector<std::string>>();
            }
            if (j.contains("variables")) {
                Variables = j["variables"].get<std::unordered_map<std::string, std::string>>();
            }

            Nodes.clear();
            if (j.contains("nodes")) {
                for (auto& [nodeId, nodeJson] : j["nodes"].items()) {
                    DialogueNode node;
                    node.FromJson(nodeJson);
                    Nodes[nodeId] = node;
                }
            }
        }
    };

    //=========================================================================
    // Dialogue Database
    //=========================================================================

    class DialogueDatabase {
    public:
        static DialogueDatabase& Get() {
            static DialogueDatabase instance;
            return instance;
        }

        void RegisterTree(const DialogueTree& tree) {
            m_Trees[tree.Id] = tree;
        }

        void UnregisterTree(const std::string& id) {
            m_Trees.erase(id);
        }

        const DialogueTree* GetTree(const std::string& id) const {
            auto it = m_Trees.find(id);
            return it != m_Trees.end() ? &it->second : nullptr;
        }

        bool HasTree(const std::string& id) const {
            return m_Trees.contains(id);
        }

        std::vector<std::string> GetAllTreeIds() const {
            std::vector<std::string> ids;
            ids.reserve(m_Trees.size());
            for (const auto& [id, _] : m_Trees) {
                ids.push_back(id);
            }
            return ids;
        }

        Json ToJson() const {
            Json j = Json::array();
            for (const auto& [_, tree] : m_Trees) {
                j.push_back(tree.ToJson());
            }
            return j;
        }

        void FromJson(const Json& j) {
            m_Trees.clear();
            if (j.is_array()) {
                for (const auto& treeJson : j) {
                    DialogueTree tree;
                    tree.FromJson(treeJson);
                    m_Trees[tree.Id] = tree;
                }
            }
        }

        void Clear() { m_Trees.clear(); }
        size_t GetTreeCount() const { return m_Trees.size(); }

    private:
        DialogueDatabase() = default;
        std::unordered_map<std::string, DialogueTree> m_Trees;
    };

    //=========================================================================
    // Dialogue Context (runtime state)
    //=========================================================================

    struct DialogueContext {
        std::string TreeId;
        std::string CurrentNodeId;
        uint32_t InitiatorEntityId = 0;           // Player or initiating entity
        uint32_t TargetEntityId = 0;              // NPC or target entity
        
        std::unordered_map<std::string, std::string> LocalVariables;
        std::vector<std::string> VisitedNodes;
        
        bool IsActive = false;
        float CurrentTextProgress = 0.0f;         // For typewriter effect
        float AutoAdvanceTimer = 0.0f;

        void Reset() {
            TreeId.clear();
            CurrentNodeId.clear();
            LocalVariables.clear();
            VisitedNodes.clear();
            IsActive = false;
            CurrentTextProgress = 0.0f;
            AutoAdvanceTimer = 0.0f;
        }

        bool HasVisitedNode(const std::string& nodeId) const {
            return std::find(VisitedNodes.begin(), VisitedNodes.end(), 
                            nodeId) != VisitedNodes.end();
        }

        void MarkNodeVisited(const std::string& nodeId) {
            if (!HasVisitedNode(nodeId)) {
                VisitedNodes.push_back(nodeId);
            }
        }
    };

    //=========================================================================
    // Dialogue Component (ECS Component)
    //=========================================================================

    struct DialogueComponent {
        std::vector<std::string> AvailableDialogues;  // Dialogue tree IDs this entity can start
        std::string DefaultDialogueId;                 // Default dialogue to start
        
        DialogueContext ActiveContext;
        
        // Cooldown between dialogues
        float DialogueCooldown = 0.0f;
        float TimeSinceLastDialogue = 0.0f;

        bool CanStartDialogue() const {
            return !ActiveContext.IsActive && 
                   TimeSinceLastDialogue >= DialogueCooldown;
        }

        Json ToJson() const {
            Json j;
            j["availableDialogues"] = AvailableDialogues;
            j["defaultDialogue"] = DefaultDialogueId;
            j["cooldown"] = DialogueCooldown;
            return j;
        }

        void FromJson(const Json& j) {
            if (j.contains("availableDialogues")) {
                AvailableDialogues = j["availableDialogues"].get<std::vector<std::string>>();
            }
            DefaultDialogueId = j.value("defaultDialogue", "");
            DialogueCooldown = j.value("cooldown", 0.0f);
        }
    };

    //=========================================================================
    // Dialogue Manager
    //=========================================================================

    class DialogueManager {
    public:
        using ConditionEvaluator = std::function<bool(const DialogueCondition&, 
                                                      const DialogueContext&)>;
        using ActionExecutor = std::function<void(const DialogueAction&,
                                                  DialogueContext&)>;
        using DialogueEventCallback = std::function<void(const std::string& treeId,
                                                         const std::string& eventType)>;
        using NodeEnteredCallback = std::function<void(const DialogueNode&,
                                                       const DialogueContext&)>;
        using ChoiceMadeCallback = std::function<void(const DialogueChoice&,
                                                      const DialogueContext&)>;

        static DialogueManager& Get() {
            static DialogueManager instance;
            return instance;
        }

        // ================================================================
        // Dialogue Control
        // ================================================================

        bool StartDialogue(DialogueComponent& comp, const std::string& treeId,
                          uint32_t initiatorId, uint32_t targetId) {
            if (!comp.CanStartDialogue()) return false;

            const DialogueTree* tree = DialogueDatabase::Get().GetTree(treeId);
            if (!tree || !tree->GetStartNode()) return false;

            comp.ActiveContext.Reset();
            comp.ActiveContext.TreeId = treeId;
            comp.ActiveContext.InitiatorEntityId = initiatorId;
            comp.ActiveContext.TargetEntityId = targetId;
            comp.ActiveContext.LocalVariables = tree->Variables;
            comp.ActiveContext.IsActive = true;

            // Go to start node
            EnterNode(comp, tree->StartNodeId);

            if (m_OnDialogueEvent) {
                m_OnDialogueEvent(treeId, "started");
            }

            return true;
        }

        void EndDialogue(DialogueComponent& comp) {
            if (!comp.ActiveContext.IsActive) return;

            std::string treeId = comp.ActiveContext.TreeId;
            comp.ActiveContext.Reset();
            comp.TimeSinceLastDialogue = 0.0f;

            if (m_OnDialogueEvent) {
                m_OnDialogueEvent(treeId, "ended");
            }
        }

        void AdvanceDialogue(DialogueComponent& comp) {
            if (!comp.ActiveContext.IsActive) return;

            const DialogueTree* tree = DialogueDatabase::Get().GetTree(
                comp.ActiveContext.TreeId);
            if (!tree) return;

            const DialogueNode* currentNode = tree->GetNode(
                comp.ActiveContext.CurrentNodeId);
            if (!currentNode) return;

            // Handle based on node type
            switch (currentNode->Type) {
                case DialogueNodeType::Text:
                case DialogueNodeType::Action:
                    if (!currentNode->NextNodeId.empty()) {
                        EnterNode(comp, currentNode->NextNodeId);
                    } else {
                        EndDialogue(comp);
                    }
                    break;
                case DialogueNodeType::End:
                    EndDialogue(comp);
                    break;
                default:
                    break;
            }
        }

        void SelectChoice(DialogueComponent& comp, int32_t choiceIndex) {
            if (!comp.ActiveContext.IsActive) return;

            const DialogueTree* tree = DialogueDatabase::Get().GetTree(
                comp.ActiveContext.TreeId);
            if (!tree) return;

            const DialogueNode* currentNode = tree->GetNode(
                comp.ActiveContext.CurrentNodeId);
            if (!currentNode || currentNode->Type != DialogueNodeType::Choice) return;

            auto availableChoices = GetAvailableChoices(comp);
            if (choiceIndex < 0 || choiceIndex >= static_cast<int32_t>(availableChoices.size())) {
                return;
            }

            const DialogueChoice& choice = *availableChoices[choiceIndex];

            // Execute choice actions
            for (const auto& action : choice.Actions) {
                ExecuteAction(action, comp.ActiveContext);
            }

            if (m_OnChoiceMade) {
                m_OnChoiceMade(choice, comp.ActiveContext);
            }

            // Go to next node
            if (!choice.NextNodeId.empty()) {
                EnterNode(comp, choice.NextNodeId);
            } else {
                EndDialogue(comp);
            }
        }

        // ================================================================
        // Choice Evaluation
        // ================================================================

        std::vector<const DialogueChoice*> GetAvailableChoices(
            const DialogueComponent& comp) const {
            std::vector<const DialogueChoice*> available;

            if (!comp.ActiveContext.IsActive) return available;

            const DialogueTree* tree = DialogueDatabase::Get().GetTree(
                comp.ActiveContext.TreeId);
            if (!tree) return available;

            const DialogueNode* node = tree->GetNode(comp.ActiveContext.CurrentNodeId);
            if (!node || node->Type != DialogueNodeType::Choice) return available;

            for (const auto& choice : node->Choices) {
                bool conditionsMet = EvaluateConditions(choice.Conditions, 
                                                        comp.ActiveContext);
                if (conditionsMet || !choice.IsHiddenIfUnavailable) {
                    available.push_back(&choice);
                }
            }

            // Sort by priority
            std::sort(available.begin(), available.end(),
                [](const DialogueChoice* a, const DialogueChoice* b) {
                    return a->Priority > b->Priority;
                });

            return available;
        }

        // ================================================================
        // Condition/Action Registration
        // ================================================================

        void RegisterConditionEvaluator(const std::string& type, 
                                        ConditionEvaluator evaluator) {
            m_ConditionEvaluators[type] = std::move(evaluator);
        }

        void RegisterActionExecutor(const std::string& type,
                                    ActionExecutor executor) {
            m_ActionExecutors[type] = std::move(executor);
        }

        // ================================================================
        // Event Callbacks
        // ================================================================

        void SetOnDialogueEvent(DialogueEventCallback callback) {
            m_OnDialogueEvent = std::move(callback);
        }

        void SetOnNodeEntered(NodeEnteredCallback callback) {
            m_OnNodeEntered = std::move(callback);
        }

        void SetOnChoiceMade(ChoiceMadeCallback callback) {
            m_OnChoiceMade = std::move(callback);
        }

    private:
        DialogueManager() {
            RegisterDefaultHandlers();
        }

        void RegisterDefaultHandlers() {
            // Variable condition
            RegisterConditionEvaluator("variable", [](const DialogueCondition& cond,
                                                       const DialogueContext& ctx) {
                auto it = ctx.LocalVariables.find(cond.Key);
                if (it == ctx.LocalVariables.end()) return false;
                
                // Simple string comparison for now
                bool result = (it->second == cond.Value);
                return cond.IsNegated ? !result : result;
            });

            // Visited node condition
            RegisterConditionEvaluator("visited", [](const DialogueCondition& cond,
                                                      const DialogueContext& ctx) {
                bool visited = ctx.HasVisitedNode(cond.Key);
                return cond.IsNegated ? !visited : visited;
            });

            // Set variable action
            RegisterActionExecutor("set_variable", [](const DialogueAction& action,
                                                      DialogueContext& ctx) {
                ctx.LocalVariables[action.Target] = action.Value;
            });
        }

        void EnterNode(DialogueComponent& comp, const std::string& nodeId) {
            const DialogueTree* tree = DialogueDatabase::Get().GetTree(
                comp.ActiveContext.TreeId);
            if (!tree) return;

            const DialogueNode* node = tree->GetNode(nodeId);
            if (!node) return;

            comp.ActiveContext.CurrentNodeId = nodeId;
            comp.ActiveContext.MarkNodeVisited(nodeId);
            comp.ActiveContext.CurrentTextProgress = 0.0f;
            comp.ActiveContext.AutoAdvanceTimer = 0.0f;

            // Execute entry actions
            for (const auto& action : node->EntryActions) {
                ExecuteAction(action, comp.ActiveContext);
            }

            if (m_OnNodeEntered) {
                m_OnNodeEntered(*node, comp.ActiveContext);
            }

            // Handle special node types
            switch (node->Type) {
                case DialogueNodeType::Branch:
                    ProcessBranchNode(comp, *node);
                    break;
                case DialogueNodeType::Random:
                    ProcessRandomNode(comp, *node);
                    break;
                case DialogueNodeType::Action:
                    // Auto-advance for action nodes
                    if (!node->NextNodeId.empty()) {
                        EnterNode(comp, node->NextNodeId);
                    }
                    break;
                case DialogueNodeType::End:
                    EndDialogue(comp);
                    break;
                default:
                    break;
            }
        }

        void ProcessBranchNode(DialogueComponent& comp, const DialogueNode& node) {
            for (const auto& [conditions, nextNodeId] : node.Branches) {
                if (EvaluateConditions(conditions, comp.ActiveContext)) {
                    EnterNode(comp, nextNodeId);
                    return;
                }
            }
            // Fallback to default next node
            if (!node.NextNodeId.empty()) {
                EnterNode(comp, node.NextNodeId);
            }
        }

        void ProcessRandomNode(DialogueComponent& comp, const DialogueNode& node) {
            if (node.RandomNodes.empty()) return;
            
            int32_t index = std::rand() % static_cast<int32_t>(node.RandomNodes.size());
            EnterNode(comp, node.RandomNodes[index]);
        }

        bool EvaluateConditions(const std::vector<DialogueCondition>& conditions,
                               const DialogueContext& ctx) const {
            for (const auto& cond : conditions) {
                auto it = m_ConditionEvaluators.find(cond.Type);
                if (it != m_ConditionEvaluators.end()) {
                    if (!it->second(cond, ctx)) {
                        return false;
                    }
                }
            }
            return true;
        }

        void ExecuteAction(const DialogueAction& action, DialogueContext& ctx) {
            auto it = m_ActionExecutors.find(action.Type);
            if (it != m_ActionExecutors.end()) {
                it->second(action, ctx);
            }
        }

        std::unordered_map<std::string, ConditionEvaluator> m_ConditionEvaluators;
        std::unordered_map<std::string, ActionExecutor> m_ActionExecutors;
        DialogueEventCallback m_OnDialogueEvent;
        NodeEnteredCallback m_OnNodeEntered;
        ChoiceMadeCallback m_OnChoiceMade;
    };

    //=========================================================================
    // Utility Functions
    //=========================================================================

    inline const char* NodeTypeToString(DialogueNodeType type) {
        switch (type) {
            case DialogueNodeType::Text: return "Text";
            case DialogueNodeType::Choice: return "Choice";
            case DialogueNodeType::Branch: return "Branch";
            case DialogueNodeType::Action: return "Action";
            case DialogueNodeType::Random: return "Random";
            case DialogueNodeType::Start: return "Start";
            case DialogueNodeType::End: return "End";
            default: return "Unknown";
        }
    }

} // namespace Dialogue
} // namespace Core

// AISystemsTests: Comprehensive unit tests for AI BehaviorTree, Blackboard,
// Composites, Decorators, Leaves, BehaviorTreeBuilder, Finite State Machine (FSM & FSMBuilder),
// and Dialogue System (DialogueTree, DialogueDatabase, DialogueManager, branching & choices).

#include "Core/AI/BehaviorTree/Blackboard.h"
#include "Core/AI/BehaviorTree/BehaviorTree.h"
#include "Core/AI/BehaviorTree/BTComposites.h"
#include "Core/AI/BehaviorTree/BTDecorators.h"
#include "Core/AI/BehaviorTree/BTLeaves.h"
#include "Core/AI/BehaviorTree/BehaviorTreeBuilder.h"
#include "Core/AI/BehaviorTree/BehaviorTreeManager.h"
#include "Core/AI/FSM/FSM.h"
#include "Core/AI/FSM/FSMBuilder.h"
#include "Core/Dialogue/Dialogue.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    Engine::Log::Init();

    using namespace Core::AI;
    using namespace Core::Dialogue;

    // =========================================================================
    // 1. Blackboard: Type-Safe Storage, Defaults, and Hierarchy
    // =========================================================================
    {
        Blackboard rootBB;
        rootBB.Set<int>("Score", 100);
        rootBB.Set<float>("Speed", 5.5f);
        rootBB.Set<std::string>("Name", "Hero");
        rootBB.Set<bool>("IsAlive", true);
        rootBB.Set<glm::vec3>("TargetPos", glm::vec3(1.0f, 2.0f, 3.0f));

        CHECK(rootBB.Has("Score"));
        CHECK(rootBB.Has("Speed"));
        CHECK(rootBB.Has("Name"));
        CHECK(rootBB.Has("IsAlive"));
        CHECK(rootBB.Has("TargetPos"));
        CHECK(!rootBB.Has("UnknownKey"));

        CHECK(rootBB.Get<int>("Score") == 100);
        CHECK(std::fabs(rootBB.Get<float>("Speed") - 5.5f) < 0.001f);
        CHECK(rootBB.Get<std::string>("Name") == "Hero");
        CHECK(rootBB.Get<bool>("IsAlive") == true);
        CHECK(rootBB.Get<glm::vec3>("TargetPos").x == 1.0f);

        // Default values
        CHECK(rootBB.Get<int>("NonExistent", 42) == 42);

        // TryGet
        auto valOpt = rootBB.TryGet<int>("Score");
        CHECK(valOpt.has_value() && valOpt.value() == 100);
        auto missOpt = rootBB.TryGet<int>("Missing");
        CHECK(!missOpt.has_value());

        // Hierarchical Blackboard (Child inheriting from Parent)
        Blackboard childBB;
        childBB.SetParent(&rootBB);
        childBB.Set<int>("Score", 200); // Shadow parent score
        childBB.Set<std::string>("ChildVar", "Local");

        CHECK(childBB.Get<int>("Score") == 200);           // Local override
        CHECK(childBB.Get<std::string>("Name") == "Hero"); // Inherited from root
        CHECK(childBB.Get<std::string>("ChildVar") == "Local");
        CHECK(rootBB.Get<int>("Score") == 100);            // Root unchanged

        // Remove & Clear
        childBB.Remove("Score");
        CHECK(childBB.Get<int>("Score") == 100); // Falls back to parent after removal
        rootBB.Clear();
        CHECK(!rootBB.Has("Score"));
    }

    // =========================================================================
    // 2. Behavior Tree Composites (Sequence, Selector, Parallel)
    // =========================================================================
    {
        Blackboard bb;

        // Sequence Node: executes until failure
        {
            auto seq = std::make_unique<SequenceNode>("TestSequence");
            int stepCounter = 0;

            seq->AddChild(std::make_unique<ActionNode>([&stepCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                stepCounter++;
                return BTStatus::Success;
            }));
            seq->AddChild(std::make_unique<ActionNode>([&stepCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                stepCounter++;
                return BTStatus::Success;
            }));
            seq->AddChild(std::make_unique<ActionNode>([&stepCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                stepCounter++;
                return BTStatus::Failure; // Stops sequence here
            }));
            seq->AddChild(std::make_unique<ActionNode>([&stepCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                stepCounter++;
                return BTStatus::Success; // Should not execute
            }));

            BTStatus result = seq->Tick(0.016f, bb);
            CHECK(result == BTStatus::Failure);
            CHECK(stepCounter == 3);
        }

        // Selector Node: executes until success
        {
            auto sel = std::make_unique<SelectorNode>("TestSelector");
            int selectorCounter = 0;

            sel->AddChild(std::make_unique<ActionNode>([&selectorCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                selectorCounter++;
                return BTStatus::Failure;
            }));
            sel->AddChild(std::make_unique<ActionNode>([&selectorCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                selectorCounter++;
                return BTStatus::Success; // Stops selector with Success
            }));
            sel->AddChild(std::make_unique<ActionNode>([&selectorCounter]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                selectorCounter++;
                return BTStatus::Success; // Should not execute
            }));

            BTStatus result = sel->Tick(0.016f, bb);
            CHECK(result == BTStatus::Success);
            CHECK(selectorCounter == 2);
        }

        // Parallel Node: executes all children
        {
            auto par = std::make_unique<ParallelNode>(
                ParallelNode::Policy::RequireAll, 
                ParallelNode::Policy::RequireOne, 
                "TestParallel");
            int p1 = 0, p2 = 0;

            par->AddChild(std::make_unique<ActionNode>([&p1]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                p1++;
                return BTStatus::Success;
            }));
            par->AddChild(std::make_unique<ActionNode>([&p2]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                p2++;
                return BTStatus::Success;
            }));

            BTStatus result = par->Tick(0.016f, bb);
            CHECK(result == BTStatus::Success);
            CHECK(p1 == 1 && p2 == 1);
        }
    }

    // =========================================================================
    // 3. Behavior Tree Decorators (Inverter, Succeeder, Failer, Repeater, Cooldown)
    // =========================================================================
    {
        Blackboard bb;

        // Inverter: Success -> Failure, Failure -> Success
        {
            auto inv = std::make_unique<InverterNode>("TestInverter");
            inv->AddChild(std::make_unique<ActionNode>([]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                return BTStatus::Success;
            }));
            CHECK(inv->Tick(0.016f, bb) == BTStatus::Failure);

            // A second AddChild appends rather than replaces, and a decorator
            // only ever reads its first child, so the other direction needs its
            // own node.
            auto inv2 = std::make_unique<InverterNode>("TestInverterFailure");
            inv2->AddChild(std::make_unique<ActionNode>([]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                return BTStatus::Failure;
            }));
            CHECK(inv2->Tick(0.016f, bb) == BTStatus::Success);
        }

        // Succeeder & Failer
        {
            auto succ = std::make_unique<SucceederNode>("TestSucceeder");
            succ->AddChild(std::make_unique<ActionNode>([]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                return BTStatus::Failure;
            }));
            CHECK(succ->Tick(0.016f, bb) == BTStatus::Success);

            auto fail = std::make_unique<FailerNode>("TestFailer");
            fail->AddChild(std::make_unique<ActionNode>([]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                return BTStatus::Success;
            }));
            CHECK(fail->Tick(0.016f, bb) == BTStatus::Failure);
        }

        // Repeater (repeat 3 times)
        {
            auto rep = std::make_unique<RepeaterNode>(3, "TestRepeater");
            int repCount = 0;
            rep->AddChild(std::make_unique<ActionNode>([&repCount]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                repCount++;
                return BTStatus::Success;
            }));

            // A finite repeater loops internally and only yields when the child
            // yields. With a child that completes immediately, all three runs
            // happen inside one tick.
            CHECK(rep->Tick(0.016f, bb) == BTStatus::Success);
            CHECK(repCount == 3);

            // It resets on completion, so ticking again repeats the whole run
            // rather than returning Success for free.
            CHECK(rep->Tick(0.016f, bb) == BTStatus::Success);
            CHECK(repCount == 6);
        }

        // Cooldown Node
        {
            auto cd = std::make_unique<CooldownNode>(1.0f, "TestCooldown");
            int executionCount = 0;
            cd->AddChild(std::make_unique<ActionNode>([&executionCount]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                executionCount++;
                return BTStatus::Success;
            }));

            CHECK(cd->Tick(0.1f, bb) == BTStatus::Success);
            CHECK(executionCount == 1);

            // On cooldown -> returns Failure without executing child
            CHECK(cd->Tick(0.5f, bb) == BTStatus::Failure);
            CHECK(executionCount == 1);

            // Cooldown expires (0.5 + 0.6 = 1.1s > 1.0s)
            CHECK(cd->Tick(0.6f, bb) == BTStatus::Success);
            CHECK(executionCount == 2);
        }
    }

    // =========================================================================
    // 4. BehaviorTreeBuilder: Fluent Hierarchical Tree Construction
    // =========================================================================
    {
        int attackActions = 0;
        int healActions = 0;
        int searchActions = 0;

        BehaviorTreeBuilder builder("AIBattleTree");
        auto tree = builder
            .Selector("RootSelector")
                // Branch 1: If health is low, heal
                .Sequence("HealSequence")
                    .Condition([](const Blackboard& b) {
                        return b.Get<float>("Health") < 30.0f;
                    }, "CheckLowHealth")
                    .Action([&healActions]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& b) {
                        healActions++;
                        b.Set<float>("Health", 100.0f);
                        return BTStatus::Success;
                    }, "HealAction")
                .End() // End HealSequence
                // Branch 2: If has target, attack
                .Sequence("CombatSequence")
                    .Condition([](const Blackboard& b) {
                        return b.Get<bool>("HasTarget") == true;
                    }, "CheckTarget")
                    .Action([&attackActions]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& blackboard) {
                        attackActions++;
                        return BTStatus::Success;
                    }, "AttackAction")
                .End() // End CombatSequence
                // Branch 3: Otherwise search
                .Action([&searchActions]([[maybe_unused]] float deltaTime, [[maybe_unused]] Blackboard& b) {
                    searchActions++;
                    b.Set<bool>("HasTarget", true); // Found target!
                    return BTStatus::Success;
                }, "SearchAction")
            .End() // End RootSelector
            .Build();

        CHECK(tree != nullptr);

        // The tree owns its blackboard, so seed that one rather than a local
        // copy nothing would ever read.
        Blackboard& bb = tree->GetBlackboard();
        bb.Set<float>("Health", 100.0f);
        bb.Set<bool>("HasTarget", false);

        // First tick: High health, no target -> falls through to SearchAction
        BTStatus status1 = tree->Tick(0.016f);
        CHECK(status1 == BTStatus::Success);
        CHECK(searchActions == 1);
        CHECK(attackActions == 0);
        CHECK(healActions == 0);
        CHECK(bb.Get<bool>("HasTarget") == true);

        // Second tick: Has target -> executes CombatSequence (AttackAction)
        BTStatus status2 = tree->Tick(0.016f);
        CHECK(status2 == BTStatus::Success);
        CHECK(attackActions == 1);
        CHECK(searchActions == 1);
        CHECK(healActions == 0);

        // Third tick: Set health < 30 -> executes HealSequence (HealAction)
        bb.Set<float>("Health", 20.0f);
        BTStatus status3 = tree->Tick(0.016f);
        CHECK(status3 == BTStatus::Success);
        CHECK(healActions == 1);
        CHECK(bb.Get<float>("Health") == 100.0f);
    }

    // =========================================================================
    // 5. Finite State Machine (FSM & FSMBuilder)
    // =========================================================================
    {
        bool enteredPatrol = false;
        bool enteredCombat = false;
        bool exitedPatrol = false;

        FSMBuilder builder("GuardAI");
        auto fsm = builder
            .State("Idle")
            .State("Patrol")
                .OnEnter([&enteredPatrol](Blackboard&) { enteredPatrol = true; })
                .OnExit([&exitedPatrol](Blackboard&) { exitedPatrol = true; })
            .State("Combat")
                .OnEnter([&enteredCombat](Blackboard&) { enteredCombat = true; })
            .InitialState("Idle")
            // Transitions
            .EventTransition("StartPatrol", "Idle", "Patrol", "OnStartDuty")
            .Transition("DetectEnemy", "Patrol", "Combat")
                .When([](const Blackboard& b) {
                    return b.Get<float>("DistanceToEnemy") < 15.0f || b.Get<bool>("AlertTriggered");
                })
            .Transition("LostEnemy", "Combat", "Patrol")
                .When([](const Blackboard& b) {
                    return b.Get<float>("DistanceToEnemy") > 40.0f && !b.Get<bool>("AlertTriggered");
                })
            .Build();

        CHECK(fsm != nullptr);

        Blackboard& bb = fsm->GetBlackboard();
        bb.Set<bool>("AlertTriggered", false);
        bb.Set<float>("DistanceToEnemy", 50.0f);

        fsm->Start();
        CHECK(fsm->GetCurrentStateName() == "Idle");

        // Send duty event
        // SendEvent arms the matching transition; the FSM only takes it on the
        // next update, which is what keeps a state change from happening in the
        // middle of whatever code raised the event.
        fsm->SendEvent("OnStartDuty");
        fsm->Update(0.0f);
        CHECK(fsm->GetCurrentStateName() == "Patrol");
        CHECK(enteredPatrol);
        CHECK(!exitedPatrol);

        // Update with far enemy -> remains in Patrol
        fsm->Update(0.1f);
        CHECK(fsm->GetCurrentStateName() == "Patrol");

        // Enemy gets close -> transitions to Combat
        bb.Set<float>("DistanceToEnemy", 10.0f);
        fsm->Update(0.1f);
        CHECK(fsm->GetCurrentStateName() == "Combat");
        CHECK(exitedPatrol);
        CHECK(enteredCombat);

        // Enemy retreats -> transitions back to Patrol
        bb.Set<float>("DistanceToEnemy", 45.0f);
        fsm->Update(0.1f);
        CHECK(fsm->GetCurrentStateName() == "Patrol");
    }

    // =========================================================================
    // 6. Dialogue System: Trees, Choices, Conditions, Actions & Database
    // =========================================================================
    {
        DialogueDatabase& db = DialogueDatabase::Get();
        db.Clear();

        DialogueTree tree;
        tree.Id = "QuestGiver_Elder";
        tree.Name = "Village Elder Dialogue";
        tree.StartNodeId = "node_start";

        // Node 1: Start Greeting
        DialogueNode startNode;
        startNode.Id = "node_start";
        startNode.Type = DialogueNodeType::Text;
        startNode.SpeakerName = "Elder";
        startNode.Text = "Greetings, traveler! Do you seek knowledge or quests?";
        startNode.NextNodeId = "node_choice";
        tree.Nodes[startNode.Id] = startNode;

        // Node 2: Choice Node
        DialogueNode choiceNode;
        choiceNode.Id = "node_choice";
        choiceNode.Type = DialogueNodeType::Choice;

        DialogueChoice c1;
        c1.Text = "Tell me about the ancient ruins.";
        c1.NextNodeId = "node_lore";
        choiceNode.Choices.push_back(c1);

        DialogueChoice c2;
        c2.Text = "I am ready for the quest.";
        c2.NextNodeId = "node_quest";
        // Condition: requires level >= 5
        DialogueCondition lvlCond;
        lvlCond.Type = "variable";
        lvlCond.Key = "PlayerLevel";
        lvlCond.Operator = ">=";
        lvlCond.Value = "5";
        c2.Conditions.push_back(lvlCond);
        choiceNode.Choices.push_back(c2);

        tree.Nodes[choiceNode.Id] = choiceNode;

        // Node 3: Lore Text
        DialogueNode loreNode;
        loreNode.Id = "node_lore";
        loreNode.Type = DialogueNodeType::Text;
        loreNode.SpeakerName = "Elder";
        loreNode.Text = "The ruins were built centuries ago before the great collapse.";
        loreNode.NextNodeId = "node_end";
        tree.Nodes[loreNode.Id] = loreNode;

        // Node 4: Quest Offer Text
        DialogueNode questNode;
        questNode.Id = "node_quest";
        questNode.Type = DialogueNodeType::Text;
        questNode.SpeakerName = "Elder";
        questNode.Text = "Take this relic and venture deep into the cavern!";
        DialogueAction act;
        act.Type = "give_item";
        act.Target = "Player";
        act.Value = "AncientRelic";
        questNode.EntryActions.push_back(act);
        questNode.NextNodeId = "node_end";
        tree.Nodes[questNode.Id] = questNode;

        // Node 5: End Node
        DialogueNode endNode;
        endNode.Id = "node_end";
        endNode.Type = DialogueNodeType::End;
        tree.Nodes[endNode.Id] = endNode;

        db.RegisterTree(tree);
        CHECK(db.HasTree("QuestGiver_Elder"));
        CHECK(db.GetTreeCount() == 1);

        // Run dialogue simulation
        DialogueManager& dialogueMgr = DialogueManager::Get();
        DialogueComponent comp;

        // Custom condition evaluator
        dialogueMgr.RegisterConditionEvaluator("variable", [](const DialogueCondition& cond, const DialogueContext& ctx) -> bool {
            auto it = ctx.LocalVariables.find(cond.Key);
            if (it != ctx.LocalVariables.end()) {
                if (cond.Operator == ">=") {
                    return std::stoi(it->second) >= std::stoi(cond.Value);
                }
            }
            return true;
        });

        std::string awardedItem = "";
        dialogueMgr.RegisterActionExecutor("give_item", [&awardedItem](const DialogueAction& act, DialogueContext&) {
            if (act.Type == "give_item") {
                awardedItem = act.Value;
            }
        });

        // Start dialogue
        bool started = dialogueMgr.StartDialogue(comp, "QuestGiver_Elder", 1, 2);
        CHECK(started);
        CHECK(comp.ActiveContext.IsActive);
        CHECK(comp.ActiveContext.CurrentNodeId == "node_start");

        // Advance to choice node
        dialogueMgr.AdvanceDialogue(comp);
        CHECK(comp.ActiveContext.CurrentNodeId == "node_choice");

        // Set player level to 10 in dialogue local variables
        comp.ActiveContext.LocalVariables["PlayerLevel"] = "10";

        // Select quest choice (index 1)
        // SelectChoice reports through the context rather than a return value:
        // landing on the quest node is what says the choice took.
        dialogueMgr.SelectChoice(comp, 1);
        CHECK(comp.ActiveContext.CurrentNodeId == "node_quest");
        CHECK(awardedItem == "AncientRelic");

        // Entering an End node closes the dialogue on the spot, so advancing off
        // the quest node finishes it rather than parking on the end node - the
        // context is reset, not left pointing at a node nobody can leave.
        dialogueMgr.AdvanceDialogue(comp);
        CHECK(!comp.ActiveContext.IsActive);
        CHECK(comp.ActiveContext.CurrentNodeId.empty());

        // Advancing a finished dialogue is a no-op rather than a crash.
        dialogueMgr.AdvanceDialogue(comp);
        CHECK(!comp.ActiveContext.IsActive);
    }

    std::printf("AISystemsTests: ALL TESTS PASSED!\n");
    return 0;
}

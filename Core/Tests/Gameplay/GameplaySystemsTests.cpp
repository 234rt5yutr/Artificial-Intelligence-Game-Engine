// GameplaySystemsTests: Unit tests for ItemDatabase, InventoryManager, QuestManager,
// and DialogueTree / DialogueManager branching & execution.

#include "Core/Gameplay/Item.h"
#include "Core/Gameplay/Inventory.h"
#include "Core/Gameplay/Quest.h"
#include "Core/Dialogue/Dialogue.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    using namespace Core::Gameplay;
    using namespace Core::Dialogue;

    Engine::Log::Init();

    // 1. Item Database & Definition Setup
    {
        auto& itemDb = ItemDatabase::Get();

        ItemDefinition swordDef;
        swordDef.Id = "iron_sword";
        swordDef.Name = "Iron Sword";
        swordDef.Type = ItemType::Weapon;
        swordDef.Rarity = ItemRarity::Common;
        swordDef.Slot = EquipmentSlot::MainHand;
        swordDef.Stats.Damage = 25.0f;
        swordDef.Weight = 4.5f;
        swordDef.MaxStackSize = 1;
        itemDb.RegisterItem(swordDef);

        ItemDefinition potionDef;
        potionDef.Id = "health_potion";
        potionDef.Name = "Health Potion";
        potionDef.Type = ItemType::Consumable;
        potionDef.Rarity = ItemRarity::Common;
        potionDef.Weight = 0.5f;
        potionDef.MaxStackSize = 10;
        itemDb.RegisterItem(potionDef);

        CHECK(itemDb.HasItem("iron_sword"));
        CHECK(itemDb.HasItem("health_potion"));
        CHECK(itemDb.GetItem("iron_sword")->Stats.Damage == 25.0f);
    }

    // 2. Inventory Management & Equipping
    {
        auto& invMgr = InventoryManager::Get();
        InventoryComponent inv;
        inv.Initialize(10);
        inv.EnableWeightLimit = true;
        inv.MaxWeight = 50.0f;

        // Add 5 health potions (stackable)
        auto res1 = invMgr.AddItem(inv, "health_potion", 5);
        CHECK(res1 == InventoryResult::Success);
        CHECK(inv.GetItemQuantity("health_potion") == 5);
        CHECK(inv.GetUsedSlotCount() == 1);

        // Add 3 more health potions (should stack into the same slot)
        auto res2 = invMgr.AddItem(inv, "health_potion", 3);
        CHECK(res2 == InventoryResult::Success);
        CHECK(inv.GetItemQuantity("health_potion") == 8);
        CHECK(inv.GetUsedSlotCount() == 1);

        // Add Iron Sword
        auto res3 = invMgr.AddItem(inv, "iron_sword", 1);
        CHECK(res3 == InventoryResult::Success);
        CHECK(inv.GetUsedSlotCount() == 2);

        int32_t swordSlot = inv.FindItemSlot("iron_sword");
        CHECK(swordSlot >= 0);

        // Equip Iron Sword
        auto equipRes = invMgr.EquipItem(inv, static_cast<uint32_t>(swordSlot));
        CHECK(equipRes == InventoryResult::Success);
        CHECK(inv.IsItemEquipped(EquipmentSlot::MainHand));
        CHECK(inv.GetUsedSlotCount() == 1); // Sword moved to equipment slot

        // Unequip Iron Sword back to inventory
        auto unequipRes = invMgr.UnequipItem(inv, EquipmentSlot::MainHand);
        CHECK(unequipRes == InventoryResult::Success);
        CHECK(!inv.IsItemEquipped(EquipmentSlot::MainHand));
        CHECK(inv.GetUsedSlotCount() == 2);
    }

    // 3. Quest Progression & Objective Tracking
    {
        auto& questDb = QuestDatabase::Get();
        auto& questMgr = QuestManager::Get();

        QuestDefinition quest;
        quest.Id = "quest_goblin_trouble";
        quest.Name = "Goblin Trouble";
        quest.Description = "Defeat the goblins and collect their totems.";

        QuestObjective killObj;
        killObj.Id = "kill_goblins";
        killObj.Description = "Defeat 3 goblins";
        killObj.Type = ObjectiveType::Kill;
        killObj.TargetId = "goblin_scout";
        killObj.RequiredCount = 3;
        quest.Objectives.push_back(killObj);

        QuestObjective collectObj;
        collectObj.Id = "collect_totems";
        collectObj.Description = "Collect 2 goblin totems";
        collectObj.Type = ObjectiveType::Collect;
        collectObj.TargetId = "goblin_totem";
        collectObj.RequiredCount = 2;
        quest.Objectives.push_back(collectObj);

        questDb.RegisterQuest(quest);
        CHECK(questDb.HasQuest("quest_goblin_trouble"));

        QuestComponent playerQuestComp;
        CHECK(questMgr.StartQuest(playerQuestComp, "quest_goblin_trouble"));
        CHECK(playerQuestComp.HasQuest("quest_goblin_trouble"));
        CHECK(playerQuestComp.GetQuestProgress("quest_goblin_trouble") != nullptr);
        CHECK(playerQuestComp.GetQuestProgress("quest_goblin_trouble")->Status == QuestStatus::InProgress);

        // Update kill progress (all 3 goblins)
        CHECK(questMgr.UpdateObjective(playerQuestComp, "quest_goblin_trouble", "kill_goblins", 3));
        CHECK(playerQuestComp.GetQuestProgress("quest_goblin_trouble")->Status == QuestStatus::InProgress);

        // Update collect progress to finish quest (2 totems)
        CHECK(questMgr.UpdateObjective(playerQuestComp, "quest_goblin_trouble", "collect_totems", 2));
        CHECK(playerQuestComp.HasCompletedQuest("quest_goblin_trouble"));
    }

    // 4. Dialogue Tree Graph & Manager Execution
    {
        DialogueTree dialogue;
        dialogue.Id = "VillageElderDialogue";
        dialogue.Name = "Village Elder Dialogue";
        dialogue.StartNodeId = "start";

        DialogueNode startNode;
        startNode.Id = "start";
        startNode.Type = DialogueNodeType::Text;
        startNode.SpeakerName = "Narrator";
        startNode.Text = "You approach the elder.";
        startNode.NextNodeId = "elder_greeting";
        dialogue.Nodes[startNode.Id] = startNode;

        DialogueNode greetingNode;
        greetingNode.Id = "elder_greeting";
        greetingNode.Type = DialogueNodeType::Text;
        greetingNode.SpeakerName = "Village Elder";
        greetingNode.Text = "Welcome, traveler! How can I help you today?";
        greetingNode.NextNodeId = "elder_choice";
        dialogue.Nodes[greetingNode.Id] = greetingNode;

        DialogueNode choiceNode;
        choiceNode.Id = "elder_choice";
        choiceNode.Type = DialogueNodeType::Choice;
        
        DialogueChoice choice1;
        choice1.Text = "I heard you have a goblin problem.";
        choice1.NextNodeId = "elder_quest_response";
        choice1.Priority = 10;
        choiceNode.Choices.push_back(choice1);

        DialogueChoice choice2;
        choice2.Text = "Just passing through. Farewell.";
        choice2.NextNodeId = "elder_farewell";
        choice2.Priority = 5;
        choiceNode.Choices.push_back(choice2);

        dialogue.Nodes[choiceNode.Id] = choiceNode;

        DialogueNode questResponseNode;
        questResponseNode.Id = "elder_quest_response";
        questResponseNode.Type = DialogueNodeType::Text;
        questResponseNode.SpeakerName = "Village Elder";
        questResponseNode.Text = "Indeed! Please help our village.";
        questResponseNode.NextNodeId = "elder_farewell";
        dialogue.Nodes[questResponseNode.Id] = questResponseNode;

        DialogueNode farewellNode;
        farewellNode.Id = "elder_farewell";
        farewellNode.Type = DialogueNodeType::End;
        dialogue.Nodes[farewellNode.Id] = farewellNode;

        DialogueDatabase::Get().RegisterTree(dialogue);
        CHECK(DialogueDatabase::Get().HasTree("VillageElderDialogue"));

        auto& dlgMgr = DialogueManager::Get();
        DialogueComponent comp;

        // Start dialogue
        CHECK(dlgMgr.StartDialogue(comp, "VillageElderDialogue", 1, 2));
        CHECK(comp.ActiveContext.IsActive);
        CHECK(comp.ActiveContext.CurrentNodeId == "start");

        // Advance to greeting
        dlgMgr.AdvanceDialogue(comp);
        CHECK(comp.ActiveContext.CurrentNodeId == "elder_greeting");

        // Advance to choices
        dlgMgr.AdvanceDialogue(comp);
        CHECK(comp.ActiveContext.CurrentNodeId == "elder_choice");

        auto choices = dlgMgr.GetAvailableChoices(comp);
        CHECK(choices.size() == 2);
        CHECK(choices[0]->Text == "I heard you have a goblin problem.");

        // Select first choice
        dlgMgr.SelectChoice(comp, 0);
        CHECK(comp.ActiveContext.CurrentNodeId == "elder_quest_response");

        // Advance to farewell (End node, which automatically finishes the dialogue)
        dlgMgr.AdvanceDialogue(comp);
        CHECK(!comp.ActiveContext.IsActive); // Cleanly finished dialogue
    }

    return 0;
}

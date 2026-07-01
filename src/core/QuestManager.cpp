#include "QuestManager.h"
#include "EventBus.h"
#include "InventoryManager.h"
#include <algorithm>

void QuestManager::AcceptQuest(const Quest& quest) {
    if (_activeQuests.count(quest.Id) || _completedIds.count(quest.Id)) return;
    _activeQuests[quest.Id] = quest;
    EventBus::Get().Emit("QuestAccepted", quest.Id);
}

void QuestManager::UpdateProgress(const std::string& questId,
                                   const std::string& objectiveId,
                                   int progress) {
    auto qIt = _activeQuests.find(questId);
    if (qIt == _activeQuests.end()) return;

    auto oIt = qIt->second.Objectives.find(objectiveId);
    if (oIt == qIt->second.Objectives.end()) return;

    oIt->second.Current = std::min(progress, oIt->second.Required);

    if (qIt->second.IsComplete())
        EventBus::Get().Emit("QuestObjectiveMet", questId);
}

void QuestManager::CompleteQuest(const std::string& questId) {
    auto it = _activeQuests.find(questId);
    if (it == _activeQuests.end()) return;

    Quest quest = std::move(it->second);
    _activeQuests.erase(it);
    _completedIds.insert(questId);

    for (const auto& reward : quest.Rewards)
        InventoryManager::Get().AddItem(reward.ItemId, reward.Amount);

    EventBus::Get().Emit("QuestCompleted", questId);
}

void QuestManager::RestoreCompletedIds(const std::vector<std::string>& ids) {
    _completedIds.clear();
    _completedIds.insert(ids.begin(), ids.end());
}

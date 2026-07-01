#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct QuestObjective {
    std::string Description;
    int Required = 1;
    int Current  = 0;
};

struct QuestReward {
    std::string ItemId;
    int         Amount = 1;
};

struct Quest {
    std::string Id;
    std::string Title;
    std::string Description;
    std::unordered_map<std::string, QuestObjective> Objectives;
    std::vector<QuestReward>                        Rewards;

    bool IsComplete() const {
        return std::all_of(Objectives.begin(), Objectives.end(),
            [](const auto& kv) { return kv.second.Current >= kv.second.Required; });
    }
};

/// Tracks active and completed quests.
/// Quest data lives in Quest objects; this manager owns their lifecycle.
class QuestManager {
public:
    static QuestManager& Get() {
        static QuestManager instance;
        return instance;
    }

    void AcceptQuest(const Quest& quest);
    void UpdateProgress(const std::string& questId, const std::string& objectiveId, int progress);
    void CompleteQuest(const std::string& questId);
    void RestoreCompletedIds(const std::vector<std::string>& ids);

    const std::unordered_map<std::string, Quest>& ActiveQuests() const { return _activeQuests; }
    const std::unordered_set<std::string>&        CompletedIds() const { return _completedIds; }

private:
    QuestManager() = default;
    std::unordered_map<std::string, Quest> _activeQuests;
    std::unordered_set<std::string>        _completedIds;
};

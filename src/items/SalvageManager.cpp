#include "items/SalvageManager.h"

std::vector<AttributeToken> SalvageManager::s_pending;

std::string SalvageManager::AttributeForType(ModuleType type) {
    switch (type) {
        case ModuleType::Weapon: return "damage_focused";
        case ModuleType::Armor:  return "hull_reinforced";
        case ModuleType::Shield: return "shield_amplified";
        case ModuleType::Engine: return "thrust_overcharged";
        default:                 return "";   // Hyperdrive/Auxiliary yield no token
    }
}

std::vector<AttributeToken> SalvageManager::Harvest(const LoadoutComponent& loadout) {
    std::vector<AttributeToken> tokens;
    for (const auto& slot : loadout.slots) {
        if (!slot.equipped) continue;
        std::string attrId = AttributeForType(slot.equipped->type);
        if (attrId.empty()) continue;
        tokens.push_back({ slot.equipped->id, attrId, 1 });
    }
    return tokens;
}

void SalvageManager::RecordKill(const LoadoutComponent& loadout) {
    auto tokens = Harvest(loadout);
    s_pending.insert(s_pending.end(), tokens.begin(), tokens.end());
}

std::vector<AttributeToken> SalvageManager::DrainPending() {
    std::vector<AttributeToken> out = std::move(s_pending);
    s_pending.clear();
    return out;
}

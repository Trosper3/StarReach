#include "systems/diplomacy/DiplomacySystem.h"
#include <algorithm>

std::vector<DiplomacyOverride> DiplomacySystem::s_overrides;

void DiplomacySystem::ApplyOverride(Faction from, Faction to, Relation rel, float duration)
{
    // Replace existing override for this pair if one is active.
    for (auto& o : s_overrides)
    {
        if (o.from == from && o.to == to)
        {
            o.overrideRelation = rel;
            o.timeRemaining    = duration;
            return;
        }
    }
    s_overrides.push_back({ from, to, rel, duration });
}

void DiplomacySystem::ClearOverride(Faction from, Faction to)
{
    s_overrides.erase(
        std::remove_if(s_overrides.begin(), s_overrides.end(),
            [from, to](const DiplomacyOverride& o) {
                return o.from == from && o.to == to;
            }),
        s_overrides.end());
}

Relation DiplomacySystem::GetRelation(Faction from, Faction to)
{
    for (const auto& o : s_overrides)
    {
        if (o.from == from && o.to == to)
            return o.overrideRelation;
    }
    return DiplomaticRegistry::Get(from, to);
}

bool DiplomacySystem::HasOverride(Faction from, Faction to)
{
    for (const auto& o : s_overrides)
    {
        if (o.from == from && o.to == to)
            return true;
    }
    return false;
}

void DiplomacySystem::Update(float dt)
{
    for (auto& o : s_overrides)
        o.timeRemaining -= dt;

    s_overrides.erase(
        std::remove_if(s_overrides.begin(), s_overrides.end(),
            [](const DiplomacyOverride& o) { return o.timeRemaining <= 0.f; }),
        s_overrides.end());
}

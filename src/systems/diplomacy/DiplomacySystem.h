#pragma once

#include "systems/diplomacy/DiplomaticRegistry.h"
#include "core/FactionEnum.h"
#include <vector>

// A live diplomatic override that shadows the static matrix entry for (from, to)
// until timeRemaining reaches zero, at which point it is removed and the matrix
// relation resumes. Overrides are one-directional: (from, to) != (to, from).
struct DiplomacyOverride
{
    Faction  from;
    Faction  to;
    Relation overrideRelation;
    float    timeRemaining; // seconds
};

// Event-driven diplomacy override layer ("Crucible Override Engine").
//
// GetRelation() is the single call-site for all diplomacy queries.
// It checks active overrides first and falls back to DiplomaticRegistry.
//
// Typical flow:
//   1. Game event fires → ApplyOverride(Rep, Kor, Friendly, 120.f)
//   2. Every frame: DiplomacySystem::Update(dt)
//   3. After 120 s: override expires; Rep→Kor reverts to matrix (Neutral)
class DiplomacySystem
{
public:
    // Add or replace a temporary override for the (from, to) pair.
    static void ApplyOverride(Faction from, Faction to, Relation rel, float duration);

    // Remove an override early (before it expires).
    static void ClearOverride(Faction from, Faction to);

    // Returns the effective relation: override if one is active, otherwise matrix.
    static Relation GetRelation(Faction from, Faction to);

    // True if an active override exists for (from, to).
    static bool HasOverride(Faction from, Faction to);

    // Advance all override timers by dt; expired overrides are removed.
    // Call once per frame.
    static void Update(float dt);

private:
    static std::vector<DiplomacyOverride> s_overrides;
};

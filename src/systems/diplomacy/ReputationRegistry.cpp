#include "systems/diplomacy/ReputationRegistry.h"
#include <algorithm>

Faction ReputationRegistry::s_playerFaction = Faction::Republic;
float   ReputationRegistry::s_score[9]      = {};
float   ReputationRegistry::s_baseline[9]   = {};

void ReputationRegistry::ResetForPlayerFaction(Faction playerFaction) {
    s_playerFaction = playerFaction;
    for (int i = 0; i < 9; ++i) {
        Faction f = static_cast<Faction>(i);
        if (f == playerFaction) { s_score[i] = s_baseline[i] = 60.0f; continue; }
        switch (DiplomaticRegistry::Get(f, playerFaction)) {
            case Relation::Friendly: s_score[i] =  60.0f; break;
            case Relation::Hostile:  s_score[i] = -60.0f; break;
            default:                 s_score[i] =   0.0f; break;
        }
        s_baseline[i] = s_score[i];
    }
}

float ReputationRegistry::Score(Faction f) {
    size_t i = static_cast<size_t>(f);
    return i < 9 ? s_score[i] : 0.0f;
}

void ReputationRegistry::Adjust(Faction f, float delta) {
    size_t i = static_cast<size_t>(f);
    if (i >= 9 || f == s_playerFaction) return;
    s_score[i] = std::clamp(s_score[i] + delta, -100.0f, 100.0f);
}

Relation ReputationRegistry::PlayerRelation(Faction f) {
    if (f == s_playerFaction) return Relation::Friendly;
    float s = Score(f);
    if (s >= kFriendlyThreshold) return Relation::Friendly;
    if (s <= kHostileThreshold)  return Relation::Hostile;
    return Relation::Neutral;
}

void ReputationRegistry::Tick(float dt) {
    // ~0.4 pts/sec drift toward each faction's matrix-seeded baseline (not
    // absolute 0) — a full swing away from baseline takes about 2.5 minutes
    // to settle back, slow enough that a single event doesn't wash out
    // before the player notices its effect, while still preserving a
    // faction's underlying diplomatic stance instead of eroding it to
    // Neutral with no reinforcing events.
    const float rate = 0.4f * dt;
    for (int i = 0; i < 9; ++i) {
        if (static_cast<Faction>(i) == s_playerFaction) continue;
        float& s = s_score[i];
        const float base = s_baseline[i];
        if      (s > base) s = std::max(base, s - rate);
        else if (s < base) s = std::min(base, s + rate);
    }
}

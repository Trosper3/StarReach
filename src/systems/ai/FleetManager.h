#pragma once
#include "systems/ai/BehaviorRegistry.h"
#include "components/FleetAIComponent.h"
#include "core/FactionEnum.h"
#include "shared/Entity.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ai {

struct ContactResult {
    Relation    relation;
    BehaviorSet behaviors;
};

// Evaluates the diplomatic relationship between two factions on contact.
// Init() pre-builds a 9x9 cache combining DiplomaticRegistry + BehaviorRegistry
// so every in-flight EvaluateContact call is a single array index.
//
// Also owns the Fleet AI state machine for NPC entities:
//   RegisterNpc  — give an entity a FleetAIComponent
//   NotifyContact — start detection timer when contact enters range
//   Update        — advance timers; fire transitions; write back AIControllerComponent
class FleetManager {
public:
    // ── Diplomatic cache ──────────────────────────────────────────────────────
    static void          Init();
    static ContactResult EvaluateContact(Faction self, Faction other);

    // ── Fleet AI state machine ────────────────────────────────────────────────

    // Register an NPC entity so it participates in the state machine.
    static void RegisterNpc(uint32_t entityId, Faction faction);

    // Call when a contact enters sensor range of an NPC.
    // Starts (or resets) the detection timer; does nothing if already Identified.
    static void NotifyContact(uint32_t entityId,
                              uint32_t contactId, Faction contactFaction);

    // Advance all detection timers by dt. When a timer expires, transition
    // Unknown/Detecting → Identified and write the resolved AIState back to
    // the matching Entity in `entities`. Deterministic: no RNG.
    static void Update(std::vector<ecs::Entity>& entities, float dt);

private:
    static ContactResult                             s_cache[9][9];
    static std::unordered_map<uint32_t, FleetAIComponent> s_components;
};

} // namespace ai

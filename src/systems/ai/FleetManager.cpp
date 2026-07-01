#include "systems/ai/FleetManager.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "shared/entities/AIControllerComponent.h"
#include <algorithm>

namespace ai {

ContactResult FleetManager::s_cache[9][9] = {};
std::unordered_map<uint32_t, FleetAIComponent> FleetManager::s_components;

void FleetManager::Init() {
    for (int a = 0; a < 9; ++a)
        for (int b = 0; b < 9; ++b) {
            Faction fa = static_cast<Faction>(a);
            Faction fb = static_cast<Faction>(b);
            Relation rel = DiplomaticRegistry::Get(fa, fb);
            s_cache[a][b] = { rel, BehaviorRegistry::Get(rel) };
        }
    s_components.clear();
}

ContactResult FleetManager::EvaluateContact(Faction self, Faction other) {
    return s_cache[static_cast<uint8_t>(self)][static_cast<uint8_t>(other)];
}

void FleetManager::RegisterNpc(uint32_t entityId, Faction faction) {
    FleetAIComponent comp;
    comp.selfFaction = faction;
    s_components[entityId] = comp;
}

void FleetManager::NotifyContact(uint32_t entityId,
                                  uint32_t contactId, Faction contactFaction) {
    auto it = s_components.find(entityId);
    if (it == s_components.end()) return;

    FleetAIComponent& ai = it->second;
    if (ai.state == FleetAIState::Identified) return; // already resolved, no reset

    ai.state         = FleetAIState::Detecting;
    ai.detectionTimer = kDetectionDelay;
    ai.targetId      = contactId;
    ai.targetFaction = contactFaction;
}

// Maps resolved BehaviorSet → AIState written back to the entity.
// Deterministic: priority order Escort > Patrol > Chase, no RNG.
static ecs::AIState BehaviorToAIState(BehaviorSet bs) {
    if (Has(bs, Behavior::Escort))     return ecs::AIState::Escort;
    if (Has(bs, Behavior::Patrol))     return ecs::AIState::Patrol;
    if (Has(bs, Behavior::Aggressive)) return ecs::AIState::Chase;
    return ecs::AIState::Idle;
}

void FleetManager::Update(std::vector<ecs::Entity>& entities, float dt) {
    for (auto& [id, ai] : s_components) {
        if (ai.state != FleetAIState::Detecting) continue;

        ai.detectionTimer -= dt;
        if (ai.detectionTimer > 0.0f) continue;

        // Timer expired — resolve contact deterministically.
        ContactResult cr = EvaluateContact(ai.selfFaction, ai.targetFaction);
        ai.relation  = cr.relation;
        ai.behaviors = cr.behaviors;
        ai.state     = FleetAIState::Identified;

        // Write resolved AIState back to the entity.
        auto it = std::find_if(entities.begin(), entities.end(),
                               [id](const ecs::Entity& e) { return e.id == id; });
        if (it != entities.end())
            it->aiController.state = BehaviorToAIState(ai.behaviors);
    }
}

} // namespace ai

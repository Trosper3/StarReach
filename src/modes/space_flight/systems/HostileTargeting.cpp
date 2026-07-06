#include "modes/space_flight/systems/HostileTargeting.h"
#include "modes/space_flight/SpaceFlight.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "raymath.h"

namespace combat {

HostileTarget FindNearestHostileTarget(const SystemWorld&  world,
                                       const ecs::Entity&  playerEntity,
                                       Faction             playerFaction,
                                       Faction             selfFaction,
                                       Vector2             selfPos,
                                       float               maxRange,
                                       unsigned int         selfStationId) {
    HostileTarget best;
    float bestDist = maxRange;

    for (size_t i = 0; i < world.npcMeta.size(); ++i) {
        const NpcMeta& m = world.npcMeta[i];
        if (!m.alive) continue;
        if (DiplomaticRegistry::Get(selfFaction, m.npcFaction) != Relation::Hostile) continue;
        float d = Vector2Distance(selfPos, world.entities[i].transform.position);
        if (d < bestDist) {
            bestDist = d;
            best = { HostileTargetKind::Npc, m.id, world.entities[i].transform.position, true };
        }
    }

    if (DiplomaticRegistry::Get(selfFaction, playerFaction) == Relation::Hostile) {
        float d = Vector2Distance(selfPos, playerEntity.transform.position);
        if (d < bestDist) {
            bestDist = d;
            best = { HostileTargetKind::Player, 0, playerEntity.transform.position, true };
        }
    }

    for (const SpaceStation& st : world.stations) {
        if (!st.alive || st.id == selfStationId) continue;
        if (DiplomaticRegistry::Get(selfFaction, st.faction) != Relation::Hostile) continue;
        float d = Vector2Distance(selfPos, st.position);
        if (d < bestDist) {
            bestDist = d;
            best = { HostileTargetKind::Station, st.id, st.position, true };
        }
    }

    return best;
}

bool CoreIsProtected(const std::vector<HardpointState>& hardpoints) {
    for (const HardpointState& hp : hardpoints)
        if (!hp.isCore && hp.alive) return true;
    return false;
}

} // namespace combat

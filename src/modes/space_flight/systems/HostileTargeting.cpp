#include "modes/space_flight/systems/HostileTargeting.h"
#include "modes/space_flight/SpaceFlight.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "systems/diplomacy/ReputationRegistry.h"
#include "raymath.h"

namespace combat {

HostileTarget FindNearestHostileTarget(const SystemWorld&  world,
                                       const ecs::Entity&  playerEntity,
                                       Faction             selfFaction,
                                       Vector2             selfPos,
                                       float               maxRange,
                                       unsigned int         selfStationId,
                                       bool                playerTargetable) {
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

    if (playerTargetable && ReputationRegistry::PlayerRelation(selfFaction) == Relation::Hostile) {
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

bool AllHardpointsDestroyed(const std::vector<Hardpoint>& hardpoints) {
    for (const Hardpoint& hp : hardpoints)
        if (hp.alive) return false;
    return true;
}

bool IsDisabled(const std::vector<Hardpoint>& hardpoints) {
    // Keyed off weapon-capable hardpoints only (wSlots > 0), not "every
    // non-core hardpoint" — a docking bay, trade hub, mining drill, engine,
    // or shield array can't shoot back, and destroying them to reach this
    // state would gut the very thing capture is supposed to hand over.
    // Caller only reaches this once combat::AllHardpointsDestroyed is false,
    // so the object is already known to still have something alive.
    for (const Hardpoint& hp : hardpoints)
        if (hp.alive && !hp.WeaponSlots().empty()) return false; // still has a live weapon hardpoint — armed
    return true;
}

} // namespace combat

#include "modes/space_flight/systems/DockRepair.h"
#include "modes/space_flight/SpaceFlight.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "raymath.h"
#include <cfloat>
#include <cmath>

namespace repair {

// Mirrors GetNpcStationHardpointPos (SpaceFlight.cpp) — duplicated here since
// that function is a private file-scope static and the formula is small and
// stable, so duplicating it is lower-risk than exporting it out of a very
// large file.
static Vector2 ComputeHardpointPos(const SpaceStation& st, int hpIndex) {
    if (hpIndex < 0 || hpIndex >= (int)st.hardpoints.size()) return st.position;
    const HardpointState& hp = st.hardpoints[hpIndex];
    if (hp.isCore) return st.position;
    int nonCoreCount = 0, nonCoreIndex = 0;
    for (int i = 0; i < (int)st.hardpoints.size(); ++i) {
        if (!st.hardpoints[i].isCore) {
            if (i < hpIndex) nonCoreIndex++;
            nonCoreCount++;
        }
    }
    if (nonCoreCount == 0) return st.position;
    float angle = ((float)nonCoreIndex / (float)nonCoreCount) * 2.0f * PI;
    float offsetRad = st.radius * 0.70f;
    return { st.position.x + cosf(angle) * offsetRad,
             st.position.y + sinf(angle) * offsetRad };
}

FriendlyDock FindNearestFriendlyDock(const SystemWorld& world,
                                     Faction             selfFaction,
                                     Vector2             selfPos) {
    FriendlyDock best;
    float bestDist = FLT_MAX;

    for (const SpaceStation& st : world.stations) {
        if (!st.alive) continue;
        if (DiplomaticRegistry::Get(selfFaction, st.faction) == Relation::Hostile) continue;

        for (int i = 0; i < (int)st.hardpoints.size(); ++i) {
            const HardpointState& hp = st.hardpoints[i];
            if (!hp.isDockingBay || !hp.alive) continue;

            Vector2 pos = ComputeHardpointPos(st, i);
            float   d   = Vector2Distance(selfPos, pos);
            if (d < bestDist) {
                bestDist = d;
                best = { true, st.id, i, pos };
            }
        }
    }

    return best;
}

} // namespace repair

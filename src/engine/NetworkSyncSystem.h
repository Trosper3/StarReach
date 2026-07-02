#pragma once
#include "../shared/Entity.h"
#include <vector>

namespace ecs {

struct NetworkSnapshot {
    uint32_t networkId    = 0;
    Vector2  position     = { 0.0f, 0.0f };
    Vector2  velocity     = { 0.0f, 0.0f };
    float    rotation     = 0.0f;
    uint32_t shipNameHash = 0;   // fnv32(shipTypeId); 0 for player entities
};

// Reconciles remote entity transforms against server-provided snapshots.
// Local player entities are skipped; client-side prediction owns their state.
class NetworkSyncSystem {
public:
    // Blend factor: fraction of the gap to close each frame (0=no snap, 1=instant).
    static constexpr float kLerpAlpha = 0.2f;

    static void Update(std::vector<Entity>& entities,
                       const std::vector<NetworkSnapshot>& snapshots)
    {
        for (auto& e : entities) {
            if (e.id == 0 || e.network.networkId == 0) continue;
            if (e.network.isLocalPlayer) continue;

            for (const auto& snap : snapshots) {
                if (snap.networkId != e.network.networkId) continue;

                e.transform.position.x += (snap.position.x - e.transform.position.x) * kLerpAlpha;
                e.transform.position.y += (snap.position.y - e.transform.position.y) * kLerpAlpha;
                e.transform.velocity    = snap.velocity;
                e.transform.rotation    = snap.rotation;
                break;
            }
        }
    }
};

} // namespace ecs

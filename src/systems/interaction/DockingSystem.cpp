#include "systems/interaction/DockingSystem.h"
#include "shared/Entity.h"
#include <cmath>

namespace ecs {

namespace {
float Distance(Vector2 a, Vector2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
} // namespace

DockingEvent DockingSystem::CheckProximity(const std::vector<Entity>& entities,
                                            uint32_t playerEntityId) {
    const Entity* player = nullptr;
    for (const auto& e : entities) {
        if (e.id == playerEntityId) { player = &e; break; }
    }
    if (!player) return {};

    const Vector2 playerPos = player->transform.position;

    for (const auto& e : entities) {
        if (e.id == 0 || e.id == playerEntityId)  continue;
        if (e.dockingPort.dockRadius <= 0.f)       continue;

        if (Distance(playerPos, e.transform.position) <= e.dockingPort.dockRadius) {
            DockService svc = e.dockingPort.isEngineering
                              ? DockService::Engineering
                              : DockService::None;
            return { e.id, svc };
        }
    }

    return {};
}

} // namespace ecs

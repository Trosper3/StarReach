#pragma once
#include <cstdint>
#include <vector>

namespace ecs { struct Entity; }

namespace ecs {

enum class DockService { None, Engineering };

// Emitted by DockingSystem::CheckProximity when the player overlaps a docking port.
// stationEntityId == 0 means no overlap this frame.
struct DockingEvent {
    uint32_t   stationEntityId = 0;
    DockService service        = DockService::None;
};

class DockingSystem {
public:
    // Scans all entities for active docking ports (dockRadius > 0) and checks
    // Euclidean distance against the player entity's transform.position.
    // Returns the first overlapping port, or an event with service==None if clear.
    static DockingEvent CheckProximity(const std::vector<Entity>& entities,
                                       uint32_t playerEntityId);
};

} // namespace ecs

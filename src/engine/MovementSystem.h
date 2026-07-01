#pragma once
#include "../shared/Entity.h"
#include <vector>

namespace ecs {

class MovementSystem {
public:
    // Integrates velocity into position for every active entity.
    static void Update(std::vector<Entity>& entities, float dt) {
        for (auto& e : entities) {
            if (e.id == 0) continue;
            float scale = e.loadout.IsOverloaded()
                          ? LoadoutComponent::kOverloadThrustFactor
                          : 1.0f;
            e.transform.position.x += e.transform.velocity.x * scale * dt;
            e.transform.position.y += e.transform.velocity.y * scale * dt;
        }
    }
};

} // namespace ecs

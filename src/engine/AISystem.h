#pragma once
#include "../shared/Entity.h"
#include <vector>
#include <cmath>

namespace ecs {

class AISystem {
public:
    static constexpr float kChaseSpeed  = 150.0f;
    static constexpr float kPatrolSpeed = 80.0f;
    static constexpr float kArrivalDist = 8.0f;

    static void Update(std::vector<Entity>& entities, float /*dt*/) {
        for (auto& e : entities) {
            if (e.id == 0 || e.network.isLocalPlayer) continue;
            if (!e.health.IsAlive()) continue;

            Vector2& vel = e.transform.velocity;

            switch (e.aiController.state) {
            case AIState::Idle:
                vel = { 0.0f, 0.0f };
                break;

            case AIState::Patrol:
            case AIState::Chase: {
                float speed = (e.aiController.state == AIState::Chase)
                              ? kChaseSpeed : kPatrolSpeed;
                float dx   = e.aiController.targetPosition.x - e.transform.position.x;
                float dy   = e.aiController.targetPosition.y - e.transform.position.y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < kArrivalDist) {
                    vel = { 0.0f, 0.0f };
                } else {
                    vel = { (dx / dist) * speed, (dy / dist) * speed };
                    e.transform.rotation = atan2f(dy, dx) * RAD2DEG;
                }
                break;
            }
            }
        }
    }
};

} // namespace ecs

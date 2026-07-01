#pragma once
#include "../shared/Entity.h"
#include "Projectile.h"
#include "../items/SalvageManager.h"
#include <vector>
#include <algorithm>

namespace ecs {

class DamageSystem {
public:
    static void Update(std::vector<Entity>& entities,
                       std::vector<Projectile>& projectiles,
                       float dt)
    {
        // Advance all projectiles
        for (auto& p : projectiles) {
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.lifetime   -= dt;
        }

        // Hit detection — first live entity within hit radius wins
        constexpr float HIT_RADIUS_SQ = 16.0f * 16.0f;
        for (auto& p : projectiles) {
            if (p.lifetime <= 0.0f) continue;
            for (auto& e : entities) {
                if (e.id == 0 || e.id == p.shooterId || !e.health.IsAlive()) continue;
                float dx     = e.transform.position.x - p.position.x;
                float dy     = e.transform.position.y - p.position.y;
                float distSq = dx * dx + dy * dy;
                if (distSq < HIT_RADIUS_SQ) {
                    e.health.ApplyDamage(p.damage);
                    p.lifetime = -1.0f;   // consume on hit
                    if (!e.health.IsAlive())
                        SalvageManager::RecordKill(e.loadout);
                    break;
                }
            }
        }

        // Erase expired and hit projectiles
        projectiles.erase(
            std::remove_if(projectiles.begin(), projectiles.end(),
                [](const Projectile& p) { return p.lifetime <= 0.0f; }),
            projectiles.end());
    }
};

} // namespace ecs

#pragma once
#include "../shared/Entity.h"
#include "Projectile.h"
#include <vector>
#include <cmath>

namespace ecs {

// Updates weapon slot cooldowns and spawns Projectiles for AI entities in Chase state.
// Player fire is injected by PlayerInputSystem (Phase 7) which sets wantsToFire on the entity.
class CombatSystem {
public:
    static void Update(std::vector<Entity>& entities,
                       std::vector<Projectile>& projectiles,
                       float dt)
    {
        for (auto& e : entities) {
            if (e.id == 0 || !e.health.IsAlive()) continue;
            if (e.aiController.state != AIState::Chase)  continue;

            Vector2 origin = e.transform.position;
            Vector2 target = e.aiController.targetPosition;
            float   dx     = target.x - origin.x;
            float   dy     = target.y - origin.y;
            float   dist   = sqrtf(dx * dx + dy * dy);
            if (dist < 1.0f) continue;

            Vector2 dir = { dx / dist, dy / dist };

            for (auto& slot : e.loadout.slots) {
                if (!slot.equipped || slot.equipped->type != ModuleType::Weapon) continue;

                slot.cooldownRemaining -= dt;
                if (slot.cooldownRemaining > 0.0f) continue;

                const WeaponStats& ws = slot.equipped->weapon;
                float resetTime = (ws.fireRate > 0.0f) ? 1.0f / ws.fireRate : 1.0f;
                if (e.loadout.IsOverloaded())
                    resetTime *= LoadoutComponent::kOverloadCooldownMult;
                slot.cooldownRemaining = resetTime;

                float spawnX = origin.x + slot.hardpointOffset.x;
                float spawnY = origin.y + slot.hardpointOffset.y;
                float ttl    = (ws.projSpeed > 0.0f) ? ws.projRange / ws.projSpeed : 2.0f;

                Projectile p;
                p.position  = { spawnX, spawnY };
                p.velocity  = { dir.x * ws.projSpeed, dir.y * ws.projSpeed };
                p.damage    = ws.damage + e.loadout.GetTotalBonuses().damageBonus;
                p.lifetime  = ttl;
                p.shooterId = e.id;
                projectiles.push_back(p);
            }
        }
    }
};

} // namespace ecs

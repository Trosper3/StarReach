#pragma once
#include "../shared/Entity.h"
#include "Projectile.h"
#include <vector>
#include <cmath>

namespace ecs {

class PlayerInputSystem {
public:
    static constexpr float kMoveSpeed = 200.0f;

    static void Update(std::vector<Entity>& entities,
                       std::vector<Projectile>& projectiles,
                       float /*dt*/)
    {
        for (auto& e : entities) {
            if (e.id == 0 || !e.network.isLocalPlayer) continue;
            if (!e.health.IsAlive()) continue;

            // WASD / arrow key movement
            Vector2 vel = { 0.0f, 0.0f };
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    vel.y -= kMoveSpeed;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  vel.y += kMoveSpeed;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  vel.x -= kMoveSpeed;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) vel.x += kMoveSpeed;
            e.transform.velocity = vel;

            // Rotate to face mouse cursor
            Vector2 mouse = GetMousePosition();
            float dx = mouse.x - e.transform.position.x;
            float dy = mouse.y - e.transform.position.y;
            e.transform.rotation = atan2f(dy, dx) * RAD2DEG;

            // Fire on Space or left-click
            if (!IsKeyPressed(KEY_SPACE) && !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) continue;

            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < 1.0f) continue;
            Vector2 dir = { dx / dist, dy / dist };

            for (auto& slot : e.loadout.slots) {
                if (!slot.equipped || slot.equipped->type != ModuleType::Weapon) continue;
                if (slot.cooldownRemaining > 0.0f) continue;

                const WeaponStats& ws = slot.equipped->weapon;
                slot.cooldownRemaining = (ws.fireRate > 0.0f) ? 1.0f / ws.fireRate : 1.0f;

                Projectile p;
                p.position  = { e.transform.position.x + slot.hardpointOffset.x,
                                 e.transform.position.y + slot.hardpointOffset.y };
                p.velocity  = { dir.x * ws.projSpeed, dir.y * ws.projSpeed };
                p.damage    = ws.damage + e.loadout.GetTotalBonuses().damageBonus;
                p.lifetime  = (ws.projSpeed > 0.0f) ? ws.projRange / ws.projSpeed : 2.0f;
                p.shooterId = e.id;
                projectiles.push_back(p);
            }
        }
    }
};

} // namespace ecs

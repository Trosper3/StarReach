#include "GameModeManager.h"
#include "../engine/PlayerInputSystem.h"
#include "../engine/AISystem.h"
#include "../engine/MovementSystem.h"
#include "../engine/CombatSystem.h"
#include "../engine/DamageSystem.h"
#include "../engine/RenderSystem.h"

namespace ecs {

GameModeManager::GameModeManager() {
    SetMode(GameMode::SpaceFlight);
}

void GameModeManager::SetMode(GameMode mode) {
    _mode = mode;

    // Reset all flags to active, then selectively disable per mode.
    _inputEnabled        = true;
    _aiEnabled           = true;
    _movementEnabled     = true;
    _combatEnabled       = true;
    _damageEnabled       = true;
    _networkEnabled      = true;
    _renderEnabled       = true;
    _engineerMenuEnabled = false;

    switch (mode) {
    case GameMode::SpaceFlight:
        // All systems active. Camera follows player (CameraSystem, not controlled here).
        break;

    case GameMode::Strategy:
        // Free-roaming camera; combat disabled. SelectionSystem would go here when added.
        _combatEnabled = false;
        _damageEnabled = false;
        break;

    case GameMode::PlanetSide:
        // Space movement disabled — GravitySystem (Phase 10+) replaces it.
        _movementEnabled = false;
        break;

    case GameMode::EngineeringBay:
        // Player is docked — suspend physics and combat, surface the engineer UI.
        _movementEnabled     = false;
        _combatEnabled       = false;
        _damageEnabled       = false;
        _engineerMenuEnabled = true;
        break;
    }
}

void GameModeManager::Update(std::vector<Entity>&       entities,
                              std::vector<Projectile>&   projectiles,
                              float                      dt,
                              const std::vector<NetworkSnapshot>& snapshots)
{
    if (_inputEnabled)    PlayerInputSystem::Update(entities, projectiles, dt);
    if (_aiEnabled)       AISystem::Update(entities, dt);
    if (_movementEnabled) MovementSystem::Update(entities, dt);
    if (_combatEnabled)   CombatSystem::Update(entities, projectiles, dt);
    if (_damageEnabled)   DamageSystem::Update(entities, projectiles, dt);
    if (_networkEnabled)  NetworkSyncSystem::Update(entities, snapshots);
}

void GameModeManager::Draw(const std::vector<Entity>& entities) {
    if (_renderEnabled) RenderSystem::Draw(entities);
}

} // namespace ecs

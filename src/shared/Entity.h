#pragma once
#include <cstdint>
#include "entities/TransformComponent.h"
#include "entities/SpriteComponent.h"
#include "entities/HealthComponent.h"
#include "entities/LoadoutComponent.h"
#include "entities/InventoryComponent.h"
#include "entities/AIControllerComponent.h"
#include "entities/NetworkComponent.h"
#include "entities/DockingPortComponent.h"

namespace ecs {

// Flat entity container — all ECS components in one struct.
// Systems check component values (texture != nullptr, networkId != 0, etc.)
// to determine which components are active on a given entity.
struct Entity {
    uint32_t id = 0;   // 0 = null/unspawned

    TransformComponent    transform;
    SpriteComponent       sprite;
    HealthComponent       health;
    LoadoutComponent      loadout;
    InventoryComponent    inventory;
    AIControllerComponent aiController;
    NetworkComponent      network;
    DockingPortComponent  dockingPort;
};

} // namespace ecs

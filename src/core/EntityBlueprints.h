#pragma once
#include "shared/entities/AttributeSet.h"
#include "core/ShipDef.h"
#include "raylib.h"
#include <string>
#include <vector>

namespace ecs {

struct LoadoutEntry {
    std::string slotType;  // "weapon", "armor", "shield", "engine", "auxiliary"
    std::string moduleId;
};

// Blueprint for a spawnable ship — consumed by ecs::ShipRegistry and FleetManager.
struct ShipDef {
    std::string  id;
    std::string  displayName;
    std::string  paletteId;
    AttributeSet baseStats;           // hull, shield, thrust, damageBonus

    // Visual pipeline: use assetPath OR designArray, not both.
    std::string                           assetPath;
    std::vector<std::vector<std::string>> designArray;

    int       weaponSlots    = 2;
    int       armorSlots     = 1;
    int       shieldSlots    = 1;
    int       engineSlots    = 1;
    int       hyperdriveSlots= 1;
    int       auxSlots       = 0;

    float     radius         = 18.0f;
    float     pixelScale     = 1.0f;
    float     turnSpeed      = 0.0f;
    float     projSpeed      = 0.0f;
    float     projRange      = 0.0f;
    float     fireRate       = 0.0f;
    bool      flipSprite     = false;
    ShipType  shipType       = ShipType::Fighter;
    std::vector<LoadoutEntry> loadout;
};

// A single mounting point on a station, with a screen-space offset for composite rendering.
struct StationHardpointDef {
    std::string name;
    std::string slotType;             // "weapon", "shield", "engine", etc.
    Vector2     offset    = { 0.0f, 0.0f };
    int         slotCount = 1;
    std::vector<std::string> preloadedModules;
};

// Blueprint for a spawnable station — consumed by ecs::ShipRegistry and FleetManager.
struct StationDef {
    std::string  id;
    std::string  displayName;
    AttributeSet baseStats;

    std::string                        assetPath;
    std::vector<std::vector<std::string>> designArray;

    std::vector<StationHardpointDef> hardpoints;
};

} // namespace ecs

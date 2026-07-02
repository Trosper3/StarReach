#pragma once
#include "core/Module.h"
#include "raylib.h"
#include <optional>
#include <string>
#include <vector>
#include "shared/entities/HealthComponent.h"

struct HardpointState {
    std::string id;
    std::string displayName;
    bool        isCore       = false;
    bool        isDockingBay = false; // future capture/boarding target; no combat slots
    float       hull     = 100.0f;
    float       maxHull  = 100.0f;
    bool        alive    = true;
    int         wSlots   = 0;
    int         arSlots  = 0;
    int         shSlots  = 0;
    int         enSlots  = 0;
    int         auxSlots = 0;
    float       fireCooldown = 0.0f;

    std::vector<std::optional<ModuleDef>> weapons;
    std::optional<ModuleDef>              armor;
    std::vector<std::optional<ModuleDef>> shields;
    std::optional<ModuleDef>              engine;
    std::vector<std::optional<ModuleDef>> aux;
};

struct PlayerStation {
    unsigned int id = 0;
    std::string  stationDefId;
    std::string  displayName;
    Vector2      position = {};
    bool         alive = true;
    std::vector<HardpointState> hardpoints;

    HealthComponent health;

    // Add a wrapper if you have legacy code relying on these names
    float GetHull() const { return health.currentHull; }
};

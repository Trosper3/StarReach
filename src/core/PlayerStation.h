#pragma once
#include "core/Module.h"
#include "core/StorageItem.h"
#include "core/StationEconomy.h"
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
    Vector2     localOffset = { 0.0f, 0.0f }; // ship/station-local, unrotated; capital ships use this for turret placement
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

    // Onboard cargo hold (currently only populated for mining stations).
    std::vector<StorageItem> storage;
    float                    miningTimer = 0.0f;   // seconds until next auto-collected material

    // Epic 3: per-station supply/demand (tasks_spaceflight_dynamics.md #3).
    // Player stations get their own live stock too — a Trader NPC route can
    // terminate at one just as easily as at an NPC-owned SpaceStation.
    StationEconomy economy;

    HealthComponent health;

    // Add a wrapper if you have legacy code relying on these names
    float GetHull() const { return health.currentHull; }
};

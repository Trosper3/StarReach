#pragma once
#include "core/Module.h"
#include "core/StorageItem.h"
#include "core/StationEconomy.h"
#include "raylib.h"
#include <optional>
#include <string>
#include <vector>
#include "shared/entities/HealthComponent.h"
#include "shared/entities/Hardpoint.h"

struct PlayerStation {
    unsigned int id = 0;
    std::string  stationDefId;
    std::string  displayName;
    Vector2      position = {};
    bool         alive = true;
    std::vector<Hardpoint> hardpoints;

    // Onboard cargo hold (currently only populated for mining stations).
    std::vector<StorageItem> storage;
    float                    miningTimer = 0.0f;   // seconds until next auto-collected material

    // Epic 3: per-station supply/demand (tasks_spaceflight_dynamics.md #3).
    // Player stations get their own live stock too — a Trader NPC route can
    // terminate at one just as easily as at an NPC-owned SpaceStation.
    StationEconomy economy;

    HealthComponent health;

    // P7-T1: last-tick power budget, recomputed every UpdatePlayerStations
    // call (SpaceFlight.cpp) via RecalculatePowerBudget — stored here (rather
    // than only living as that call's discarded return value) so
    // StationModuleMenu can read load/capacity/throttle/shed-count for its
    // power bar without recomputing.
    PowerBudget powerBudget;

    // Add a wrapper if you have legacy code relying on these names
    float GetHull() const { return health.currentHull; }
};

#pragma once
#include <string>
#include <vector>

struct BuildIngredient {
    std::string itemId;
    int         amount;
};

struct StationHardpointDef {
    std::string id;
    std::string displayName;
    bool        isCore   = false;
    float       maxHull  = 100.0f;
    int         wSlots   = 0;
    int         arSlots  = 0;
    int         shSlots  = 0;
    int         enSlots  = 0;
    int         auxSlots = 0;
    int         fSlots   = 0; // P4: facility-typed slots (see Hardpoint::Facility())
    std::vector<std::string> preloadedModules;
};

struct PlayerStationDef {
    std::string id;
    std::string displayName;
    std::string description;
    float       radius = 120.0f;
    std::vector<StationHardpointDef> hardpoints;
    std::vector<BuildIngredient>     buildCost;
    // Ceiling on hardpoints.size() after player-crafted hardpoints are attached
    // post-build. Defaults to the built-in count (no attach room) if the JSON
    // config omits it, so it can never be locked below what the def already has.
    int         maxHardpoints = 0;
};

enum class BuildableType { Station, Module, Hardpoint };

struct BuildableDef {
    std::string   id;
    std::string   displayName;
    std::string   description;
    BuildableType type       = BuildableType::Station;
    std::vector<BuildIngredient> itemCost;
    std::string   stationDefId;
    std::string   moduleDefId;
    // Populated when type == Hardpoint: the blueprint attached to a station on craft.
    StationHardpointDef hardpointDef;
};

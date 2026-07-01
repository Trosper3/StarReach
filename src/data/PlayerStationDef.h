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
    std::vector<std::string> preloadedModules;
};

struct PlayerStationDef {
    std::string id;
    std::string displayName;
    std::string description;
    float       radius = 120.0f;
    std::vector<StationHardpointDef> hardpoints;
    std::vector<BuildIngredient>     buildCost;
};

enum class BuildableType { Station, Module };

struct BuildableDef {
    std::string   id;
    std::string   displayName;
    std::string   description;
    BuildableType type       = BuildableType::Station;
    std::vector<BuildIngredient> itemCost;
    std::string   stationDefId;
    std::string   moduleDefId;
};

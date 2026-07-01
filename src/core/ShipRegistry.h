#pragma once
#include "EntityBlueprints.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecs {

// ECS-layer blueprint registry. Single source of truth for ship and station blueprints.
class ShipRegistry {
public:
    static void                              Init(const char* jsonPath);
    static const std::vector<ShipDef>&       AllShips();
    static const ShipDef*                    ShipById(const std::string& id);

    static const std::vector<StationDef>&    AllStations();
    static const StationDef*                 StationById(const std::string& id);

private:
    static std::vector<ShipDef>                    s_ships;
    static std::unordered_map<std::string, size_t> s_shipIndex;
    static std::vector<StationDef>                 s_stations;
    static std::unordered_map<std::string, size_t> s_stationIndex;
};

} // namespace ecs

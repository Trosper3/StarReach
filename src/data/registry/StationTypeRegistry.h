#pragma once
#include "data/PlayerStationDef.h"
#include <string>
#include <unordered_map>
#include <vector>

struct StationTypeDef {
    std::string id;
    std::string displayName;
    float       radius      = 90.0f;
    bool        hasTrading  = false;
    bool        hasShipyard = false;
    bool        hasRepair   = false;
    std::vector<StationHardpointDef> hardpoints;
};

class StationTypeRegistry {
public:
    static void                                 Init();
    static const std::vector<StationTypeDef>&   All();
    static const StationTypeDef*                ById(const std::string& id);

private:
    static std::vector<StationTypeDef>             s_all;
    static std::unordered_map<std::string, size_t> s_byId;
};

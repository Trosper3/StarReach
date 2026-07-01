#pragma once
#include "data/PlayerStationDef.h"
#include <string>
#include <unordered_map>
#include <vector>

class PlayerStationRegistry {
public:
    static void                                 Init();
    static const std::vector<PlayerStationDef>& All();
    static const PlayerStationDef*              ById(const std::string& id);

private:
    static std::vector<PlayerStationDef>             s_all;
    static std::unordered_map<std::string, size_t>  s_byId;
};

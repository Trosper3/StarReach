#pragma once
#include "data/PlayerStationDef.h"
#include <string>
#include <unordered_map>
#include <vector>

class BuildableRegistry {
public:
    static void                              Init();
    static const std::vector<BuildableDef>&  All();
    static std::vector<BuildableDef>         ByType(BuildableType t);
    static const BuildableDef*               ById(const std::string& id);

private:
    static std::vector<BuildableDef>                 s_all;
    static std::unordered_map<std::string, size_t>  s_byId;
};

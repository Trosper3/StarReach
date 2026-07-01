#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>
#include <vector>

struct PlanetTypeDef {
    std::string id;
    std::string displayName;
    float       radius          = 180.0f;
    Color       atmosphereColor = { 80, 120, 210, 18 };
};

class PlanetTypeRegistry {
public:
    static void                                Init();
    static const std::vector<PlanetTypeDef>&   All();
    static const PlanetTypeDef*                ById(const std::string& id);

private:
    static std::vector<PlanetTypeDef>              s_all;
    static std::unordered_map<std::string, size_t> s_byId;
};

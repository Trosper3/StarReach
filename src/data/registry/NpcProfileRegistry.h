#pragma once
#include "core/Module.h"
#include <string>
#include <unordered_map>
#include <vector>

struct NpcProfileDef {
    std::string  id;
    std::string  displayName;
    std::string  shipTypeId;     // references ShipRegistry
    std::string  factionId;      // "hostile" | "neutral" | "friendly"
    float        aggroRange  = 480.0f;
    float        attackRange = 300.0f;
    ModuleGrade  minGrade    = ModuleGrade::Common;
    ModuleGrade  maxGrade    = ModuleGrade::Uncommon;
};

class NpcProfileRegistry {
public:
    static void                                 Init();
    static const std::vector<NpcProfileDef>&    All();
    static const NpcProfileDef*                 ById(const std::string& id);
    static std::vector<NpcProfileDef>           ByFaction(const std::string& factionId);

private:
    static std::vector<NpcProfileDef>              s_all;
    static std::unordered_map<std::string, size_t> s_byId;
};

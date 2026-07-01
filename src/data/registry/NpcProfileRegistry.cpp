#include "data/registry/NpcProfileRegistry.h"
#include "data/JsonLoader.h"

std::vector<NpcProfileDef>              NpcProfileRegistry::s_all;
std::unordered_map<std::string, size_t> NpcProfileRegistry::s_byId;

static ModuleGrade ParseGrade(const std::string& s) {
    if (s == "Uncommon")   return ModuleGrade::Uncommon;
    if (s == "Unique")     return ModuleGrade::Unique;
    if (s == "Remarkable") return ModuleGrade::Remarkable;
    if (s == "Epic")       return ModuleGrade::Epic;
    if (s == "Legendary")  return ModuleGrade::Legendary;
    if (s == "Mythic")     return ModuleGrade::Mythic;
    return ModuleGrade::Common;
}

void NpcProfileRegistry::Init() {
    auto j = JL::LoadFile("config/npc_profiles.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            NpcProfileDef d;
            d.id          = JL::Str(item, "id");
            d.displayName = JL::Str(item, "displayName");
            d.shipTypeId  = JL::Str(item, "shipTypeId");
            d.factionId   = JL::Str(item, "factionId", "neutral");
            d.aggroRange  = JL::Float(item, "aggroRange",  480.f, 0.f, 5000.f);
            d.attackRange = JL::Float(item, "attackRange", 300.f, 0.f, 5000.f);
            d.minGrade    = ParseGrade(JL::Str(item, "minGrade", "Common"));
            d.maxGrade    = ParseGrade(JL::Str(item, "maxGrade", "Uncommon"));
            if (d.id.empty()) continue;
            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "NpcProfileRegistry: loaded %d profiles from config/npc_profiles.json", (int)s_all.size());
    } else {
        s_all = {
            { "gargos_pirate",  "Gargos Pirate",  "gargos", "hostile",
              480.0f, 300.0f, ModuleGrade::Common,   ModuleGrade::Uncommon },
            { "gargos_patrol",  "Gargos Patrol",  "gargos", "neutral",
              360.0f, 280.0f, ModuleGrade::Common,   ModuleGrade::Common   },
            { "gargos_veteran", "Gargos Veteran", "gargos", "hostile",
              560.0f, 340.0f, ModuleGrade::Uncommon, ModuleGrade::Unique   },
        };
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<NpcProfileDef>& NpcProfileRegistry::All() { return s_all; }

const NpcProfileDef* NpcProfileRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

std::vector<NpcProfileDef> NpcProfileRegistry::ByFaction(const std::string& factionId) {
    std::vector<NpcProfileDef> out;
    for (const NpcProfileDef& p : s_all)
        if (p.factionId == factionId) out.push_back(p);
    return out;
}

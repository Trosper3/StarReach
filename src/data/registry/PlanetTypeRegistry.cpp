#include "data/registry/PlanetTypeRegistry.h"
#include "data/JsonLoader.h"

std::vector<PlanetTypeDef>              PlanetTypeRegistry::s_all;
std::unordered_map<std::string, size_t> PlanetTypeRegistry::s_byId;

void PlanetTypeRegistry::Init() {
    auto j = JL::LoadFile("config/planet_types.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            PlanetTypeDef d;
            d.id              = JL::Str  (item, "id");
            d.displayName     = JL::Str  (item, "displayName");
            d.radius          = JL::Float(item, "radius", 180.f, 10.f, 2000.f);
            d.atmosphereColor = JL::Clr  (item, "atmosphereColor", { 80, 120, 210, 18 });
            if (d.id.empty()) continue;
            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "PlanetTypeRegistry: loaded %d types from config/planet_types.json", (int)s_all.size());
    } else {
        s_all = {
            { "terrestrial", "Terrestrial", 180.0f, {  80, 120, 210, 18 } },
            { "barren",      "Barren",      180.0f, {  90,  80,  70, 14 } },
            { "gas_giant",   "Gas Giant",   240.0f, { 200, 140,  60, 20 } },
            { "ice",         "Ice World",   180.0f, { 140, 200, 240, 16 } },
        };
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<PlanetTypeDef>& PlanetTypeRegistry::All() { return s_all; }

const PlanetTypeDef* PlanetTypeRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

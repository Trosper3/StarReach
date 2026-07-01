#include "data/registry/FactionRegistry.h"
#include "data/JsonLoader.h"

std::vector<FactionDef>                 FactionRegistry::s_all;
std::unordered_map<std::string, size_t> FactionRegistry::s_byId;

void FactionRegistry::Init() {
    auto j = JL::LoadFile("config/factions.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            FactionDef d;
            d.id              = JL::Str(item, "id");
            d.displayName     = JL::Str(item, "displayName");
            d.defaultRelation = JL::Str(item, "defaultRelation", "neutral");
            d.loreText        = JL::Str(item, "loreText");
            if (item.contains("ranks") && item["ranks"].is_array())
                for (const auto& r : item["ranks"])
                    if (r.is_string()) d.ranks.push_back(r.get<std::string>());
            if (d.id.empty()) continue;
            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "FactionRegistry: loaded %d factions from config/factions.json", (int)s_all.size());
    } else {
        s_all = {
            { "hostile",  "Hostile",  "hostile"  },
            { "neutral",  "Neutral",  "neutral"  },
            { "friendly", "Friendly", "friendly" },
        };
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<FactionDef>& FactionRegistry::All() { return s_all; }

const FactionDef* FactionRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

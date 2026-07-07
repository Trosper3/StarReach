#include "data/registry/MaterialRegistry.h"
#include "data/JsonLoader.h"

std::vector<MatDef>                     MaterialRegistry::s_all;
std::unordered_map<std::string, size_t> MaterialRegistry::s_byId;

void MaterialRegistry::Init() {
    auto j = JL::LoadFile("config/materials.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            MatDef d;
            // MatDef uses const char* — store in a persistent string map via the registry vector
            // We hold copies via s_strings so pointers remain stable after Init().
            static std::vector<std::string> s_strings;
            s_strings.push_back(JL::Str(item, "id"));
            s_strings.push_back(JL::Str(item, "displayName"));
            d.id          = s_strings[s_strings.size() - 2].c_str();
            d.displayName = s_strings[s_strings.size() - 1].c_str();
            d.hudColor    = JL::Clr(item, "color", { 180, 180, 180, 255 });
            d.sellValue   = JL::Int(item, "sellValue", 5, 0, 100000);
            if (d.id[0] == '\0') continue;
            s_all.push_back(d);
        }
        TraceLog(LOG_INFO, "MaterialRegistry: loaded %d materials from config/materials.json", (int)s_all.size());
    } else {
        int count = 0;
        const MatDef* raw = AllMaterials(&count);
        s_all.assign(raw, raw + count);
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<MatDef>& MaterialRegistry::All() { return s_all; }

const MatDef* MaterialRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

#include "data/registry/ItemRegistry.h"
#include "data/JsonLoader.h"
#include "data/items/BasicItemDefs.h"

std::vector<ItemDef>                     ItemRegistry::s_all;
std::unordered_map<std::string, size_t>  ItemRegistry::s_byId;

void ItemRegistry::Init() {
    auto j = JL::LoadFile("config/items.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            ItemDef d;
            d.id          = JL::Str(item, "id");
            d.displayName = JL::Str(item, "displayName");
            d.description = JL::Str(item, "description");
            if (d.id.empty()) continue;

            if (item.contains("craftCost") && item["craftCost"].is_array()) {
                for (const auto& c : item["craftCost"]) {
                    if (!c.is_object()) continue;
                    CraftIngredient ci;
                    ci.materialId = JL::Str(c, "materialId");
                    ci.amount     = JL::Int(c, "amount", 1, 1, 9999);
                    if (!ci.materialId.empty())
                        d.craftCost.push_back(std::move(ci));
                }
            }
            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "ItemRegistry: loaded %d items from config/items.json", (int)s_all.size());
    } else {
        s_all = AllBasicItems();
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<ItemDef>& ItemRegistry::All() { return s_all; }

const ItemDef* ItemRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

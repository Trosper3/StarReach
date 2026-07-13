#include "data/registry/PlayerStationRegistry.h"
#include "data/JsonLoader.h"

std::vector<PlayerStationDef>                   PlayerStationRegistry::s_all;
std::unordered_map<std::string, size_t>         PlayerStationRegistry::s_byId;

static StationHardpointDef ParseHardpoint(const nlohmann::json& h) {
    StationHardpointDef d;
    d.id          = JL::Str  (h, "id");
    d.displayName = JL::Str  (h, "displayName");
    d.isCore      = JL::Bool (h, "isCore",   false);
    d.maxHull     = JL::Float(h, "maxHull",  100.f, 1.f, 10000.f);
    d.wSlots      = JL::Int  (h, "wSlots",   0, 0, 20);
    d.arSlots     = JL::Int  (h, "arSlots",  0, 0, 20);
    d.shSlots     = JL::Int  (h, "shSlots",  0, 0, 20);
    d.enSlots     = JL::Int  (h, "enSlots",  0, 0, 20);
    d.auxSlots    = JL::Int  (h, "auxSlots", 0, 0, 20);
    d.fSlots      = JL::Int  (h, "fSlots",   0, 0, 20);
    if (h.contains("modules") && h["modules"].is_array())
        for (const auto& m : h["modules"])
            if (m.is_string()) d.preloadedModules.push_back(m.get<std::string>());
    return d;
}

void PlayerStationRegistry::Init() {
    auto j = JL::LoadFile("config/station_defs.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            PlayerStationDef d;
            d.id          = JL::Str  (item, "id");
            d.displayName = JL::Str  (item, "displayName");
            d.description = JL::Str  (item, "description");
            d.radius      = JL::Float(item, "radius", 120.f, 10.f, 2000.f);
            if (d.id.empty()) continue;

            if (item.contains("hardpoints") && item["hardpoints"].is_array())
                for (const auto& h : item["hardpoints"])
                    if (h.is_object()) d.hardpoints.push_back(ParseHardpoint(h));

            d.maxHardpoints = JL::Int(item, "maxHardpoints", (int)d.hardpoints.size(),
                                       (int)d.hardpoints.size(), 20);

            if (item.contains("buildCost") && item["buildCost"].is_array()) {
                for (const auto& c : item["buildCost"]) {
                    if (!c.is_object()) continue;
                    BuildIngredient bi;
                    bi.itemId  = JL::Str(c, "itemId");
                    bi.amount  = JL::Int(c, "amount", 1, 1, 9999);
                    if (!bi.itemId.empty()) d.buildCost.push_back(std::move(bi));
                }
            }
            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "PlayerStationRegistry: loaded %d defs from config/station_defs.json", (int)s_all.size());
    } else {
        s_all.clear();
        {
            PlayerStationDef d;
            d.id = "mining_station"; d.displayName = "Mining Station";
            d.description = "Automated ore extraction platform with defensive capability.";
            d.radius = 140.0f;
            d.hardpoints = {
                { "mining_drill", "Mining Drill",    false, 120.0f, 0, 1, 0, 0, 1 },
                { "def_battery",  "Defense Battery", false,  80.0f, 1, 1, 0, 0, 0 },
                { "station_core", "Station Core",    true,  200.0f, 0, 1, 0, 0, 0 },
            };
            d.buildCost = { {"hull_frame",8}, {"circuit_board",4}, {"titanium_alloy",3} };
            d.maxHardpoints = 4;
            s_all.push_back(d);
        }
        {
            PlayerStationDef d;
            d.id = "defense_platform"; d.displayName = "Defense Platform";
            d.description = "Heavily armed autonomous defense structure.";
            d.radius = 100.0f;
            d.hardpoints = {
                { "main_battery", "Main Battery",  false, 100.0f, 2, 1, 0, 0, 0 },
                { "shield_array", "Shield Array",  false,  80.0f, 0, 1, 2, 0, 0 },
                { "station_core", "Station Core",  true,  150.0f, 0, 1, 0, 0, 0 },
            };
            d.buildCost = { {"hull_frame",6}, {"weapons_rack",3}, {"circuit_board",3} };
            d.maxHardpoints = 5;
            s_all.push_back(d);
        }
        {
            PlayerStationDef d;
            d.id = "space_station"; d.displayName = "Space Station";
            d.description = "Multi-purpose orbital facility with docking and trade capabilities.";
            d.radius = 200.0f;
            d.hardpoints = {
                { "docking_bay",  "Docking Bay",     false, 140.0f, 0, 1, 0, 0, 2 },
                { "trade_hub",    "Trade Hub",        false, 120.0f, 0, 1, 0, 0, 2 },
                { "def_battery",  "Defense Battery",  false, 100.0f, 2, 1, 0, 0, 0 },
                { "station_core", "Station Core",     true,  250.0f, 0, 1, 0, 0, 0 },
            };
            d.buildCost = { {"hull_frame",15}, {"circuit_board",8}, {"power_cell",5} };
            d.maxHardpoints = 6;
            s_all.push_back(d);
        }
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<PlayerStationDef>& PlayerStationRegistry::All() { return s_all; }

const PlayerStationDef* PlayerStationRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

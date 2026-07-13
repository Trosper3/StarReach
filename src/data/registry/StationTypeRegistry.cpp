#include "data/registry/StationTypeRegistry.h"
#include "data/JsonLoader.h"

std::vector<StationTypeDef>             StationTypeRegistry::s_all;
std::unordered_map<std::string, size_t> StationTypeRegistry::s_byId;

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

void StationTypeRegistry::Init() {
    auto j = JL::LoadFile("config/station_types.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            StationTypeDef d;
            d.id          = JL::Str  (item, "id");
            d.displayName = JL::Str  (item, "displayName");
            d.radius      = JL::Float(item, "radius", 90.f, 10.f, 2000.f);
            d.hasTrading  = JL::Bool (item, "hasTrading",  false);
            d.hasShipyard = JL::Bool (item, "hasShipyard", false);
            d.hasRepair   = JL::Bool (item, "hasRepair",   false);
            if (d.id.empty()) continue;

            if (item.contains("hardpoints") && item["hardpoints"].is_array())
                for (const auto& h : item["hardpoints"])
                    if (h.is_object()) d.hardpoints.push_back(ParseHardpoint(h));

            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "StationTypeRegistry: loaded %d types from config/station_types.json", (int)s_all.size());
    } else {
        s_all.clear();
        {
            StationTypeDef d;
            d.id = "trading_post"; d.displayName = "Trading Post";
            d.radius = 90.0f; d.hasTrading = true; d.hasRepair = true;
            d.hardpoints = {
                { "def_battery",  "Defense Battery", false, 80.0f,  1, 1, 0, 0, 0 },
                { "station_core", "Station Core",    true,  200.0f, 0, 1, 0, 0, 0 },
            };
            s_all.push_back(d);
        }
        {
            StationTypeDef d;
            d.id = "shipyard"; d.displayName = "Shipyard";
            d.radius = 90.0f; d.hasTrading = true; d.hasShipyard = true; d.hasRepair = true;
            d.hardpoints = {
                { "def_battery_a", "Defense Battery A", false, 80.0f,  1, 1, 0, 0, 0 },
                { "def_battery_b", "Defense Battery B", false, 80.0f,  1, 1, 0, 0, 0 },
                { "station_core",  "Station Core",      true,  250.0f, 0, 1, 0, 0, 0 },
            };
            s_all.push_back(d);
        }
        {
            StationTypeDef d;
            d.id = "military_outpost"; d.displayName = "Military Outpost";
            d.radius = 90.0f; d.hasRepair = true;
            d.hardpoints = {
                { "weapon_bay_a", "Weapon Bay A", false, 100.0f, 2, 1, 0, 0, 0 },
                { "weapon_bay_b", "Weapon Bay B", false, 100.0f, 2, 1, 0, 0, 0 },
                { "station_core", "Station Core", true,  300.0f, 0, 1, 0, 0, 0 },
            };
            s_all.push_back(d);
        }
        {
            StationTypeDef d;
            d.id = "waystation"; d.displayName = "Waystation";
            d.radius = 90.0f; d.hasTrading = true;
            d.hardpoints = {
                { "def_battery",  "Defense Battery", false, 80.0f,  1, 1, 0, 0, 0 },
                { "station_core", "Station Core",    true,  180.0f, 0, 1, 0, 0, 0 },
            };
            s_all.push_back(d);
        }
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<StationTypeDef>& StationTypeRegistry::All() { return s_all; }

const StationTypeDef* StationTypeRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

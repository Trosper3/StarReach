#include "data/registry/BuildableRegistry.h"
#include "data/JsonLoader.h"

std::vector<BuildableDef>                    BuildableRegistry::s_all;
std::unordered_map<std::string, size_t>      BuildableRegistry::s_byId;

// Mirrors PlayerStationRegistry.cpp's ParseHardpoint — same StationHardpointDef
// shape, kept as a separate small static helper rather than shared, consistent
// with this codebase's existing per-file duplication of small parse helpers.
static StationHardpointDef ParseHardpointBlueprint(const nlohmann::json& h) {
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
    return d;
}

void BuildableRegistry::Init() {
    auto j = JL::LoadFile("config/buildables.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            BuildableDef d;
            d.id          = JL::Str(item, "id");
            d.displayName = JL::Str(item, "displayName");
            d.description = JL::Str(item, "description");
            d.stationDefId = JL::Str(item, "stationDefId");
            d.moduleDefId  = JL::Str(item, "moduleDefId");
            if (d.id.empty()) continue;

            auto typeStr = JL::Str(item, "type", "Station");
            d.type = (typeStr == "Module")    ? BuildableType::Module
                   : (typeStr == "Hardpoint") ? BuildableType::Hardpoint
                                              : BuildableType::Station;

            if (d.type == BuildableType::Hardpoint && item.contains("hardpoint") && item["hardpoint"].is_object())
                d.hardpointDef = ParseHardpointBlueprint(item["hardpoint"]);

            if (item.contains("itemCost") && item["itemCost"].is_array()) {
                for (const auto& c : item["itemCost"]) {
                    if (!c.is_object()) continue;
                    BuildIngredient bi;
                    bi.itemId  = JL::Str(c, "itemId");
                    bi.amount  = JL::Int(c, "amount", 1, 1, 9999);
                    if (!bi.itemId.empty()) d.itemCost.push_back(std::move(bi));
                }
            }
            s_all.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "BuildableRegistry: loaded %d buildables from config/buildables.json", (int)s_all.size());
    } else {
        s_all.clear();
        {
            BuildableDef d;
            d.id = "build_mining_station"; d.displayName = "Mining Station";
            d.description = "Automated ore extraction platform.";
            d.type = BuildableType::Station; d.stationDefId = "mining_station";
            d.itemCost = { {"hull_frame",8}, {"circuit_board",4}, {"titanium_alloy",3} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "build_defense_platform"; d.displayName = "Defense Platform";
            d.description = "Heavily armed autonomous defense structure.";
            d.type = BuildableType::Station; d.stationDefId = "defense_platform";
            d.itemCost = { {"hull_frame",6}, {"weapons_rack",3}, {"circuit_board",3} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "build_space_station"; d.displayName = "Space Station";
            d.description = "Multi-purpose orbital facility.";
            d.type = BuildableType::Station; d.stationDefId = "space_station";
            d.itemCost = { {"hull_frame",15}, {"circuit_board",8}, {"power_cell",5} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_basic_armor"; d.displayName = "Basic Armor Mk.I";
            d.description = "Reinforced hull plating (+50 hull).";
            d.type = BuildableType::Module; d.moduleDefId = "armor_basic";
            d.itemCost = { {"steel_plate",2} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_basic_engine"; d.displayName = "Basic Engine Mk.I";
            d.description = "Standard sublight drive.";
            d.type = BuildableType::Module; d.moduleDefId = "engine_basic";
            d.itemCost = { {"thruster_core",1}, {"steel_plate",1} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_sensor_aux"; d.displayName = "Sensor Suite";
            d.description = "Extends detection range significantly.";
            d.type = BuildableType::Module; d.moduleDefId = "aux_sensors";
            d.itemCost = { {"sensor_array",1}, {"circuit_board",1} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_proximity_array"; d.displayName = "Proximity Array";
            d.description = "Short-range gravimetric mapper. Reveals the nearest neighboring systems.";
            d.type = BuildableType::Module; d.moduleDefId = "aux_proximity_array";
            d.itemCost = { {"sensor_array",1}, {"circuit_board",1} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_long_range_array"; d.displayName = "Long-Range Array";
            d.description = "Extended gravimetric mapper. Reliably spans the gap to adjacent systems.";
            d.type = BuildableType::Module; d.moduleDefId = "aux_long_range_array";
            d.itemCost = { {"sensor_array",2}, {"circuit_board",1} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_deep_scan_array"; d.displayName = "Deep Scan Array";
            d.description = "Multi-band deep-space telescope. Maps a wide local neighborhood of systems.";
            d.type = BuildableType::Module; d.moduleDefId = "aux_deep_scan_array";
            d.itemCost = { {"sensor_array",2}, {"crystal_lens",2} };
            s_all.push_back(d);
        }
        {
            BuildableDef d;
            d.id = "craft_astrometric_sensor"; d.displayName = "Astrometric Sensor";
            d.description = "Precision astrometric suite. Charts entire stellar neighborhoods at once.";
            d.type = BuildableType::Module; d.moduleDefId = "aux_astrometric_sensor";
            d.itemCost = { {"sensor_array",3}, {"crystal_lens",3}, {"power_cell",2} };
            s_all.push_back(d);
        }
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<BuildableDef>& BuildableRegistry::All() { return s_all; }

std::vector<BuildableDef> BuildableRegistry::ByType(BuildableType t) {
    std::vector<BuildableDef> out;
    for (const auto& d : s_all)
        if (d.type == t) out.push_back(d);
    return out;
}

const BuildableDef* BuildableRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

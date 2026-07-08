#include "core/ShipRegistry.h"
#include <nlohmann/json.hpp>
#include <fstream>

static std::vector<std::vector<std::string>> ParseDesignArray(const nlohmann::json& jarr) {
    std::vector<std::vector<std::string>> grid;
    for (const auto& row : jarr) {
        if (row.is_string()) {
            const std::string& s = row.get_ref<const std::string&>();
            std::vector<std::string> pixels;
            pixels.reserve(s.size());
            for (char c : s) pixels.emplace_back(1, c);
            grid.push_back(std::move(pixels));
        } else if (row.is_array()) {
            grid.push_back(row.get<std::vector<std::string>>());
        }
    }
    return grid;
}

namespace ecs {

std::vector<ShipDef>                    ShipRegistry::s_ships;
std::unordered_map<std::string, size_t> ShipRegistry::s_shipIndex;
std::vector<StationDef>                 ShipRegistry::s_stations;
std::unordered_map<std::string, size_t> ShipRegistry::s_stationIndex;

static std::vector<ecs::StationHardpointDef> ParseHardpoints(const nlohmann::json& jarr) {
    std::vector<ecs::StationHardpointDef> out;
    for (auto& h : jarr) {
        ecs::StationHardpointDef hp;
        hp.name      = h.value("name",      "");
        hp.slotType  = h.value("slotType",  "");
        hp.slotCount = h.value("slotCount", 1);
        if (h.contains("offset") && h["offset"].is_object())
            hp.offset = { h["offset"].value("x", 0.0f),
                          h["offset"].value("y", 0.0f) };
        if (h.contains("modules") && h["modules"].is_array())
            for (const auto& m : h["modules"])
                if (m.is_string()) hp.preloadedModules.push_back(m.get<std::string>());
        out.push_back(std::move(hp));
    }
    return out;
}

static AttributeSet ParseBaseStats(const nlohmann::json& j) {
    AttributeSet s;
    // Support explicit baseStats block or map from legacy flat fields.
    if (j.contains("baseStats") && j["baseStats"].is_object()) {
        const auto& b = j["baseStats"];
        s.hull        = b.value("hull",        0.0f);
        s.shield      = b.value("shield",      0.0f);
        s.thrust      = b.value("thrust",      0.0f);
        s.damageBonus = b.value("damageBonus", 0.0f);
    } else {
        s.hull   = j.value("maxHull", 0.0f);
        s.thrust = j.value("thrust",  0.0f);
    }
    return s;
}

void ShipRegistry::Init(const char* jsonPath) {
    std::ifstream f(jsonPath);
    if (!f.is_open()) return;
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded()) return;

    s_ships.clear();
    s_stations.clear();

    for (auto& e : j) {
        const std::string kind = e.value("kind", "ship");

        if (kind == "station") {
            StationDef d;
            d.id          = e.value("id",          "");
            d.displayName = e.value("displayName",  "Unknown Station");
            d.baseStats   = ParseBaseStats(e);
            d.assetPath   = e.value("assetPath",   "");
            if (e.contains("designArray") && e["designArray"].is_array())
                d.designArray = ParseDesignArray(e["designArray"]);
            if (e.contains("hardpoints") && e["hardpoints"].is_array())
                d.hardpoints = ParseHardpoints(e["hardpoints"]);
            s_stationIndex[d.id] = s_stations.size();
            s_stations.push_back(std::move(d));
        } else {
            ShipDef d;
            d.id          = e.value("id",          "");
            d.displayName = e.value("displayName",  "Unknown Ship");
            d.paletteId   = e.value("paletteId",    "");
            d.baseStats   = ParseBaseStats(e);
            d.assetPath   = e.value("assetPath",   e.value("spritePath", ""));
            d.radius      = e.value("radius",      18.0f);
            d.pixelScale  = e.value("pixelScale",  1.0f);
            d.flipSprite  = e.value("flipSprite",  false);
            d.turnSpeed   = e.value("turnSpeed",   0.0f);
            d.projSpeed   = e.value("projSpeed",   0.0f);
            d.projRange   = e.value("projRange",   0.0f);
            d.fireRate    = e.value("fireRate",    0.0f);
            d.shipType    = (e.value("shipType", "fighter") == "capital")
                            ? ShipType::Capital : ShipType::Fighter;
            if (e.contains("designArray") && e["designArray"].is_array())
                d.designArray = ParseDesignArray(e["designArray"]);
            if (e.contains("slots") && e["slots"].is_object()) {
                const auto& sl = e["slots"];
                d.weaponSlots     = sl.value("weapon",     2);
                d.armorSlots      = sl.value("armor",      1);
                d.shieldSlots     = sl.value("shield",     1);
                d.engineSlots     = sl.value("engine",     1);
                d.hyperdriveSlots = sl.value("hyperdrive", 1);
                d.auxSlots        = sl.value("auxiliary",  0);
            }
            if (e.contains("hardpoints") && e["hardpoints"].is_array())
                d.hardpoints = ParseHardpoints(e["hardpoints"]);
            if (e.contains("loadout") && e["loadout"].is_array()) {
                for (const auto& le : e["loadout"]) {
                    if (!le.is_object()) continue;
                    LoadoutEntry entry;
                    entry.slotType = le.value("slot",     "");
                    entry.moduleId = le.value("moduleId", "");
                    if (!entry.slotType.empty() && !entry.moduleId.empty())
                        d.loadout.push_back(std::move(entry));
                }
            }
            s_shipIndex[d.id] = s_ships.size();
            s_ships.push_back(std::move(d));
        }
    }
}

const std::vector<ShipDef>& ShipRegistry::AllShips() { return s_ships; }

const ShipDef* ShipRegistry::ShipById(const std::string& id) {
    auto it = s_shipIndex.find(id);
    return (it == s_shipIndex.end()) ? nullptr : &s_ships[it->second];
}

const std::vector<StationDef>& ShipRegistry::AllStations() { return s_stations; }

const StationDef* ShipRegistry::StationById(const std::string& id) {
    auto it = s_stationIndex.find(id);
    return (it == s_stationIndex.end()) ? nullptr : &s_stations[it->second];
}

} // namespace ecs

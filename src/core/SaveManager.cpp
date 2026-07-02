#include "SaveManager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <direct.h>
#include <windows.h>

using json = nlohmann::json;

static std::string TimestampKey() {
    time_t now = time(nullptr);
    struct tm t = {};
    localtime_s(&t, &now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &t);
    return buf;
}

static std::string TimestampDisplay() {
    time_t now = time(nullptr);
    struct tm t = {};
    localtime_s(&t, &now);
    char buf[48];
    strftime(buf, sizeof(buf), "%b %d, %Y  %H:%M", &t);
    return buf;
}

// ── Helpers to serialise optional strings (empty = absent slot) ──────────────

static std::string OptStr(const json& j, const char* key) {
    return j.contains(key) ? j[key].get<std::string>() : std::string{};
}

// ── SaveGameToPath ────────────────────────────────────────────────────────────

bool SaveManager::SaveGameToPath(const GameState& gs, const std::string& path,
                                  const std::string& displayName) {
    _mkdir("saves");
    json data;
    data["version"]   = 2;
    data["timestamp"] = displayName.empty() ? TimestampDisplay() : displayName;

    // ── Player ship ──────────────────────────────────────────────────────────
    data["ship"]["typeId"]   = gs.shipTypeId;
    data["ship"]["hull"]     = gs.hull;
    data["ship"]["maxHull"]  = gs.maxHull;
    data["ship"]["x"]        = gs.posX;
    data["ship"]["y"]        = gs.posY;
    data["ship"]["velX"]     = gs.velX;
    data["ship"]["velY"]     = gs.velY;
    data["ship"]["rotation"] = gs.rotation;

    // ── Player loadout ───────────────────────────────────────────────────────
    {
        json lo;
        lo["armor"]       = gs.armorId;
        lo["engine"]      = gs.engineId;
        lo["hyperdrive"]  = gs.hyperdriveId;
        json wa = json::array();
        for (const auto& id : gs.weaponIds) wa.push_back(id);
        lo["weapons"] = wa;
        json sh = json::array();
        for (const auto& id : gs.shieldIds) sh.push_back(id);
        lo["shields"] = sh;
        json ax = json::array();
        for (const auto& id : gs.auxIds) ax.push_back(id);
        lo["aux"] = ax;
        data["loadout"] = lo;
    }

    // ── Discovered stellar IDs (in-system) ───────────────────────────────────
    {
        json arr = json::array();
        for (unsigned int id : gs.discoveredIds) arr.push_back(id);
        data["discovered"] = arr;
    }

    // ── Galactic state ────────────────────────────────────────────────────────
    data["currentSystemId"] = gs.currentSystemId;
    data["gameSeed"]        = gs.gameSeed;
    {
        json arr = json::array();
        for (unsigned int id : gs.discoveredSystemIds) arr.push_back(id);
        data["discoveredSystems"] = arr;
    }

    // ── Storage ──────────────────────────────────────────────────────────────
    {
        json arr = json::array();
        for (const auto& s : gs.storage) {
            json slot;
            slot["type"]        = s.type;
            slot["displayName"] = s.displayName;
            slot["materialId"]  = s.materialId;
            slot["moduleId"]    = s.moduleId;
            slot["count"]       = s.count;
            arr.push_back(slot);
        }
        data["storage"] = arr;
    }

    // ── World ────────────────────────────────────────────────────────────────
    data["hasWorldState"] = gs.hasWorldState;
    data["nextNpcId"]     = gs.nextNpcId;

    // Asteroids
    {
        json arr = json::array();
        for (const auto& a : gs.asteroids) {
            json o;
            o["id"]       = a.id;
            o["x"]        = a.posX;    o["y"]        = a.posY;
            o["vx"]       = a.velX;    o["vy"]       = a.velY;
            o["rot"]      = a.rotation; o["rotSpd"]  = a.rotSpeed;
            o["health"]   = a.health;   o["tier"]    = a.tier;
            o["alive"]    = a.alive;
            json mats = json::array();
            for (const auto& m : a.materials) {
                json mo; mo["id"] = m.materialId; mo["pct"] = m.percent;
                mats.push_back(mo);
            }
            o["materials"] = mats;
            arr.push_back(o);
        }
        data["asteroids"] = arr;
    }

    // NPCs
    {
        json arr = json::array();
        for (const auto& n : gs.npcs) {
            json o;
            o["id"]             = n.id;
            o["x"]              = n.posX;          o["y"]              = n.posY;
            o["vx"]             = n.velX;           o["vy"]             = n.velY;
            o["rot"]            = n.rotation;
            o["hull"]           = n.hull;           o["maxHull"]        = n.maxHull;
            o["radius"]         = n.radius;
            o["alive"]          = n.alive;
            o["faction"]        = n.faction;        o["aiState"]        = n.aiState;
            o["wpX"]            = n.waypointX;      o["wpY"]            = n.waypointY;
            o["wpSet"]          = n.waypointSet;
            o["fireCd"]         = n.fireCooldown;   o["aggroR"]         = n.aggroRange;
            o["attackR"]        = n.attackRange;
            o["kShield"]        = n.kineticShield;  o["eShield"]        = n.energyShield;
            o["chargeTmr"]      = n.npcChargeTimer;
            o["greeted"]        = n.hasGreeted;     o["announced"]      = n.hasAnnounced;
            o["wingman"]        = n.wingman;         o["wingSlot"]       = n.wingmanSlot;
            o["escortTgt"]      = n.escortTargetId;
            o["shipTypeId"]     = n.shipTypeId;
            o["weaponId"]       = n.weaponId;       o["armorId"]        = n.armorId;
            o["shieldId"]       = n.shieldId;       o["engineId"]       = n.engineId;
            arr.push_back(o);
        }
        data["npcs"] = arr;
    }

    // Sun
    data["sunTypeId"] = gs.sunTypeId;
    data["sunRadius"] = gs.sunRadius;

    // Planets
    {
        json arr = json::array();
        for (const auto& p : gs.planets) {
            json o;
            o["x"]           = p.posX;        o["y"]           = p.posY;
            o["r"]           = p.radius;       o["id"]          = p.id;
            o["orbitRadius"] = p.orbitRadius;  o["orbitAngle"]  = p.orbitAngle;
            o["orbitSpeed"]  = p.orbitSpeed;
            arr.push_back(o);
        }
        data["planets"] = arr;
    }

    // Stations
    {
        json arr = json::array();
        for (const auto& s : gs.stations) {
            json o;
            o["x"] = s.posX; o["y"] = s.posY; o["r"] = s.radius; o["id"] = s.id;
            arr.push_back(o);
        }
        data["stations"] = arr;
    }

    // Loot drops
    {
        json arr = json::array();
        for (const auto& l : gs.lootDrops) {
            json o;
            o["x"]         = l.posX;       o["y"]         = l.posY;
            o["lifetime"]  = l.lifetime;    o["pulse"]     = l.pulseTimer;
            o["collected"] = l.collected;   o["moduleId"]  = l.moduleId;
            arr.push_back(o);
        }
        data["loot"] = arr;
    }

    // Material drops
    {
        json arr = json::array();
        for (const auto& m : gs.matDrops) {
            json o;
            o["x"]          = m.posX;       o["y"]          = m.posY;
            o["lifetime"]   = m.lifetime;    o["pulse"]      = m.pulseTimer;
            o["collected"]  = m.collected;   o["materialId"] = m.materialId;
            arr.push_back(o);
        }
        data["matdrops"] = arr;
    }

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << data.dump(2);
    return true;
}

bool SaveManager::SaveGame(const GameState& gs) {
    _mkdir("saves");
    std::string path = std::string("saves/save_") + TimestampKey() + ".json";
    return SaveGameToPath(gs, path);
}

// ── LoadGame ──────────────────────────────────────────────────────────────────

bool SaveManager::LoadGame(const std::string& filename, GameState& out) {
    std::ifstream f(filename);
    if (!f.is_open()) return false;

    json data = json::parse(f, nullptr, false);
    if (data.is_discarded() || !data.contains("ship")) return false;

    // ── Player ship ──────────────────────────────────────────────────────────
    out.shipTypeId = data["ship"].value("typeId",   "starter_ship");
    out.hull       = data["ship"].value("hull",     100.f);
    out.maxHull    = data["ship"].value("maxHull",  100.f);
    out.posX       = data["ship"].value("x",        0.f);
    out.posY       = data["ship"].value("y",        0.f);
    out.velX       = data["ship"].value("velX",     0.f);
    out.velY       = data["ship"].value("velY",     0.f);
    out.rotation   = data["ship"].value("rotation", 0.f);

    // ── Player loadout ───────────────────────────────────────────────────────
    out.weaponIds.clear();
    out.shieldIds.clear();
    out.auxIds.clear();
    out.armorId.clear();
    out.engineId.clear();

    if (data.contains("loadout") && data["loadout"].is_object()) {
        const auto& lo = data["loadout"];
        out.armorId       = lo.value("armor",      std::string{});
        out.engineId      = lo.value("engine",     std::string{});
        out.hyperdriveId  = lo.value("hyperdrive", std::string{});
        if (lo.contains("weapons") && lo["weapons"].is_array())
            for (const auto& w : lo["weapons"]) out.weaponIds.push_back(w.get<std::string>());
        if (lo.contains("shields") && lo["shields"].is_array())
            for (const auto& s : lo["shields"]) out.shieldIds.push_back(s.get<std::string>());
        if (lo.contains("aux") && lo["aux"].is_array())
            for (const auto& a : lo["aux"]) out.auxIds.push_back(a.get<std::string>());
    }

    // ── Discovered stellar IDs (in-system) ───────────────────────────────────
    out.discoveredIds.clear();
    if (data.contains("discovered") && data["discovered"].is_array())
        for (const auto& d : data["discovered"])
            out.discoveredIds.push_back(d.get<unsigned int>());

    // ── Galactic state ────────────────────────────────────────────────────────
    out.currentSystemId = data.value("currentSystemId", 1u);
    out.gameSeed         = data.value("gameSeed", 1u); // old saves: fixed fallback seed
    out.discoveredSystemIds.clear();
    if (data.contains("discoveredSystems") && data["discoveredSystems"].is_array())
        for (const auto& d : data["discoveredSystems"])
            out.discoveredSystemIds.push_back(d.get<unsigned int>());

    // ── Storage ──────────────────────────────────────────────────────────────
    out.storage.clear();
    if (data.contains("storage") && data["storage"].is_array()) {
        for (const auto& s : data["storage"]) {
            StorageSave ss;
            ss.type        = s.value("type",        0);
            ss.displayName = s.value("displayName", std::string{});
            ss.materialId  = s.value("materialId",  std::string{});
            ss.moduleId    = s.value("moduleId",    std::string{});
            ss.count       = s.value("count",       0);
            out.storage.push_back(std::move(ss));
        }
    }

    // ── World state ──────────────────────────────────────────────────────────
    out.hasWorldState = data.value("hasWorldState", false);
    out.nextNpcId     = data.value("nextNpcId", 1000u);

    out.asteroids.clear();
    if (data.contains("asteroids") && data["asteroids"].is_array()) {
        for (const auto& a : data["asteroids"]) {
            AsteroidSave as;
            as.id        = a.value("id",     0u);
            as.posX      = a.value("x",      0.f);  as.posY    = a.value("y",      0.f);
            as.velX      = a.value("vx",     0.f);  as.velY    = a.value("vy",     0.f);
            as.rotation  = a.value("rot",    0.f);  as.rotSpeed= a.value("rotSpd", 0.f);
            as.health    = a.value("health", 0);    as.tier    = a.value("tier",   2);
            as.alive     = a.value("alive",  true);
            if (a.contains("materials") && a["materials"].is_array()) {
                for (const auto& m : a["materials"]) {
                    MaterialEntry me;
                    me.materialId = m.value("id",  std::string{});
                    me.percent    = m.value("pct", 0);
                    as.materials.push_back(std::move(me));
                }
            }
            out.asteroids.push_back(std::move(as));
        }
    }

    out.npcs.clear();
    if (data.contains("npcs") && data["npcs"].is_array()) {
        for (const auto& n : data["npcs"]) {
            NpcSave ns;
            ns.id             = n.value("id",        0u);
            ns.posX           = n.value("x",         0.f);   ns.posY           = n.value("y",         0.f);
            ns.velX           = n.value("vx",        0.f);   ns.velY           = n.value("vy",        0.f);
            ns.rotation       = n.value("rot",       0.f);
            ns.hull           = n.value("hull",      0.f);   ns.maxHull        = n.value("maxHull",   0.f);
            ns.radius         = n.value("radius",    18.f);
            ns.alive          = n.value("alive",     true);
            ns.faction        = n.value("faction",   0);     ns.aiState        = n.value("aiState",   0);
            ns.waypointX      = n.value("wpX",       0.f);   ns.waypointY      = n.value("wpY",       0.f);
            ns.waypointSet    = n.value("wpSet",     false);
            ns.fireCooldown   = n.value("fireCd",    0.f);   ns.aggroRange     = n.value("aggroR",    480.f);
            ns.attackRange    = n.value("attackR",   300.f);
            ns.kineticShield  = n.value("kShield",   0.f);   ns.energyShield   = n.value("eShield",   0.f);
            ns.npcChargeTimer = n.value("chargeTmr", 0.f);
            ns.hasGreeted     = n.value("greeted",   false); ns.hasAnnounced   = n.value("announced", false);
            ns.wingman        = n.value("wingman",   false); ns.wingmanSlot    = n.value("wingSlot",  -1);
            ns.escortTargetId = n.value("escortTgt", 0u);
            ns.shipTypeId     = n.value("shipTypeId",std::string{"ar3_saber"});
            ns.weaponId       = n.value("weaponId",  std::string{});
            ns.armorId        = n.value("armorId",   std::string{});
            ns.shieldId       = n.value("shieldId",  std::string{});
            ns.engineId       = n.value("engineId",  std::string{});
            out.npcs.push_back(std::move(ns));
        }
    }

    out.sunTypeId = data.value("sunTypeId", std::string{});
    out.sunRadius = data.value("sunRadius", 0.f);

    out.planets.clear();
    if (data.contains("planets") && data["planets"].is_array()) {
        for (const auto& p : data["planets"]) {
            PlanetSave ps;
            ps.posX        = p.value("x",           0.f);
            ps.posY        = p.value("y",           0.f);
            ps.radius      = p.value("r",           180.f);
            ps.id          = p.value("id",          0u);
            ps.orbitRadius = p.value("orbitRadius", 0.f);
            ps.orbitAngle  = p.value("orbitAngle",  0.f);
            ps.orbitSpeed  = p.value("orbitSpeed",  0.f);
            out.planets.push_back(ps);
        }
    }

    out.stations.clear();
    if (data.contains("stations") && data["stations"].is_array()) {
        for (const auto& s : data["stations"]) {
            StationSave ss;
            ss.posX   = s.value("x",  0.f); ss.posY   = s.value("y",  0.f);
            ss.radius = s.value("r",  90.f); ss.id    = s.value("id", 0u);
            out.stations.push_back(ss);
        }
    }

    out.lootDrops.clear();
    if (data.contains("loot") && data["loot"].is_array()) {
        for (const auto& l : data["loot"]) {
            LootSave ls;
            ls.posX      = l.value("x",         0.f);
            ls.posY      = l.value("y",         0.f);
            ls.lifetime  = l.value("lifetime",  0.f);
            ls.pulseTimer= l.value("pulse",     0.f);
            ls.collected = l.value("collected", false);
            ls.moduleId  = l.value("moduleId",  std::string{});
            out.lootDrops.push_back(std::move(ls));
        }
    }

    out.matDrops.clear();
    if (data.contains("matdrops") && data["matdrops"].is_array()) {
        for (const auto& m : data["matdrops"]) {
            MatDropSave ms;
            ms.posX       = m.value("x",          0.f);
            ms.posY       = m.value("y",          0.f);
            ms.lifetime   = m.value("lifetime",   0.f);
            ms.pulseTimer = m.value("pulse",      0.f);
            ms.collected  = m.value("collected",  false);
            ms.materialId = m.value("materialId", std::string{});
            out.matDrops.push_back(std::move(ms));
        }
    }

    return true;
}

// ── Pending load ──────────────────────────────────────────────────────────────

bool SaveManager::DeleteSave(const std::string& filename) {
    return DeleteFileA(filename.c_str()) != 0;
}

std::vector<SaveManager::SaveMeta> SaveManager::ListSaves() {
    std::vector<SaveMeta> result;
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA("saves\\*.json", &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return result;
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string cname(ffd.cFileName);
        std::string filename = "saves/" + cname;
        std::string stem     = cname.size() > 5 ? cname.substr(0, cname.size() - 5) : cname;
        SaveMeta meta;
        meta.filename  = filename;
        meta.id        = stem;
        meta.hullPct   = 1.f;
        meta.timestamp = stem;
        std::ifstream f(filename);
        if (f.is_open()) {
            json data = json::parse(f, nullptr, false);
            if (!data.is_discarded()) {
                meta.timestamp = data.value("timestamp", stem);
                if (data.contains("ship")) {
                    float h  = data["ship"].value("hull",    100.f);
                    float mh = data["ship"].value("maxHull", 100.f);
                    meta.hullPct = (mh > 0.f) ? (h / mh) : 1.f;
                }
            }
        }
        result.push_back(std::move(meta));
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
    std::sort(result.begin(), result.end(),
        [](const SaveMeta& a, const SaveMeta& b) { return a.id > b.id; });
    return result;
}

void SaveManager::SetPendingLoad(const std::string& filename) {
    _hasPending = LoadGame(filename, _pending);
}

SaveManager::GameState SaveManager::ConsumePendingLoad() {
    _hasPending = false;
    return _pending;
}

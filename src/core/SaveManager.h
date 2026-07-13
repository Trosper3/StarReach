#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class SaveManager {
public:
    // ── Nested world-entity save structs ─────────────────────────────────────

    struct MaterialEntry {
        std::string materialId;
        int         percent = 0;
    };

    struct AsteroidSave {
        unsigned int id      = 0;
        float posX = 0, posY = 0;
        float velX = 0, velY = 0;
        float rotation = 0, rotSpeed = 0;
        int   health = 0, tier = 2;
        bool  alive = true;
        std::vector<MaterialEntry> materials;
    };

    struct NpcSave {
        unsigned int id           = 0;
        float posX = 0, posY = 0;
        float velX = 0, velY = 0;
        float rotation       = 0;
        float hull           = 0, maxHull = 0, radius = 18;
        float fireCooldown   = 0, aggroRange = 480, attackRange = 300;
        float kineticShield  = 0, energyShield = 0;
        float npcChargeTimer = 0;
        float waypointX = 0, waypointY = 0;
        bool  alive        = true;
        bool  waypointSet  = false;
        bool  hasGreeted   = false;
        bool  hasAnnounced = false;
        bool  wingman      = false;
        int   faction      = 0;   // NpcFaction as int
        int   aiState      = 0;   // NpcAiState as int
        int   wingmanSlot  = -1;
        unsigned int escortTargetId = 0;
        std::string shipTypeId;
        std::string weaponId, armorId, shieldId, engineId;
    };

    struct PlanetSave {
        float        posX = 0, posY = 0, radius = 180;
        unsigned int id   = 0;
        float        orbitRadius = 0.f, orbitAngle = 0.f, orbitSpeed = 0.f;
    };

    struct StationSave {
        float        posX = 0, posY = 0, radius = 90;
        unsigned int id   = 0;
    };

    struct LootSave {
        float       posX = 0, posY = 0;
        float       lifetime = 0, pulseTimer = 0;
        bool        collected = false;
        std::string moduleId;
    };

    struct MatDropSave {
        float       posX = 0, posY = 0;
        float       lifetime = 0, pulseTimer = 0;
        bool        collected = false;
        std::string materialId;
    };

    struct StorageSave {
        int         type = 0;          // StorageItemType as int
        std::string displayName;
        std::string materialId;
        std::string moduleId;
        int         count = 0;
    };

    // Player-built stations (FleetManager::PlayerStations) — distinct from
    // the NPC/world StationSave above. Hardpoint layout is rebuilt from
    // stationDefId on load (mirroring FleetManager::SpawnStation), then
    // overlaid with the per-hardpoint hull/alive/equipped-module state below.
    struct HardpointSlotSave {
        std::string moduleId;   // empty = unequipped; slot order/type comes from the def rebuild
    };

    struct HardpointSave {
        std::string id;
        float       hull = 100.f, maxHull = 100.f;
        bool        alive = true;
        std::vector<HardpointSlotSave> slots;
        // P7-T4: player-tunable shed priority override, -1 = untouched
        // (falls back to the type-based heuristic — see Hardpoint.h). Unlike
        // throttle/shed/adjacency*, this is real player state, not derived,
        // so it needs to round-trip through saves.
        int shedPriority = -1;
    };

    struct PlayerStationSave {
        unsigned int id = 0;
        std::string  stationDefId;
        std::string  displayName;
        float        posX = 0.f, posY = 0.f;
        bool         alive = true;
        std::vector<HardpointSave> hardpoints;
        std::vector<StorageSave>   storage;
        float        miningTimer = 0.f;
        std::unordered_map<std::string, int> economyStock;
    };

    // ── Main game state ───────────────────────────────────────────────────────

    struct GameState {
        // Player ship
        std::string shipTypeId = "starter_ship";
        float hull = 100.f, maxHull = 100.f;
        float posX = 0.f,  posY = 0.f;
        float velX = 0.f,  velY = 0.f;
        float rotation = 0.f;

        // Player loadout (empty = not saved / use defaults)
        std::vector<std::string>  weaponIds;
        std::string               armorId;
        std::string               engineId;
        std::string               hyperdriveId;
        std::vector<std::string>  shieldIds;
        std::vector<std::string>  auxIds;

        // Discovered stellar object IDs (in current system)
        std::vector<unsigned int> discoveredIds;

        // Galactic state
        unsigned int              currentSystemId     = 1;
        std::vector<unsigned int> discoveredSystemIds;
        uint32_t                  gameSeed            = 0; // universe seed (UniverseRegistry)

        // Universe state. Defaults to 0 so saves written before this field
        // existed load as galaxy 1 (home) — see SpaceFlight::OnEnter, which
        // treats 0 the same as 1 when restoring.
        unsigned int              currentGalaxyId     = 0;
        // Every galaxy id ever warped into (see SpaceFlight::_visitedGalaxyIds).
        // Empty on saves written before this field existed — SpaceFlight::OnEnter
        // falls back to just the restored currentGalaxyId in that case.
        std::vector<unsigned int> visitedGalaxyIds;

        // Faction standing (faction id → rank index, 0 = first rank)
        std::unordered_map<std::string, int> factionRankIndices;

        // Storage
        std::vector<StorageSave> storage;

        // Sun data for the current system
        std::string sunTypeId;
        float       sunRadius = 0.f;

        // World entities (hasWorldState=false → spawn fresh on load)
        bool hasWorldState = false;
        std::vector<AsteroidSave>  asteroids;
        std::vector<NpcSave>       npcs;
        std::vector<PlanetSave>    planets;
        std::vector<StationSave>   stations;
        std::vector<PlayerStationSave> playerStations;
        std::vector<LootSave>      lootDrops;
        std::vector<MatDropSave>   matDrops;
        unsigned int               nextNpcId = 1000;
    };

    // ── Save metadata (for the picker list) ──────────────────────────────────

    struct SaveMeta {
        std::string filename;
        std::string id;
        std::string timestamp;
        float       hullPct = 1.f;
    };

    // ── Singleton ─────────────────────────────────────────────────────────────

    static SaveManager& Get() {
        static SaveManager instance;
        return instance;
    }

    bool                  SaveGame(const GameState& state);
    bool                  SaveGameToPath(const GameState& state, const std::string& path,
                                         const std::string& displayName = "");
    bool                  LoadGame(const std::string& filename, GameState& out);
    std::vector<SaveMeta> ListSaves();
    bool                  DeleteSave(const std::string& filename);

    void      Snapshot() {}

    void      SetPendingLoad(const std::string& filename);
    bool      HasPendingLoad() const { return _hasPending; }
    GameState ConsumePendingLoad();

private:
    SaveManager() = default;
    bool      _hasPending = false;
    GameState _pending;
};

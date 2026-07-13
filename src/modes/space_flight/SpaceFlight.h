#pragma once
#include "modes/IGameMode.h"
#include "Projectile.h"
#include "Asteroid.h"
#include "GalaxyMap.h"
#include "StorageMenu.h"
#include "ModulesMenu.h"
#include "BuildMenu.h"
#include "StationModuleMenu.h"
#include "MiningStationMenu.h"
#include "StationServicesMenu.h"
#include "core/Module.h"
#include "core/PlayerStation.h"
#include "core/StationEconomy.h"
#include "core/Contract.h"
#include "core/SaveManager.h"
#include "core/GameModeManager.h"
#include "data/registry/UniverseRegistry.h"
#include "shared/Entity.h"
#include "engine/ResourceManager.h"
#include "engine/LightingSystem.h"
#include "raylib.h"
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <core/ShipDef.h>
#include "core/FactionEnum.h"
#include "net/Protocol.h"

namespace ecs { struct ShipDef; }

// ── Stellar objects ───────────────────────────────────────────────────────────

struct SpacePlanet {
    Vector2      position    = {};
    float        radius      = 180.0f;
    unsigned int id          = 0;
    float        orbitRadius = 0.0f;
    float        orbitAngle  = 0.0f;
    float        orbitSpeed  = 0.0f;
};

struct SpaceSun {
    float       radius      = 300.0f;
    float       gravRange   = 1200.0f;
    float       gravStrength= 350.0f;
    Color       coreColor   = { 255, 240, 120, 255 };
    Color       innerGlow   = { 255, 200,  60, 255 };
    Color       outerGlow   = { 255, 160,  20, 255 };
    std::string typeId      = "G";
    bool        active      = false;
};

struct SpaceStation {
    Vector2      position      = {};
    float        radius        = 90.0f;
    unsigned int id            = 0;
    Faction      faction       = Faction::Merchant;
    float        hull          = 200.0f;
    float        maxHull       = 200.0f;
    bool         alive         = true;
    bool         retaliating       = false;
    float        retaliateTimer    = 0.0f;
    bool         retaliateAtPlayer = false;
    unsigned int retaliateAtNpcId  = 0;
    std::string  stationTypeId;
    std::vector<Hardpoint> hardpoints;
    // Epic 3: per-station supply/demand (tasks_spaceflight_dynamics.md #3).
    StationEconomy economy;
    // Epic 5.3: destroyed stations rebuild after a cooldown instead of
    // staying permanently dead — see SpaceFlight::TickStationRebuilds.
    bool  rebuilding   = false;
    float rebuildTimer = 0.0f;
    // Epic 9.1 (capture): true once every non-core hardpoint is dead but the
    // station_core hardpoint survives — station is unarmed and awaiting an
    // instant-capture approach. See combat::IsDisabled / UpdateCaptureProximity.
    bool disabled = false;
};

// ── NPC AI state ─────────────────────────────────────────────────────────────

enum class NpcFaction : unsigned char { Friendly, Neutral, Hostile };
// Epic 9.2 (fighter capture): Disabled is a temporary ion-stun state (see
// NpcMeta::ionDisableTimer) — no thrust, no fire, capturable via
// UpdateCaptureProximity; reverts to Chase on its own if the timer runs out
// before the player captures it. Fighter-only; capitals/stations use the
// permanent hardpoint-based combat::IsDisabled instead.
enum class NpcAiState : unsigned char { Patrol, Chase, Attack, Flee, Escort, Disabled };

// Officer/vocation archetype rolled once at spawn from a per-faction weighted
// table (see RollNpcRole in SpaceFlight.cpp). Plugs alongside NpcAiState:
// aiState still drives moment-to-moment combat/movement behavior; role is the
// higher-level "job" that will bias which aiState transitions a role prefers
// once each role's distinct behavior lands (see tasks_spaceflight_dynamics.md
// Epic 2.3-2.7). Not yet behavior-differentiated — currently cosmetic/informational.
enum class NpcRole : unsigned char { None, Explorer, Raider, Military, Trader, Industrialist };

// Per-NPC game-specific state not stored in the ECS Entity components.
// Parallel to _entities by index.
struct NpcMeta {
    unsigned int id            = 0;
    float        radius        = 18.0f;
    bool         alive         = true;
    bool         thrusting     = false;
    NpcFaction   faction       = NpcFaction::Friendly;
    NpcAiState   aiState       = NpcAiState::Patrol;
    NpcRole      role          = NpcRole::None;
    Vector2      waypoint      = {};
    bool         waypointSet   = false;
    bool         wingman       = false;
    int          wingmanSlot   = -1;
    unsigned int escortTargetId= 0;
    bool         hasGreeted    = false;
    bool         hasAnnounced  = false;
    float        aggroRange    = 480.0f;
    float        attackRange   = 300.0f;
    float        preferredRange= 220.0f;
    Faction      npcFaction    = Faction::Merchant;
    float        asteroidHitCooldown = 0.0f;
    bool         retaliatingVsPlayer = false;
    unsigned int retaliationTargetId = 0;
    float        fireCooldown  = 0.0f;
    // Epic 9.2 (fighter capture): counts down while aiState==Disabled; the
    // fighter reverts to Chase on its own if this hits 0 before capture.
    // Fighter-only (hardpoint-bearing capitals use the permanent
    // combat::IsDisabled instead and never set this).
    float        ionDisableTimer = 0.0f;
    bool         docked        = false; // parked at a friendly station, healing
    // Epic 3 (economy): Industrialist's production source / Trader's route
    // endpoints. Station ids are only unique within the system that
    // generated them (SpawnPlanetsAndStations resets its id counter per
    // system), so each is paired with the SystemWorld key (SpaceFlight::
    // WorldKey) it lives in — required for Trader, whose destination is
    // necessarily in a different system (a system holds at most one
    // controlled station). 0 = none assigned yet.
    unsigned int homeStationId    = 0;
    uint64_t     homeWorldKey     = 0;
    unsigned int tradeDestId      = 0; // Trader only: the other end of its route
    uint64_t     destWorldKey     = 0;
    bool         haulingToDest    = false; // Trader only: leg direction (home->dest vs dest->home)
    std::string  tradeGoodId;            // Trader only: which good this leg is carrying
    float        economyTickTimer = 0.0f; // Industrialist/Trader production-tick cadence
    // Derived weapon/movement stats (set by ApplyNpcLoadout)
    float        npcThrust     = 0.0f;
    float        npcTurnRate   = 155.0f; // deg/s; overridden much lower for capitals
    float        npcDamage     = 0.0f;
    float        npcFireRate   = 1.4f;
    float        npcProjSpeed  = 700.0f;
    float        npcProjRange  = 1000.0f;
    bool         npcHasWeapon  = false;
    WeaponFireMode npcWeaponMode= WeaponFireMode::Standard;
    WeaponProjType npcProjType = WeaponProjType::Standard;
    float        npcChargeTimer= 0.0f;
    float        npcChargeTime = 0.0f;
    int          npcBurstCount = 1;
    float        npcSpreadAngle= 0.0f;
    // Shield recharge (dual-type not in ECS single-shield model)
    float        kineticRechargeRate = 0.0f;
    float        energyShield   = 0.0f;
    float        maxEnergyShield= 0.0f;
    float        energyRechargeRate = 0.0f;
    // Ship identification
    std::string  shipTypeId  = "ar3_saber";
    std::string  shipTypeName= "AR-3 Saber";
    HardpointRig  loadout;
    std::vector<Hardpoint> hardpoints; // capital ships only; empty for fighters
    // Epic 9.1 (capture): true once every non-core hardpoint is dead but the
    // command_bridge hardpoint survives — capital is unarmed and awaiting an
    // instant-capture approach. See combat::IsDisabled / UpdateCaptureProximity.
    bool disabled = false;
	static constexpr int WSlots = 1, ArSlots = 1, ShSlots = 1, EnSlots = 1, HdSlots = 1, AuxSlots = 1;
};

// Player-specific state not stored in the ECS Entity.
struct PlayerMeta {
    std::string defId;
    std::string displayName;
    float       radius      = 18.0f;
    ShipType    shipType    = ShipType::Fighter;
    bool        thrusting   = false;
    float       thrust      = 0.0f;
    float       turnSpeed   = 0.0f;
    float       projSpeed   = 0.0f;
    float       projRange   = 0.0f;
    float       fireRate    = 0.0f;
    int         weaponSlots = 1;
    int         armorSlots  = 1;
    int         shieldSlots = 0;
    int         engineSlots = 1;
    int         hyperdriveSlots = 1;
    int         auxSlots    = 1;
    bool        canFire     = false;
    bool        canMove     = false;
    bool        hasTurret   = false;
    WeaponFireMode weaponFireMode = WeaponFireMode::Standard;
    float       weaponDamage  = 10.0f;
    float       chargeTime    = 1.0f;
    int         burstCount    = 1;
    float       spreadAngle   = 0.0f;
    float       weaponTurnRate= 0.0f;
    // Epic 9.2 (fighter capture): carried onto each spawned Projectile so
    // UpdateNpcCollisions' Block 1 knows whether this shot can ion-disable
    // a weakened fighter. WeaponEffect::None for every non-ion weapon.
    WeaponEffect weaponEffect         = WeaponEffect::None;
    float        weaponEffectDuration = 0.0f;
    float       kineticRechargeRate = 0.0f;
    float       kineticShield    = 0.0f;
    float       maxKineticShield = 0.0f;
    float       energyShield     = 0.0f;
    float       maxEnergyShield  = 0.0f;
    float       energyRechargeRate = 0.0f;
    float       _fireCooldown = 0.0f;
    float       _chargeTimer  = 0.0f;

    float ChargePct()      const { return (weaponFireMode == WeaponFireMode::Charge && chargeTime > 0.0f) ? std::min(_chargeTimer / chargeTime, 1.0f) : 0.0f; }
    float FireCooldownPct()const { return (_fireCooldown > 0.0f && fireRate > 0.0f) ? _fireCooldown / fireRate : 0.0f; }
};

struct LootDrop {
    Vector2   position   = {};
    float     lifetime   = 28.0f;
    float     pulseTimer = 0.0f;
    bool      collected  = false;
    ModuleDef module;
};

struct MaterialDrop {
    Vector2     position   = {};
    float       lifetime   = 28.0f;
    float       pulseTimer = 0.0f;
    bool        collected  = false;
    std::string materialId;
};

// Epic 11.1: left behind when a capital ship or NPC station is destroyed
// (locked decision — capitals/stations only, not routine fighter kills, to
// keep the event meaningful/rare). Unlike LootDrop/MaterialDrop this never
// expires on a timer — it's a rare, deliberate salvage target, not routine
// battlefield clutter, so it waits for the player rather than despawning.
struct DerelictWreck {
    Vector2     position      = {};
    float       radius        = 45.0f; // visual + pickup size; bigger for a capital's wreck
    bool        isCapital     = false;
    bool        collected     = false;
    int         creditsReward = 0;
    std::string sourceName;           // e.g. "Reaver Dreadnought" — shown on salvage
};

struct CommsEntry {
    std::string text;
    bool        fromPlayer = false;
};

// Epic 11.2: a rare, session-only event asking the player to help (or being
// helped). One active call per SystemWorld at a time (same "single active
// item" precedent as _activeContract). Stranded is issued to the player's own
// world when a warp fails for lack of fuel (Epic 4.3's "stranding condition"
// finally gets a rescue to hook into); ShipUnderAttack is an ambient event
// rolled when a non-hostile-faction NPC flees a fight (see UpdateNpcShips'
// Flee-transition branch) — reward is granted purely for the NPC surviving
// the window, same "ambient threat model" the Escort contract (7.3) already
// established, not a literal "player must land the killing blow" check.
enum class DistressType : unsigned char { Stranded, ShipUnderAttack };
struct DistressCall {
    DistressType type            = DistressType::Stranded;
    Vector2      position        = {};
    unsigned int npcId           = 0;    // ShipUnderAttack only; 0 for Stranded
    Faction      issuerFaction   = Faction::Merchant;
    float        timer           = 0.0f; // counts up from 0
    float        windowSeconds   = 0.0f; // Stranded: rescue ETA; ShipUnderAttack: survive-until
    int          rewardCredits   = 0;
    float        rewardReputation = 0.0f;
    bool         acknowledged    = false; // Epic 13: player hailed the distressed ship
};

// ── Per-system simulation state ──────────────────────────────────────────────
// Everything needed to simulate one star system independently of any other.
// SpaceFlight keeps one SystemWorld per system occupied this session (keyed by
// systemId in _worlds); `_w` points at the world the local player is in — that
// world is the one rendered and driven by the normal Update path. On a host,
// worlds whose only occupants are remote clients are ticked by
// TickBackgroundWorld(); unoccupied worlds are frozen in memory (state kept,
// no simulation) until someone warps back in.
struct SystemWorld {
    unsigned int systemId = 1;
    // Which galaxy systemId was generated in — every galaxy's StarSystemRegistry
    // reuses the same 1..Count() id space, so systemId alone is ambiguous once
    // more than one galaxy exists. See SpaceFlight::_worlds / WorldKey().
    unsigned int galaxyId = 1;
    uint32_t     seed     = 0;      // content seed (planets/stations/asteroids/NPCs)
    float        age      = 0.0f;   // seconds simulated since generation (orbit sync)

    // entities[i] and npcMeta[i] are always parallel — same index = same NPC.
    std::vector<ecs::Entity>  entities;
    std::vector<NpcMeta>      npcMeta;
    std::vector<size_t>       npcFreeSlots;  // indices of dead slots available for reuse
    unsigned int              nextNpcId = 1000;

    std::vector<Projectile>   projectiles;
    std::vector<Asteroid>     asteroids;
    std::vector<SpacePlanet>  planets;
    std::vector<SpaceStation> stations;
    SpaceSun                  sun;

    std::vector<LootDrop>       lootDrops;
    std::vector<MaterialDrop>   materialDrops;
    std::vector<DerelictWreck>  derelictWrecks; // Epic 11.1
    DistressCall                activeDistress;      // Epic 11.2
    bool                        hasActiveDistress = false;

    Vector2 playerSpawnPos = {};    // seed-derived; identical on host and client
    float   respawnTimer   = 0.0f;  // asteroid/NPC repopulation timer
};

// ─────────────────────────────────────────────────────────────────────────────

class SpaceFlight : public IGameMode {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void Draw() override;
    void OnExit() override;

private:
    struct TargetInfo {
        bool             valid             = false;
        bool             isStellar         = false;
        bool             isNpc             = false;
        bool             isWingman         = false;
        NpcFaction       npcFaction        = NpcFaction::Friendly;
        bool             hasFaction        = false;
        Faction          gameFaction       = Faction::Merchant;
        NpcRole          role              = NpcRole::None;
        bool             disabled          = false; // Epic 9.1: unarmed, capturable on approach
        const Texture2D* iconTex           = nullptr;
        Vector2          worldPos          = {};
        std::string      name;
        std::string      typeDesc;
        float            health            = 0.0f;
        float            maxHealth         = 1.0f;
        float            distance          = 0.0f;
        int              tier              = -1;
        float            kineticShield     = 0.0f;
        float            maxKineticShield  = 0.0f;
        float            energyShield      = 0.0f;
        float            maxEnergyShield   = 0.0f;
        std::vector<MaterialChance> materialComps;
    };

    bool        _inShipPlacementMode     = false;
    bool        _shipPlacementConfirmOpen= false;
    std::string _placingShipDefId;
    Vector2     _shipPlacementPos;

    // ── Per-system worlds ────────────────────────────────────────────────────
    // One SystemWorld per system occupied this session, keyed by a composite
    // (galaxyId, systemId) — not bare systemId, since every galaxy's
    // StarSystemRegistry::Init() regenerates ids from 1..Count(), so the same
    // numeric systemId means a different system in each galaxy. Worlds left
    // behind after a cross-galaxy warp stay frozen in the map under their old
    // key (same as how unoccupied systems within one galaxy already freeze),
    // so returning to a previously-visited galaxy resumes it as left.
    // `_w` is never null between OnEnter and OnExit and points at the local
    // player's world.
    std::unordered_map<uint64_t, std::unique_ptr<SystemWorld>> _worlds;
    SystemWorld* _w = nullptr;
    static uint64_t WorldKey(unsigned int galaxyId, unsigned int systemId) {
        return (uint64_t(galaxyId) << 32) | uint64_t(systemId);
    }
    static unsigned int WorldKeySystemId(uint64_t key) { return (unsigned int)(key & 0xFFFFFFFFu); }
    static unsigned int WorldKeyGalaxyId(uint64_t key) { return (unsigned int)(key >> 32); }
    // Both operate on the CURRENT galaxy (_currentGalaxyId) — every call site
    // already only ever asks for systems within whichever galaxy is active.
    SystemWorld& GetOrCreateWorld(unsigned int systemId);
    SystemWorld& EnsureWorldGenerated(unsigned int systemId);

    ecs::GameModeManager      _modeManager;

    // Player represented as a single ECS entity + game-specific meta.
    ecs::Entity  _playerEntity;
    PlayerMeta   _playerMeta;

    GalaxyMap               _galaxyMap;
    StorageMenu             _storageMenu;
    ModulesMenu             _modulesMenu;
    HardpointRig             _loadout;
    Camera2D                _camera      = {};
    float                   _cameraZoom  = 1.0f;
    Texture2D*              _playerShipTex = nullptr;
    float                   _hitCooldown = 0.0f;
    TargetInfo              _target;
    unsigned int            _targetId     = 0;
    bool                    _localMapOpen = false;

    struct BgStar {
        float x, y, parallax, radius;
        Color color;
    };
    static constexpr int BgStarCount = 250;
    std::array<BgStar, BgStarCount> _bgStars;

    void InitStars();
    void PrewarmSpriteCache();
    void SpawnInitialAsteroids();
    void SpawnNpcShips();
    void SpawnPlanetsAndStations(unsigned int seed = 0);
    void AdvanceProjectilesAndAsteroids(float dt);
    void UpdateWorldStationFire(float dt);
    void UpdateCapitalFire(float dt);
    // Epic 8: replaces normal player movement/fire while _seated — freezes
    // _playerEntity onto the seated hardpoint's live world position and
    // fires that hardpoint's own weapon toward mouseWorld on left-click.
    // Auto-unseats (leaving the player parked at the last known position) if
    // the capital or that specific hardpoint has died since seating.
    void UpdateSeatedTurret(float dt, Vector2 mouseWorld, bool clickedHudBtn);
    void CullAndRespawnAround(float dt, Vector2 anchor);
    void TickBackgroundWorld(float dt, SystemWorld& world);
    void ApplyWorldSyncClient(const net::WorldSyncData& ws);
    net::WorldSyncData BuildWorldSync(const SystemWorld& world) const;
    void BeginWarpSequence(unsigned int targetSystemId);
    // Cross-galaxy warp: targetGalaxyId's home system (id 1) is always the
    // arrival point — see UniverseRegistry's id==1 convention.
    void BeginGalaxyWarpSequence(unsigned int targetGalaxyId);
    void BeginLocalWarp(Vector2 targetPos);
    void UpdateWarpSequence(float dt);
    void DrawWarpParticles()   const;
    // targetGalaxyId == 0 (default) means "same galaxy as current" — the
    // existing cross-system-only warp path. A non-zero value re-Inits
    // StarSystemRegistry for the new galaxy and resets galaxy-scoped state
    // (_discoveredSystemIds) before switching systems.
    void CommitWarpWorldSwitch(unsigned int targetSystemId, unsigned int targetGalaxyId = 0);
    void SpawnWarpParticle(Vector2 pos, Vector2 dir);
    void UpdateCollisions();
    void UpdateNpcShips(float dt);
    void UpdateNpcCollisions();
    // Epic 9.1 (capture): once a hostile station/capital is disabled (see
    // combat::IsDisabled), flipping it to Friendly on player approach is
    // "instant capture" — no boarding minigame. Host/singleplayer-only,
    // same as UpdateSeatedTurret, since NpcMeta/SpaceStation are only
    // authoritative there.
    void UpdateCaptureProximity();
    // Shared by the PlayerStation/SpaceStation/capital-ship hardpoint-hit
    // blocks in UpdateNpcCollisions: finds the first alive hardpoint within
    // range of projPos, applies damage (clamped to 0), and emits the
    // destroyed-hardpoint comms message. Returns true if a hardpoint was
    // hit at all (whether or not it died) so the caller can then check
    // combat::AllHardpointsDestroyed for ship/station death — that part,
    // and any per-owner side effects (retaliation, loot, net broadcast),
    // stay in each caller since they genuinely differ per owner type.
    bool ResolveHardpointHit(std::vector<Hardpoint>& hardpoints, Vector2 projPos, float damage,
                              const std::function<Vector2(int)>& hardpointWorldPos,
                              const std::string& msgPrefix, const std::string& msgSuffix,
                              bool urgent = false);
    void UpdateOrbits(float dt);
    void ApplySunGravity(float dt);
    void UpdateTarget();
    void ApplyLoadout();
    void DrawBackground()      const;
    void DrawSun()             const;
    void DrawPlanets()         const;
    void DrawStations()        const;
    void DrawPlayerStations()  const;
    // Shared body/hardpoint draw for one station — used for both the local
    // player's own PlayerStations and remote peers' stations (Epic C MP sync,
    // [[tasks-multiplayer]]). isLocal only controls the ownership label.
    void DrawOneStation(const PlayerStation& ps, bool isLocal) const;
    void DrawNpcShips()        const;
    void DrawHUD()             const;
    void DrawLocalMap()        const;
    bool IsNearPlanet()    const;
    bool IsNearStation()   const;
    bool IsNearEnterableStation() const;
    // Same proximity rule as IsNearEnterableStation(), but also reports which
    // specific station matched — needed so entering one can remember which
    // station's alive-state to watch while the player is docked inside it.
    struct EnterableStation { bool found = false; bool isPlayerStation = false; unsigned int id = 0; };
    EnterableStation FindEnterableStation() const;
    // Epic 3 (economy): live StationEconomy* for a station found by
    // FindEnterableStation(), or null if it no longer exists (station died
    // between the find and the lookup). Non-const: StationServicesMenu
    // mutates stock through the returned pointer.
    StationEconomy* FindStationEconomy(unsigned int stationId, bool isPlayerStation);
    // Epic 6.3: the docked station's Faction, for StationServicesMenu's
    // trade-reputation hook. Player-built stations report kPlayerFaction
    // (ReputationRegistry::Adjust no-ops on the player's own faction anyway).
    Faction FindStationFaction(unsigned int stationId, bool isPlayerStation) const;
    // Epic 7: fresh contract offers for the station the player is currently
    // docking into — ephemeral, regenerated every dock rather than persisted.
    // Bounty requires a faction hostile to the issuer to exist; Courier
    // requires another currently-generated same-faction station with stock
    // to haul; Escort requires a currently-alive same-faction Trader-role NPC
    // in this system. Any of the three may be absent from the result.
    std::vector<Contract> GenerateContractOffers(Faction issuerFaction, unsigned int stationId, bool isPlayerStation);
    // Ticks the active Courier/Escort timer, fails an Escort whose target
    // died, completes a Bounty once its kill quota is met. Called once per
    // frame from Update() regardless of docked state.
    void TickActiveContract(float dt);
    void CompleteActiveContract();
    void FailActiveContract(const std::string& reason);
    // Epic 11.2 (distress calls): resolves _w->activeDistress each frame —
    // called from UpdateNpcShips so it runs for both the foreground world and
    // any background-ticked world (ShipUnderAttack calls can occur in either;
    // Stranded only ever occurs in the world that was _w when the warp
    // attempt failed, since insufficient fuel blocks leaving that system).
    void TickDistressCalls(float dt);
    // Epic 11.2: small per-tick chance for a non-hostile-faction NPC entering
    // Flee (low hull, being run down by a hostile) to broadcast a distress
    // call. No-ops if this world already has an active call.
    void TryStartAttackDistressCall(const NpcMeta& m, Vector2 pos);
    // Epic 8: nearest alive hardpoint of an alive, faction-Friendly capital
    // ship (player-built or escort wingman, either counts) within seating
    // range of the player. Returns false with all outputs untouched if none
    // qualify.
    bool FindNearestFriendlySeat(unsigned int& npcIdOut, int& hpIdxOut, Vector2& posOut) const;
    // Epic 3: looks up a SpaceStation by (worldKey, stationId) in any
    // currently-generated world, not just _w — needed for Trader routes,
    // which cross systems. Null if that world/station isn't loaded or the
    // station has since died.
    SpaceStation* FindWorldStation(uint64_t worldKey, unsigned int stationId);
    // Epic 3.2/3.3: background production (Industrialist) / cross-system
    // hauling (Trader) simulation for one NPC, ticked from UpdateNpcShips.
    // Independent of NpcAiState/movement — those roles' own AI behavior is
    // Epic 2.6/2.7, still unstarted (tasks_spaceflight_dynamics.md).
    void TickNpcEconomy(NpcMeta& m, float dt);
    // Epic 5.2: small per-tick chance for an Industrialist NPC to "found" a
    // new station in an adjacent uncontrolled system (grid-neighbor of the
    // NPC's current system) — flips StarSystemRegistry::SetControlled for
    // that id so the next time that system's world is generated,
    // SpawnPlanetsAndStations spawns a real station there. No literal NPC
    // travel involved (none exists yet) — see its own comment for the
    // abstraction rationale.
    void TryColonizeAdjacentSystem(Faction f);
    void UpdatePlayerStations(float dt);
    // Marks planets/world-stations within DiscoveryRange of the player as
    // discovered. Shared by Update()'s normal tail and TickWorldWhileDocked.
    void TickDiscovery();
    // World-sim tick run while the player is inside _stationServicesMenu:
    // everything Update()'s normal tail runs (NPCs, projectiles, station/
    // capital fire, collisions) except player input/movement/camera.
    void TickWorldWhileDocked(float dt);
    // Sends this client's InputCommand to the host; `docked` reports whether
    // the local player is inside a station menu (frozen, untargetable) so the
    // host can exclude this peer from targeting/collision/broadcast.
    void SendClientInput(bool docked, bool firing);
    void SpawnLootDrop(Vector2 pos, NpcFaction killedFaction);
    void SpawnMaterialDrop(Vector2 pos, const std::string& materialId);
    // Epic 11.1: capital/station-only "meaningful kill" reward — a
    // persistent (non-expiring) salvage target, separate from the routine
    // LootDrop/MaterialDrop scatter every kill already gets.
    void SpawnDerelictWreck(Vector2 pos, bool isCapital, int creditsReward, const std::string& sourceName);
    ModuleDef GenerateDrop(ModuleGrade grade);
    void AddCommsMessage(const std::string& text, bool fromPlayer = false);
    // Auto-collects a material into a mining station's storage at an interval
    // set by the grade of its installed Material Probe. No-op without one, or
    // while the station's storage is full.
    void TickStationMining(PlayerStation& ps, float dt);

    // Per weapon slot firing state. Number keys 1-9 (and 0 for slot 10) toggle
    // a slot on/off; every enabled+equipped weapon fires together, each on its
    // own cooldown/charge timer. A starfighter can thus fire all, some or none
    // of its weapons at once. Sized to the ship's weaponSlots by ApplyLoadout;
    // slots default to enabled.
    std::vector<bool>  _weaponEnabled;
    std::vector<float> _weaponCooldown;
    std::vector<float> _weaponCharge;
    // A slot fires when it has no explicit flag yet (freshly loaded) or its
    // flag is on. Out-of-range indices read as enabled.
    bool IsWeaponEnabled(int i) const {
        return i < 0 || i >= (int)_weaponEnabled.size() || _weaponEnabled[i];
    }
    // First enabled+equipped weapon slot (-1 if none). Only the HUD WEAPON
    // readiness/charge panel uses it as a representative; firing is per-slot.
    int          _primaryWeapon     = -1;
    unsigned int _lockTargetId      = 0;
    Vector2      _lockTargetPos     = {};
    bool         _enterPopupOpen    = false;

    BuildMenu            _buildMenu;
    StationModuleMenu    _stationModMenu;
    MiningStationMenu    _miningMenu;
    StationServicesMenu  _stationServicesMenu;
    bool              _inPlacementMode    = false;
    std::string       _placingStationDefId;
    bool              _placementConfirmOpen = false;
    Vector2           _placementPos       = {};
    unsigned int      _stationModMenuId   = 0;
    unsigned int      _miningMenuId       = 0;
    // Which station the player is currently docked inside (via
    // _stationServicesMenu), so a destruction check can find it again each
    // frame. 0 = not docked. isPlayerStation disambiguates the id namespace:
    // FleetManager::Get().PlayerStations vs _w->stations.
    unsigned int      _dockedStationId       = 0;
    bool              _dockedIsPlayerStation = false;

    // Epic 7 (contracts): offers shown at the currently-docked station, plus
    // the player's single active contract (first-pass scope: one at a time).
    std::vector<Contract> _contractOffers;
    bool                  _hasActiveContract = false;
    Contract              _activeContract;
    unsigned int          _nextContractId    = 1;

    // Epic 8 (capital-ship piloting, tasks_spaceflight_dynamics.md #8): a
    // turret seat — the player mans one hardpoint of a friendly capital and
    // fires it directly while the ship's own AI keeps flying/holding the
    // rest. Deferred bridge/strategy seat stays deferred per
    // [[project-capital-ships]]. Seating freezes _playerEntity onto the
    // hardpoint's live world position every frame, which the existing
    // camera-follow code then tracks automatically. _seated also gates the
    // same collision/targeting exclusions _stationServicesMenu.isOpen
    // already uses throughout this file, so the player can't be shot/rammed
    // while away from their own ship.
    bool         _seated             = false;
    unsigned int _seatedNpcId        = 0;
    int          _seatedHardpointIdx = -1;

    Texture2D    _planetBaseTex     = {};
    Texture2D    _stationBaseTex    = {};
    Texture2D    _gargosTex         = {};
    Texture2D    _asteroidTexLarge  = {};
    Texture2D    _asteroidTexMedium = {};
    Font         _hudFontUi         = {}; // Orbitron — HUD labels/buttons
    Font         _hudFontVal        = {}; // Exo 2 — HUD values/readouts
    Texture2D    _asteroidTexSmall  = {};

    Texture2D                  _sunTex         = {};
    Texture2D                  _sunCorona      = {};
    ecs::LightingSystem        _lighting;

    unsigned int            _npcTargetId  = 0;

    bool                      _hasSensors  = false;

    // Galaxy-map fog-of-war reveal radius — see AuxStats::mapSensorRange.
    // Independent of _hasSensors (combat targeting); no baseline, 0 until a
    // Sensor Array-line aux module is equipped.
    float                     _mapSensorRange  = 0.0f;
    // ModuleGrade+1 (Common=1..Mythic=7) of whichever equipped aux module
    // provides _mapSensorRange; 0 with nothing equipped. Higher tiers reveal
    // progressively richer intel about undiscovered-but-visible systems at
    // the Galaxy tier (star class, planet estimate, occupancy...) on top of
    // existence/distance — see GalaxyMap's PreviewSensorIntel — giving a
    // reason to upgrade sensors beyond pure reveal range.
    int                       _mapSensorTier   = 0;

    float                     _hyperdriveRange = 0.0f;
    std::vector<unsigned int> _discoveredIds;

    // Epic 4 (hyperdrive fuel, tasks_spaceflight_dynamics.md #4): flat tank,
    // not module-tied (hyperdriveRange already governs reach; fuel governs
    // how many jumps the tank holds before a refuel stop is needed). Cost
    // per jump scales with how much of the drive's max range that hop used
    // (see JumpFuelCost) — ties into the existing hyperdrive-range stat per
    // the epic's locked decision, rather than a flat per-jump price.
    static constexpr float kMaxFuel            = 100.0f;
    static constexpr float kFuelPerFullRangeJump = 40.0f;
    float                     _fuel = kMaxFuel;
    // Epic 11.2: Stranded distress-call tuning — a passing vessel takes
    // 45-90s (rolled at send time) to arrive and hands over a bit more than a
    // full-range jump's worth of fuel, matching kFuelPerFullRangeJump's scale.
    static constexpr float kDistressFuelAmount = 40.0f;
    // Epic 11.2: ambient ShipUnderAttack tuning — 1-in-4 chance per Flee
    // transition, must survive 90s to pay out.
    static constexpr int   kDistressAttackCallChancePct = 25;
    static constexpr float kDistressAttackWindowSeconds = 90.0f;
    // Per-unit fuel price is read from a station's live economy for the
    // "fuel_cells" good (Epic 3) — no stock there means no refuel available,
    // same as any other commodity running out.
    float JumpFuelCost(float jumpDistance) const {
        if (_hyperdriveRange <= 0.0f) return 0.0f;
        return std::clamp(jumpDistance / _hyperdriveRange, 0.0f, 1.0f) * kFuelPerFullRangeJump;
    }

    // Epic 5.3: how long a destroyed NPC station stays a wreck before its
    // faction rebuilds it (see TickStationRebuilds). ~8 minutes — rare/
    // meaningful, not a respawn-timer-style trivial reset.
    static constexpr float kStationRebuildSeconds = 480.0f;
    void TickStationRebuilds(float dt);

    unsigned int              _currentSystemId     = 1;
    std::vector<unsigned int> _discoveredSystemIds;
    uint32_t                  _gameSeed            = 0; // universe seed (UniverseRegistry / StarSystemRegistry)

    // Which galaxy _currentSystemId belongs to. Defaults to 1 (home galaxy),
    // which UniverseRegistry::Generate(1) special-cases to reuse _gameSeed
    // directly — so single-galaxy sessions/saves behave exactly as before.
    // _discoveredSystemIds is scoped to this galaxy only and is cleared on
    // every galaxy change (crossing into a new one, or returning to an old
    // one) — unlike _worlds, per-galaxy discovery/fog-of-war state isn't
    // remembered across a visit; only the mutable world state itself is.
    unsigned int              _currentGalaxyId     = 1;

    // Unlike _discoveredSystemIds (scoped to one galaxy, cleared on every
    // galaxy change), this tracks every galaxy id the player has ever warped
    // into across the whole game — used by the Universe tier map to color
    // galaxies visited (blue) vs. never-visited (orange). Always contains at
    // least the home galaxy (1).
    std::vector<unsigned int> _visitedGalaxyIds    = { 1 };

    // ── Warp sequence (galactic map -> new system, and in-system local warp) ──
    // TurnToFace is shared by both; _warpPhaseAfterTurn picks which flavor of
    // travel follows it. FlyOut/FadeOut/FlyIn = cross-system (fades through
    // black while the new system spawns). LocalFly = same-system dash straight
    // to the target position, no fade.
    // AwaitSync is client-only: screen stays black after FadeOut until the
    // host's WorldSync for the destination system arrives, then FlyIn plays.
    enum class WarpPhase { None, TurnToFace, FlyOut, FadeOut, AwaitSync, FlyIn, LocalFly };
    struct WarpParticle { Vector2 pos, vel; float life, maxLife; };

    WarpPhase                 _warpPhase          = WarpPhase::None;
    WarpPhase                 _warpPhaseAfterTurn = WarpPhase::FlyOut;
    float                     _warpPhaseTimer     = 0.0f;
    unsigned int              _warpTargetSystemId = 0;
    // 0 = same galaxy as current (the common case); otherwise the target
    // galaxy id, set by BeginGalaxyWarpSequence for a cross-galaxy warp.
    unsigned int              _warpTargetGalaxyId = 0;
    Vector2                   _warpDir            = { 0.0f, -1.0f }; // unit direction of travel
    float                     _warpStartRot       = 0.0f;
    float                     _warpTargetRot      = 0.0f;
    float                     _warpFadeAlpha      = 0.0f;
    Vector2                   _warpFlyInStart     = {};
    Vector2                   _warpLocalTarget    = {};   // destination for LocalFly
    std::vector<WarpParticle> _warpParticles;

    // Beacon-chaining: remaining hop ids (excluding the one currently in
    // flight) for a multi-hop warp through already-discovered systems past
    // the hyperdrive's single-jump range — see GalaxyMap::WarpChain(). Each
    // hop plays out as a full, ordinary BeginWarpSequence cinematic; FlyIn's
    // completion (UpdateWarpSequence) pops the next id and re-triggers
    // BeginWarpSequence instead of returning to WarpPhase::None, so the chain
    // reads as consecutive real jumps rather than one glossed-over trip.
    // Empty for every other kind of warp (direct single-hop, galaxy, local).
    std::vector<unsigned int> _warpChainQueue;

    bool  _playerDead  = false;
    float _deathTimer  = 0.0f;

    // ── Multiplayer ───────────────────────────────────────────────────────────
    float    _netTickAccum = 0.0f;
    bool     _worldSynced  = true;  // false on client until WorldSync received
    // Remote entities (NPCs + other players) keyed by networkId.
    std::unordered_map<uint32_t, ecs::Entity>  _remoteEntities;
    // Client-side draw/combat state for remote capital ships, keyed by networkId
    // (== the capital's NpcMeta::id). Built once via BuildCapitalHardpoints when the
    // remote entity is first seen; hull/alive is then patched per capital snapshot.
    struct RemoteCapitalInfo {
        std::vector<Hardpoint> hardpoints;
        float      radius  = 140.0f;
        NpcFaction faction = NpcFaction::Neutral;
    };
    std::unordered_map<uint32_t, RemoteCapitalInfo> _remoteCapitalHardpoints;
    // P8-T1: client-side view of a remote PLAYER's fighter loadout, keyed by
    // networkId (< 1000; NPCs use the id-1000 boundary elsewhere in this
    // file too). Shaped like the LOCAL player's own current HardpointRig
    // (Resize(weaponSlots,shieldSlots,auxSlots)) rather than the remote
    // player's actual ship type, matching the pre-existing convention that
    // remote players already render with the local player's own hull texture
    // (see DrawRemotePlayers' "networkId < 1000" branch) — the mount layout
    // and hull skin come from the same source, so they stay visually
    // consistent with each other. Overlaid per FighterHardpointSnapshot row;
    // NPCs need no equivalent map since their loadout is deterministic from
    // the shared world seed and never changes post-spawn (no per-hardpoint
    // combat damage exists for fighters, unlike capitals/stations).
    std::unordered_map<uint32_t, std::vector<Hardpoint>> _remoteFighterMounts;
    // Client-side view of stations built by OTHER peers, keyed by
    // PlayerStation::id. Rebuilt/patched each Snapshot from
    // PlayerStationSnapshot + PlayerStationHardpointSnapshot (Epic C MP sync,
    // [[tasks-multiplayer]]); rendered via DrawOneStation() alongside the
    // local player's own FleetManager::PlayerStations.
    std::unordered_map<uint32_t, PlayerStation> _remotePlayerStations;
    // Per-remote-client fire cooldown (host side).
    std::unordered_map<uint32_t, float>         _remoteFireCooldown;
    // Grace timer: new clients are invincible for 5 s so they don't die at spawn.
    std::unordered_map<uint32_t, float>         _remoteJoinGrace;
    // Host: which connected peers are currently docked (InputCommand::docked),
    // so hostile-fire/collision/broadcast can exclude them the same way a
    // docked local player is excluded.
    std::unordered_map<uint32_t, bool>          _remoteDocked;
    // Host: which system each connected peer is currently in (networkId -> systemId).
    std::unordered_map<uint32_t, unsigned int>  _peerSystem;
    // Host: which faction each connected peer plays as, learned from their
    // Hello packet (see NetSession::newPeerFactions). Drives discovery
    // pooling: peers sharing a faction ("party") see each other's discovered
    // systems — see CommitWarpWorldSwitch and the pendingWarpNotifies/
    // newPeerFactions handling in Update().
    std::unordered_map<uint32_t, Faction>       _peerFaction;
    // Host only: shared discovered-system-id sets for connected peers NOT on
    // the host's own faction (kPlayerFaction), keyed by (uint8_t)Faction. The
    // host's own faction's shared set is just _discoveredSystemIds itself —
    // no separate entry needed since the host is always a member of its own
    // faction's party.
    std::unordered_map<uint8_t, std::vector<unsigned int>> _peerFactionDiscovered;
    // Server-authoritative projectiles rendered on client.
    std::vector<net::ProjectileSnapshot>        _remoteProjectiles;
    // True while TickBackgroundWorld simulates a world the local player isn't
    // in: suppresses comms chatter and player-station (FleetManager) targeting,
    // which aren't tagged with a system and belong to the player's world.
    bool _bgTick = false;
    // Host: true for a peer currently in the same system as `_w`.
    bool PeerInCurrentWorld(uint32_t networkId) const;
    void DrawRemotePlayers() const;
    // Client: resolves a snapshot's shipNameHash back to a ShipDef so a remote
    // NPC can render with its real sprite instead of the red-circle fallback.
    const ecs::ShipDef* ResolveShipDefByHash(uint32_t shipNameHash) const;

    std::vector<CommsEntry> _commsLog;
    // Epic 12.1: how many recent messages AddCommsMessage retains — bumped
    // from the original 5 now that a panel (below) actually surfaces them.
    static constexpr size_t kCommsLogCap = 8;
    bool _commsLogOpen = false; // toggled by the [L] key; centered overlay, same style as the ranks menu

    bool         _commsMenuOpen  = false;
    int          _commsMenuPhase = 0;
    unsigned int _commsMenuNpcId = 0;
    std::string  _commsMenuNpcName;
    std::string  _commsMenuNpcText;
    // Epic 13: hailing a station (contract board, no docking required) reuses
    // this same popup instead of a separate menu; hailing the specific ship
    // broadcasting an active ShipUnderAttack distress call swaps the
    // "REQUEST JOIN" action for "ACKNOWLEDGE" instead.
    bool         _commsMenuIsStation  = false;
    unsigned int _commsMenuStationId = 0;
    bool         _commsMenuIsDistress = false;
    // Capital-class craft (hardpoint-bearing) can't join the escort wing —
    // the wing/escort AI treats members as fighters, so a capital would take
    // on skewed fighter attributes. Hide the "REQUEST JOIN" action for them.
    bool         _commsMenuIsCapital  = false;

    void ApplyNpcLoadout(ecs::Entity& entity, NpcMeta& meta);
    // Places a Friendly NPC ship into `world` at `pos` — shared by the local
    // build-menu confirm handler (host/offline) and the host's drain of
    // client PlaceShipRequests (Epic C MP sync), so both paths stay in sync.
    // placerFaction is whichever peer actually built/placed the ship (their
    // diplomatic Faction, not the ship hull's palette) — the placed ship's
    // real hostility checks key off this, not the cosmetic green "Friendly"
    // tint. Caller passes kPlayerFaction for the local player's own build,
    // or the requesting client's own faction (via _peerFaction) when the
    // host is draining a PlaceShipRequest on someone else's behalf.
    void PlaceFriendlyShip(SystemWorld& world, const std::string& shipDefId, Vector2 pos, Faction placerFaction);
    void ApplyWorldState(const SaveManager::GameState& gs);
    SaveManager::GameState BuildWorldState() const;
    void BakeSunCorona();

    bool         _ranksMenuOpen     = false;
    bool         _escortMenuOpen    = false;
    unsigned int _escortMenuSelId   = 0;
    unsigned int _escortModuleNpcId = 0;

    // Epic 12 (tutorial + home-station protection, tasks_spaceflight_dynamics.md
    // #12): an 8-step scripted onboarding, skippable at any point via the
    // persistent [T] hint. Session-only — a fresh (non-loaded) session starts
    // active; loading an existing save skips it outright (SaveManager has no
    // "has completed tutorial" field, and a returning player doesn't need one).
    // Hints are delivered via the existing AddCommsMessage feed (12.1), not a
    // new message-passing system. Home-system (galaxyId==1/systemId==1)
    // hostile-spawn suppression (12.3) rides this same flag and lifts on
    // completion, explicit skip, or the player warping away (12.4) — see
    // MakeNpcEntity's suppressHostile param and the warp-begin functions.
    enum class TutorialStep : unsigned char {
        Move, Target, DestroyAsteroid, CollectMaterial, Dock, Sell, EquipModule, Warp, Done
    };
    static constexpr float kTutorialMoveDistance = 300.0f;
    bool         _tutorialActive   = false;
    TutorialStep _tutorialStep     = TutorialStep::Move;
    Vector2      _tutorialStartPos = {};
    // Checks the two steps with no natural discrete "event" hook elsewhere
    // (move far enough, get a valid target lock) — called once per frame from
    // Update()'s normal tail. Every other step completes at its own existing
    // action site (asteroid destroyed, material collected, docked, sold,
    // equipped, warped) via AdvanceTutorialStep directly.
    void TickTutorial();
    // No-ops unless the tutorial is active and currently on exactly this step
    // (so an out-of-order action — e.g. selling before targeting — can't
    // skip steps). Advances to the next step and posts its hint via
    // AddCommsMessage, or ends the tutorial on the final step.
    void AdvanceTutorialStep(TutorialStep expected);
    void SkipTutorial();
};

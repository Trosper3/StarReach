#pragma once
#include "modes/IGameMode.h"
#include "Projectile.h"
#include "Asteroid.h"
#include "SystemMap.h"
#include "GalacticMap.h"
#include "StorageMenu.h"
#include "ModulesMenu.h"
#include "BuildMenu.h"
#include "StationModuleMenu.h"
#include "core/Module.h"
#include "core/PlayerStation.h"
#include "core/SaveManager.h"
#include "core/GameModeManager.h"
#include "shared/Entity.h"
#include "engine/ResourceManager.h"
#include "engine/LightingSystem.h"
#include "raylib.h"
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <core/ShipDef.h>
#include "core/FactionEnum.h"
#include "net/Protocol.h"

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
    std::vector<HardpointState> hardpoints;
};

// ── NPC AI state ─────────────────────────────────────────────────────────────

enum class NpcFaction : unsigned char { Friendly, Neutral, Hostile };
enum class NpcAiState : unsigned char { Patrol, Chase, Attack, Flee, Escort };

// Per-NPC game-specific state not stored in the ECS Entity components.
// Parallel to _entities by index.
struct NpcMeta {
    unsigned int id            = 0;
    float        radius        = 18.0f;
    bool         alive         = true;
    bool         thrusting     = false;
    NpcFaction   faction       = NpcFaction::Friendly;
    NpcAiState   aiState       = NpcAiState::Patrol;
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
    // Derived weapon/movement stats (set by ApplyNpcLoadout)
    float        npcThrust     = 0.0f;
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
    ShipLoadout  loadout;
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

struct CommsEntry {
    std::string text;
    bool        fromPlayer = false;
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

    // ── ECS entity list + NPC metadata ──────────────────────────────────────
    // _entities[i] and _npcMeta[i] are always parallel — same index = same NPC.
    std::vector<ecs::Entity>  _entities;
    std::vector<NpcMeta>      _npcMeta;
    std::vector<size_t>       _npcFreeSlots; // indices of dead slots available for reuse
    ecs::GameModeManager      _modeManager;

    // Player represented as a single ECS entity + game-specific meta.
    ecs::Entity  _playerEntity;
    PlayerMeta   _playerMeta;

    std::vector<Projectile> _projectiles;
    std::vector<Asteroid>   _asteroids;
    SystemMap               _systemMap;
    GalacticMap             _galacticMap;
    StorageMenu             _storageMenu;
    ModulesMenu             _modulesMenu;
    ShipLoadout             _loadout;
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
    void BeginWarpSequence(unsigned int targetSystemId);
    void BeginLocalWarp(Vector2 targetPos);
    void UpdateWarpSequence(float dt);
    void DrawWarpParticles()   const;
    void CommitWarpWorldSwitch(unsigned int targetSystemId);
    void SpawnWarpParticle(Vector2 pos, Vector2 dir);
    void UpdateCollisions();
    void UpdateNpcShips(float dt);
    void UpdateNpcCollisions();
    void UpdateOrbits(float dt);
    void ApplySunGravity(float dt);
    void UpdateTarget();
    void ApplyLoadout();
    void DrawBackground()      const;
    void DrawSun()             const;
    void DrawPlanets()         const;
    void DrawStations()        const;
    void DrawPlayerStations()  const;
    void DrawNpcShips()        const;
    void DrawHUD()             const;
    void DrawLocalMap()        const;
    bool IsNearPlanet()    const;
    bool IsNearStation()   const;
    bool IsNearEnterableStation() const;
    void SpawnLootDrop(Vector2 pos, NpcFaction killedFaction);
    void SpawnMaterialDrop(Vector2 pos, const std::string& materialId);
    ModuleDef GenerateDrop(ModuleGrade grade);
    void AddCommsMessage(const std::string& text, bool fromPlayer = false);

    int          _selectedWeapon    = 0;
    unsigned int _lockTargetId      = 0;
    Vector2      _lockTargetPos     = {};
    bool         _enterPopupOpen    = false;
    bool         _stationPopupOpen  = false;

    BuildMenu         _buildMenu;
    StationModuleMenu _stationModMenu;
    bool              _inPlacementMode    = false;
    std::string       _placingStationDefId;
    bool              _placementConfirmOpen = false;
    Vector2           _placementPos       = {};
    unsigned int      _stationModMenuId   = 0;

    Texture2D    _planetBaseTex     = {};
    Texture2D    _stationBaseTex    = {};
    Texture2D    _gargosTex         = {};
    Texture2D    _asteroidTexLarge  = {};
    Texture2D    _asteroidTexMedium = {};
    Texture2D    _asteroidTexSmall  = {};

    SpaceSun                   _sun;
    Texture2D                  _sunTex         = {};
    Texture2D                  _sunCorona      = {};
    Vector2                    _playerSpawnPos = {};
    ecs::LightingSystem        _lighting;

    std::vector<SpacePlanet>   _planets;
    std::vector<SpaceStation>  _stations;

    unsigned int            _npcTargetId  = 0;
    unsigned int            _nextNpcId    = 1000;
    float                   _respawnTimer = 0.0f;

    std::vector<LootDrop>     _lootDrops;
    std::vector<MaterialDrop> _materialDrops;
    bool                      _hasSensors  = false;

    float                     _hyperdriveRange = 0.0f;
    std::vector<unsigned int> _discoveredIds;

    unsigned int              _currentSystemId     = 1;
    std::vector<unsigned int> _discoveredSystemIds;
    uint32_t                  _gameSeed            = 0; // master galaxy seed (StarSystemRegistry)

    // ── Warp sequence (galactic map -> new system, and in-system local warp) ──
    // TurnToFace is shared by both; _warpPhaseAfterTurn picks which flavor of
    // travel follows it. FlyOut/FadeOut/FlyIn = cross-system (fades through
    // black while the new system spawns). LocalFly = same-system dash straight
    // to the target position, no fade.
    enum class WarpPhase { None, TurnToFace, FlyOut, FadeOut, FlyIn, LocalFly };
    struct WarpParticle { Vector2 pos, vel; float life, maxLife; };

    WarpPhase                 _warpPhase          = WarpPhase::None;
    WarpPhase                 _warpPhaseAfterTurn = WarpPhase::FlyOut;
    float                     _warpPhaseTimer     = 0.0f;
    unsigned int              _warpTargetSystemId = 0;
    Vector2                   _warpDir            = { 0.0f, -1.0f }; // unit direction of travel
    float                     _warpStartRot       = 0.0f;
    float                     _warpTargetRot      = 0.0f;
    float                     _warpFadeAlpha      = 0.0f;
    Vector2                   _warpFlyInStart     = {};
    Vector2                   _warpLocalTarget    = {};   // destination for LocalFly
    std::vector<WarpParticle> _warpParticles;

    bool  _playerDead  = false;
    float _deathTimer  = 0.0f;

    // ── Multiplayer ───────────────────────────────────────────────────────────
    uint32_t _worldSeed    = 0;
    float    _netTickAccum = 0.0f;
    bool     _worldSynced  = true;  // false on client until WorldSync received
    // Remote entities (NPCs + other players) keyed by networkId.
    std::unordered_map<uint32_t, ecs::Entity>  _remoteEntities;
    // Per-remote-client fire cooldown (host side).
    std::unordered_map<uint32_t, float>         _remoteFireCooldown;
    // Grace timer: new clients are invincible for 5 s so they don't die at spawn.
    std::unordered_map<uint32_t, float>         _remoteJoinGrace;
    // Server-authoritative projectiles rendered on client.
    std::vector<net::ProjectileSnapshot>        _remoteProjectiles;
    void DrawRemotePlayers() const;

    std::vector<CommsEntry> _commsLog;

    bool         _commsMenuOpen  = false;
    int          _commsMenuPhase = 0;
    unsigned int _commsMenuNpcId = 0;
    std::string  _commsMenuNpcName;
    std::string  _commsMenuNpcText;

    void ApplyNpcLoadout(ecs::Entity& entity, NpcMeta& meta);
    void ApplyWorldState(const SaveManager::GameState& gs);
    SaveManager::GameState BuildWorldState() const;
    void BakeSunCorona();

    bool         _ranksMenuOpen     = false;
    bool         _escortMenuOpen    = false;
    unsigned int _escortMenuSelId   = 0;
    unsigned int _escortModuleNpcId = 0;
};

#pragma once
#include <string>
#include <vector>     
#include <unordered_map>
#include "raylib.h"

enum class ModuleType  { Weapon, Armor, Shield, Engine, Hyperdrive, Auxiliary, Consumable, Facility };
enum class ModuleGrade { Common, Uncommon, Unique, Remarkable, Epic, Legendary, Mythic };

// P4 — facilities as hybrid chips (docs/plans/unified_hardpoint_tasks.md).
// A Facility occupies a Facility-typed slot inside a Hardpoint, same shape as
// the pre-existing mining_drill aux-slot + Material Probe combo — the
// hardpoint is the destructible mount, the facility module is what makes it
// function as a specific kind of station service.
enum class FacilityKind {
    Reactor, Trading, Manufacturing, Shipyard, Contracting,
    Refuel, Repair, ShieldGenerator, WeaponBattery, Mining
};

// Consumed by P5 adjacency rules (Mining<->Manufacturing throughput,
// Reactor<->consumer efficiency, ShieldGenerator coverage). None today since
// no adjacency system exists yet — present now so P4 facility defs have a
// stable field to set without a later struct-shape change.
enum class AdjacencyTag { None, Reactor, Consumer, Mining, Manufacturing, ShieldGenerator };

struct FacilityStats {
    FacilityKind kind        = FacilityKind::Trading;
    float        powerDraw   = 0.0f;  // P4-T6: load this facility adds to the P3 power budget
    float        powerOutput = 0.0f;  // P4-T6: capacity this facility adds (Reactor only, today)
    float        baseRate    = 0.0f;  // per-kind meaning: mining yield, trade throughput, etc.
    int          priority    = 2;     // shed priority tier if overloaded (lower sheds first)
    AdjacencyTag adjTag      = AdjacencyTag::None;
};

enum class WeaponFireMode { Standard, Charge, LockOn };
enum class WeaponProjType { Standard, Burst, Spread, Seeking };
enum class DamageType     { Kinetic, Energy };
enum class ShieldType     { Kinetic, Energy };
// WeaponEffect: special on-hit status applied to target modules
enum class WeaponEffect   { None, EMP, Ion };

struct WeaponStats {
    WeaponFireMode fireMode        = WeaponFireMode::Standard;
    WeaponProjType projType        = WeaponProjType::Standard;
    DamageType     damageType      = DamageType::Kinetic;
    WeaponEffect   effect          = WeaponEffect::None;
    float          damage          = 10.0f;
    float          fireRate        = 1.0f;
    float          projSpeed       = 650.0f;
    float          projRange       = 900.0f;
    float          effectDuration  = 0.0f;
    int            burstCount      = 1;
    float          spreadAngle     = 0.0f;
    float          chargeTime      = 0.0f;
    bool           isTurret        = false;  // allows 360° firing arc
};

struct ArmorStats {
    float hullBonus = 50.0f;
};

struct ShieldStats {
    ShieldType shieldType    = ShieldType::Kinetic;
    float      capacity      = 50.0f;
    float      rechargeRate  = 5.0f;
    float      rechargeDelay = 3.0f;
};

struct EngineStats {
    float thrustBonus     = 0.0f;
    float turnSpeedBonus  = 0.0f;
    bool  isHyperdrive    = false;
    float hyperdriveRange = 0.0f;
};

struct AuxStats {
    bool  hasSensors        = false;
    float sensorRange       = 0.0f;
    bool  hasCloaking       = false;
    float materialFindBonus = 0.0f;
    bool  hasLockOnJammer   = false;
    // Galaxy-map fog-of-war reveal radius — distinct from sensorRange (combat
    // targeting, tens of thousands of units) since map-scale detection needs
    // a completely different order of magnitude (star spacing is ~46,000u,
    // vs. sensorRange's 600-5000u tiers). Kept as its own module line (see
    // AuxDefs.h's Aux_ProximityArray..Aux_GalacticSurveyArray) rather than
    // folded into the combat Scanner line, so — since ships only have 1 aux
    // slot — equipping for exploration vs. combat awareness is a real choice.
    float mapSensorRange    = 0.0f;
};

struct ConsumableStats {
    float healAmount = 50.0f;
};

struct ModuleDef {
    std::string  id;
    std::string  displayName;
    std::string  description;
    ModuleType   type  = ModuleType::Weapon;
    ModuleGrade  grade = ModuleGrade::Common;
    Texture2D* texture = nullptr;

    std::string assetPath;                    
    std::vector<std::vector<std::string>> designArray;

    WeaponStats     weapon;
    ArmorStats      armor;
    ShieldStats     shield;
    EngineStats     engine;
    AuxStats        auxiliary;
    ConsumableStats consumable;
    FacilityStats   facility;
};


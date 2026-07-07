#pragma once
#include <string>
#include <vector>     
#include <unordered_map>
#include "raylib.h"

enum class ModuleType  { Weapon, Armor, Shield, Engine, Hyperdrive, Auxiliary, Consumable };
enum class ModuleGrade { Common, Uncommon, Unique, Remarkable, Epic, Legendary, Mythic };

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
};


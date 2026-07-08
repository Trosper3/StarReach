#include "data/registry/ModuleRegistry.h"
#include "data/JsonLoader.h"
#include "data/modules/WeaponDefs.h"
#include "data/modules/ArmorDefs.h"
#include "data/modules/ShieldDefs.h"
#include "data/modules/EngineDefs.h"
#include "data/modules/HyperdriveDefs.h"
#include "data/modules/AuxDefs.h"
#include "data/modules/ConsumableDefs.h"
#include "raylib.h"

std::vector<ModuleDef>                  ModuleRegistry::s_all;
std::unordered_map<std::string, size_t> ModuleRegistry::s_byId;

// ── Enum parsers ──────────────────────────────────────────────────────────────

static ModuleType ParseModuleType(const std::string& s) {
    if (s == "Armor")      return ModuleType::Armor;
    if (s == "Shield")     return ModuleType::Shield;
    if (s == "Engine")     return ModuleType::Engine;
    if (s == "Hyperdrive") return ModuleType::Hyperdrive;
    if (s == "Auxiliary")  return ModuleType::Auxiliary;
    if (s == "Consumable") return ModuleType::Consumable;
    return ModuleType::Weapon;
}

static ModuleGrade ParseGrade(const std::string& s) {
    if (s == "Uncommon")   return ModuleGrade::Uncommon;
    if (s == "Unique")     return ModuleGrade::Unique;
    if (s == "Remarkable") return ModuleGrade::Remarkable;
    if (s == "Epic")       return ModuleGrade::Epic;
    if (s == "Legendary")  return ModuleGrade::Legendary;
    if (s == "Mythic")     return ModuleGrade::Mythic;
    return ModuleGrade::Common;
}

// ── Per-type stat loaders ─────────────────────────────────────────────────────

static void LoadWeapon(ModuleDef& m, const nlohmann::json& w) {
    auto dm = JL::Str(w, "damageType", "Kinetic");
    m.weapon.damageType  = (dm == "Energy") ? DamageType::Energy : DamageType::Kinetic;

    auto fm = JL::Str(w, "fireMode", "Standard");
    m.weapon.fireMode    = (fm == "Charge") ? WeaponFireMode::Charge :
                           (fm == "LockOn") ? WeaponFireMode::LockOn : WeaponFireMode::Standard;

    auto pt = JL::Str(w, "projType", "Standard");
    m.weapon.projType    = (pt == "Burst")   ? WeaponProjType::Burst   :
                           (pt == "Spread")  ? WeaponProjType::Spread  :
                           (pt == "Seeking") ? WeaponProjType::Seeking : WeaponProjType::Standard;

    auto ef = JL::Str(w, "effect", "None");
    m.weapon.effect      = (ef == "EMP") ? WeaponEffect::EMP :
                           (ef == "Ion") ? WeaponEffect::Ion : WeaponEffect::None;

    m.weapon.damage         = JL::Float(w, "damage",         10.f, 0.f, 1000.f);
    m.weapon.fireRate       = JL::Float(w, "fireRate",        1.f, 0.f,  100.f);
    m.weapon.projSpeed      = JL::Float(w, "projSpeed",     650.f, 0.f, 5000.f);
    m.weapon.projRange      = JL::Float(w, "projRange",     900.f, 0.f,10000.f);
    m.weapon.burstCount     = JL::Int  (w, "burstCount",        1,   1,   100);
    m.weapon.spreadAngle    = JL::Float(w, "spreadAngle",    0.f,  0.f,  360.f);
    m.weapon.chargeTime     = JL::Float(w, "chargeTime",     0.f,  0.f,   60.f);
    m.weapon.effectDuration = JL::Float(w, "effectDuration", 0.f,  0.f,   60.f);
    m.weapon.isTurret       = JL::Bool (w, "isTurret", false);
}

static void LoadShield(ModuleDef& m, const nlohmann::json& s) {
    auto st = JL::Str(s, "shieldType", "Kinetic");
    m.shield.shieldType    = (st == "Energy") ? ShieldType::Energy : ShieldType::Kinetic;
    m.shield.capacity      = JL::Float(s, "capacity",      50.f,  0.f, 5000.f);
    m.shield.rechargeRate  = JL::Float(s, "rechargeRate",   5.f,  0.f,  500.f);
    m.shield.rechargeDelay = JL::Float(s, "rechargeDelay",  3.f,  0.f,   60.f);
}

static void LoadEngine(ModuleDef& m, const nlohmann::json& e) {
    m.engine.thrustBonus    = JL::Float(e, "thrustBonus",    0.f, 0.f, 10000.f);
    m.engine.turnSpeedBonus = JL::Float(e, "turnSpeedBonus", 0.f, 0.f,  5000.f);
}

static void LoadHyperdrive(ModuleDef& m, const nlohmann::json& h) {
    m.engine.isHyperdrive    = true;
    // Upper bound covers the Mythic "anywhere in the universe" tier
    // (Singularity Core / Cosmic Fold Drive, 6,000,000,000u) — see
    // HyperdriveDefs.h / UniverseRegistry::kUniverseSpan.
    m.engine.hyperdriveRange = JL::Float(h, "range", 3000.f, 0.f, 10000000000.f);
}

static void LoadAux(ModuleDef& m, const nlohmann::json& a) {
    m.auxiliary.hasSensors        = JL::Bool (a, "hasSensors",        false);
    m.auxiliary.sensorRange       = JL::Float(a, "sensorRange",       0.f, 0.f, 20000.f);
    m.auxiliary.hasCloaking       = JL::Bool (a, "hasCloaking",       false);
    m.auxiliary.materialFindBonus = JL::Float(a, "materialFindBonus", 0.f, 0.f,     2.f);
    m.auxiliary.hasLockOnJammer   = JL::Bool (a, "hasLockOnJammer",   false);
    // Galaxy-map fog reveal radius — see AuxStats::mapSensorRange. Upper
    // bound covers the Omniscient Sensor Core's whole-galaxy tier (150,000,000u).
    m.auxiliary.mapSensorRange    = JL::Float(a, "mapSensorRange",    0.f, 0.f, 200000000.f);
}

static void LoadConsumable(ModuleDef& m, const nlohmann::json& c) {
    m.consumable.healAmount = JL::Float(c, "healAmount", 50.f, 0.f, 100000.f);
}

// ── Registry init ─────────────────────────────────────────────────────────────

void ModuleRegistry::Init() {
    auto j = JL::LoadFile("config/modules.json");
    if (j.is_array() && !j.empty()) {
        s_all.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            ModuleDef m;
            m.id          = JL::Str(item, "id");
            m.displayName = JL::Str(item, "displayName");
            m.description = JL::Str(item, "description");
            m.type        = ParseModuleType(JL::Str(item, "type",  "Weapon"));
            m.grade       = ParseGrade     (JL::Str(item, "grade", "Common"));

            if (m.type == ModuleType::Weapon     && item.contains("weapon"))     LoadWeapon   (m, item["weapon"]);
            if (m.type == ModuleType::Shield     && item.contains("shield"))     LoadShield   (m, item["shield"]);
            if (m.type == ModuleType::Engine     && item.contains("engine"))     LoadEngine   (m, item["engine"]);
            if (m.type == ModuleType::Hyperdrive && item.contains("hyperdrive")) LoadHyperdrive(m, item["hyperdrive"]);
            if (m.type == ModuleType::Auxiliary  && item.contains("aux"))        LoadAux      (m, item["aux"]);
            if (m.type == ModuleType::Consumable && item.contains("consumable")) LoadConsumable(m, item["consumable"]);
            if (m.type == ModuleType::Armor      && item.contains("armor"))
                m.armor.hullBonus = JL::Float(item["armor"], "hullBonus", 50.f, 0.f, 10000.f);

            if (m.id.empty()) continue;
            s_all.push_back(std::move(m));
        }
        TraceLog(LOG_INFO, "ModuleRegistry: loaded %d modules from config/modules.json", (int)s_all.size());
    } else {
        TraceLog(LOG_WARNING, "ModuleRegistry: config/modules.json not found or invalid — using hardcoded defaults");
        s_all = {
            Weapon_PulseCannon_I(), Weapon_ArcCannon_I(), Weapon_ScatterRifle_I(),
            Weapon_Seeker_I(), Weapon_PlasmaCannon_I(), Weapon_VoidLancer_I(),
            Weapon_SingularityDriver(), Weapon_Chaingun(),
            Armor_HullPatch(), Armor_SteelPlating(), Armor_CompositeShell(),
            Armor_ReactiveHull(), Armor_PhaseArmor(), Armor_VoidPlate(), Armor_SingularitySkin(),
            Shield_KineticBarrier_I(), Shield_EnergyVeil_I(), Shield_HardenedBarrier(),
            Shield_PhaseVeil(), Shield_TitanShield(), Shield_NovaShroud(), Shield_AegisCore(),
            Engine_Thruster_I(), Engine_ImpulseDrive(), Engine_OverdriveCore(),
            Engine_IonThruster(), Engine_GraviticEngine(), Engine_QuantumDrive(), Engine_VoidEngine(),
            Hyperdrive_ShortJump(), Hyperdrive_SectorDrive(), Hyperdrive_WarpCore(),
            Hyperdrive_FoldEngine(), Hyperdrive_QuantumLeap(), Hyperdrive_VoidPiercer(), Hyperdrive_Singularity(),
            Hyperdrive_CosmicFold(),
            Aux_BasicScanner(), Aux_MaterialProbe(), Aux_EnhancedScanner(),
            Aux_EcmJammer(), Aux_StealthCore(), Aux_ReconSuite(), Aux_Omnisystem(),
            Aux_ProximityArray(), Aux_LongRangeArray(), Aux_DeepScanArray(),
            Aux_AstrometricSensor(), Aux_StellarCartographySuite(),
            Aux_GalacticSurveyArray(), Aux_OmniscientSensorCore(),
            Consumable_RepairKit_I(), Consumable_RepairKit_II(), Consumable_RepairKit_III(),
            Consumable_RepairKit_IV(), Consumable_RepairKit_V(), Consumable_RepairKit_VI(),
            Consumable_RepairKit_VII(),
        };
    }

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<ModuleDef>& ModuleRegistry::All() { return s_all; }

std::optional<ModuleDef> ModuleRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    if (it == s_byId.end()) return std::nullopt;
    return s_all[it->second];
}

std::vector<ModuleDef> ModuleRegistry::ByType(ModuleType type) {
    std::vector<ModuleDef> out;
    for (const ModuleDef& m : s_all)
        if (m.type == type) out.push_back(m);
    return out;
}

std::vector<ModuleDef> ModuleRegistry::ByTypeAndGrade(ModuleType type, ModuleGrade grade) {
    std::vector<ModuleDef> out;
    for (const ModuleDef& m : s_all)
        if (m.type == type && m.grade == grade) out.push_back(m);
    return out;
}

ModuleDef ModuleRegistry::Random(ModuleType type, ModuleGrade grade) {
    std::vector<ModuleDef> pool = ByTypeAndGrade(type, grade);
    if (pool.empty()) pool = ByType(type);
    if (pool.empty()) return s_all.front();
    return pool[GetRandomValue(0, (int)pool.size() - 1)];
}

ModuleDef ModuleRegistry::RandomDrop(ModuleGrade grade) {
    // Every equippable module type can drop — Consumable (repair kits) is
    // deliberately excluded, since those are a purchasable/usable item
    // category rather than a loadout slot module.
    static const ModuleType kDropTypes[] = {
        ModuleType::Weapon, ModuleType::Armor,    ModuleType::Shield,
        ModuleType::Engine, ModuleType::Hyperdrive, ModuleType::Auxiliary,
    };
    static constexpr int kDropTypeCount = sizeof(kDropTypes) / sizeof(kDropTypes[0]);
    ModuleType type = kDropTypes[GetRandomValue(0, kDropTypeCount - 1)];
    return Random(type, grade);
}

ModuleDef ModuleRegistry::RandomDrop() { return RandomDrop(RollGrade()); }

ModuleGrade ModuleRegistry::RollGrade() {
    int r = GetRandomValue(0, 99999);
    if (r < 60000) return ModuleGrade::Common;
    if (r < 90000) return ModuleGrade::Uncommon;
    if (r < 98000) return ModuleGrade::Unique;
    if (r < 99700) return ModuleGrade::Remarkable;
    if (r < 99970) return ModuleGrade::Epic;
    if (r < 99999) return ModuleGrade::Legendary;
    return ModuleGrade::Mythic;
}

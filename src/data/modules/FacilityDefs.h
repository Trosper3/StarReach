#pragma once
#include "core/Module.h"
#include <optional>
#include <string>

// Facility chips (docs/plans/unified_hardpoint_tasks.md P4) — one factory per
// FacilityKind, mirroring AuxDefs.h's pattern. A facility occupies a
// Facility-typed slot inside a Hardpoint; the hardpoint itself is the
// destructible mount (same shape as the pre-existing mining_drill aux-slot +
// Material Probe combo). powerDraw/powerOutput aren't wired into the P3
// budget yet (P4-T6); baseRate's per-kind meaning is filled in as each
// facility's actual gameplay function lands (mining/trading/etc. mostly
// already work via other systems today — see P4-T4's backward-compat note).

inline ModuleDef Facility_Reactor() {
    ModuleDef m;
    m.id = "facility_reactor"; m.displayName = "Reactor Core";
    m.description = "Primary power plant. Raises the station's power capacity.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Uncommon;
    m.facility.kind = FacilityKind::Reactor;
    m.facility.powerOutput = 20.0f;
    m.facility.priority = 4; // never worth shedding — it's the thing providing capacity
    m.facility.adjTag = AdjacencyTag::Reactor;
    return m;
}

inline ModuleDef Facility_TradingHub() {
    ModuleDef m;
    m.id = "facility_trading_hub"; m.displayName = "Trading Hub";
    m.description = "Commodities exchange. Lets visiting ships buy and sell cargo.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Common;
    m.facility.kind = FacilityKind::Trading;
    m.facility.powerDraw = 1.0f;
    m.facility.priority = 0;
    return m;
}

inline ModuleDef Facility_ManufacturingBay() {
    ModuleDef m;
    m.id = "facility_manufacturing_bay"; m.displayName = "Manufacturing Bay";
    m.description = "Converts raw materials into refined goods.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Uncommon;
    m.facility.kind = FacilityKind::Manufacturing;
    m.facility.powerDraw = 1.5f;
    m.facility.priority = 1;
    m.facility.adjTag = AdjacencyTag::Manufacturing;
    return m;
}

inline ModuleDef Facility_Shipyard() {
    ModuleDef m;
    m.id = "facility_shipyard"; m.displayName = "Shipyard";
    m.description = "Docking and construction bay. Lets the station build and launch ships.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Uncommon;
    m.facility.kind = FacilityKind::Shipyard;
    m.facility.powerDraw = 1.5f;
    m.facility.priority = 2;
    return m;
}

inline ModuleDef Facility_ContractBoard() {
    ModuleDef m;
    m.id = "facility_contract_board"; m.displayName = "Contract Board";
    m.description = "Posts courier and bounty contracts for passing pilots.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Common;
    m.facility.kind = FacilityKind::Contracting;
    m.facility.powerDraw = 0.5f;
    m.facility.priority = 0;
    return m;
}

inline ModuleDef Facility_RefuelStation() {
    ModuleDef m;
    m.id = "facility_refuel_station"; m.displayName = "Refuel Station";
    m.description = "Hyperdrive fuel depot. Tops off docked ships.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Common;
    m.facility.kind = FacilityKind::Refuel;
    m.facility.powerDraw = 1.0f;
    m.facility.priority = 1;
    return m;
}

inline ModuleDef Facility_RepairBay() {
    ModuleDef m;
    m.id = "facility_repair_bay"; m.displayName = "Repair Bay";
    m.description = "Nanite hull-repair gantry for docked ships.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Common;
    m.facility.kind = FacilityKind::Repair;
    m.facility.powerDraw = 1.5f;
    m.facility.priority = 2;
    return m;
}

inline ModuleDef Facility_ShieldGeneratorChip() {
    ModuleDef m;
    m.id = "facility_shield_generator"; m.displayName = "Shield Generator";
    m.description = "Projects a defensive shield envelope over nearby hardpoints.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Uncommon;
    m.facility.kind = FacilityKind::ShieldGenerator;
    m.facility.powerDraw = 2.0f;
    m.facility.priority = 3;
    m.facility.adjTag = AdjacencyTag::ShieldGenerator;
    return m;
}

inline ModuleDef Facility_WeaponBatteryChip() {
    ModuleDef m;
    m.id = "facility_weapon_battery"; m.displayName = "Weapon Battery Control";
    m.description = "Fire-control system coordinating this hardpoint's mounted weapons.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Common;
    m.facility.kind = FacilityKind::WeaponBattery;
    m.facility.powerDraw = 2.0f;
    m.facility.priority = 3;
    return m;
}

inline ModuleDef Facility_MiningRig() {
    ModuleDef m;
    m.id = "facility_mining_rig"; m.displayName = "Mining Rig";
    m.description = "Drill and ore-processing rig for automated asteroid extraction.";
    m.type = ModuleType::Facility; m.grade = ModuleGrade::Common;
    m.facility.kind = FacilityKind::Mining;
    m.facility.powerDraw = 1.0f;
    m.facility.priority = 1;
    m.facility.adjTag = AdjacencyTag::Mining;
    return m;
}

inline ModuleDef Facility_ForKind(FacilityKind kind) {
    switch (kind) {
        case FacilityKind::Reactor:         return Facility_Reactor();
        case FacilityKind::Trading:         return Facility_TradingHub();
        case FacilityKind::Manufacturing:   return Facility_ManufacturingBay();
        case FacilityKind::Shipyard:        return Facility_Shipyard();
        case FacilityKind::Contracting:     return Facility_ContractBoard();
        case FacilityKind::Refuel:          return Facility_RefuelStation();
        case FacilityKind::Repair:          return Facility_RepairBay();
        case FacilityKind::ShieldGenerator: return Facility_ShieldGeneratorChip();
        case FacilityKind::WeaponBattery:   return Facility_WeaponBatteryChip();
        case FacilityKind::Mining:          return Facility_MiningRig();
    }
    return Facility_TradingHub();
}

// P4-T4 backward-compat backfill: known legacy hardpoint IDs (from
// station_defs.json / station_types.json / buildables.json, all predating
// facilities) map to the facility kind that already matches their existing
// behavior, so old configs/saves keep working without edits.
inline std::optional<FacilityKind> LegacyHardpointFacilityKind(const std::string& hardpointId) {
    if (hardpointId == "mining_drill")  return FacilityKind::Mining;
    if (hardpointId == "trade_hub")     return FacilityKind::Trading;
    if (hardpointId == "docking_bay")   return FacilityKind::Shipyard;
    if (hardpointId == "def_battery" || hardpointId == "main_battery") return FacilityKind::WeaponBattery;
    if (hardpointId == "shield_array")  return FacilityKind::ShieldGenerator;
    return std::nullopt;
}

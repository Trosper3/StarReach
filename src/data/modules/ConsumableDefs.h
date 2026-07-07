#pragma once
#include "core/Module.h"

inline ModuleDef Consumable_RepairKit_I() {
    ModuleDef m;
    m.id = "repair_kit_i"; m.displayName = "Repair Kit Mk.I";
    m.description = "Emergency patch kit. Restores a modest amount of hull.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Common;
    m.consumable.healAmount = 40.0f;
    return m;
}

inline ModuleDef Consumable_RepairKit_II() {
    ModuleDef m;
    m.id = "repair_kit_ii"; m.displayName = "Repair Kit Mk.II";
    m.description = "Field-issue nanite patch. Restores a solid amount of hull.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Uncommon;
    m.consumable.healAmount = 100.0f;
    return m;
}

inline ModuleDef Consumable_RepairKit_III() {
    ModuleDef m;
    m.id = "repair_kit_iii"; m.displayName = "Repair Kit Mk.III";
    m.description = "Advanced auto-welding rig. Restores a significant amount of hull.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Unique;
    m.consumable.healAmount = 220.0f;
    return m;
}

inline ModuleDef Consumable_RepairKit_IV() {
    ModuleDef m;
    m.id = "repair_kit_iv"; m.displayName = "Repair Kit Mk.IV";
    m.description = "Military nanite reconstructor. Restores a large amount of hull.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Remarkable;
    m.consumable.healAmount = 450.0f;
    return m;
}

inline ModuleDef Consumable_RepairKit_V() {
    ModuleDef m;
    m.id = "repair_kit_v"; m.displayName = "Repair Kit Mk.V";
    m.description = "Experimental hull regenerator. Restores a huge amount of hull.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Epic;
    m.consumable.healAmount = 900.0f;
    return m;
}

inline ModuleDef Consumable_RepairKit_VI() {
    ModuleDef m;
    m.id = "repair_kit_vi"; m.displayName = "Repair Kit Mk.VI";
    m.description = "Self-replicating nanite swarm. Restores a massive amount of hull.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Legendary;
    m.consumable.healAmount = 1800.0f;
    return m;
}

inline ModuleDef Consumable_RepairKit_VII() {
    ModuleDef m;
    m.id = "repair_kit_vii"; m.displayName = "Singularity Reconstructor";
    m.description = "Reassembles hull matter at a subatomic level. Near-total restoration.";
    m.type = ModuleType::Consumable; m.grade = ModuleGrade::Mythic;
    m.consumable.healAmount = 4000.0f;
    return m;
}

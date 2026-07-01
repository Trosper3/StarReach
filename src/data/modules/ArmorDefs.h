#pragma once
#include "core/Module.h"

inline ModuleDef Armor_HullPatch() {
    ModuleDef m;
    m.id = "armor_hull_patch"; m.displayName = "Hull Patch";
    m.description = "Emergency hull plating. Provides minimal structural protection.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Common;
    m.armor.hullBonus = 25.0f;
    return m;
}

inline ModuleDef Armor_SteelPlating() {
    ModuleDef m;
    m.id = "armor_steel_plating"; m.displayName = "Steel Plating";
    m.description = "Reinforced alloy plating. A significant upgrade over salvaged material.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Uncommon;
    m.armor.hullBonus = 70.0f;
    return m;
}

inline ModuleDef Armor_CompositeShell() {
    ModuleDef m;
    m.id = "armor_composite_shell"; m.displayName = "Composite Shell";
    m.description = "Multi-layer composite hull bonded with impact-absorbing polymer.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Unique;
    m.armor.hullBonus = 160.0f;
    return m;
}

inline ModuleDef Armor_ReactiveHull() {
    ModuleDef m;
    m.id = "armor_reactive_hull"; m.displayName = "Reactive Hull";
    m.description = "Reactive plating that hardens on impact. Military specification.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Remarkable;
    m.armor.hullBonus = 340.0f;
    return m;
}

inline ModuleDef Armor_PhaseArmor() {
    ModuleDef m;
    m.id = "armor_phase"; m.displayName = "Phase Armor";
    m.description = "Partially phase-shifted hull lattice. Extreme structural integrity.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Epic;
    m.armor.hullBonus = 700.0f;
    return m;
}

inline ModuleDef Armor_VoidPlate() {
    ModuleDef m;
    m.id = "armor_void_plate"; m.displayName = "Void Plate";
    m.description = "Harvested from collapsed star material. Effectively indestructible.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Legendary;
    m.armor.hullBonus = 1500.0f;
    return m;
}

inline ModuleDef Armor_SingularitySkin() {
    ModuleDef m;
    m.id = "armor_singularity_skin"; m.displayName = "Singularity Skin";
    m.description = "Hull coated with a layer of compressed spacetime. Classified origin.";
    m.type = ModuleType::Armor; m.grade = ModuleGrade::Mythic;
    m.armor.hullBonus = 5000.0f;
    return m;
}

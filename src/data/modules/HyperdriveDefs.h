#pragma once
#include "core/Module.h"

inline ModuleDef Hyperdrive_ShortJump() {
    ModuleDef m;
    m.id = "hyperdrive_short_jump"; m.displayName = "Short-Jump Drive";
    m.description = "Rudimentary warp coil. Barely reaches nearby stations.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Common;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 3000.0f;
    return m;
}

inline ModuleDef Hyperdrive_SectorDrive() {
    ModuleDef m;
    m.id = "hyperdrive_sector"; m.displayName = "Sector Drive";
    m.description = "Standard hyperdrive. Reliable mid-range jump capability.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Uncommon;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 7500.0f;
    return m;
}

inline ModuleDef Hyperdrive_WarpCore() {
    ModuleDef m;
    m.id = "hyperdrive_warp_core"; m.displayName = "Warp Core";
    m.description = "Folded-space drive capable of reaching most stellar objects.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Unique;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 15000.0f;
    return m;
}

inline ModuleDef Hyperdrive_FoldEngine() {
    ModuleDef m;
    m.id = "hyperdrive_fold_engine"; m.displayName = "Fold Engine";
    m.description = "Military-grade spatial fold drive. Near-instantaneous sector crossing.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Remarkable;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 30000.0f;
    return m;
}

inline ModuleDef Hyperdrive_QuantumLeap() {
    ModuleDef m;
    m.id = "hyperdrive_quantum_leap"; m.displayName = "Quantum Leap Drive";
    m.description = "Quantum tunneling matrix. Traverses extreme distances effortlessly.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Epic;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 60000.0f;
    return m;
}

inline ModuleDef Hyperdrive_VoidPiercer() {
    ModuleDef m;
    m.id = "hyperdrive_void_piercer"; m.displayName = "Void Piercer";
    m.description = "Tears through the void itself. No known range limit.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Legendary;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 100000.0f;
    return m;
}

inline ModuleDef Hyperdrive_Singularity() {
    ModuleDef m;
    m.id = "hyperdrive_singularity"; m.displayName = "Singularity Core";
    m.description = "Reality is a suggestion. Instantaneous travel to any location.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Mythic;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 1000000.0f;
    return m;
}

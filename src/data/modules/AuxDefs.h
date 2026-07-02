#pragma once
#include "core/Module.h"

inline ModuleDef Aux_BasicScanner() {
    ModuleDef m;
    m.id = "aux_basic_scanner"; m.displayName = "Basic Scanner";
    m.description = "Short-range sensor suite. Detects nearby contacts and deposits.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Common;
    m.auxiliary.hasSensors = true; m.auxiliary.sensorRange = 600.0f;
    return m;
}

inline ModuleDef Aux_MaterialProbe() {
    ModuleDef m;
    m.id = "aux_material_probe"; m.displayName = "Material Probe";
    m.description = "Resonance scanner tuned to identify valuable asteroid compositions. "
                     "Installed on a mining station's drill hardpoint, its grade sets how "
                     "quickly the station harvests raw materials.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Uncommon;
    m.auxiliary.materialFindBonus = 0.30f;
    return m;
}

inline ModuleDef Aux_EnhancedScanner() {
    ModuleDef m;
    m.id = "aux_enhanced_scanner"; m.displayName = "Enhanced Scanner";
    m.description = "Long-range array with deep-scan mineral analysis capability.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Unique;
    m.auxiliary.hasSensors = true; m.auxiliary.sensorRange = 1500.0f;
    m.auxiliary.materialFindBonus = 0.15f;
    return m;
}

inline ModuleDef Aux_EcmJammer() {
    ModuleDef m;
    m.id = "aux_ecm_jammer"; m.displayName = "ECM Jammer";
    m.description = "Broadband electronic countermeasure. Disrupts incoming lock-on systems.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Remarkable;
    m.auxiliary.hasLockOnJammer = true;
    return m;
}

inline ModuleDef Aux_StealthCore() {
    ModuleDef m;
    m.id = "aux_stealth_core"; m.displayName = "Stealth Core";
    m.description = "Active emission suppressor. Renders the ship nearly undetectable.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Epic;
    m.auxiliary.hasCloaking = true;
    return m;
}

inline ModuleDef Aux_ReconSuite() {
    ModuleDef m;
    m.id = "aux_recon_suite"; m.displayName = "Recon Suite";
    m.description = "Full-spectrum sensor and countermeasure package. Military black budget.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Legendary;
    m.auxiliary.hasSensors = true; m.auxiliary.sensorRange = 2500.0f;
    m.auxiliary.materialFindBonus = 0.50f; m.auxiliary.hasLockOnJammer = true;
    return m;
}

inline ModuleDef Aux_Omnisystem() {
    ModuleDef m;
    m.id = "aux_omnisystem"; m.displayName = "Omnisystem";
    m.description = "Integrated intelligence module of pre-collapse manufacture. All systems nominal.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Mythic;
    m.auxiliary.hasSensors = true; m.auxiliary.sensorRange = 5000.0f;
    m.auxiliary.materialFindBonus = 1.00f; m.auxiliary.hasLockOnJammer = true;
    m.auxiliary.hasCloaking = true;
    return m;
}

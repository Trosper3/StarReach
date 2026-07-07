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

// Sensor Array line: galaxy-map fog-of-war reveal range, distinct from the
// Scanner line's combat sensorRange above (see AuxStats::mapSensorRange).
// Deliberately its own aux item rather than a stat bolted onto Scanner —
// ships only have 1 aux slot, so equipping for exploration vs. combat
// awareness is a real choice. No baseline: with none of these equipped, a
// ship sees nothing undiscovered beyond its home system on the galaxy map.
inline ModuleDef Aux_ProximityArray() {
    ModuleDef m;
    m.id = "aux_proximity_array"; m.displayName = "Proximity Array";
    m.description = "Short-range gravimetric mapper. Reveals the nearest neighboring systems.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Common;
    m.auxiliary.mapSensorRange = 20000.0f;
    return m;
}

inline ModuleDef Aux_LongRangeArray() {
    ModuleDef m;
    m.id = "aux_long_range_array"; m.displayName = "Long-Range Array";
    m.description = "Extended gravimetric mapper. Reliably spans the gap to adjacent systems.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Uncommon;
    m.auxiliary.mapSensorRange = 60000.0f;
    return m;
}

inline ModuleDef Aux_DeepScanArray() {
    ModuleDef m;
    m.id = "aux_deep_scan_array"; m.displayName = "Deep Scan Array";
    m.description = "Multi-band deep-space telescope. Maps a wide local neighborhood of systems.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Unique;
    m.auxiliary.mapSensorRange = 300000.0f;
    return m;
}

inline ModuleDef Aux_AstrometricSensor() {
    ModuleDef m;
    m.id = "aux_astrometric_sensor"; m.displayName = "Astrometric Sensor";
    m.description = "Precision astrometric suite. Charts entire stellar neighborhoods at once.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Remarkable;
    m.auxiliary.mapSensorRange = 2000000.0f;
    return m;
}

inline ModuleDef Aux_StellarCartographySuite() {
    ModuleDef m;
    m.id = "aux_stellar_cartography_suite"; m.displayName = "Stellar Cartography Suite";
    m.description = "Military-grade cartography array. Maps deep into the galaxy's structure.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Epic;
    m.auxiliary.mapSensorRange = 10000000.0f;
    return m;
}

inline ModuleDef Aux_GalacticSurveyArray() {
    ModuleDef m;
    m.id = "aux_galactic_survey_array"; m.displayName = "Galactic Survey Array";
    m.description = "Full-spectrum survey platform. Reveals the sweep of the galaxy around you.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Legendary;
    m.auxiliary.mapSensorRange = 40000000.0f;
    return m;
}

inline ModuleDef Aux_OmniscientSensorCore() {
    ModuleDef m;
    m.id = "aux_omniscient_sensor_core"; m.displayName = "Omniscient Sensor Core";
    m.description = "Reality is legible. Reveals every system in the galaxy from wherever you stand.";
    m.type = ModuleType::Auxiliary; m.grade = ModuleGrade::Mythic;
    m.auxiliary.mapSensorRange = 150000000.0f;
    return m;
}

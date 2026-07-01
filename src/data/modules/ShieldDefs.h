#pragma once
#include "core/Module.h"

inline ModuleDef Shield_KineticBarrier_I() {
    ModuleDef m;
    m.id = "shield_kinetic_barrier_i"; m.displayName = "Kinetic Barrier I";
    m.description = "Basic kinetic shield emitter. Absorbs physical impact.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Common;
    m.shield.shieldType = ShieldType::Kinetic;
    m.shield.capacity = 80.0f; m.shield.rechargeRate = 8.0f;
    m.shield.rechargeDelay = 4.0f;
    return m;
}

inline ModuleDef Shield_EnergyVeil_I() {
    ModuleDef m;
    m.id = "shield_energy_veil_i"; m.displayName = "Energy Veil I";
    m.description = "Low-power energy field that deflects directed energy weapons.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Uncommon;
    m.shield.shieldType = ShieldType::Energy;
    m.shield.capacity = 65.0f; m.shield.rechargeRate = 7.0f;
    m.shield.rechargeDelay = 3.5f;
    return m;
}

inline ModuleDef Shield_HardenedBarrier() {
    ModuleDef m;
    m.id = "shield_hardened_barrier"; m.displayName = "Hardened Barrier";
    m.description = "Dense kinetic projector with reinforced emitter coils.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Unique;
    m.shield.shieldType = ShieldType::Kinetic;
    m.shield.capacity = 200.0f; m.shield.rechargeRate = 18.0f;
    m.shield.rechargeDelay = 3.0f;
    return m;
}

inline ModuleDef Shield_PhaseVeil() {
    ModuleDef m;
    m.id = "shield_phase_veil"; m.displayName = "Phase Veil";
    m.description = "Resonant energy field tuned to deflect high-frequency weapon fire.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Remarkable;
    m.shield.shieldType = ShieldType::Energy;
    m.shield.capacity = 170.0f; m.shield.rechargeRate = 15.0f;
    m.shield.rechargeDelay = 2.5f;
    return m;
}

inline ModuleDef Shield_TitanShield() {
    ModuleDef m;
    m.id = "shield_titan"; m.displayName = "Titan Shield";
    m.description = "Heavy-grade kinetic barrier rated for capital ship engagements.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Epic;
    m.shield.shieldType = ShieldType::Kinetic;
    m.shield.capacity = 450.0f; m.shield.rechargeRate = 35.0f;
    m.shield.rechargeDelay = 2.0f;
    return m;
}

inline ModuleDef Shield_NovaShroud() {
    ModuleDef m;
    m.id = "shield_nova_shroud"; m.displayName = "Nova Shroud";
    m.description = "Adaptive energy field draws power from incoming fire to recharge.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Legendary;
    m.shield.shieldType = ShieldType::Energy;
    m.shield.capacity = 380.0f; m.shield.rechargeRate = 30.0f;
    m.shield.rechargeDelay = 1.8f;
    return m;
}

inline ModuleDef Shield_AegisCore() {
    ModuleDef m;
    m.id = "shield_aegis_core"; m.displayName = "Aegis Core";
    m.description = "Collapsed-field emitter of unknown manufacture. Nearly impenetrable.";
    m.type = ModuleType::Shield; m.grade = ModuleGrade::Mythic;
    m.shield.shieldType = ShieldType::Kinetic;
    m.shield.capacity = 1000.0f; m.shield.rechargeRate = 80.0f;
    m.shield.rechargeDelay = 1.2f;
    return m;
}

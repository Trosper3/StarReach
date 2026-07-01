#pragma once
#include "core/Module.h"

inline ModuleDef Weapon_PulseCannon_I() {
    ModuleDef m;
    m.id = "pulse_cannon_i"; m.displayName = "Pulse Cannon I";
    m.description = "Standard kinetic repeater. Reliable but unspectacular.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Common;
    m.weapon.damageType = DamageType::Kinetic;
    m.weapon.damage = 15.0f; m.weapon.fireRate = 1.0f;
    m.weapon.projSpeed = 700.0f; m.weapon.projRange = 1000.0f;
    return m;
}

inline ModuleDef Weapon_ArcCannon_I() {
    ModuleDef m;
    m.id = "arc_cannon_i"; m.displayName = "Arc Cannon I";
    m.description = "Charged energy weapon that releases a spread burst on discharge.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Uncommon;
    m.weapon.damageType = DamageType::Energy;
    m.weapon.fireMode = WeaponFireMode::Charge;
    m.weapon.projType = WeaponProjType::Spread;
    m.weapon.damage = 22.0f; m.weapon.fireRate = 1.4f;
    m.weapon.projSpeed = 750.0f; m.weapon.projRange = 1100.0f;
    m.weapon.chargeTime = 1.5f; m.weapon.burstCount = 5;
    m.weapon.spreadAngle = 40.0f;
    return m;
}

inline ModuleDef Weapon_ScatterRifle_I() {
    ModuleDef m;
    m.id = "scatter_rifle_i"; m.displayName = "Scatter Rifle I";
    m.description = "Fires a burst of kinetic slugs in a tight cone. High close-range output.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Unique;
    m.weapon.damageType = DamageType::Kinetic;
    m.weapon.projType = WeaponProjType::Burst;
    m.weapon.damage = 30.0f; m.weapon.fireRate = 1.2f;
    m.weapon.projSpeed = 680.0f; m.weapon.projRange = 950.0f;
    m.weapon.burstCount = 3; m.weapon.spreadAngle = 18.0f;
    return m;
}

inline ModuleDef Weapon_Seeker_I() {
    ModuleDef m;
    m.id = "seeker_i"; m.displayName = "Seeker I";
    m.description = "Lock-on missile system. Tracks targets through evasive maneuvers.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Remarkable;
    m.weapon.damageType = DamageType::Kinetic;
    m.weapon.fireMode = WeaponFireMode::LockOn;
    m.weapon.projType = WeaponProjType::Seeking;
    m.weapon.damage = 48.0f; m.weapon.fireRate = 2.5f;
    m.weapon.projSpeed = 580.0f; m.weapon.projRange = 1600.0f;
    return m;
}

inline ModuleDef Weapon_PlasmaCannon_I() {
    ModuleDef m;
    m.id = "plasma_cannon_i"; m.displayName = "Plasma Cannon I";
    m.description = "High-output plasma accelerator. Devastating against energy shields.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Epic;
    m.weapon.damageType = DamageType::Energy;
    m.weapon.fireMode = WeaponFireMode::Charge;
    m.weapon.damage = 90.0f; m.weapon.fireRate = 2.0f;
    m.weapon.projSpeed = 850.0f; m.weapon.projRange = 1400.0f;
    m.weapon.chargeTime = 0.8f;
    return m;
}

inline ModuleDef Weapon_VoidLancer_I() {
    ModuleDef m;
    m.id = "void_lancer_i"; m.displayName = "Void Lancer I";
    m.description = "Experimental kinetic rail accelerated to near-relativistic velocity.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Legendary;
    m.weapon.damageType = DamageType::Kinetic;
    m.weapon.fireMode = WeaponFireMode::LockOn;
    m.weapon.projType = WeaponProjType::Seeking;
    m.weapon.damage = 200.0f; m.weapon.fireRate = 1.8f;
    m.weapon.projSpeed = 1100.0f; m.weapon.projRange = 2400.0f;
    return m;
}

inline ModuleDef Weapon_SingularityDriver() {
    ModuleDef m;
    m.id = "singularity_driver"; m.displayName = "Singularity Driver";
    m.description = "Theoretical weapon. Fires a collapsed energy point. Origin unknown.";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Mythic;
    m.weapon.damageType = DamageType::Energy;
    m.weapon.fireMode = WeaponFireMode::LockOn;
    m.weapon.projType = WeaponProjType::Seeking;
    m.weapon.damage = 500.0f; m.weapon.fireRate = 1.4f;
    m.weapon.projSpeed = 1600.0f; m.weapon.projRange = 3200.0f;
    return m;
}

inline ModuleDef Weapon_Chaingun() {
    ModuleDef m;
    m.id = "chaingun"; m.displayName = "Chaingun";
    m.description = "Rapid firing kinetic weapon. Spray and pray!";
    m.type = ModuleType::Weapon; m.grade = ModuleGrade::Common;
    m.weapon.damageType = DamageType::Kinetic;
    m.weapon.damage = 1.0f; m.weapon.fireRate = 0.05f;
    m.weapon.projSpeed = 1000.0f; m.weapon.projRange = 2000.0f;
    return m;
}

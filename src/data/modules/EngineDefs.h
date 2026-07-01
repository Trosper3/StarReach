#pragma once
#include "core/Module.h"

inline ModuleDef Engine_Thruster_I() {
    ModuleDef m;
    m.id = "engine_thruster_i"; m.displayName = "Thruster Mk.I";
    m.description = "Salvaged thruster unit. Barely functional but gets you moving.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Common;
    m.engine.thrustBonus = 220.0f; m.engine.turnSpeedBonus = 90.0f;
    return m;
}

inline ModuleDef Engine_ImpulseDrive() {
    ModuleDef m;
    m.id = "engine_impulse_drive"; m.displayName = "Impulse Drive";
    m.description = "Compact ion drive with improved throttle response.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Uncommon;
    m.engine.thrustBonus = 340.0f; m.engine.turnSpeedBonus = 135.0f;
    return m;
}

inline ModuleDef Engine_OverdriveCore() {
    ModuleDef m;
    m.id = "engine_overdrive_core"; m.displayName = "Overdrive Core";
    m.description = "Pushes thrust output well beyond rated safety margins.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Unique;
    m.engine.thrustBonus = 500.0f; m.engine.turnSpeedBonus = 190.0f;
    return m;
}

inline ModuleDef Engine_IonThruster() {
    ModuleDef m;
    m.id = "engine_ion_thruster"; m.displayName = "Ion Thruster";
    m.description = "Military-grade ion thruster with precision vectored nozzles.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Remarkable;
    m.engine.thrustBonus = 720.0f; m.engine.turnSpeedBonus = 260.0f;
    return m;
}

inline ModuleDef Engine_GraviticEngine() {
    ModuleDef m;
    m.id = "engine_gravitic"; m.displayName = "Gravitic Engine";
    m.description = "Bends local gravity to generate thrust. Near-instant direction change.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Epic;
    m.engine.thrustBonus = 1050.0f; m.engine.turnSpeedBonus = 370.0f;
    return m;
}

inline ModuleDef Engine_QuantumDrive() {
    ModuleDef m;
    m.id = "engine_quantum_drive"; m.displayName = "Quantum Drive";
    m.description = "Exploits quantum tunneling to achieve extraordinary acceleration.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Legendary;
    m.engine.thrustBonus = 1550.0f; m.engine.turnSpeedBonus = 520.0f;
    return m;
}

inline ModuleDef Engine_VoidEngine() {
    ModuleDef m;
    m.id = "engine_void"; m.displayName = "Void Engine";
    m.description = "Draws propulsion from the fabric of space itself. Origin unknown.";
    m.type = ModuleType::Engine; m.grade = ModuleGrade::Mythic;
    m.engine.thrustBonus = 3000.0f; m.engine.turnSpeedBonus = 750.0f;
    return m;
}

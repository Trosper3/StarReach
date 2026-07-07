#pragma once
#include "core/Module.h"

// Range tiers follow rarity grade one-for-one, scaled to this galaxy's actual
// geometry (see StarSystemRegistry.h / UniverseRegistry.h):
//   Common     — in-system only (planets/stations sit within a few thousand u)
//   Uncommon   — the closest neighboring star systems (~46,000u nominal spacing,
//                worst-case-closest ~23,000u)
//   Unique/Remarkable/Epic — progressively deeper into the local galaxy
//                (kGalaxySpan = 80,000,000u across)
//   Legendary  — crosses into neighboring galaxies (~40,000,000u nominal
//                spacing, worst-case-closest ~20,000,000u — see
//                UniverseRegistry::kUniverseSpan)
//   Mythic     — anywhere in the universe (kUniverseSpan = 4,000,000,000u,
//                so full diagonal reach needs comfortably more than that)

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
    m.description = "Standard hyperdrive. Reaches the closest neighboring star systems.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Uncommon;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 50000.0f;
    return m;
}

inline ModuleDef Hyperdrive_WarpCore() {
    ModuleDef m;
    m.id = "hyperdrive_warp_core"; m.displayName = "Warp Core";
    m.description = "Folded-space drive capable of reaching most stellar objects.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Unique;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 300000.0f;
    return m;
}

inline ModuleDef Hyperdrive_FoldEngine() {
    ModuleDef m;
    m.id = "hyperdrive_fold_engine"; m.displayName = "Fold Engine";
    m.description = "Military-grade spatial fold drive. Near-instantaneous sector crossing.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Remarkable;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 2000000.0f;
    return m;
}

inline ModuleDef Hyperdrive_QuantumLeap() {
    ModuleDef m;
    m.id = "hyperdrive_quantum_leap"; m.displayName = "Quantum Leap Drive";
    m.description = "Quantum tunneling matrix. Traverses extreme distances effortlessly.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Epic;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 10000000.0f;
    return m;
}

// Intergalactic tier: reaches beyond a single galaxy's ~80,000,000u span into
// nearby galaxies (see UniverseRegistry::kUniverseSpan / GalaxyMap's Universe
// tier). Nearest-neighbor galaxy spacing is on the order of tens of millions
// of units, so this puts the closest one or two galaxies just within reach.
//
// Why this is safe without sensors: every hyperdrive only ever jumps to a
// specifically targeted, already-known point — a system you've discovered
// yourself, or (for cross-galaxy jumps) a galaxy's universally-charted home
// beacon (every galaxy's id==1 system; see UniverseRegistry/GalaxyMap's
// Universe tier, which shows all galaxies regardless of sensor range for
// exactly this reason). A drive powerful enough to fold space across
// galaxies comes with those charts pre-loaded — what it can't do is blind-
// jump to somewhere nobody has ever scanned, which is why finding a brand
// new star system needs sensor range in addition to hyperdrive range (see
// GalaxyMap's fog-of-war gating).
inline ModuleDef Hyperdrive_VoidPiercer() {
    ModuleDef m;
    m.id = "hyperdrive_void_piercer"; m.displayName = "Void Piercer";
    m.description = "Tears through the void itself, navigating by charted beacons to reach the nearest neighboring galaxies.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Legendary;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 50000000.0f;
    return m;
}

inline ModuleDef Hyperdrive_Singularity() {
    ModuleDef m;
    m.id = "hyperdrive_singularity"; m.displayName = "Singularity Core";
    m.description = "Reality is a suggestion. Instantaneous travel to any charted location in the universe.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Mythic;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 6000000000.0f;
    return m;
}

inline ModuleDef Hyperdrive_CosmicFold() {
    ModuleDef m;
    m.id = "hyperdrive_cosmic_fold"; m.displayName = "Cosmic Fold Drive";
    m.description = "Folds space across galaxies, riding pre-loaded charts from beacon to beacon. The stars themselves become stepping stones.";
    m.type = ModuleType::Hyperdrive; m.grade = ModuleGrade::Mythic;
    m.engine.isHyperdrive = true;
    m.engine.hyperdriveRange = 6000000000.0f;
    return m;
}

#pragma once
#include "core/Module.h"
#include "items/Item.h"

// Enforces stat ceiling rules per grade and merge status.
//   Found items   → 1.0  (full tier potential)
//   Merged items  → 0.9  (90% of tier baseline — kMergedCap)
//   Mythic items  → primary stat grafts blocked; secondary efficiency perks only
class GradeRegistry {
public:
    // Stat ceiling fraction for the given grade + merge combination.
    static float GetStatCap(ModuleGrade grade, bool isMerged);

    // Returns false for Mythic: primary stat grafts (hull/shield/thrust/damage) are locked.
    // Secondary efficiency perks (thermal, energy, reload speed) remain graftable.
    static bool  AllowsPrimaryGraft(ModuleGrade grade);

    // Writes item.baseStatCap from its current grade and isMerged flag.
    // Call after setting isMerged = true or upgrading grade.
    static void  Apply(Item& item);

    static constexpr float kMergedCap = 0.9f;
};

#pragma once
#include "core/Module.h"
#include "data/ItemDef.h"
#include <string>
#include <vector>

// Loads per-grade cost multipliers from config/progression/MaterialCosts.json.
// Call Init() once at startup (after other registries). If the file is missing,
// built-in defaults are used so the game always runs.
//
// Usage in EngineerService:
//   auto costs = ProgressionRegistry::ScaledCost(attr.graftCost, item.grade);
class ProgressionRegistry {
public:
    static void Init(const std::string& jsonPath =
                         "config/progression/MaterialCosts.json");

    // Grade multiplier for this tier (1.0 for Common, up to 13.0 for Mythic by default).
    static float GetMultiplier(ModuleGrade grade);

    // Returns baseCost with each ingredient amount multiplied by the grade factor.
    // Amounts are ceiling'd and clamped to a minimum of 1.
    static std::vector<CraftIngredient> ScaledCost(
        const std::vector<CraftIngredient>& baseCost,
        ModuleGrade                         grade);

private:
    // Indexed by static_cast<int>(ModuleGrade): Common=0 … Mythic=6.
    static float s_multipliers[7];
};

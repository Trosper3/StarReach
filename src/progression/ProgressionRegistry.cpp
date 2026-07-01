#include "progression/ProgressionRegistry.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>

// Defaults mirror the JSON values — fallback if the file is missing.
float ProgressionRegistry::s_multipliers[7] = {
    1.0f,   // Common
    1.5f,   // Uncommon
    2.25f,  // Unique
    3.5f,   // Remarkable
    5.5f,   // Epic
    8.5f,   // Legendary
    13.0f,  // Mythic
};

static const char* kGradeNames[7] = {
    "Common", "Uncommon", "Unique", "Remarkable", "Epic", "Legendary", "Mythic"
};

void ProgressionRegistry::Init(const std::string& jsonPath) {
    std::ifstream f(jsonPath);
    if (!f.is_open()) return; // keep defaults

    auto j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return;

    if (!j.contains("gradeMultipliers") || !j["gradeMultipliers"].is_object()) return;
    const auto& gm = j["gradeMultipliers"];

    for (int i = 0; i < 7; ++i) {
        if (gm.contains(kGradeNames[i]) && gm[kGradeNames[i]].is_number())
            s_multipliers[i] = gm[kGradeNames[i]].get<float>();
    }
}

float ProgressionRegistry::GetMultiplier(ModuleGrade grade) {
    int idx = static_cast<int>(grade);
    if (idx < 0 || idx >= 7) return 1.0f;
    return s_multipliers[idx];
}

std::vector<CraftIngredient> ProgressionRegistry::ScaledCost(
    const std::vector<CraftIngredient>& baseCost,
    ModuleGrade                         grade)
{
    float mult = GetMultiplier(grade);
    std::vector<CraftIngredient> scaled = baseCost;
    for (auto& ing : scaled)
        ing.amount = std::max(1, static_cast<int>(std::ceil(ing.amount * mult)));
    return scaled;
}

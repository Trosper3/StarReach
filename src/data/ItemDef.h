#pragma once
#include <string>
#include <vector>

struct CraftIngredient {
    std::string materialId;
    int         amount;
};

struct ItemDef {
    std::string id;
    std::string displayName;
    std::string description;
    std::vector<CraftIngredient> craftCost;
};

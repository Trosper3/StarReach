#pragma once
#include "data/ItemDef.h"
#include <string>
#include <unordered_map>
#include <vector>

// A graftable trait that can be applied to an Item via the engineer service.
struct AttributeDef {
    std::string                  id;
    std::string                  displayName;
    bool                         isPrimary;    // true = AttributeSet stat (hull/shield/thrust/dmg)
                                               // false = secondary efficiency perk (thermal/energy/reload)
    float                        rarityScale;  // bonus multiplier added per grade tier above Common
    std::vector<CraftIngredient> graftCost;    // base material cost consumed during grafting
};

// Catalog of all graftable attributes — primary stats and secondary efficiency perks.
// GradeRegistry::AllowsPrimaryGraft() must be checked before grafting isPrimary attributes
// onto Mythic items.
class AttributeRegistry {
public:
    static void                          Init();
    static const std::vector<AttributeDef>& All();
    static const AttributeDef*           ById(const std::string& id);

private:
    static std::vector<AttributeDef>                   s_all;
    static std::unordered_map<std::string, size_t>     s_byId;
};

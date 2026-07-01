#pragma once
#include "core/Module.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Runtime instance of a craftable/graftable item.
// defId links to the static definition in ModuleDef/ItemDef registries.
// Crafting metadata (isMerged, graftedAttributes, baseStatCap) is instance-owned.
struct Item {
    std::string              defId;               // key into ModuleDef or ItemDef registry
    ModuleGrade              grade       = ModuleGrade::Common;
    bool                     isMerged    = false; // true when created via attribute grafting
    std::vector<std::string> graftedAttributes;   // attribute IDs applied through grafting
    float                    baseStatCap = 1.0f;  // stat ceiling: 1.0 = found, 0.9 = merged
    std::vector<std::string> lineage;             // origin strings appended on each graft event

    nlohmann::json Serialize()   const;
    static Item    Deserialize(const nlohmann::json& j);
};

#pragma once
#include "items/Item.h"
#include <optional>
#include <string>
#include <vector>

// A serialized snapshot of a crafted item's attribute configuration.
// Extracted from a perfected item; can be re-applied to fresh items for mass-production.
struct Blueprint {
    std::string              name;               // filename stem — used as the blueprint ID
    std::string              defId;              // source item's defId
    std::string              grade;              // human-readable grade name (e.g. "Epic")
    bool                     isMerged   = false;
    float                    baseStatCap = 1.0f;
    std::vector<std::string> graftedAttributes;  // ordered list of attribute IDs
    std::vector<std::string> lineage;            // origin strings from the source item
};

// Saves and loads item attribute blueprints to/from config/blueprints/<name>.json.
// Extract() → persists a crafted item's configuration.
// Load()    → retrieves a saved blueprint by name.
// ListAll() → returns all saved blueprint names (filename stems).
// Apply()   → stamps a target Item with the blueprint's attributes (no material cost).
class BlueprintManager {
public:
    static constexpr const char* kBlueprintDir = "config/blueprints";

    // Serializes item to kBlueprintDir/<name>.json.
    // Creates the directory if it doesn't exist.
    // Returns false if the write fails.
    static bool Extract(const Item& item, const std::string& name);

    // Loads a blueprint by filename stem. Returns nullopt if missing or malformed.
    static std::optional<Blueprint> Load(const std::string& name);

    // Returns filename stems of all .json files in kBlueprintDir.
    static std::vector<std::string> ListAll();

    // Applies blueprint's graftedAttributes to item: sets isMerged, appends attrs,
    // sets baseStatCap from blueprint, appends lineage entries.
    // Returns the number of attributes applied.
    static int Apply(const Blueprint& bp, Item& item);
};

#pragma once
#include "shared/entities/LoadoutComponent.h"
#include <string>
#include <vector>

// One harvestable data unit extracted from a destroyed entity's loadout slot.
struct AttributeToken {
    std::string moduleDefId;  // source ModuleDef::id (empty if slot had no definition)
    std::string attributeId;  // canonical AttributeRegistry id for this module type
    int         count = 1;
};

// Converts destroyed ships' LoadoutComponents into AttributeTokens.
// DamageSystem calls RecordKill() on death; game loop drains via DrainPending().
class SalvageManager {
public:
    // Returns one AttributeToken per filled, recognisable slot on loadout.
    static std::vector<AttributeToken> Harvest(const LoadoutComponent& loadout);

    // Appends Harvest() result to the static pending list. Called by DamageSystem.
    static void                        RecordKill(const LoadoutComponent& loadout);

    // Moves all pending tokens out. Game loop calls this each frame to award to player.
    static std::vector<AttributeToken> DrainPending();

private:
    static std::string AttributeForType(ModuleType type);
    static std::vector<AttributeToken> s_pending;
};

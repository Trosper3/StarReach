#pragma once
#include "systems/diplomacy/DiplomaticRegistry.h"
#include <cstdint>

// Bitmask of behaviors an NPC exhibits based on diplomatic standing.
enum class Behavior : uint8_t {
    None          = 0,
    Escort        = 1 << 0,   // follow & protect; share sensor telemetry
    OpenComms     = 1 << 1,   // hail + exchange data
    Patrol        = 1 << 2,   // hold distance, weapons cold
    Transactional = 1 << 3,   // allow passage or demand toll
    Aggressive    = 1 << 4,   // fire at will, pursuit enabled
    Jamming       = 1 << 5,   // electronic jamming + demand surrender
};

using BehaviorSet = uint8_t;

inline BehaviorSet operator|(Behavior a, Behavior b) {
    return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}
inline bool Has(BehaviorSet set, Behavior b) {
    return (set & static_cast<uint8_t>(b)) != 0;
}

// Maps Relation → BehaviorSet. Constant-time lookup.
// Friendly → Escort | OpenComms
// Neutral  → Patrol | Transactional
// Hostile  → Aggressive | Jamming
class BehaviorRegistry {
public:
    static BehaviorSet Get(Relation rel);
};

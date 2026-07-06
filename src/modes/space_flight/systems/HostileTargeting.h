#pragma once
#include "raylib.h"
#include "core/FactionEnum.h"
#include "core/PlayerStation.h" // HardpointState
#include <vector>

struct SystemWorld;
namespace ecs { struct Entity; }

// Shared "who do I shoot" behavior for any hostile-capable combatant.
// Space stations are the first adopter, but ships, turrets, and any future
// unit that needs to pick a target and respect the hardpoints-before-core
// rule should call into this instead of hand-rolling their own scan.
namespace combat {

enum class HostileTargetKind : unsigned char { None, Npc, Player, Station };

struct HostileTarget {
    HostileTargetKind kind     = HostileTargetKind::None;
    unsigned int      id       = 0; // npcMeta id / station id; unused for Player
    Vector2           position = {};
    bool              valid    = false;
};

// Nearest combatant hostile to `selfFaction` within `maxRange` of `selfPos`.
// Scans NPC ships, the player, and other space stations in `world`.
// `selfStationId` (0 = n/a) excludes a station from targeting itself.
HostileTarget FindNearestHostileTarget(const SystemWorld&  world,
                                       const ecs::Entity&  playerEntity,
                                       Faction             playerFaction,
                                       Faction             selfFaction,
                                       Vector2             selfPos,
                                       float               maxRange,
                                       unsigned int         selfStationId = 0);

// True while any non-core hardpoint is still alive — per the shared rule,
// the core cannot take damage until every outer hardpoint has been destroyed.
bool CoreIsProtected(const std::vector<HardpointState>& hardpoints);

} // namespace combat

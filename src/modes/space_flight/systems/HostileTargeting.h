#pragma once
#include "raylib.h"
#include "core/FactionEnum.h"
#include "core/PlayerStation.h" // Hardpoint
#include <vector>

struct SystemWorld;
namespace ecs { struct Entity; }

// Shared "who do I shoot" behavior for any hostile-capable combatant.
// Space stations are the first adopter, but ships, turrets, and any future
// unit that needs to pick a target should call into this instead of
// hand-rolling their own scan.
namespace combat {

enum class HostileTargetKind : unsigned char { None, Npc, Player, Station };

struct HostileTarget {
    HostileTargetKind kind     = HostileTargetKind::None;
    unsigned int      id       = 0; // npcMeta id / station id; unused for Player
    Vector2           position = {};
    bool              valid    = false;
};

// Nearest combatant hostile to `selfFaction` within `maxRange` of `selfPos`.
// Scans NPC ships, the player, and other space stations in `world`. Whether
// the player counts as hostile is resolved via ReputationRegistry (Epic 6.1)
// rather than a passed-in player faction, so this template automatically
// tracks the player's live standing.
// `selfStationId` (0 = n/a) excludes a station from targeting itself.
// `playerTargetable` (default true) lets a caller exclude the player from
// consideration entirely — e.g. while docked inside a station menu, where
// the player's ship is frozen/invisible and shouldn't be chased or aimed at.
HostileTarget FindNearestHostileTarget(const SystemWorld&  world,
                                       const ecs::Entity&  playerEntity,
                                       Faction             selfFaction,
                                       Vector2             selfPos,
                                       float               maxRange,
                                       unsigned int         selfStationId = 0,
                                       bool                playerTargetable = true);

// True once every hardpoint is destroyed (or the list is empty) — the
// object dies. No hardpoint, including the isCore one, is ever invulnerable.
bool AllHardpointsDestroyed(const std::vector<Hardpoint>& hardpoints);

// Epic 9.1 (capture): true when every weapon-capable hardpoint (wSlots > 0)
// is dead — the object can no longer fight back, regardless of whether its
// core, engine, shield, or non-combat utility hardpoints (docking bay,
// trade hub, mining drill) survive. Deliberately NOT "every non-core
// hardpoint dead" — that would require destroying a station's own docking
// bay to make it capturable, defeating the point of capturing it. Only
// meaningful when combat::AllHardpointsDestroyed is false (object still alive).
bool IsDisabled(const std::vector<Hardpoint>& hardpoints);

} // namespace combat

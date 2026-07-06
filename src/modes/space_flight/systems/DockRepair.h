#pragma once
#include "raylib.h"
#include "core/FactionEnum.h"

struct SystemWorld;

// Shared "where do I go to heal" behavior — the retreat-and-repair
// counterpart to combat::FindNearestHostileTarget (HostileTargeting.h).
// Any ship that needs to retreat to a friendly station calls into this
// instead of hand-rolling its own station scan.
namespace repair {

// Fraction of max hull restored per second while docked (~6.7s full heal).
constexpr float kDockHealPerSecond = 0.15f;

// Distance from a docking-bay hardpoint at which a ship is considered
// "arrived" and can stop and begin healing.
constexpr float kDockRadius = 50.0f;

struct FriendlyDock {
    bool         valid          = false;
    unsigned int stationId      = 0;
    int          hardpointIndex = -1;
    Vector2      position       = {};
};

// Nearest station not hostile to `selfFaction` that still has a living
// docking-bay hardpoint. Only world/NPC stations (`world.stations`) are
// considered — player-built stations never have a docking-bay hardpoint.
FriendlyDock FindNearestFriendlyDock(const SystemWorld& world,
                                     Faction             selfFaction,
                                     Vector2             selfPos);

} // namespace repair

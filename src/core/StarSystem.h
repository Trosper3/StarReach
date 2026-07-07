#pragma once
#include "raylib.h"
#include "core/FactionEnum.h"
#include <string>

struct StarSystem {
    unsigned int id          = 0;
    std::string  name;
    unsigned int seed        = 0;
    Vector2      galacticPos = {}; // includes intra-cell jitter (see Generate())
    // Un-jittered lattice cell center. Prefer this over galacticPos when
    // laying out fixed-size tiles (e.g. the galactic map's widest zoom) —
    // jitter is up to +/-25% of a *single* cell, which barely matters next to
    // a tile spanning many cells, but becomes a large fraction of a tile's
    // own size once that tile shrinks to a single cell, leaving visible gaps
    // between otherwise-adjacent tiles.
    Vector2      cellCenter  = {};
    // False for grid cells thinned out by the galaxy's density field (see
    // StarSystemRegistry::Generate) — most callers only ever see true, since
    // ById()/QueryRegion() already filter these out before returning.
    bool         hasStar     = true;
    // Density field value already computed for the existence roll in
    // Generate() — cached here so callers that shade by density (e.g. the
    // galactic map's widest zoom tile) don't need a second, redundant
    // evaluation of the (fairly expensive: trig + exp/log) density formula.
    float        density     = 0.0f;
    // Most systems are uncontrolled (no NPC space station at all); a minority
    // are controlled by a single faction and get exactly one station for
    // that faction — see StarSystemRegistry::Generate's control roll and
    // SpaceFlight::SpawnPlanetsAndStations, which reads this instead of
    // independently rolling a station count/faction per system as before.
    bool         isControlled      = false;
    Faction      controllingFaction = Faction::Republic; // meaningless unless isControlled
};

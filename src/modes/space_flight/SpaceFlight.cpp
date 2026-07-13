#include "SpaceFlight.h"
#include "raylib.h"
#include "raymath.h"
#include "core/FleetManager.h"
#include "core/GameManager.h"
#include "core/InventoryManager.h"
#include "core/Module.h"
#include "core/SaveManager.h"
#include "engine/RenderSystem.h"
#include "engine/ResourceManager.h"
#include "data/modules/ModuleLookup.h"
#include "data/modules/WeaponDefs.h"
#include "data/modules/ArmorDefs.h"
#include "data/modules/ShieldDefs.h"
#include "data/modules/EngineDefs.h"
#include "data/modules/HyperdriveDefs.h"
#include "data/modules/AuxDefs.h"
#include "data/registry/FactionRegistry.h"
#include "data/registry/ModuleRegistry.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/registry/StationTypeRegistry.h"
#include "data/registry/StarRegistry.h"
#include "data/registry/StarSystemRegistry.h"
#include "data/registry/BuildableRegistry.h"
#include "data/registry/MaterialRegistry.h"
#include "data/registry/ItemRegistry.h"
#include "engine/SpriteCache.h"
#include "core/ShipRegistry.h"
#include "data/MaterialDefs.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "systems/diplomacy/ReputationRegistry.h"
#include "modes/space_flight/systems/HostileTargeting.h"
#include "modes/space_flight/systems/DockRepair.h"
#include "net/NetworkManager.h"
#include "shared/ui/HudTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <core/WorldManager.h>

static constexpr float kCoronaReach = 5.0f;

// Shortest-path angle interpolation in degrees.
static float LerpAngleDeg(float from, float to, float t) {
    float diff = fmodf(to - from + 540.0f, 360.0f) - 180.0f;
    return from + diff * t;
}

// Identifies an NPC's ship type across the wire without sending strings: the
// host hashes NpcMeta::shipTypeId into the snapshot, the client matches it
// against ShipRegistry (both sides load the same ships.json) to pick a texture.
static uint32_t Fnv1a32(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}
static const uint32_t kGargosShipHash = Fnv1a32("gargos");

static Faction kPlayerFaction = Faction::Republic;

// Minimum distance a fresh player spawn must keep from anything hostile.
static constexpr float kEnemySpawnMargin = 700.0f;

// Credit cost to build a player-allied capital ship via StationModuleMenu.
// Regular ships built through this same flow remain free — only capitals
// (heavy multi-hardpoint hulls) are gated, per the C.02a design decision.
static constexpr int kCapitalShipBuildCost = 50000;

// Epic 9.2 (fighter capture): a fighter must already be at/below this hull
// fraction before an Ion-effect weapon disables it instead of just damaging
// it — you have to wear it down with any weapon first, ion or otherwise.
static constexpr float kIonDisableHullPct = 0.30f;

static bool SpawnPosSafeFromStations(Vector2 pos, const std::vector<SpaceStation>& stations, float margin) {
    for (const SpaceStation& s : stations) {
        if (!s.alive) continue;
        if (ReputationRegistry::PlayerRelation(s.faction) != Relation::Hostile) continue;
        if (Vector2Distance(pos, s.position) < s.radius + margin) return false;
    }
    return true;
}

static bool SpawnPosSafeFromNpcs(Vector2 pos, const std::vector<NpcMeta>& npcMeta,
                                  const std::vector<ecs::Entity>& entities, float margin) {
    for (size_t i = 0; i < npcMeta.size(); ++i) {
        if (!npcMeta[i].alive || npcMeta[i].faction != NpcFaction::Hostile) continue;
        if (Vector2Distance(pos, entities[i].transform.position) < margin) return false;
    }
    return true;
}

static Vector2 GetSafeSpawnPosition(SystemWorld* w, float baseDistance, float margin) {
    Vector2 safeSpawn = { 0.0f, 0.0f };

    // 100-attempt retry loop to find a safe location
    for (int attempt = 0; attempt < 100; ++attempt) {
        float spawnAngle = (float)GetRandomValue(0, 359) * DEG2RAD;
        safeSpawn = { cosf(spawnAngle) * baseDistance, sinf(spawnAngle) * baseDistance };

        // Validate against both static stations AND currently active NPCs
        if (SpawnPosSafeFromStations(safeSpawn, w->stations, margin) &&
            SpawnPosSafeFromNpcs(safeSpawn, w->npcMeta, w->entities, margin)) {
            return safeSpawn; // Found a safe spot!
        }
    }
    return safeSpawn; // Fallback to the last attempt if the map is completely swamped
}

// MODULES/STORAGE/ESCORTS/RANKS moved to the SystemMap pause menu.
// Right section of the HUD now just holds ENTER (top bar) with BUILD, COMMS,
// and SEAT (Epic 8, turret seat) stacked vertically as icon buttons below it.
static void ComputeHudButtons(int sw, int sh,
    Rectangle& enterBtn, Rectangle& buildBtn, Rectangle& commsBtn, Rectangle& seatBtn) {
    static constexpr int HudH2 = 178; // +4px to fit the SEAT button's row under COMMS
    static constexpr int CenterW = 190;
    int hx = 12, hw = sw - 24;
    int rDiv = hx + (hw - CenterW) / 2 + CenterW;
    int hy = sh - HudH2 - 6;
    int rx = rDiv + 12, ry = hy + 10;
    int rw = (hx + hw) - rDiv - 16;
    int btnW = std::min(rw, 160);
    enterBtn = { (float)rx, (float)ry,        (float)btnW, 38.0f };
    buildBtn = { (float)rx, (float)(ry + 43), (float)btnW, 38.0f };
    commsBtn = { (float)rx, (float)(ry + 86), (float)btnW, 38.0f };
    seatBtn  = { (float)rx, (float)(ry + 129),(float)btnW, 38.0f };
}

static const char* FactionName(Faction f) {
    switch (f) {
        case Faction::Republic: return "Republic";
        case Faction::Zenith:   return "Zenith";
        case Faction::Korrath:  return "Korrath";
        case Faction::Merchant: return "Merchant";
        case Faction::Eden:     return "Eden";
        case Faction::Reavers:  return "Reavers";
        case Faction::Forge:    return "Forge";
        case Faction::Conclave: return "Conclave";
        case Faction::Void:     return "Void";
        default:                return "Unknown";
    }
}

static const char* NpcRoleName(NpcRole r) {
    switch (r) {
        case NpcRole::Explorer:     return "Explorer";
        case NpcRole::Raider:       return "Raider";
        case NpcRole::Military:     return "Military";
        case NpcRole::Trader:       return "Trader";
        case NpcRole::Industrialist:return "Industrialist";
        default:                    return "";
    }
}

// Epic 6: HUD label for a faction's current player-standing relation, next
// to the existing FACTION/ROLE lines in the target info panel.
static const char* PlayerRelationLabel(Relation r) {
    switch (r) {
        case Relation::Friendly: return "FRIENDLY";
        case Relation::Hostile:  return "HOSTILE";
        default:                 return "NEUTRAL";
    }
}

// Per-faction role-weighting profile (locked decision: varies by faction, not
// a uniform mix — tasks_spaceflight_dynamics.md Epic 2.2). Weights derived
// from each faction's config/factions.json loreText/briefing rather than
// invented: e.g. Reavers ("raid high-value targets... grow the fleet through
// salvage") skew Raider-heavy, Merchant ("largest commercial fleet... trade
// routes") skew Trader-heavy. Order matches NpcRole's declaration: Explorer,
// Raider, Military, Trader, Industrialist.
static NpcRole RollNpcRole(Faction f) {
    struct Weights { int explorer, raider, military, trader, industrialist; };
    Weights w;
    switch (f) {
        case Faction::Republic:  w = { 10, 5,  45, 25, 15 }; break; // naval doctrine, protect shipping lanes
        case Faction::Zenith:    w = { 40, 5,  10, 10, 35 }; break; // recover research, prototypes, rare elements
        case Faction::Korrath:   w = { 5,  45, 15, 10, 25 }; break; // mercenary contracts, aggressive extraction
        case Faction::Merchant:  w = { 10, 5,  15, 55, 15 }; break; // largest commercial fleet, trade routes
        case Faction::Eden:      w = { 10, 5,  25, 25, 35 }; break; // colony footholds, sustainable expansion
        case Faction::Reavers:   w = { 5,  60, 15, 5,  15 }; break; // raid, salvage, grow the fleet through conquest
        case Faction::Forge:     w = { 10, 35, 15, 10, 30 }; break; // rebuilt from wreckage; salvager/scavenger/raider ranks
        case Faction::Conclave:  w = { 15, 5,  30, 10, 40 }; break; // optimized systems, expand the network
        case Faction::Void:      w = { 55, 5,  15, 10, 15 }; break; // chart the unchartable, uncover anomalies
        default:                 w = { 20, 20, 20, 20, 20 }; break;
    }
    int total = w.explorer + w.raider + w.military + w.trader + w.industrialist;
    int roll = GetRandomValue(0, total - 1);
    if ((roll -= w.explorer)     < 0) return NpcRole::Explorer;
    if ((roll -= w.raider)       < 0) return NpcRole::Raider;
    if ((roll -= w.military)     < 0) return NpcRole::Military;
    if ((roll -= w.trader)       < 0) return NpcRole::Trader;
    return NpcRole::Industrialist;
}

static Vector2 GetNpcStationHardpointPos(const SpaceStation& st, int hpIndex) {
    if (hpIndex < 0 || hpIndex >= (int)st.hardpoints.size()) return st.position;
    const Hardpoint& hp = st.hardpoints[hpIndex];
    if (hp.isCore) return st.position;
    int nonCoreCount = 0, nonCoreIndex = 0;
    for (int i = 0; i < (int)st.hardpoints.size(); ++i) {
        if (!st.hardpoints[i].isCore) {
            if (i < hpIndex) nonCoreIndex++;
            nonCoreCount++;
        }
    }
    if (nonCoreCount == 0) return st.position;
    float angle = ((float)nonCoreIndex / (float)nonCoreCount) * 2.0f * PI;
    float offsetRad = st.radius * 0.70f;
    return { st.position.x + cosf(angle) * offsetRad,
             st.position.y + sinf(angle) * offsetRad };
}

// Builds every NPC station's hardpoint loadout procedurally (independent of
// StationTypeDef::hardpoints, which is now just flavor/name/service data):
// one Core hub, one Docking Bay, and 1-3 Weapon Batteries (75% / 15% / 10%
// chance of 1/2/3). Every combat-capable hardpoint gets a real module so
// there's always something concrete to damage/capture later.
static void BuildNpcStationHardpoints(SpaceStation& st) {
    st.hardpoints.clear();
    float totalHull = 0.0f;

    Hardpoint core;
    core.id          = "core";
    core.displayName = "Core";
    core.isCore      = true;
    core.maxHull     = 250.0f;
    core.hull        = core.maxHull;
    core.slots.push_back({ ModuleType::Armor, ModuleRegistry::Random(ModuleType::Armor, ModuleRegistry::RollGrade()) });
    totalHull += core.maxHull;
    st.hardpoints.push_back(core);

    Hardpoint dock;
    dock.id          = "docking_bay";
    dock.displayName = "Docking Bay";
    dock.isDockingBay = true;
    dock.maxHull     = 150.0f;
    dock.hull        = dock.maxHull;
    totalHull += dock.maxHull;
    st.hardpoints.push_back(dock);

    int roll = GetRandomValue(1, 100);
    int weaponCount = (roll <= 75) ? 1 : (roll <= 90) ? 2 : 3;
    for (int i = 0; i < weaponCount; ++i) {
        Hardpoint wb;
        wb.id          = "weapon_battery_" + std::to_string(i + 1);
        wb.displayName = "Weapon Battery " + std::to_string(i + 1);
        wb.maxHull     = 120.0f;
        wb.hull        = wb.maxHull;
        wb.slots.push_back({ ModuleType::Weapon, ModuleRegistry::Random(ModuleType::Weapon, ModuleRegistry::RollGrade()) });
        totalHull += wb.maxHull;
        st.hardpoints.push_back(wb);
    }

    st.maxHull = totalHull;
    st.hull    = totalHull;
}

// Capital ships move and rotate, so unlike station hardpoints (static ring
// layout via GetNpcStationHardpointPos), their hardpoints are placed from a
// fixed ship-local offset rotated by current heading. B.04 firing and B.06
// hit-detection MUST call this same function so draw/fire/hit all agree.
static Vector2 GetCapitalHardpointWorldPos(Vector2 shipPos, float shipRotationDeg, Vector2 localOffset) {
    return Vector2Add(shipPos, Vector2Rotate(localOffset, shipRotationDeg * DEG2RAD));
}

// Capital ships get a fixed hardpoint layout from ships.json instead of the
// procedural station roll above — each StationHardpointDef becomes one
// Hardpoint, keyed off slotType. "command_bridge" is the future
// bridge-piloting seat (isCore, no combat slots, no special death rule now —
// see project_capital_ships.md).
static std::vector<Hardpoint> BuildCapitalHardpoints(const ecs::ShipDef& def) {
    std::vector<Hardpoint> out;
    for (const ecs::StationHardpointDef& hpd : def.hardpoints) {
        Hardpoint hp;
        hp.id          = hpd.name;
        hp.displayName = hpd.name;
        hp.localOffset = hpd.offset;
        hp.maxHull     = 120.0f;
        hp.hull        = hp.maxHull;
        hp.alive       = true;

        if (hpd.slotType == "weapon") {
            for (int i = 0; i < hpd.slotCount; ++i) hp.slots.push_back({ ModuleType::Weapon });
            for (const std::string& modId : hpd.preloadedModules) {
                auto mod = ModuleById(modId);
                if (!mod || mod->type != ModuleType::Weapon) continue;
                for (auto* w : hp.WeaponSlots()) if (!w->equipped.has_value()) { w->equipped = *mod; break; }
            }
        } else if (hpd.slotType == "engine") {
            for (int i = 0; i < hpd.slotCount; ++i) hp.slots.push_back({ ModuleType::Engine });
            for (const std::string& modId : hpd.preloadedModules) {
                auto mod = ModuleById(modId);
                if (mod && mod->type == ModuleType::Engine) { if (auto* e = hp.Engine()) e->equipped = *mod; break; }
            }
        } else if (hpd.slotType == "shield") {
            for (int i = 0; i < hpd.slotCount; ++i) hp.slots.push_back({ ModuleType::Shield });
            for (const std::string& modId : hpd.preloadedModules) {
                auto mod = ModuleById(modId);
                if (!mod || mod->type != ModuleType::Shield) continue;
                for (auto& s : hp.slots) if (s.type == ModuleType::Shield && !s.equipped.has_value()) { s.equipped = *mod; break; }
            }
        } else if (hpd.slotType == "command_bridge") {
            hp.isCore = true;
        } else if (hpd.slotType == "facility") {
            // Capitals never got P4's facility support wired into their own
            // parse branch (only the station registries did) — added here so
            // a capital hardpoint can carry a facility chip the same way a
            // station one can. No player-facing editor exists for capital
            // hardpoints (they're placed pre-built, not attached at
            // runtime), so facility modules on a capital are always
            // preloaded from config, same as its weapons/shields/engines.
            for (int i = 0; i < hpd.slotCount; ++i) hp.slots.push_back({ ModuleType::Facility });
            for (const std::string& modId : hpd.preloadedModules) {
                auto mod = ModuleById(modId);
                if (!mod || mod->type != ModuleType::Facility) continue;
                if (auto* f = hp.Facility()) f->equipped = *mod;
            }
        } else {
            for (int i = 0; i < hpd.slotCount; ++i) hp.slots.push_back({ ModuleType::Auxiliary });
            for (const std::string& modId : hpd.preloadedModules) {
                auto mod = ModuleById(modId);
                if (!mod || mod->type != ModuleType::Auxiliary) {
                    TraceLog(LOG_WARNING, "BuildCapitalHardpoints: hardpoint '%s' has unrecognized slotType '%s' — module '%s' discarded (expected an Auxiliary-type module)",
                             hpd.name.c_str(), hpd.slotType.c_str(), modId.c_str());
                    continue;
                }
                for (auto* a : hp.AuxSlots()) if (!a->equipped.has_value()) { a->equipped = *mod; break; }
            }
        }

        out.push_back(std::move(hp));
    }
    return out;
}

static Vector2 GetHardpointPos(const PlayerStation& ps, int hpIndex, float stationRadius) {
    if (hpIndex < 0 || hpIndex >= (int)ps.hardpoints.size()) return ps.position;
    const Hardpoint& hp = ps.hardpoints[hpIndex];

    // Core sits exactly at the center of the station
    if (hp.isCore) return ps.position;

    // Count how many non-core hardpoints exist to evenly space them
    int nonCoreCount = 0;
    int nonCoreIndex = 0;
    for (int i = 0; i < (int)ps.hardpoints.size(); i++) {
        if (!ps.hardpoints[i].isCore) {
            if (i < hpIndex) nonCoreIndex++;
            nonCoreCount++;
        }
    }

    if (nonCoreCount == 0) return ps.position;

    // Distribute evenly in a ring 70% of the way to the station's edge
    float angle = ((float)nonCoreIndex / (float)nonCoreCount) * 2.0f * PI;
    float offsetRad = stationRadius * 0.70f;
    return { ps.position.x + cosf(angle) * offsetRad,
             ps.position.y + sinf(angle) * offsetRad };
}

static Vector2 GetBestHardpointAimPos(const PlayerStation& ps, float rad) {
    for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
        const Hardpoint& hp = ps.hardpoints[i];
        if (!hp.alive || hp.isCore) continue;
        return GetHardpointPos(ps, i, rad);
    }
    for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
        if (ps.hardpoints[i].alive) return GetHardpointPos(ps, i, rad);
    }
    return ps.position;
}

// Same idea as GetBestHardpointAimPos, but for a world-owned NPC SpaceStation.
// Without this, attackers aim at st.position — which is exactly where the
// isCore hardpoint sits — so once the core dies (no longer invulnerable
// since the no-core rework) every attacker keeps aiming dead-center at
// nothing instead of redirecting to whatever hardpoint is still alive.
static Vector2 GetStationAimPos(const SpaceStation& st) {
    for (int i = 0; i < (int)st.hardpoints.size(); ++i) {
        const Hardpoint& hp = st.hardpoints[i];
        if (!hp.alive || hp.isCore) continue;
        return GetNpcStationHardpointPos(st, i);
    }
    for (int i = 0; i < (int)st.hardpoints.size(); ++i)
        if (st.hardpoints[i].alive) return GetNpcStationHardpointPos(st, i);
    return st.position;
}

// Same idea for a capital ship's own hardpoint list.
static Vector2 GetCapitalAimPos(const NpcMeta& m, Vector2 shipPos, float shipRotationDeg) {
    for (const Hardpoint& hp : m.hardpoints) {
        if (!hp.alive || hp.isCore) continue;
        return GetCapitalHardpointWorldPos(shipPos, shipRotationDeg, hp.localOffset);
    }
    for (const Hardpoint& hp : m.hardpoints)
        if (hp.alive) return GetCapitalHardpointWorldPos(shipPos, shipRotationDeg, hp.localOffset);
    return shipPos;
}

// One entry point for "aim at NPC index j" — redirects to GetCapitalAimPos
// when j is a capital ship (non-empty hardpoints), otherwise just its
// ship-center position (fighters have no hardpoint list to redirect through).
static Vector2 GetNpcAimPos(const SystemWorld& w, size_t j) {
    const NpcMeta&      m = w.npcMeta[j];
    const ecs::Entity&  e = w.entities[j];
    if (m.hardpoints.empty()) return e.transform.position;
    return GetCapitalAimPos(m, e.transform.position, e.transform.rotation);
}

// Shared by the PlayerStation/SpaceStation/capital-ship hardpoint-hit blocks
// in UpdateNpcCollisions. Finds the first alive hardpoint within range of
// projPos, applies damage (clamped to 0 — a hardpoint's hull must never go
// negative, since it's later cast to uint16_t for the MP snapshot), and
// emits the destroyed-hardpoint message as msgPrefix + displayName + msgSuffix.
// Returns true if a hardpoint was hit at all (whether or not it died); the
// caller still owns the AllHardpointsDestroyed death check and any per-owner
// side effects (retaliation, loot, net broadcast, AI-state transition), since
// those genuinely differ between stations and capital ships.
bool SpaceFlight::ResolveHardpointHit(std::vector<Hardpoint>& hardpoints, Vector2 projPos, float damage,
                                       const std::function<Vector2(int)>& hardpointWorldPos,
                                       const std::string& msgPrefix, const std::string& msgSuffix,
                                       bool urgent) {
    for (int i = 0; i < (int)hardpoints.size(); ++i) {
        Hardpoint& hp = hardpoints[i];
        if (!hp.alive) continue;
        Vector2 hpPos  = hardpointWorldPos(i);
        float   hitRad = hp.isCore ? 18.0f : 14.0f;
        if (Vector2Distance(projPos, hpPos) >= hitRad + 3.5f) continue;

        // P6: a live ShieldGenerator's coverage (Hardpoint::shieldCovered,
        // RecalculateAdjacency in Hardpoint.h) reduces incoming damage; the
        // reduction disappears on its own the tick the covering generator
        // dies or gets power-shed, since shieldCovered flips false then.
        float applied = hp.shieldCovered
            ? damage * (1.0f - hardpoint_adjacency_detail::kShieldGeneratorMitigation)
            : damage;
        hp.hull = std::max(0.0f, hp.hull - applied);
        if (hp.hull <= 0.0f) {
            hp.alive = false;
            AddCommsMessage(msgPrefix + hp.displayName + msgSuffix, urgent);
        }
        return true;
    }
    return false;
}

void SpaceFlight::InitStars() {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (int i = 0; i < BgStarCount; ++i) {
        BgStar& s = _bgStars[i];
        s.x = (float)GetRandomValue(0, sw);
        s.y = (float)GetRandomValue(0, sh);
        if (i < 150) {
            s.parallax = 0.002f;
            s.radius = (float)GetRandomValue(60, 110) / 100.0f;
            s.color = { 170, 185, 220, (unsigned char)GetRandomValue(55,  115) };
        }
        else if (i < 220) {
            s.parallax = 0.01f;
            s.radius = (float)GetRandomValue(110, 170) / 100.0f;
            s.color = { 195, 210, 240, (unsigned char)GetRandomValue(120, 185) };
        }
        else {
            s.parallax = 0.04f;
            s.radius = (float)GetRandomValue(155, 210) / 100.0f;
            s.color = { 215, 225, 255, (unsigned char)GetRandomValue(185, 245) };
        }
    }
}

void SpaceFlight::PrewarmSpriteCache() {
    // Bake every ship design × faction color combo up front so mid-game spawns
    // are O(id.length) cache hits instead of O(pixels) key builds + GPU uploads.
    const Color factionColors[] = { RED, SKYBLUE, GREEN };
    for (const ecs::ShipDef& def : ecs::ShipRegistry::AllShips()) {
        if (def.designArray.empty()) continue;
        for (Color c : factionColors)
            SpriteCache::BakeForId(def.id, c, WHITE, def.designArray);
    }
}

void SpaceFlight::DrawBackground() const {
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    for (const BgStar& s : _bgStars) {
        float px = fmodf(s.x - _camera.target.x * s.parallax, sw);
        if (px < 0.0f) px += sw;
        float py = fmodf(s.y - _camera.target.y * s.parallax, sh);
        if (py < 0.0f) py += sh;
        DrawCircleV({ px, py }, s.radius, s.color);
        if (s.radius > 1.7f)
            DrawCircleV({ px, py }, s.radius * 3.0f,
                { s.color.r, s.color.g, s.color.b, (unsigned char)(s.color.a / 5) });
    }
}

static constexpr float PlanetDrawRadius  = 180.0f;
// Matches the player's general-purpose "space_station" blueprint radius
// (PlayerStationRegistry) so NPC stations read at the same visual scale.
static constexpr float StationDrawRadius = 200.0f;

void SpaceFlight::DrawPlanets() const {
    if (_planetBaseTex.id == 0) return;
    float size = PlanetDrawRadius * 2.0f;
    Rectangle src = { 0.0f, 0.0f, (float)_planetBaseTex.width, (float)_planetBaseTex.height };
    Vector2   origin = { size * 0.5f, size * 0.5f };
    float     lightRange = _w->sun.active ? _w->sun.gravRange * 5.0f : 0.0f;
    for (const SpacePlanet& p : _w->planets) {
        DrawCircleV(p.position, p.radius * 1.10f, Color{ 80, 120, 210, 18 });
        DrawCircleV(p.position, p.radius * 1.05f, Color{ 90, 130, 220, 12 });
        Rectangle dst = { p.position.x, p.position.y, size, size };
        Color lit = _lighting.BeginLit(p.position, { 0.0f, 0.0f }, lightRange);
        DrawTexturePro(_planetBaseTex, src, dst, origin, 0.0f, lit);
        _lighting.EndLit();
    }
}

void SpaceFlight::DrawStations() const {
    if (_stationBaseTex.id == 0) return;
    float size = StationDrawRadius * 2.0f;
    Rectangle src = { 0.0f, 0.0f, (float)_stationBaseTex.width, (float)_stationBaseTex.height };
    Vector2   origin = { size * 0.5f, size * 0.5f };
    float     lightRange = _w->sun.active ? _w->sun.gravRange * 5.0f : 0.0f;
    for (const SpaceStation& s : _w->stations) {
        if (!s.alive) continue;
        DrawCircleV(s.position, s.radius * 1.25f, Color{ 60, 120, 200, 14 });
        Rectangle dst = { s.position.x, s.position.y, size, size };
        Color lit = _lighting.BeginLit(s.position, { 0.0f, 0.0f }, lightRange);
        DrawTexturePro(_stationBaseTex, src, dst, origin, 0.0f, lit);
        _lighting.EndLit();

        for (int i = 0; i < (int)s.hardpoints.size(); ++i) {
            const Hardpoint& hp = s.hardpoints[i];
            if (!hp.alive) continue;
            Vector2 hpPos = GetNpcStationHardpointPos(s, i);
            float hpDrawRad = hp.isCore ? 18.0f : 14.0f;

            float hpHullPct = hp.maxHull > 0.0f ? std::clamp(hp.hull / hp.maxHull, 0.0f, 1.0f) : 0.0f;
            Color ringCol = hpHullPct > 0.5f ? Color{ 48,188,68,255 }
                : hpHullPct > 0.25f ? Color{ 212,168,28,255 } : Color{ 208,42,32,255 };
            DrawCircleV(hpPos, hpDrawRad, Color{ 15, 25, 40, 240 });
            DrawCircleLinesV(hpPos, hpDrawRad, ringCol);

            Color markCol = hp.isCore       ? Color{ 200, 160,  30, 255 }
                : hp.isDockingBay ? Color{ 140, 220, 255, 255 }
                                  : Color{};
            if (markCol.a != 0) DrawCircleV(hpPos, hpDrawRad * 0.4f, markCol);
        }
        // P2: composited module render replaces the old per-hardpoint
        // weapon-presence dot — the hull-integrity ring/backdrop above stays.
        ecs::RenderSystem::DrawHardpointRig(s.position, 0.0f, 1.0f, s.hardpoints,
                                             Color{ 100, 180, 255, 255 }, WHITE);
    }
}

void SpaceFlight::DrawPlayerStations() const {
    for (const PlayerStation& ps : FleetManager::Get().PlayerStations)
        DrawOneStation(ps, true);
    for (const auto& [id, ps] : _remotePlayerStations)
        DrawOneStation(ps, false);
}

void SpaceFlight::DrawOneStation(const PlayerStation& ps, bool isLocal) const {
        if (!ps.alive) return;
        const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
        float rad = def ? def->radius : 120.0f;

        // Count alive / total hardpoints for damage tinting
        int totalHp = (int)ps.hardpoints.size();
        int aliveHp = 0;
        for (const Hardpoint& hp : ps.hardpoints)
            if (hp.alive) aliveHp++;
        float integrity = totalHp > 0 ? (float)aliveHp / (float)totalHp : 1.0f;

        // ── Draw the actual station base ──
        if (_stationBaseTex.id > 0 && ps.stationDefId != "mining_station") {
            // Draw textured station at 100% opacity (255)
            float size = rad * 2.0f;
            Rectangle src = { 0.0f, 0.0f, (float)_stationBaseTex.width, (float)_stationBaseTex.height };
            Rectangle dst = { ps.position.x, ps.position.y, size, size };
            Vector2 origin = { size * 0.5f, size * 0.5f };
            float lightRange = _w->sun.active ? _w->sun.gravRange * 5.0f : 0.0f;
            Color lit = _lighting.BeginLit(ps.position, { 0.0f, 0.0f }, lightRange);
            DrawTexturePro(_stationBaseTex, src, dst, origin, 0.0f, lit);
            _lighting.EndLit();
        }
        else if (ps.stationDefId == "mining_station") {
            // Draw the solid mining station box
            Rectangle box = { ps.position.x - rad, ps.position.y - rad, rad * 2.0f, rad * 2.0f };
            DrawRectangleRec(box, Color{ 30, 65, 110, 255 }); // 100% solid dark blue background
            DrawRectangleLinesEx(box, 2.0f, Color{ 80, 160, 255, 255 }); // 100% solid border
        }
        else {
            // Fallback for missing textures
            unsigned char alpha = 255; // 100% opacity
            Color bodyColor = { (unsigned char)(60 + (unsigned char)(integrity * 40)),
                                (unsigned char)(120 + (unsigned char)(integrity * 60)),
                                (unsigned char)(200 + (unsigned char)(integrity * 30)), alpha };
            DrawCircleV(ps.position, rad, Color{ 10, 20, 50, 255 });
            DrawCircleLinesV(ps.position, rad, bodyColor);
            DrawCircleLinesV(ps.position, rad * 0.65f, Color{ bodyColor.r, bodyColor.g, bodyColor.b, 200 });
        }

        // Cross-hair inner mark
        DrawLine((int)(ps.position.x - rad * 0.2f), (int)ps.position.y,
            (int)(ps.position.x + rad * 0.2f), (int)ps.position.y,
            Color{ 100, 180, 255, 160 });
        DrawLine((int)ps.position.x, (int)(ps.position.y - rad * 0.2f),
            (int)ps.position.x, (int)(ps.position.y + rad * 0.2f),
            Color{ 100, 180, 255, 160 });

        // Name label
        const char* nm = ps.displayName.c_str();
        DrawText(nm, (int)(ps.position.x - MeasureText(nm, 11) / 2), (int)(ps.position.y - rad - 16),
            11, Color{ 160,210,255,200 });

        // Ownership label — "[YOURS]" for the local player's own stations,
        // "[ALLY]" for another peer's (Epic C MP sync).
        const char* ownlbl = isLocal ? "[YOURS]" : "[ALLY]";
        Color ownCol = isLocal ? Color{ 80,200,100,200 } : Color{ 100,180,255,200 };
        DrawText(ownlbl,
            (int)(ps.position.x - MeasureText(ownlbl, 9) / 2),
            (int)(ps.position.y - rad - 28), 9, ownCol);

        // ── Draw Physical Hardpoints ──────────────────────────────────────────────
        for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
            const Hardpoint& hp = ps.hardpoints[i];
            if (!hp.alive) continue;

            Vector2 hpPos = GetHardpointPos(ps, i, rad);
            float hpDrawRad = hp.isCore ? 18.0f : 14.0f;

            // Color code based on remaining health
            float hpHullPct = hp.maxHull > 0.0f ? std::clamp(hp.hull / hp.maxHull, 0.0f, 1.0f) : 0.0f;
            Color hpCol = hpHullPct > 0.5f ? Color{ 48,188,68,255 } :
                hpHullPct > 0.25f ? Color{ 212,168,28,255 } : Color{ 208,42,32,255 };

            // Draw Base
            DrawCircleV(hpPos, hpDrawRad, Color{ 15, 25, 40, 240 });
            DrawCircleLinesV(hpPos, hpDrawRad, hpCol);

            // Core indicator inside the hardpoint (P2: the old content-type
            // dot is gone — DrawHardpointRig below draws the actual modules).
            if (hp.isCore) DrawCircleV(hpPos, hpDrawRad * 0.4f, Color{ 200, 160, 30, 255 });

            // Render small text label near hovered hardpoints if needed here
        }
        ecs::RenderSystem::DrawHardpointRig(ps.position, 0.0f, 1.0f, ps.hardpoints,
                                             Color{ 100, 180, 255, 255 }, WHITE);
}

void SpaceFlight::BakeSunCorona() {
    if (_sunCorona.id != 0) UnloadTexture(_sunCorona);

    static constexpr int kTexSize = 1024;
    const float halfSize  = kTexSize * 0.5f;
    const float r         = _w->sun.radius;
    const float outerDist = r * kCoronaReach;
    const float scale     = halfSize / outerDist;  // world units → texture pixels

    const Color& c  = _w->sun.coreColor;
    const Color& ig = _w->sun.innerGlow;
    const Color& og = _w->sun.outerGlow;

    // 1.5 texture-pixel blend zone at the core/corona boundary eliminates the staircase ring
    const float aaWidth = 1.5f / scale;

    auto coronaAt = [&](float d) -> Color {
        float t = std::clamp((outerDist - d) / (outerDist - r * 1.25f), 0.0f, 1.0f);
        unsigned char a = (unsigned char)std::clamp(powf(t, 2.5f) * 150.0f, 0.0f, 255.0f);
        unsigned char rc, gc, bc;
        if (t < 0.5f) {
            float s = t / 0.5f;
            rc = (unsigned char)(og.r + (ig.r - og.r) * s);
            gc = (unsigned char)(og.g + (ig.g - og.g) * s);
            bc = (unsigned char)(og.b + (ig.b - og.b) * s);
        } else {
            float s = (t - 0.5f) / 0.5f;
            rc = (unsigned char)(ig.r + (c.r - ig.r) * s);
            gc = (unsigned char)(ig.g + (c.g - ig.g) * s);
            bc = (unsigned char)(ig.b + (c.b - ig.b) * s);
        }
        return { rc, gc, bc, a };
    };

    auto coreAt = [&](float d) -> Color {
        float tc = 1.0f - std::clamp(d / r, 0.0f, 1.0f);
        float s  = powf(tc, 0.6f);
        return {
            (unsigned char)(c.r + (ig.r - c.r) * s),
            (unsigned char)(c.g + (ig.g - c.g) * s),
            (unsigned char)(c.b + (ig.b - c.b) * s),
            255
        };
    };

    Image img = GenImageColor(kTexSize, kTexSize, BLANK);

    for (int py = 0; py < kTexSize; ++py) {
        for (int px = 0; px < kTexSize; ++px) {
            float dx   = px - halfSize;
            float dy   = py - halfSize;
            float dist = sqrtf(dx*dx + dy*dy) / scale;

            Color col = BLANK;

            if (dist < r - aaWidth) {
                col = coreAt(dist);
            } else if (dist < r + aaWidth) {
                // Smooth blend across the core/corona boundary — eliminates staircase ring
                float blend = (dist - (r - aaWidth)) / (2.0f * aaWidth);
                Color cc = coreAt(r - aaWidth);
                Color cr = coronaAt(r + aaWidth);
                col = {
                    (unsigned char)(cc.r + (int(cr.r) - int(cc.r)) * blend),
                    (unsigned char)(cc.g + (int(cr.g) - int(cc.g)) * blend),
                    (unsigned char)(cc.b + (int(cr.b) - int(cc.b)) * blend),
                    (unsigned char)(cc.a + (int(cr.a) - int(cc.a)) * blend)
                };
            } else if (dist <= outerDist) {
                col = coronaAt(dist);
            }

            ImageDrawPixel(&img, px, py, col);
        }
    }

    _sunCorona = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(_sunCorona, TEXTURE_FILTER_BILINEAR);
}

void SpaceFlight::DrawSun() const {
    if (!_w->sun.active) return;
    Vector2 pos = { 0.0f, 0.0f };
    float   r = _w->sun.radius;
    float   t = (float)GetTime();
    const Color& c = _w->sun.coreColor;

    // ── Corona + core: single draw from per-pixel baked texture (no polygon edges)
    if (_sunCorona.id != 0) {
        float     d   = r * kCoronaReach * 2.0f;
        Rectangle src = { 0.0f, 0.0f, (float)_sunCorona.width, (float)_sunCorona.height };
        Rectangle dst = { pos.x - d * 0.5f, pos.y - d * 0.5f, d, d };
        DrawTexturePro(_sunCorona, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);
    }

    // ── Sun core surface texture tinted by star class color
    // Additive blend: black background pixels add 0 (invisible), white/grey add brightness
    if (_sunTex.id != 0) {
        float     d   = r * 2.0f;
        Rectangle src = { 0.0f, 0.0f, (float)_sunTex.width, (float)_sunTex.height };
        Rectangle dst = { pos.x - r, pos.y - r, d, d };
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(_sunTex, src, dst, { 0.0f, 0.0f }, 0.0f, c);
        EndBlendMode();
    }

    // ── Solar surface dynamics: bright granules + dark sunspots
    static constexpr int kGranules = 28;
    static constexpr int kSunspots = 5;

    for (int i = 0; i < kGranules; ++i) {
        float seed  = (float)i * 2.399f;
        float angle = seed + sinf(t * 0.18f + seed) * 2.0f + cosf(t * 0.11f + seed * 1.4f);
        float rpct  = 0.15f + 0.65f * fabsf(sinf(t * 0.09f + seed * 1.1f));
        float gx    = cosf(angle) * rpct * r * 0.88f;
        float gy    = sinf(angle) * rpct * r * 0.88f;
        float gr    = r * (0.055f + 0.035f * sinf(t * 0.45f + seed * 2.2f));
        auto  ga    = (unsigned char)(100.0f + 80.0f * sinf(t * 0.5f + seed));
        auto  gg    = (unsigned char)std::min(255, 220 + (int)(35.0f * sinf(t * 0.7f + seed)));
        //DrawCircleV({ pos.x + gx, pos.y + gy }, gr, { 255, gg, 180, ga });
    }

    for (int i = 0; i < kSunspots; ++i) {
        float seed  = (float)(i + 100) * 3.7f;
        float angle = seed + sinf(t * 0.04f + seed) * 0.6f + cosf(t * 0.06f + seed * 0.5f);
        float rpct  = 0.05f + 0.55f * fabsf(sinf(t * 0.06f + seed));
        float sx    = cosf(angle) * rpct * r * 0.78f;
        float sy    = sinf(angle) * rpct * r * 0.78f;
        float sr    = r * (0.09f + 0.05f * sinf(t * 0.25f + seed));
        auto  dr    = (unsigned char)((float)c.r * 0.20f);
        auto  dg    = (unsigned char)((float)c.g * 0.20f);
        auto  db    = (unsigned char)((float)c.b * 0.20f);
        //DrawCircleV({ pos.x + sx, pos.y + sy }, sr * 1.6f, { dr, dg, db,  80 });
        //DrawCircleV({ pos.x + sx, pos.y + sy }, sr,         { dr, dg, db, 200 });
    }

    // ── Pulsing brightness at the core center ────────────────────────────────
    //DrawCircleSector(pos, r * 0.52f,  0, 360, 144, { 255, 255, 255, 200 });
    //DrawCircleSector(pos, r * 0.22f,  0, 360,  72, { 255, 255, 255, 255 });
}

bool SpaceFlight::IsNearPlanet() const {
    const Vector2& pos = _playerEntity.transform.position;
    for (const SpacePlanet& p : _w->planets)
        if (Vector2Distance(pos, p.position) < p.radius + 50.0f) return true;
    return false;
}

bool SpaceFlight::IsNearStation() const {
    const Vector2& pos = _playerEntity.transform.position;
    for (const SpaceStation& s : _w->stations) {
        if (!s.alive) continue;
        if (Vector2Distance(pos, s.position) < s.radius + 50.0f) return true;
    }
    for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
        if (!ps.alive) continue;
        const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
        float rad = def ? def->radius : 120.0f;
        if (Vector2Distance(pos, ps.position) < rad + 50.0f) return true;
    }
    return false;
}

void SpaceFlight::SpawnPlanetsAndStations(unsigned int seed) {
    if (seed != 0) SetRandomSeed(seed);
    _w->planets.clear();
    _w->stations.clear();
    unsigned int nextId = 100;

    // ── Spawn sun ──────────────────────────────────────────────────────────────
    const StarTypeDef* starDef = StarRegistry::Pick(seed != 0 ? seed : (unsigned int)GetRandomValue(1, 99999));
    if (!starDef) starDef = StarRegistry::ById("G");
    _w->sun.typeId      = starDef->id;
    _w->sun.radius      = starDef->minRadius + (float)GetRandomValue(0, (int)(starDef->maxRadius - starDef->minRadius));
    _w->sun.gravRange   = _w->sun.radius * starDef->gravRangeMult;
    _w->sun.gravStrength= starDef->gravStrength;
    _w->sun.coreColor   = starDef->coreColor;
    _w->sun.innerGlow   = starDef->innerGlowColor;
    _w->sun.outerGlow   = starDef->outerGlowColor;
    _w->sun.active      = true;
    BakeSunCorona();

    // Planet orbits must start outside gravity zone
    float minOrbit = std::max(2500.0f, _w->sun.gravRange * 1.4f);
    float maxOrbit = minOrbit + 3000.0f;

    int planetCount = GetRandomValue(0, 10);
    for (int attempt = 0; attempt < 300 && (int)_w->planets.size() < planetCount; ++attempt) {
        float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
        float dist = minOrbit + (float)GetRandomValue(0, (int)(maxOrbit - minOrbit));
        Vector2 pos = { cosf(ang) * dist, sinf(ang) * dist };
        bool tooClose = false;
        for (const SpacePlanet& p : _w->planets)
            if (Vector2Distance(p.position, pos) < PlanetDrawRadius * 3.0f) { tooClose = true; break; }
        if (!tooClose) {
            SpacePlanet planet;
            planet.position    = pos;
            planet.radius      = PlanetDrawRadius;
            planet.id          = nextId++;
            planet.orbitRadius = dist;
            planet.orbitAngle  = ang;
            planet.orbitSpeed  = 0.1f / sqrtf(dist);  // Kepler-style: closer = faster
            _w->planets.push_back(planet);
        }
    }

    // Stations also pushed outside gravity zone
    float minStation = std::max(1500.0f, _w->sun.gravRange + 600.0f);
    float maxStation = std::max(3500.0f, _w->sun.gravRange + 2500.0f);

    // Most systems are uncontrolled (no station at all); a minority are
    // controlled by a single faction and get exactly one station belonging
    // to it — see StarSystemRegistry::Generate's control roll
    // (StarSystem::isControlled/controllingFaction), which replaces the old
    // per-station independent "1-10 stations, id-modulo faction" scheme.
    auto sys = StarSystemRegistry::ById(_w->systemId);
    int stationCount = (sys && sys->isControlled) ? 1 : 0;
    for (int attempt = 0; attempt < 300 && (int)_w->stations.size() < stationCount; ++attempt) {
        float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
        float dist = minStation + (float)GetRandomValue(0, (int)(maxStation - minStation));
        Vector2 pos = { cosf(ang) * dist, sinf(ang) * dist };
        bool tooClose = false;
        for (const SpaceStation& s : _w->stations)
            if (Vector2Distance(s.position, pos) < StationDrawRadius * 4.0f) { tooClose = true; break; }
        if (!tooClose) {
            SpaceStation st;
            st.position = pos;
            st.radius   = StationDrawRadius;
            st.id       = nextId++;
            st.faction  = sys->controllingFaction;
            const auto& stTypes = StationTypeRegistry::All();
            const StationTypeDef& typeDef = stTypes[st.id % stTypes.size()];
            st.stationTypeId = typeDef.id;
            BuildNpcStationHardpoints(st);
            SeedStationEconomy(st.economy);
            _w->stations.push_back(std::move(st));
        }
    }
    float spawnDist = _w->sun.gravRange + 800.0f;
    _w->playerSpawnPos = GetSafeSpawnPosition(_w, spawnDist, kEnemySpawnMargin);
}

SystemWorld& SpaceFlight::GetOrCreateWorld(unsigned int systemId) {
    uint64_t key = WorldKey(_currentGalaxyId, systemId);
    auto it = _worlds.find(key);
    if (it == _worlds.end()) {
        auto w = std::make_unique<SystemWorld>();
        w->systemId = systemId;
        w->galaxyId = _currentGalaxyId;
        it = _worlds.emplace(key, std::move(w)).first;
    }
    return *it->second;
}

// Returns the world for systemId, generating its content from the registry
// seed on first visit. Generation runs with `_w` temporarily pointed at the
// new world (every spawn helper operates through `_w`); afterwards `_w` is
// restored and the displayed sun corona re-baked, since SpawnPlanetsAndStations
// overwrote the shared _sunCorona texture with the new world's sun.
SystemWorld& SpaceFlight::EnsureWorldGenerated(unsigned int systemId) {
    bool existed = _worlds.find(WorldKey(_currentGalaxyId, systemId)) != _worlds.end();
    SystemWorld& world = GetOrCreateWorld(systemId);
    if (!existed) {
        auto sys = StarSystemRegistry::ById(systemId);
        SystemWorld* prev = _w;
        _w = &world;
        world.seed = sys ? sys->seed : 0;
        SpawnPlanetsAndStations(world.seed);
        SpawnInitialAsteroids();
        SpawnNpcShips();
        _w = prev;
        if (_w && _w != &world) BakeSunCorona();
    }
    return world;
}

bool SpaceFlight::PeerInCurrentWorld(uint32_t networkId) const {
    if (!net::Game().IsHost()) return true;  // clients only ever hold their own system
    auto it = _peerSystem.find(networkId);
    return it != _peerSystem.end() && _w && it->second == _w->systemId;
}

net::WorldSyncData SpaceFlight::BuildWorldSync(const SystemWorld& world) const {
    net::WorldSyncData ws;
    ws.systemId  = world.systemId;
    ws.worldSeed = world.seed;
    ws.gameSeed  = _gameSeed;
    ws.galaxyId  = _currentGalaxyId;
    ws.worldAge  = world.age;
    for (const SpaceStation& st : world.stations)
        if (!st.alive || st.hull < st.maxHull)
            ws.stations.push_back({ st.id, st.hull, uint8_t(st.alive ? 1 : 0) });
    return ws;
}

void SpaceFlight::UpdateOrbits(float dt) {
    for (SpacePlanet& p : _w->planets) {
        p.orbitAngle += p.orbitSpeed * dt;
        p.position = {
            cosf(p.orbitAngle) * p.orbitRadius,
            sinf(p.orbitAngle) * p.orbitRadius
        };
    }
}

void SpaceFlight::ApplySunGravity(float dt) {
    if (!_w->sun.active) return;

    // Player — skipped while docked (TickWorldWhileDocked calls this too, but
    // a frozen docked player shouldn't take gravity damage or drift from a
    // sun their ship isn't actually flying near anymore) or seated in a
    // turret (Epic 8) for the same reason.
    if (!_stationServicesMenu.isOpen && !_seated) {
        Vector2& pPos = _playerEntity.transform.position;
        Vector2& pVel = _playerEntity.transform.velocity;
        float playerDist = Vector2Length(pPos);
        if (playerDist < _w->sun.radius * 0.6f) {
            _playerEntity.health.currentHull = 0.0f;
        }
        else if (playerDist < _w->sun.gravRange && playerDist > 0.01f) {
            float t     = 1.0f - (playerDist / _w->sun.gravRange);
            float accel = _w->sun.gravStrength * t * t;
            pVel.x += (-pPos.x / playerDist) * accel * dt;
            pVel.y += (-pPos.y / playerDist) * accel * dt;
        }
    }

    // NPC ships
    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        NpcMeta& m = _w->npcMeta[i];
        ecs::Entity& e = _w->entities[i];
        if (!m.alive) continue;
        float dist = Vector2Length(e.transform.position);
        if (dist < _w->sun.radius * 0.6f) {
            m.alive = false;
            _w->npcFreeSlots.push_back(i);
            if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
            AddCommsMessage(m.wingman ? "WINGMAN lost to stellar gravity." : "Ship destroyed by stellar gravity.");
        }
        else if (dist < _w->sun.gravRange && dist > 0.01f) {
            float t     = 1.0f - (dist / _w->sun.gravRange);
            float accel = _w->sun.gravStrength * t * t;
            e.transform.velocity.x += (-e.transform.position.x / dist) * accel * dt;
            e.transform.velocity.y += (-e.transform.position.y / dist) * accel * dt;
        }
    }

    // Asteroids — silently consumed, no splits or drops
    for (Asteroid& a : _w->asteroids) {
        if (!a.alive) continue;
        float dist = Vector2Length(a.position);
        if (dist < _w->sun.radius * 0.6f) {
            a.alive = false;
        }
        else if (dist < _w->sun.gravRange && dist > 0.01f) {
            float t     = 1.0f - (dist / _w->sun.gravRange);
            float accel = _w->sun.gravStrength * t * t;
            a.velocity.x += (-a.position.x / dist) * accel * dt;
            a.velocity.y += (-a.position.y / dist) * accel * dt;
        }
    }
}

static constexpr float NpcThrust = 180.0f;
static constexpr float NpcDrag = 2.0f; // Adjusted from 2.8f to 2.0f to match player top speed bounds
static constexpr float NpcProjSpd = 480.0f;
static constexpr float NpcProjRng = 700.0f;
static constexpr float NpcDmg = 8.0f;
static constexpr float NpcFireRate = 1.4f;

static const char* FriendlyLines[] = {
    "Safe travels, pilot.",
    "Sector looks clear on our end.",
    "We're on routine patrol.",
    "Good hunting out there.",
    "Stay sharp — asteroids ahead.",
};
static const char* HostileLines[] = {
    "You chose the wrong sector.",
    "We don't negotiate.",
    "Your ship will make good salvage.",
    "Disengage or be destroyed.",
};
static const char* AggroLines[] = {
    "Hostile contact — engaging.",
    "Target acquired. Opening fire.",
    "All craft, engage the intruder.",
};
static const char* FleeLines[] = {
    "Retreating — shields critical.",
    "Breaking off — hull damage severe.",
};
static const char* JoinAcceptLines[] = {
    "Affirmative. Joining your wing.",
    "Copy that. Wing position assumed.",
    "Roger. Escort formation.",
};
static const char* FriendlyRefusalLines[] = {
    "Negative — I have other orders.",
    "Can't do that. Orders bind me here.",
};
static const char* HostileJoinLines[] = {
    "...Fine. Credits talk. I'm in.",
    "You've got guts. I respect that. Wing up.",
};
static const char* HostileRefusalLines[] = {
    "You've got nerve. Disengage or die.",
    "Not happening. Weapons free!",
};
// Epic 13: hailing the specific ship broadcasting an active ShipUnderAttack
// distress call.
static const char* DistressHailLines[] = {
    "MAYDAY! We're taking fire out here!",
    "Under attack, requesting assistance!",
    "Hostiles closing in — can you help?",
};
static const char* DistressAckLines[] = {
    "Copy that — hold on, we're inbound!",
    "Acknowledged. Standing by for support.",
    "Received. Doing what we can to hold out.",
};

void SpaceFlight::AddCommsMessage(const std::string& text, bool fromPlayer) {
    if (_bgTick) return;  // chatter from systems the player isn't in
    CommsEntry e;  e.text = text;  e.fromPlayer = fromPlayer;
    _commsLog.push_back(e);
    if (_commsLog.size() > kCommsLogCap) _commsLog.erase(_commsLog.begin());
}

void SpaceFlight::AdvanceTutorialStep(TutorialStep expected) {
    if (!_tutorialActive || _tutorialStep != expected) return;

    // One hint per step 1-7 (index == the step just completed); step 0 (Move)
    // gets its opening hint at tutorial start in OnEnter instead.
    static const char* kStepHints[] = {
        "TUTORIAL: Target an asteroid or your home station (click it).",
        "TUTORIAL: Destroy the targeted asteroid with your weapons.",
        "TUTORIAL: Fly over a material drop to collect it.",
        "TUTORIAL: Dock at your home station (ENTER when in range).",
        "TUTORIAL: Open Station Services and sell an item.",
        "TUTORIAL: Open Modules and equip one into a slot.",
        "TUTORIAL: Open the galaxy map and warp to an adjacent system.",
    };
    int idx = (int)_tutorialStep;
    _tutorialStep = (TutorialStep)(idx + 1);
    if (_tutorialStep == TutorialStep::Done) {
        _tutorialActive = false;
        AddCommsMessage("TUTORIAL COMPLETE. Good luck out there.", true);
    } else {
        AddCommsMessage(kStepHints[idx], true);
    }
}

void SpaceFlight::SkipTutorial() {
    if (!_tutorialActive) return;
    _tutorialActive = false;
    AddCommsMessage("TUTORIAL SKIPPED.", true);
}

void SpaceFlight::TickTutorial() {
    if (!_tutorialActive) return;
    if (_tutorialStep == TutorialStep::Move) {
        if (Vector2Distance(_playerEntity.transform.position, _tutorialStartPos) >= kTutorialMoveDistance)
            AdvanceTutorialStep(TutorialStep::Move);
    } else if (_tutorialStep == TutorialStep::Target) {
        bool asteroidTargeted = _target.valid && !_target.isNpc && !_target.isStellar;
        bool stationTargeted  = _target.valid && _target.isStellar && _target.hasFaction;
        if (asteroidTargeted || stationTargeted) AdvanceTutorialStep(TutorialStep::Target);
    }
}

static uint32_t s_npcEntityIdCounter = 10000;

static Faction FactionFromPaletteId(const std::string& pid) {
    if (pid == "faction_republic")                            return Faction::Republic;
    if (pid == "faction_zenith")                              return Faction::Zenith;
    if (pid == "faction_korrath")                             return Faction::Korrath;
    if (pid == "faction_merchant")                            return Faction::Merchant;
    if (pid == "faction_eden")                                return Faction::Eden;
    if (pid == "faction_reapers" || pid == "faction_reaperians") return Faction::Reavers;
    if (pid == "faction_forge")                               return Faction::Forge;
    if (pid == "faction_conclave")                            return Faction::Conclave;
    if (pid == "faction_void")                                return Faction::Void;
    return Faction::Merchant;
}

static NpcFaction RelationToNpcFaction(Relation r) {
    if (r == Relation::Friendly) return NpcFaction::Friendly;
    if (r == Relation::Hostile)  return NpcFaction::Hostile;
    return NpcFaction::Neutral;
}

// Same proximity rule as IsNearStation(), but hostile NPC stations don't
// count — you can't dock with (or board) something actively shooting at you.
// Player-owned stations are always enterable regardless of faction data.
bool SpaceFlight::IsNearEnterableStation() const {
    return FindEnterableStation().found;
}

SpaceFlight::EnterableStation SpaceFlight::FindEnterableStation() const {
    const Vector2& pos = _playerEntity.transform.position;
    for (const SpaceStation& s : _w->stations) {
        if (!s.alive) continue;
        if (Vector2Distance(pos, s.position) >= s.radius + 50.0f) continue;
        if (ReputationRegistry::PlayerRelation(s.faction) == Relation::Hostile) continue;
        return { true, false, s.id };
    }
    for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
        if (!ps.alive) continue;
        const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
        float rad = def ? def->radius : 120.0f;
        if (Vector2Distance(pos, ps.position) < rad + 50.0f) return { true, true, ps.id };
    }
    return {};
}

StationEconomy* SpaceFlight::FindStationEconomy(unsigned int stationId, bool isPlayerStation) {
    if (isPlayerStation) {
        for (PlayerStation& ps : FleetManager::Get().PlayerStations)
            if (ps.id == stationId) return &ps.economy;
        return nullptr;
    }
    for (SpaceStation& st : _w->stations)
        if (st.id == stationId) return &st.economy;
    return nullptr;
}

Faction SpaceFlight::FindStationFaction(unsigned int stationId, bool isPlayerStation) const {
    if (isPlayerStation) return kPlayerFaction;
    for (const SpaceStation& st : _w->stations)
        if (st.id == stationId) return st.faction;
    return kPlayerFaction;
}

bool SpaceFlight::FindNearestFriendlySeat(unsigned int& npcIdOut, int& hpIdxOut, Vector2& posOut) const {
    static constexpr float kSeatRange = 500.0f;
    float bestDist = kSeatRange;
    bool  found = false;
    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        const NpcMeta& m = _w->npcMeta[i];
        if (!m.alive || m.faction != NpcFaction::Friendly || m.hardpoints.empty()) continue;
        const ecs::Entity& e = _w->entities[i];
        for (int h = 0; h < (int)m.hardpoints.size(); ++h) {
            const Hardpoint& hp = m.hardpoints[h];
            if (!hp.alive) continue;
            Vector2 hpPos = GetCapitalHardpointWorldPos(e.transform.position, e.transform.rotation, hp.localOffset);
            float d = Vector2Distance(_playerEntity.transform.position, hpPos);
            if (d < bestDist) {
                bestDist = d;
                found = true;
                npcIdOut = m.id;
                hpIdxOut = h;
                posOut   = hpPos;
            }
        }
    }
    return found;
}

// Epic 9.1 (capture): a disabled hostile/neutral station or capital (see
// combat::IsDisabled, set on the rising edge in UpdateNpcCollisions) is
// captured the instant the player closes to docking-style range — no
// boarding minigame for v1. Reuses FindEnterableStation's radius+50 margin
// so "close enough to capture" reads the same as "close enough to dock".
// Host/singleplayer-only (see declaration) since only the host resolves
// hardpoint hits that set the disabled flag in the first place.
void SpaceFlight::UpdateCaptureProximity() {
    static constexpr float kCaptureApproachDist = 50.0f;
    const Vector2& pos = _playerEntity.transform.position;

    for (SpaceStation& st : _w->stations) {
        if (!st.alive || !st.disabled) continue;
        if (ReputationRegistry::PlayerRelation(st.faction) == Relation::Friendly) continue;
        if (Vector2Distance(pos, st.position) >= st.radius + kCaptureApproachDist) continue;
        st.faction     = kPlayerFaction;
        st.disabled    = false;
        st.retaliating = false;
        AddCommsMessage(st.stationTypeId + " captured! Now under your control.", true);
    }

    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        NpcMeta& m = _w->npcMeta[i];
        if (!m.alive || !m.disabled || m.faction == NpcFaction::Friendly) continue;
        const ecs::Entity& e = _w->entities[i];
        if (Vector2Distance(pos, e.transform.position) >= m.radius + kCaptureApproachDist) continue;
        m.faction             = NpcFaction::Friendly;
        m.npcFaction          = kPlayerFaction;
        m.disabled            = false;
        m.ionDisableTimer     = 0.0f;
        m.retaliatingVsPlayer = false;
        m.retaliationTargetId = 0;
        m.aiState             = NpcAiState::Patrol; // re-enters the guard/patrol logic from C.02b
        AddCommsMessage(m.shipTypeName + " captured! It has joined your fleet.", true);
    }
}

std::vector<Contract> SpaceFlight::GenerateContractOffers(Faction issuerFaction, unsigned int stationId, bool isPlayerStation) {
    std::vector<Contract> offers;

    // Bounty: kill a quota of ships from a faction hostile to the issuer.
    // Uses the static faction-vs-faction matrix (issuer's actual rivals),
    // not player reputation — a contract board reflects standing feuds.
    {
        std::vector<Faction> rivals;
        for (int i = 0; i < (int)Faction::COUNT; ++i) {
            Faction f = (Faction)i;
            if (f != issuerFaction && DiplomaticRegistry::Get(issuerFaction, f) == Relation::Hostile)
                rivals.push_back(f);
        }
        if (!rivals.empty()) {
            Faction target = rivals[GetRandomValue(0, (int)rivals.size() - 1)];
            Contract c;
            c.id             = _nextContractId++;
            c.type           = ContractType::Bounty;
            c.issuerFaction  = issuerFaction;
            c.targetFaction  = target;
            c.killsRequired  = GetRandomValue(2, 5);
            c.rewardCredits  = c.killsRequired * GetRandomValue(400, 700);
            c.rewardReputation = 6.0f;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Destroy %d %s vessel%s", c.killsRequired,
                          FactionName(target), c.killsRequired == 1 ? "" : "s");
            c.title    = buf;
            c.briefing = std::string(FactionName(issuerFaction)) + " wants " + FactionName(target) +
                         " activity suppressed. Pay is on quota completion, not per kill.";
            offers.push_back(std::move(c));
        }
    }

    // Courier: haul a good to another currently-generated same-faction
    // station (NPC stations only — player stations have nowhere canonical
    // to route to and no faction of their own beyond the player's).
    if (!isPlayerStation) {
        unsigned int destId = 0;
        uint64_t     destKey = 0;
        uint64_t     originKey = WorldKey(_w->galaxyId, _w->systemId);
        for (auto& [key, worldPtr] : _worlds) {
            if (key == originKey) continue;
            for (const SpaceStation& st : worldPtr->stations) {
                if (st.alive && st.faction == issuerFaction) { destId = st.id; destKey = key; break; }
            }
            if (destId != 0) break;
        }
        StationEconomy* econ = destId != 0 ? FindStationEconomy(stationId, isPlayerStation) : nullptr;
        if (econ) {
            const auto& mats = MaterialRegistry::All();
            if (!mats.empty()) {
                const MatDef& pick = mats[GetRandomValue(0, (int)mats.size() - 1)];
                int amount = std::min(econ->GetStock(pick.id), GetRandomValue(10, 30));
                if (amount > 0) {
                    Contract c;
                    c.id              = _nextContractId++;
                    c.type            = ContractType::Courier;
                    c.issuerFaction   = issuerFaction;
                    c.originStationId = stationId;
                    c.destStationId   = destId;
                    c.destWorldKey    = destKey;
                    c.goodId          = pick.id;
                    c.amount          = amount;
                    c.timeLimit       = 240.0f;
                    c.timeRemaining   = c.timeLimit;
                    c.rewardCredits   = amount * GetRandomValue(15, 30);
                    c.rewardReputation = 4.0f;
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "Haul %d %s cross-system", amount, pick.displayName);
                    c.title    = buf;
                    c.briefing = "Deliver within " + std::to_string((int)(c.timeLimit / 60.0f)) +
                                 " minutes of accepting or the contract lapses.";
                    offers.push_back(std::move(c));
                }
            }
        }
    }

    // Escort: protect a currently-alive same-faction Trader-role NPC in this system.
    {
        unsigned int traderId = 0;
        for (const NpcMeta& m : _w->npcMeta) {
            if (m.alive && !m.wingman && m.role == NpcRole::Trader && m.npcFaction == issuerFaction) {
                traderId = m.id;
                break;
            }
        }
        if (traderId != 0) {
            Contract c;
            c.id              = _nextContractId++;
            c.type            = ContractType::Escort;
            c.issuerFaction   = issuerFaction;
            c.escortNpcId     = traderId;
            c.timeLimit       = 120.0f;
            c.timeRemaining   = c.timeLimit;
            c.rewardCredits   = 1200;
            c.rewardReputation = 8.0f;
            c.title    = "Escort a trade convoy";
            c.briefing = std::string("Keep the marked ") + FactionName(issuerFaction) +
                         " trader alive for 2 minutes. Raiders are known to hunt this route.";
            offers.push_back(std::move(c));
        }
    }

    return offers;
}

void SpaceFlight::TickActiveContract(float dt) {
    if (!_hasActiveContract) return;

    if (_activeContract.type == ContractType::Bounty) {
        if (_activeContract.killsDone >= _activeContract.killsRequired) CompleteActiveContract();
        return;
    }

    if (_activeContract.type == ContractType::Escort) {
        bool alive = false;
        for (const NpcMeta& m : _w->npcMeta)
            if (m.id == _activeContract.escortNpcId && m.alive) { alive = true; break; }
        if (!alive) { FailActiveContract("Escort target destroyed."); return; }
    }

    _activeContract.timeRemaining -= dt;
    if (_activeContract.timeRemaining <= 0.0f) {
        FailActiveContract(_activeContract.type == ContractType::Courier
                                ? "Delivery window expired." : "Escort window expired.");
    }
}

void SpaceFlight::CompleteActiveContract() {
    InventoryManager::Get().AddCredits(_activeContract.rewardCredits);
    ReputationRegistry::Adjust(_activeContract.issuerFaction, _activeContract.rewardReputation);
    AddCommsMessage("CONTRACT COMPLETE: " + _activeContract.title + " (+" +
                     std::to_string(_activeContract.rewardCredits) + "cr)", true);
    _hasActiveContract = false;
    _activeContract = Contract{};
}

void SpaceFlight::FailActiveContract(const std::string& reason) {
    AddCommsMessage("CONTRACT FAILED: " + reason, true);
    _hasActiveContract = false;
    _activeContract = Contract{};
}

SpaceStation* SpaceFlight::FindWorldStation(uint64_t worldKey, unsigned int stationId) {
    auto it = _worlds.find(worldKey);
    if (it == _worlds.end()) return nullptr;
    for (SpaceStation& st : it->second->stations)
        if (st.id == stationId && st.alive) return &st;
    return nullptr;
}

// Epic 3.2/3.3: background station-economy simulation for Industrialist
// (production) and Trader (cross-system hauling) role NPCs. Runs regardless
// of the NPC's current NpcAiState/movement — Trader/Industrialist don't have
// their own distinct movement pattern yet (Epic 2.6/2.7, blocked until this
// landed; still unstarted), so they keep drifting/patrolling like any other
// undifferentiated role while this quietly moves stock numbers around.
void SpaceFlight::TickNpcEconomy(NpcMeta& m, float dt) {
    if (m.role != NpcRole::Industrialist && m.role != NpcRole::Trader) return;
    if (m.wingman) return; // wingmen work for the player, not their faction's economy

    uint64_t homeKey = WorldKey(_w->galaxyId, _w->systemId);

    // Lazy home-station assignment: the one same-faction SpaceStation in this
    // NPC's current system, if one exists. Most systems are uncontrolled
    // (StarSystemRegistry::Generate's kControlChance=0.15f) and NPCs can't
    // warp between systems today, so most Industrialists/Traders never find
    // a home and stay economically idle — same single-system-confinement
    // scope limit as Explorer (Epic 2.3).
    if (m.homeStationId == 0) {
        for (const SpaceStation& st : _w->stations) {
            if (st.alive && st.faction == m.npcFaction) {
                m.homeStationId = st.id;
                m.homeWorldKey  = homeKey;
                break;
            }
        }
        if (m.homeStationId == 0) return;
    }
    SpaceStation* home = FindWorldStation(m.homeWorldKey, m.homeStationId);
    if (!home) { m.homeStationId = 0; return; } // home destroyed; re-search next tick

    m.economyTickTimer -= dt;
    if (m.economyTickTimer > 0.0f) return;

    if (m.role == NpcRole::Industrialist) {
        m.economyTickTimer = (float)GetRandomValue(8, 15);
        const auto& mats = MaterialRegistry::All();
        if (!mats.empty()) {
            const MatDef& pick = mats[GetRandomValue(0, (int)mats.size() - 1)];
            home->economy.AddStock(pick.id, GetRandomValue(5, 15));
        }
        TryColonizeAdjacentSystem(m.npcFaction);
        return;
    }

    // Trader: needs a second, different-system, same-faction station to haul
    // toward/from — a system holds at most one controlled station
    // (SpawnPlanetsAndStations), so a route is necessarily cross-system.
    // Re-search each tick until one turns up; the pool grows as the player
    // visits more systems (only currently-generated worlds are searched).
    SpaceStation* dest = (m.tradeDestId != 0) ? FindWorldStation(m.destWorldKey, m.tradeDestId) : nullptr;
    if (!dest) {
        m.tradeDestId = 0;
        for (auto& [key, worldPtr] : _worlds) {
            if (key == m.homeWorldKey) continue;
            for (const SpaceStation& st : worldPtr->stations) {
                if (st.alive && st.faction == m.npcFaction) {
                    m.tradeDestId = st.id;
                    m.destWorldKey = key;
                    break;
                }
            }
            if (m.tradeDestId != 0) break;
        }
        dest = (m.tradeDestId != 0) ? FindWorldStation(m.destWorldKey, m.tradeDestId) : nullptr;
    }
    m.economyTickTimer = (float)GetRandomValue(15, 30);
    if (!dest) return; // nowhere to haul to yet

    SpaceStation* origin = m.haulingToDest ? home : dest;
    SpaceStation* target = m.haulingToDest ? dest : home;

    // Pick the origin's biggest-surplus good to export; if nothing is above
    // baseline right now, skip this leg rather than force a delivery.
    std::string good;
    {
        int bestSurplus = StationEconomy::kBaselineStock;
        auto consider = [&](const std::string& id) {
            int s = origin->economy.GetStock(id);
            if (s > bestSurplus) { bestSurplus = s; good = id; }
        };
        for (const MatDef&  mt : MaterialRegistry::All()) consider(mt.id);
        for (const ItemDef& it : ItemRegistry::All())      consider(it.id);
    }
    if (good.empty()) return;

    int amount = std::min(origin->economy.GetStock(good), 20);
    if (amount <= 0) return;
    origin->economy.RemoveStock(good, amount);
    target->economy.AddStock(good, amount);
    m.tradeGoodId    = good;
    m.haulingToDest  = !m.haulingToDest;
}

// Epic 5.2: no literal NPC travel exists between systems, so "founding a
// station" is modeled as a rare background roll on the Industrialist's own
// economy tick rather than a physical journey — flips one adjacent (grid-
// neighbor, same lattice math as Generate()) uncontrolled system's
// StarSystemRegistry override. SpawnPlanetsAndStations already reads
// isControlled to decide whether to spawn a station, so the very next time
// that system's world is generated (player warps there, or it's swept up in
// a future background-world pass), a real SpaceStation appears there for
// free — no separate station-creation path to maintain.
void SpaceFlight::TryColonizeAdjacentSystem(Faction f) {
    if (GetRandomValue(1, 1000) > 15) return; // ~1.5% per Industrialist tick — rare by design

    unsigned int dim = StarSystemRegistry::GridDim();
    unsigned int idx = _w->systemId - 1;
    long long cx = (long long)(idx % dim);
    long long cy = (long long)(idx / dim);

    std::vector<StarSystem> candidates;
    for (long long dy = -1; dy <= 1; ++dy) {
        for (long long dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            long long ncx = cx + dx, ncy = cy + dy;
            if (ncx < 0 || ncy < 0 || ncx >= (long long)dim || ncy >= (long long)dim) continue;
            unsigned int nid = (unsigned int)(ncy * (long long)dim + ncx + 1);
            if (nid == 0 || nid > StarSystemRegistry::Count() || nid == 1) continue; // 1 = home, always Republic
            auto sys = StarSystemRegistry::ById(nid);
            if (sys && !sys->isControlled) candidates.push_back(*sys);
        }
    }
    if (candidates.empty()) return;

    const StarSystem& chosen = candidates[GetRandomValue(0, (int)candidates.size() - 1)];
    StarSystemRegistry::SetControlled(chosen.id, f);
    AddCommsMessage(std::string("INTEL: ") + FactionName(f) +
                     " forces have established a new station at " +
                     StarSystemRegistry::NameOf(chosen.seed) + ".");
}

// `suppressHostile` (Epic 12.3): while the tutorial protects the home
// system, a roll that would have come up Hostile is downgraded to Neutral
// instead — a wary bystander rather than a fight the player isn't ready for.
static std::pair<ecs::Entity, NpcMeta> MakeNpcEntity(unsigned int npcId, Vector2 pos, bool suppressHostile = false) {
    // 1. AUTO-INJECT: Pick a random ship definition directly from the JSON Registry
    const auto& allShips = ecs::ShipRegistry::AllShips();
    int randIdx = GetRandomValue(0, allShips.size() - 1);
    const ecs::ShipDef& chosenShip = allShips[randIdx];

    // 2. Derive faction from ship palette then DiplomaticRegistry
    NpcMeta m;
    m.npcFaction = FactionFromPaletteId(chosenShip.paletteId);
    m.role = RollNpcRole(m.npcFaction);
    NpcFaction faction = RelationToNpcFaction(ReputationRegistry::PlayerRelation(m.npcFaction));
    if (suppressHostile && faction == NpcFaction::Hostile) faction = NpcFaction::Neutral;
    Color primaryColor;
    switch (faction) {
        case NpcFaction::Hostile:  primaryColor = RED;    break;
        case NpcFaction::Friendly: primaryColor = GREEN;  break;
        default:                   primaryColor = SKYBLUE; break;
    }

    // 3. Delegate to the Data-Driven Factory! (This handles array baking automatically)
    ecs::Entity e = ecs::FleetManager::SpawnShip(chosenShip.id, pos, primaryColor, WHITE);
    e.id = npcId;
    e.aiController.state = ecs::AIState::Patrol;
    e.aiController.targetPosition = pos;

    // 4. Setup the legacy Meta tracker
    m.id = npcId;
    m.faction = faction;
    m.waypoint = pos;
    m.waypointSet = false;
    m.shipTypeId = chosenShip.id;
    m.shipTypeName = chosenShip.displayName;
    m.radius = chosenShip.radius;

    if (chosenShip.shipType == ShipType::Capital)
        m.hardpoints = BuildCapitalHardpoints(chosenShip);

    // (Keep your existing random module loadout logic here)
    m.loadout.Resize(NpcMeta::WSlots, NpcMeta::ShSlots, NpcMeta::AuxSlots);

    for (auto* slot : m.loadout.WeaponSlots())
        slot->equipped = ModuleRegistry::Random(ModuleType::Weapon, ModuleRegistry::RollGrade());

    if (auto* armorSlot = m.loadout.Armor())
        armorSlot->equipped = ModuleRegistry::Random(ModuleType::Armor, ModuleRegistry::RollGrade());
    if (auto* engineSlot = m.loadout.Engine())
        engineSlot->equipped = ModuleRegistry::Random(ModuleType::Engine, ModuleRegistry::RollGrade());

    for (auto* slot : m.loadout.ShieldSlots())
        slot->equipped = ModuleRegistry::Random(ModuleType::Shield, ModuleRegistry::RollGrade());

    return { e, m };
}

void SpaceFlight::PlaceFriendlyShip(SystemWorld& world, const std::string& shipDefId, Vector2 pos, Faction placerFaction) {
    auto [alliedE, alliedM] = MakeNpcEntity(world.nextNpcId++, pos);
    alliedM.faction    = NpcFaction::Friendly;
    alliedM.shipTypeId = shipDefId;

    std::string dName = "Ship";
    float mHull = 100.0f;
    const ecs::ShipDef* placedDef = ecs::ShipRegistry::ShipById(shipDefId);
    if (placedDef) {
        dName = placedDef->displayName;
        mHull = placedDef->baseStats.hull;
    }

    // MakeNpcEntity() rolled m.npcFaction (the DiplomaticRegistry/ReputationRegistry
    // faction that ALL real hostility checks — combat::FindNearestHostileTarget,
    // UpdateCapitalFire's retaliation logic — actually use, as opposed to the
    // purely cosmetic m.faction (NpcFaction::Friendly, just the green tint)
    // set above) from a RANDOM ship pick before shipTypeId was overwritten.
    // A prior fix re-derived it from the PLACED ship's own paletteId instead
    // (e.g. republic_battlecruiser -> Faction::Republic) — still wrong: a
    // ship the player builds and places belongs to the PLAYER's faction, not
    // whatever faction the hull asset happens to be palette-skinned as. If
    // the player isn't playing as that ship's palette faction (e.g. Zenith
    // player placing the Republic-skinned battlecruiser, and Republic is
    // Hostile to Zenith on the base matrix), the "friendly" green ship was
    // diplomatically hostile to its own builder and opened fire on them —
    // reported by user 2026-07-09. Fixed to match UpdateCaptureProximity's
    // already-correct pattern for a captured ship joining the player's fleet
    // (`m.npcFaction = kPlayerFaction`) — ownership determines allegiance,
    // not hull skin.
    alliedM.npcFaction = placerFaction;
    alliedM.role = RollNpcRole(alliedM.npcFaction); // re-roll: same stale-random-pick issue as npcFaction above

    alliedM.shipTypeName = dName;
    alliedM.wingman = false; // Player side but NOT part of escort
    alliedE.health.maxStats.hull = mHull;
    alliedE.health.currentHull   = mHull;
    ApplyNpcLoadout(alliedE, alliedM); // Assign generic NPC loadout to fight with

    // MakeNpcEntity() also rolled hardpoints (if any) for that same RANDOM
    // ship type. Rebuild them from the actual placed def so a placed capital
    // gets its own hardpoint layout/radius instead of a random pick's (or an
    // empty list).
    if (placedDef && placedDef->shipType == ShipType::Capital) {
        alliedM.hardpoints = BuildCapitalHardpoints(*placedDef);
        alliedM.radius     = placedDef->radius;
    }
    alliedE.network.networkId = alliedE.id;   // expose NPC to HostBroadcast
    if (!world.npcFreeSlots.empty()) {
        size_t slot = world.npcFreeSlots.back(); world.npcFreeSlots.pop_back();
        world.entities[slot] = std::move(alliedE); world.npcMeta[slot] = std::move(alliedM);
    } else {
        world.entities.push_back(std::move(alliedE)); world.npcMeta.push_back(std::move(alliedM));
    }
}

void SpaceFlight::SpawnNpcShips() {
    _w->entities.clear();
    _w->npcMeta.clear();
    _w->npcFreeSlots.clear();
    _w->nextNpcId = 1000;

    int count = GetRandomValue(3, 5);
    float minDist = std::max(700.0f, _w->sun.active ? _w->sun.gravRange + 500.0f : 700.0f);
    float maxDist = std::max(1400.0f, _w->sun.active ? _w->sun.gravRange + 1500.0f : 1400.0f);
    // Epic 12.3: suppress hostile rolls while the tutorial protects the home system.
    bool suppressHostile = _tutorialActive && _w->galaxyId == 1 && _w->systemId == 1;

    for (int i = 0; i < count; ++i) {
        Vector2 pos = { 0.0f, 0.0f };

        // 50-attempt retry loop to find a safe distance from the player
        for (int attempt = 0; attempt < 50; ++attempt) {
            float ang = (float)GetRandomValue(0, 359) * DEG2RAD;
            float dist = minDist + (float)GetRandomValue(0, (int)(maxDist - minDist));
            pos = { cosf(ang) * dist, sinf(ang) * dist };

            // Check if this NPC spawn is too close to the player's chosen spawn position
            if (Vector2Distance(pos, _w->playerSpawnPos) >= kEnemySpawnMargin) {
                break;
            }
        }

        auto [entity, meta] = MakeNpcEntity(_w->nextNpcId++, pos, suppressHostile);
        ApplyNpcLoadout(entity, meta);
        meta.preferredRange = meta.attackRange * 0.75f;
        entity.health.currentHull = entity.health.maxStats.hull;
        entity.network.networkId = entity.id;   // expose NPC to HostBroadcast

        _w->entities.push_back(std::move(entity));
        _w->npcMeta.push_back(std::move(meta));
    }
}

// Epic 5.3: destroyed NPC stations aren't permanent — after a cooldown, the
// controlling faction rebuilds (fresh hull/hardpoints, economy reseeded at a
// reduced level reflecting the setback). Host/singleplayer only: no net
// message broadcasts a revival yet, so a connected client's copy of a
// destroyed station just stays dead until its next full state sync happens
// to include the rebuild — same class of flagged-but-unsynced gap as Epic
// 3/4's own state (see tasks_spaceflight_dynamics.md's cross-cutting sync
// note; #5 NPC-built stations is explicitly one of the epics that rule
// covers).
void SpaceFlight::TickStationRebuilds(float dt) {
    if (net::Game().IsClient()) return;
    for (SpaceStation& st : _w->stations) {
        if (st.alive || !st.rebuilding) continue;
        st.rebuildTimer -= dt;
        if (st.rebuildTimer > 0.0f) continue;
        st.alive      = true;
        st.rebuilding = false;
        st.hull       = st.maxHull;
        BuildNpcStationHardpoints(st);
        SeedStationEconomy(st.economy, 0.6f); // rebuilt smaller than a founding station
        AddCommsMessage(std::string("INTEL: ") + FactionName(st.faction) + " forces have rebuilt a station.");
    }
}

void SpaceFlight::TryStartAttackDistressCall(const NpcMeta& m, Vector2 pos) {
    if (_w->hasActiveDistress) return;
    if (GetRandomValue(0, 99) >= kDistressAttackCallChancePct) return;

    DistressCall dc;
    dc.type             = DistressType::ShipUnderAttack;
    dc.position          = pos;
    dc.npcId             = m.id;
    dc.issuerFaction      = m.npcFaction;
    dc.windowSeconds      = kDistressAttackWindowSeconds;
    dc.rewardCredits      = GetRandomValue(300, 700);
    dc.rewardReputation   = 10.0f;
    _w->activeDistress    = dc;
    _w->hasActiveDistress = true;
    AddCommsMessage(std::string("DISTRESS CALL: ") + FactionName(m.npcFaction) + " " + m.shipTypeName +
                     " under attack! Keep it alive to earn a reward.");
}

void SpaceFlight::TickDistressCalls(float dt) {
    if (!_w->hasActiveDistress) return;
    DistressCall& dc = _w->activeDistress;
    dc.timer += dt;

    if (dc.type == DistressType::Stranded) {
        if (dc.timer < dc.windowSeconds) return;
        if (_fuel < kMaxFuel - 0.01f) {
            _fuel = std::min(kMaxFuel, _fuel + kDistressFuelAmount);
            AddCommsMessage("DISTRESS CALL ANSWERED: a passing vessel transfers emergency fuel cells (+" +
                             std::to_string((int)kDistressFuelAmount) + ").");
        }
        _w->hasActiveDistress = false;
        _w->activeDistress    = DistressCall{};
        return;
    }

    // ShipUnderAttack
    bool stillAlive = false;
    for (const NpcMeta& m : _w->npcMeta) {
        if (m.id == dc.npcId && m.alive) { stillAlive = true; break; }
    }
    if (!stillAlive) {
        AddCommsMessage("DISTRESS CALL FAILED: the vessel was lost.");
        _w->hasActiveDistress = false;
        _w->activeDistress    = DistressCall{};
        return;
    }
    if (dc.timer >= dc.windowSeconds) {
        InventoryManager::Get().AddCredits(dc.rewardCredits);
        ReputationRegistry::Adjust(dc.issuerFaction, dc.rewardReputation);
        AddCommsMessage("DISTRESS CALL RESOLVED: vessel saved (+" +
                         std::to_string(dc.rewardCredits) + "cr)", true);
        _w->hasActiveDistress = false;
        _w->activeDistress    = DistressCall{};
    }
}

void SpaceFlight::UpdateNpcShips(float dt) {
    // Player-built stations (FleetManager) aren't tagged with a system and
    // belong to the player's world — hide them from AI in background worlds.
    static const std::vector<PlayerStation> kNoPlayerStations;
    const auto& playerStations = _bgTick ? kNoPlayerStations
                                         : FleetManager::Get().PlayerStations;

    TickStationRebuilds(dt);
    TickDistressCalls(dt);

    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        NpcMeta&    m = _w->npcMeta[i];
        ecs::Entity& e = _w->entities[i];
        if (!m.alive) continue;

        if (m.docked) {
            e.health.currentHull = std::min(
                e.health.currentHull + repair::kDockHealPerSecond * e.health.maxStats.hull * dt,
                e.health.maxStats.hull);
            if (e.health.currentHull >= e.health.maxStats.hull) {
                m.docked = false;
                m.aiState = NpcAiState::Patrol;
                m.waypointSet = false;
                m.retaliatingVsPlayer = false;
                m.retaliationTargetId = 0;
            }
            continue;
        }

        // Epic 9.2 (fighter capture): ion-stunned — frozen in place (no
        // movement/economy/firing this frame) until either the player
        // captures it (UpdateCaptureProximity, separate from this loop) or
        // the timer runs out and it re-engages on its own.
        if (m.aiState == NpcAiState::Disabled) {
            m.ionDisableTimer -= dt;
            if (m.ionDisableTimer <= 0.0f) {
                m.disabled = false;
                m.aiState  = NpcAiState::Chase;
                AddCommsMessage(m.shipTypeName + " has recovered from the ion strike!");
            }
            continue;
        }

        TickNpcEconomy(m, dt);

        // P3/P5: capitals aren't a HardpointRig (meta.hardpoints is a bare
        // vector<Hardpoint>), so recompute their power budget directly here,
        // once per tick — same shared logic fighters get via ApplyNpcLoadout.
        // Adjacency must run first — it feeds adjacencyPowerDrawMult into the
        // power budget below.
        if (!m.hardpoints.empty()) {
            RecalculateAdjacency(m.hardpoints);
            RecalculatePowerBudget(m.hardpoints, HardpointRig::kStationBaseCapacity);
        }

        float distToPlayer = Vector2Distance(e.transform.position, _playerEntity.transform.position);

        if (m.faction == NpcFaction::Hostile && !m.wingman) {
            float distToClosestTarget = distToPlayer;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                if (d < distToClosestTarget) distToClosestTarget = d;
            }
            for (const PlayerStation& ps : playerStations) {
                if (!ps.alive) continue;
                float d = Vector2Distance(e.transform.position, ps.position);
                if (d < distToClosestTarget) distToClosestTarget = d;
            }
            for (const SpaceStation& st : _w->stations) {
                if (!st.alive) continue;
                if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, st.position);
                if (d < distToClosestTarget) distToClosestTarget = d;
            }

            switch (m.aiState) {
            case NpcAiState::Patrol:
                if (distToClosestTarget < m.aggroRange) {
                    m.aiState = NpcAiState::Chase;
                    if (!m.hasAnnounced) {
                        int idx = GetRandomValue(0, 2);
                        AddCommsMessage(std::string("HOSTILE: \"") + AggroLines[idx] + "\"");
                        m.hasAnnounced = true;
                    }
                }
                break;
            case NpcAiState::Chase:
                if (distToClosestTarget < m.attackRange)       m.aiState = NpcAiState::Attack;
                if (distToClosestTarget > m.aggroRange * 1.6f) {
                    m.aiState = NpcAiState::Patrol;
                    m.waypointSet = false;
                }
                break;
            case NpcAiState::Attack: {
                if (distToClosestTarget > m.attackRange * 1.4f) m.aiState = NpcAiState::Chase;
                // Military holds the line for duty/discipline; Raiders press
                // recklessly per their "strike without warning" doctrine —
                // both flee later than the 20% default (Epic 2.4/2.5).
                float fleeThreshold = m.role == NpcRole::Military ? 0.15f
                                    : m.role == NpcRole::Raider   ? 0.10f : 0.20f;
                if (e.health.currentHull / e.health.maxStats.hull < fleeThreshold) {
                    m.aiState = NpcAiState::Flee;
                    int idx = GetRandomValue(0, 1);
                    AddCommsMessage(std::string("HOSTILE: \"") + FleeLines[idx] + "\"");
                }
                break;
            }
            case NpcAiState::Flee:
                if (distToClosestTarget > m.aggroRange * 2.2f &&
                    !repair::FindNearestFriendlyDock(*_w, m.npcFaction, e.transform.position).valid) {
                    m.aiState = NpcAiState::Patrol;
                    m.waypointSet = false;
                }
                break;
            default: break;
            }
        }
        else {
            if (m.wingman) {
                if (e.health.currentHull / e.health.maxStats.hull < 0.20f) {
                    m.aiState = NpcAiState::Flee;
                    m.waypointSet = false;
                }
                else {
                    m.aiState = NpcAiState::Escort;
                }
            }
            else {
                if (!m.hasGreeted && distToPlayer < m.aggroRange * 0.9f) {
                    int idx = GetRandomValue(0, 4);
                    const char* prefix = (m.faction == NpcFaction::Friendly) ? "FRIENDLY" : "UNKNOWN";
                    AddCommsMessage(std::string(prefix) + ": \"" + FriendlyLines[idx] + "\"");
                    m.hasGreeted = true;
                }
                float closestHostileDist = FLT_MAX;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < closestHostileDist) closestHostileDist = d;
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < closestHostileDist) closestHostileDist = d;
                }
                if (m.retaliatingVsPlayer) {
                    float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                    if (d < closestHostileDist) closestHostileDist = d;
                }
                if (m.retaliationTargetId != 0) {
                    for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                        if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                            if (d < closestHostileDist) closestHostileDist = d;
                            break;
                        }
                    }
                }
                if (e.health.currentHull / e.health.maxStats.hull < 0.20f) {
                    if (m.aiState != NpcAiState::Flee) {
                        m.aiState = NpcAiState::Flee; m.waypointSet = false;
                        TryStartAttackDistressCall(m, e.transform.position);
                    }
                } else if (m.aiState == NpcAiState::Flee) {
                    if (closestHostileDist > m.aggroRange * 2.2f &&
                        !repair::FindNearestFriendlyDock(*_w, m.npcFaction, e.transform.position).valid) {
                        m.aiState = NpcAiState::Patrol; m.waypointSet = false;
                        m.retaliatingVsPlayer = false; m.retaliationTargetId = 0;
                    }
                } else if (m.aiState == NpcAiState::Attack) {
                    if (closestHostileDist > m.attackRange * 1.4f) m.aiState = NpcAiState::Chase;
                    if (closestHostileDist > m.aggroRange * 1.6f) {
                        m.aiState = NpcAiState::Patrol; m.waypointSet = false;
                        m.retaliatingVsPlayer = false; m.retaliationTargetId = 0;
                    }
                } else if (m.aiState == NpcAiState::Chase) {
                    if (closestHostileDist <= m.attackRange)        m.aiState = NpcAiState::Attack;
                    if (closestHostileDist > m.aggroRange * 1.6f) {
                        m.aiState = NpcAiState::Patrol; m.waypointSet = false;
                        m.retaliatingVsPlayer = false; m.retaliationTargetId = 0;
                    }
                } else {
                    if (closestHostileDist <= m.aggroRange) m.aiState = NpcAiState::Chase;
                    else {
                        m.aiState = NpcAiState::Patrol;
                        m.retaliatingVsPlayer = false; m.retaliationTargetId = 0;
                    }
                }
            }
        }

        float desiredRot = e.transform.rotation;
        float thrustMult = 1.0f;
        Vector2 lateralBoost = { 0.0f, 0.0f };

        switch (m.aiState) {
        case NpcAiState::Patrol: {
            // Player-allied capitals (built via StationModuleMenu, not wingmen)
            // guard the player's nearest station, or cycle through random
            // points across the system when no player station exists.
            if (!m.hardpoints.empty() && m.faction == NpcFaction::Friendly) {
                Vector2 guardTarget = {};
                bool    hasStation  = false;
                float   holdRadius  = 250.0f;
                float   bestDist    = 0.0f;
                for (const PlayerStation& ps : playerStations) {
                    if (!ps.alive) continue;
                    float dist = Vector2Distance(e.transform.position, ps.position);
                    if (!hasStation || dist < bestDist) {
                        guardTarget = ps.position;
                        bestDist    = dist;
                        hasStation  = true;
                    }
                }
                if (!hasStation) {
                    if (!m.waypointSet || Vector2Distance(e.transform.position, m.waypoint) < 200.0f) {
                        float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
                        float dist = (float)GetRandomValue(600, 2500);
                        m.waypoint    = { cosf(ang) * dist, sinf(ang) * dist };
                        m.waypointSet = true;
                    }
                    guardTarget = m.waypoint;
                    holdRadius  = 0.0f; // no loiter — keep cycling waypoint to waypoint
                }
                if (Vector2Distance(e.transform.position, guardTarget) > holdRadius) {
                    Vector2 toP = Vector2Subtract(guardTarget, e.transform.position);
                    desiredRot  = atan2f(toP.x, -toP.y) * RAD2DEG;
                    thrustMult  = 0.5f;
                } else {
                    thrustMult = 0.0f; // holding position near the guarded station
                }
                break;
            }
            // Explorer-role NPCs (Epic 2.3) actively roam the system between
            // random waypoints instead of idly drifting forward — the same
            // waypoint-cycle pattern the capital no-station fallback above
            // already uses. Every other role keeps the old drift behavior
            // until its own Epic 2.x behavior lands.
            if (m.role == NpcRole::Explorer && !m.wingman) {
                if (!m.waypointSet || Vector2Distance(e.transform.position, m.waypoint) < 150.0f) {
                    float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
                    float dist = (float)GetRandomValue(500, 3000);
                    m.waypoint    = { cosf(ang) * dist, sinf(ang) * dist };
                    m.waypointSet = true;
                }
                Vector2 toP = Vector2Subtract(m.waypoint, e.transform.position);
                desiredRot = atan2f(toP.x, -toP.y) * RAD2DEG;
                thrustMult = 0.6f;
                break;
            }
            // Trader-role NPCs (Epic 2.6) fly a supply-run loop instead of
            // drifting: haul out to a departure point away from their home
            // station while TickNpcEconomy's haulingToDest flag (Epic 3.3)
            // is true ("cargo loaded, outbound"), then return and idle at
            // the dock while it's false ("delivering/between hauls"). Reuses
            // the same flag the background economy tick already toggles per
            // leg so movement roughly tracks the simulated trade route
            // rather than running an unrelated random walk.
            if (m.role == NpcRole::Trader && !m.wingman && m.homeStationId != 0) {
                SpaceStation* home = FindWorldStation(m.homeWorldKey, m.homeStationId);
                if (home) {
                    Vector2 target;
                    float   holdRadius;
                    if (m.haulingToDest) {
                        if (!m.waypointSet || Vector2Distance(e.transform.position, m.waypoint) < 200.0f) {
                            float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
                            float dist = (float)GetRandomValue(1500, 4000);
                            m.waypoint    = Vector2Add(home->position, { cosf(ang) * dist, sinf(ang) * dist });
                            m.waypointSet = true;
                        }
                        target     = m.waypoint;
                        holdRadius = 0.0f;
                    } else {
                        target        = home->position;
                        holdRadius    = 180.0f;
                        m.waypointSet = false; // re-roll a fresh departure point next haul-out leg
                    }
                    if (Vector2Distance(e.transform.position, target) > holdRadius) {
                        Vector2 toP = Vector2Subtract(target, e.transform.position);
                        desiredRot  = atan2f(toP.x, -toP.y) * RAD2DEG;
                        thrustMult  = 0.55f;
                    } else {
                        thrustMult = 0.0f; // idling at dock between hauls
                    }
                    break;
                }
            }
            // Industrialist-role NPCs (Epic 2.7) loiter near an asteroid
            // field instead of drifting — visual flavor for the background
            // production Epic 3.2 already simulates numerically. Holds near
            // the nearest alive asteroid; a small per-frame chance to
            // re-scan while holding (rather than a dedicated timer field)
            // catches the target asteroid being destroyed or drifting far.
            if (m.role == NpcRole::Industrialist && !m.wingman) {
                if (!m.waypointSet || GetRandomValue(1, 600) == 1) {
                    Vector2 best = {};
                    bool    found = false;
                    float   bestDist = 0.0f;
                    for (const Asteroid& a : _w->asteroids) {
                        if (!a.alive) continue;
                        float d = Vector2Distance(e.transform.position, a.position);
                        if (!found || d < bestDist) { best = a.position; bestDist = d; found = true; }
                    }
                    if (found) { m.waypoint = best; m.waypointSet = true; }
                }
                if (m.waypointSet) {
                    const float holdRadius = 220.0f;
                    if (Vector2Distance(e.transform.position, m.waypoint) > holdRadius) {
                        Vector2 toP = Vector2Subtract(m.waypoint, e.transform.position);
                        desiredRot  = atan2f(toP.x, -toP.y) * RAD2DEG;
                        thrustMult  = 0.5f;
                    } else {
                        thrustMult = 0.0f; // holding position, "mining" the field
                    }
                    break;
                }
            }
            thrustMult = 0.50f;
            break;
        }
        case NpcAiState::Chase: {
            Vector2 chaseTarget = _playerEntity.transform.position;
            if (m.faction == NpcFaction::Hostile) {
                float best = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; chaseTarget = GetNpcAimPos(*_w, j); }
                }
                for (const PlayerStation& ps : playerStations) {
                    if (!ps.alive) continue;
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                    float rad = def ? def->radius : 120.0f;
                    Vector2 aimPos = GetBestHardpointAimPos(ps, rad);
                    float d = Vector2Distance(e.transform.position, aimPos);
                    if (d < best) { best = d; chaseTarget = aimPos; }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; chaseTarget = GetStationAimPos(st); }
                }
            }
            else {
                float best = FLT_MAX;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; chaseTarget = GetNpcAimPos(*_w, j); }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; chaseTarget = GetStationAimPos(st); }
                }
                if (m.retaliatingVsPlayer) {
                    float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                    if (d < best) { best = d; chaseTarget = _playerEntity.transform.position; }
                }
                if (m.retaliationTargetId != 0) {
                    for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                        if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                            if (d < best) { best = d; chaseTarget = GetNpcAimPos(*_w, j); }
                            break;
                        }
                    }
                }
                if (best == FLT_MAX) { thrustMult = 0.5f; break; }
            }
            Vector2 toP = Vector2Subtract(chaseTarget, e.transform.position);
            desiredRot = atan2f(toP.x, -toP.y) * RAD2DEG;
            thrustMult = 1.0f;
            break;
        }
        case NpcAiState::Attack: {
            Vector2 attackTarget = _playerEntity.transform.position;
            bool    hasTarget    = false;
            if (ReputationRegistry::PlayerRelation(m.npcFaction) == Relation::Hostile) {
                float best = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                hasTarget  = true;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; attackTarget = GetNpcAimPos(*_w, j); }
                }
                for (const PlayerStation& ps : playerStations) {
                    if (!ps.alive) continue;
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                    float rad = def ? def->radius : 120.0f;
                    Vector2 aimPos = GetBestHardpointAimPos(ps, rad);
                    float d = Vector2Distance(e.transform.position, aimPos);
                    if (d < best) { best = d; attackTarget = aimPos; }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; attackTarget = GetStationAimPos(st); }
                }
            } else {
                float best = FLT_MAX;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; attackTarget = GetNpcAimPos(*_w, j); hasTarget = true; }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; attackTarget = GetStationAimPos(st); hasTarget = true; }
                }
                if (m.retaliatingVsPlayer) {
                    float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                    if (d < best) { best = d; attackTarget = _playerEntity.transform.position; hasTarget = true; }
                }
                if (m.retaliationTargetId != 0) {
                    for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                        if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                            if (d < best) { best = d; attackTarget = GetNpcAimPos(*_w, j); hasTarget = true; }
                            break;
                        }
                    }
                }
                if (!hasTarget) { thrustMult = 0.5f; break; }
            }

            Vector2 toP  = Vector2Subtract(attackTarget, e.transform.position);
            float   dist = sqrtf(toP.x * toP.x + toP.y * toP.y);

            // Always face the target so weapons stay aimed
            desiredRot = atan2f(toP.x, -toP.y) * RAD2DEG;

            // Range control: back off if too close, close in if too far, hold otherwise
            if      (dist < m.preferredRange * 0.65f)  thrustMult = -0.8f;
            else if (dist > m.preferredRange * 1.3f)   thrustMult =  1.0f;
            else                                        thrustMult =  0.0f;

            // Fighter-only dodge/orbit jitter — makes a small fast ship harder
            // to hit. Capital ships (m.hardpoints non-empty) skip this: on a
            // big slow hull it reads as an ugly wobble rather than a dodge,
            // and their turrets already aim independently of hull facing
            // (see UpdateCapitalFire), so there's no accuracy tradeoff to
            // holding a straight line to the target instead.
            if (m.hardpoints.empty()) {
                desiredRot += sinf((float)GetTime() * 1.8f + (float)m.id) * 20.0f;

                // Strafe perpendicular to target line to orbit and dodge projectiles
                if (dist > 1.0f) {
                    float sPhase = sinf((float)GetTime() * 0.45f + (float)m.id * 1.7f);
                    float sDir   = (sPhase > 0.0f ? 1.0f : -1.0f) * ((m.id % 2 == 0) ? 1.0f : -1.0f);
                    float str    = m.npcThrust * 0.55f * dt;
                    lateralBoost.x += (-toP.y / dist) * sDir * str;
                    lateralBoost.y += ( toP.x / dist) * sDir * str;
                }
            }
            break;
        }
        case NpcAiState::Flee: {
            m.waypointSet = false;
            repair::FriendlyDock dock = repair::FindNearestFriendlyDock(*_w, m.npcFaction, e.transform.position);
            if (dock.valid) {
                float distToDock = Vector2Distance(e.transform.position, dock.position);
                if (distToDock < repair::kDockRadius) {
                    // Arrived — park just outside the station center from the
                    // dock hardpoint so the ship doesn't render on top of it.
                    SpaceStation* st = nullptr;
                    for (SpaceStation& s : _w->stations) if (s.id == dock.stationId) { st = &s; break; }
                    Vector2 outward = st ? Vector2Normalize(Vector2Subtract(dock.position, st->position))
                                          : Vector2{ 0.0f, -1.0f };
                    e.transform.position = Vector2Add(dock.position, Vector2Scale(outward, 20.0f));
                    e.transform.velocity = { 0.0f, 0.0f };
                    m.docked = true;
                    desiredRot  = e.transform.rotation;
                    thrustMult  = 0.0f;
                    break;
                }
                Vector2 toDock = Vector2Subtract(dock.position, e.transform.position);
                desiredRot = atan2f(toDock.x, -toDock.y) * RAD2DEG;
                thrustMult = 1.0f;
                break;
            }
            Vector2 threatPos = _playerEntity.transform.position;
            if (ReputationRegistry::PlayerRelation(m.npcFaction) != Relation::Hostile) {
                float best = FLT_MAX;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; threatPos = _w->entities[j].transform.position; }
                }
            }
            Vector2 away = Vector2Subtract(e.transform.position, threatPos);
            desiredRot = atan2f(away.x, -away.y) * RAD2DEG;
            thrustMult = 1.0f;
            break;
        }
        case NpcAiState::Escort: {
            m.escortTargetId = 0;
            float closestEnemy = 900.0f;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                if (!_w->npcMeta[j].alive || _w->npcMeta[j].wingman || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                if (d < closestEnemy) { closestEnemy = d; m.escortTargetId = _w->npcMeta[j].id; }
            }
            if (m.escortTargetId != 0) {
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (_w->npcMeta[j].id != m.escortTargetId) continue;
                    Vector2 toE = Vector2Subtract(_w->entities[j].transform.position, e.transform.position);
                    desiredRot = atan2f(toE.x, -toE.y) * RAD2DEG;
                    thrustMult = 1.0f;
                    break;
                }
            }
            else {
                static const float kFormRight[4] = {  60.f, -60.f,  120.f, -120.f };
                static const float kFormBack[4]  = { -5.f, -5.f, -30.f, -30.f };

                // Epic 10: these offsets/thresholds were sized for fighter-scale
                // wingmen (radius ~18-25) — a wingman'd capital (radius ~140,
                // reachable via Epic 9 capture or the StationModuleMenu build
                // path) would otherwise get a "formation slot" well inside its
                // own hull, physically overlapping the player. Scale by the
                // wingman's own radius against a fighter-sized baseline so a
                // capital instead holds a proportionally wider slot.
                float formScale = std::max(1.0f, m.radius / 20.0f);

                int     slot  = m.wingmanSlot;
                float   pr    = _playerEntity.transform.rotation * DEG2RAD;
                Vector2 fwd   = { sinf(pr), -cosf(pr) };
                Vector2 right = { cosf(pr),  sinf(pr) };

                Vector2 formTarget = _playerEntity.transform.position;
                if (slot >= 0 && slot < 4) {
                    formTarget.x += (right.x * kFormRight[slot] + fwd.x * kFormBack[slot]) * formScale;
                    formTarget.y += (right.y * kFormRight[slot] + fwd.y * kFormBack[slot]) * formScale;
                }

                float dToTarget = Vector2Distance(e.transform.position, formTarget);
                if (dToTarget > 120.0f * formScale) {
                    Vector2 toP = Vector2Subtract(formTarget, e.transform.position);
                    desiredRot = atan2f(toP.x, -toP.y) * RAD2DEG;
                    thrustMult = 1.0f;
                } else if (dToTarget < 40.0f * formScale) {
                    Vector2 away2 = Vector2Subtract(e.transform.position, formTarget);
                    desiredRot = atan2f(away2.x, -away2.y) * RAD2DEG;
                    thrustMult = 0.3f;
                } else {
                    thrustMult = 0.05f;
                }
            }
            break;
        }
        }

        if (_w->sun.active) {
            float distToSun = Vector2Length(e.transform.position);
            float avoidZone = _w->sun.gravRange * 1.4f;
            if (distToSun < avoidZone && distToSun > 0.01f) {
                float urgency = 1.0f - (distToSun / avoidZone);
                urgency = urgency * urgency;
                Vector2 away = { e.transform.position.x / distToSun, e.transform.position.y / distToSun };
                float avoidRot = atan2f(away.x, -away.y) * RAD2DEG;
                float rotDiff = avoidRot - desiredRot;
                while (rotDiff >  180.0f) rotDiff -= 360.0f;
                while (rotDiff < -180.0f) rotDiff += 360.0f;
                desiredRot  += rotDiff * urgency;
                thrustMult   = std::max(thrustMult, urgency);
            }
        }

        float diff = desiredRot - e.transform.rotation;
        while (diff > 180.0f) diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;
        float maxTurn = m.npcTurnRate * dt;
        e.transform.rotation += std::clamp(diff, -maxTurn, maxTurn);

        m.thrusting = (fabsf(thrustMult) > 0.01f && m.npcThrust > 0.0f);
        Vector2 fwd = { sinf(e.transform.rotation * DEG2RAD), -cosf(e.transform.rotation * DEG2RAD) };
        e.transform.velocity.x += fwd.x * m.npcThrust * thrustMult * dt;
        e.transform.velocity.y += fwd.y * m.npcThrust * thrustMult * dt;
        e.transform.velocity.x += lateralBoost.x;
        e.transform.velocity.y += lateralBoost.y;

        float drag = expf(-NpcDrag * dt);
        e.transform.velocity.x *= drag;
        e.transform.velocity.y *= drag;

        e.transform.position.x += e.transform.velocity.x * dt;
        e.transform.position.y += e.transform.velocity.y * dt;

        if (e.health.maxStats.shield > 0.0f)
            e.health.currentShield = std::min(e.health.currentShield + m.kineticRechargeRate * dt,
                e.health.maxStats.shield);
        if (m.maxEnergyShield > 0.0f)
            m.energyShield = std::min(m.energyShield + m.energyRechargeRate * dt, m.maxEnergyShield);
        if (m.asteroidHitCooldown > 0.0f) m.asteroidHitCooldown -= dt;

        // Capital ships (m.hardpoints non-empty) fire exclusively through
        // UpdateCapitalFire's independent per-turret logic — skip the
        // single-shared-target fighter fire path so they don't double-fire.
        if (m.faction == NpcFaction::Hostile && !m.wingman && m.hardpoints.empty() &&
            m.aiState == NpcAiState::Attack && m.npcHasWeapon) {

            Vector2      fireTarget = _playerEntity.transform.position;
            unsigned int fireNpcId = 0;
            bool         fireTargetIsStation = false;
            {
                float best = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; fireTarget = GetNpcAimPos(*_w, j); fireNpcId = _w->npcMeta[j].id; fireTargetIsStation = false; }
                }
                for (const PlayerStation& ps : playerStations) {
                    if (!ps.alive) continue;
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                    float rad = def ? def->radius : 120.0f;
                    Vector2 aimPos = GetBestHardpointAimPos(ps, rad);
                    float d = Vector2Distance(e.transform.position, aimPos);
                    if (d < best) { best = d; fireTarget = aimPos; fireNpcId = 0; fireTargetIsStation = true; }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; fireTarget = GetStationAimPos(st); fireNpcId = 0; fireTargetIsStation = true; }
                }
            }
            m.fireCooldown -= dt;
            Vector2 toT = Vector2Subtract(fireTarget, e.transform.position);
            float   aimAng = atan2f(toT.x, -toT.y) * RAD2DEG;
            float   faceDelt = aimAng - e.transform.rotation;
            while (faceDelt > 180.0f) faceDelt -= 360.0f;
            while (faceDelt < -180.0f) faceDelt += 360.0f;

            switch (m.npcWeaponMode) {
            case WeaponFireMode::Standard: {
                if (m.fireCooldown <= 0.0f && fabsf(faceDelt) < 22.0f) {
                    float len = Vector2Length(toT);
                    Vector2 dir = (len > 1.0f) ? Vector2Scale(toT, 1.0f / len) : Vector2{ 0, -1 };
                    Projectile p;
                    p.position = e.transform.position;
                    p.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                    p.maxLife = m.npcProjRange / m.npcProjSpeed;
                    p.damage = m.npcDamage;
                    p.fromPlayer = false;
                    p.ownerId = m.id;
                    _w->projectiles.push_back(p);
                    m.fireCooldown = m.npcFireRate;
                }
                break;
            }
            case WeaponFireMode::Charge: {
                if (fabsf(faceDelt) < 22.0f) m.npcChargeTimer += dt;
                else                           m.npcChargeTimer = 0.0f;
                if (m.npcChargeTimer >= m.npcChargeTime && m.fireCooldown <= 0.0f) {
                    int count = m.npcBurstCount > 0 ? m.npcBurstCount : 1;
                    for (int b = 0; b < count; ++b) {
                        float spOff = (count > 1)
                            ? m.npcSpreadAngle * ((float)b / (count - 1) - 0.5f)
                            : 0.0f;
                        float fRad = (aimAng + spOff) * DEG2RAD;
                        Vector2 dir = { sinf(fRad), -cosf(fRad) };
                        Projectile p;
                        p.position = e.transform.position;
                        p.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                        p.maxLife = m.npcProjRange / m.npcProjSpeed;
                        p.damage = m.npcDamage;
                        p.fromPlayer = false;
                        p.ownerId = m.id;
                        _w->projectiles.push_back(p);
                    }
                    m.npcChargeTimer = 0.0f;
                    m.fireCooldown = m.npcFireRate;
                }
                break;
            }
            case WeaponFireMode::LockOn: {
                if (m.fireCooldown <= 0.0f && fabsf(faceDelt) < 90.0f) {
                    float len = Vector2Length(toT);
                    Vector2 dir = (len > 1.0f) ? Vector2Scale(toT, 1.0f / len) : Vector2{ 0, -1 };
                    Projectile p;
                    p.position = e.transform.position;
                    p.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                    p.maxLife = m.npcProjRange / m.npcProjSpeed;
                    p.damage = m.npcDamage;
                    p.fromPlayer = false;
                    p.ownerId = m.id;
                    p.isHoming = true;
                    p.turnRate = 3.0f;
                    if (fireNpcId != 0) { p.targetId = fireNpcId; }
                    else if (!fireTargetIsStation) { p.targetIsPlayer = true; }
                    else { p.isHoming = false; }
                    _w->projectiles.push_back(p);
                    m.fireCooldown = m.npcFireRate;
                }
                break;
            }
            }
        }

        if (m.wingman && m.escortTargetId != 0 && m.npcHasWeapon) {
            m.fireCooldown -= dt;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                if (_w->npcMeta[j].id != m.escortTargetId || !_w->npcMeta[j].alive) continue;
                Vector2 toE = Vector2Subtract(_w->entities[j].transform.position, e.transform.position);
                float   aimAng = atan2f(toE.x, -toE.y) * RAD2DEG;
                float   delta2 = aimAng - e.transform.rotation;
                while (delta2 > 180.0f) delta2 -= 360.0f;
                while (delta2 < -180.0f) delta2 += 360.0f;

                switch (m.npcWeaponMode) {
                case WeaponFireMode::Standard: {
                    if (m.fireCooldown <= 0.0f && fabsf(delta2) < 25.0f) {
                        float len = Vector2Length(toE);
                        Vector2 dir = (len > 1.0f) ? Vector2Scale(toE, 1.0f / len) : Vector2{ 0,-1 };
                        Projectile ep;
                        ep.position = e.transform.position;
                        ep.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                        ep.maxLife = m.npcProjRange / m.npcProjSpeed;
                        ep.damage = m.npcDamage;
                        ep.fromPlayer = false;
                        ep.ownerId = m.id;
                        _w->projectiles.push_back(ep);
                        m.fireCooldown = m.npcFireRate;
                    }
                    break;
                }
                case WeaponFireMode::Charge: {
                    if (fabsf(delta2) < 25.0f) m.npcChargeTimer += dt;
                    else                         m.npcChargeTimer = 0.0f;
                    if (m.npcChargeTimer >= m.npcChargeTime && m.fireCooldown <= 0.0f) {
                        int count = m.npcBurstCount > 0 ? m.npcBurstCount : 1;
                        for (int b = 0; b < count; ++b) {
                            float spOff = (count > 1)
                                ? m.npcSpreadAngle * ((float)b / (count - 1) - 0.5f)
                                : 0.0f;
                            float fRad = (aimAng + spOff) * DEG2RAD;
                            Vector2 dir = { sinf(fRad), -cosf(fRad) };
                            Projectile ep;
                            ep.position = e.transform.position;
                            ep.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                            ep.maxLife = m.npcProjRange / m.npcProjSpeed;
                            ep.damage = m.npcDamage;
                            ep.fromPlayer = false;
                            ep.ownerId = m.id;
                            _w->projectiles.push_back(ep);
                        }
                        m.npcChargeTimer = 0.0f;
                        m.fireCooldown = m.npcFireRate;
                    }
                    break;
                }
                case WeaponFireMode::LockOn: {
                    if (m.fireCooldown <= 0.0f) {
                        float len = Vector2Length(toE);
                        Vector2 dir = (len > 1.0f) ? Vector2Scale(toE, 1.0f / len) : Vector2{ 0,-1 };
                        Projectile ep;
                        ep.position = e.transform.position;
                        ep.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                        ep.maxLife = m.npcProjRange / m.npcProjSpeed;
                        ep.damage = m.npcDamage;
                        ep.fromPlayer = false;
                        ep.ownerId = m.id;
                        ep.isHoming = true;
                        ep.targetId = _w->npcMeta[j].id;
                        ep.turnRate = 3.0f;
                        _w->projectiles.push_back(ep);
                        m.fireCooldown = m.npcFireRate;
                    }
                    break;
                }
                }
                break;
            }
        }

        // Capitals fire exclusively through UpdateCapitalFire (see the hostile
        // fire block above) — guard here too in case a future capital is ever
        // spawned on a friendly/neutral faction.
        if (m.faction != NpcFaction::Hostile && !m.wingman && m.hardpoints.empty() &&
            m.aiState == NpcAiState::Attack && m.npcHasWeapon) {

            Vector2 fireTarget = {}; unsigned int fireNpcId = 0; bool fireTargetIsPlayer = false; float best = FLT_MAX;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                if (!_w->npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                if (d < best) { best = d; fireTarget = GetNpcAimPos(*_w, j); fireNpcId = _w->npcMeta[j].id; fireTargetIsPlayer = false; }
            }
            if (m.retaliatingVsPlayer) {
                float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                if (d < best) { best = d; fireTarget = _playerEntity.transform.position; fireNpcId = 0; fireTargetIsPlayer = true; }
            }
            if (m.retaliationTargetId != 0) {
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                        float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                        if (d < best) { best = d; fireTarget = GetNpcAimPos(*_w, j); fireNpcId = m.retaliationTargetId; fireTargetIsPlayer = false; }
                        break;
                    }
                }
            }
            if (best < FLT_MAX) {
                m.fireCooldown -= dt;
                Vector2 toT    = Vector2Subtract(fireTarget, e.transform.position);
                float aimAng   = atan2f(toT.x, -toT.y) * RAD2DEG;
                float faceDelt = aimAng - e.transform.rotation;
                while (faceDelt >  180.0f) faceDelt -= 360.0f;
                while (faceDelt < -180.0f) faceDelt += 360.0f;

                switch (m.npcWeaponMode) {
                case WeaponFireMode::Standard:
                    if (m.fireCooldown <= 0.0f && fabsf(faceDelt) < 22.0f) {
                        float len = Vector2Length(toT);
                        Vector2 dir = (len > 1.0f) ? Vector2Scale(toT, 1.0f / len) : Vector2{ 0,-1 };
                        Projectile p; p.position = e.transform.position;
                        p.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                        p.maxLife = m.npcProjRange / m.npcProjSpeed;
                        p.damage = m.npcDamage; p.fromPlayer = false; p.ownerId = m.id;
                        _w->projectiles.push_back(p); m.fireCooldown = m.npcFireRate;
                    }
                    break;
                case WeaponFireMode::Charge:
                    if (fabsf(faceDelt) < 22.0f) m.npcChargeTimer += dt;
                    else m.npcChargeTimer = 0.0f;
                    if (m.npcChargeTimer >= m.npcChargeTime && m.fireCooldown <= 0.0f) {
                        int cnt = m.npcBurstCount > 0 ? m.npcBurstCount : 1;
                        for (int b = 0; b < cnt; ++b) {
                            float spOff = (cnt > 1) ? m.npcSpreadAngle * ((float)b / (cnt-1) - 0.5f) : 0.0f;
                            float fRad = (aimAng + spOff) * DEG2RAD;
                            Vector2 dir = { sinf(fRad), -cosf(fRad) };
                            Projectile p; p.position = e.transform.position;
                            p.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                            p.maxLife = m.npcProjRange / m.npcProjSpeed;
                            p.damage = m.npcDamage; p.fromPlayer = false; p.ownerId = m.id;
                            _w->projectiles.push_back(p);
                        }
                        m.npcChargeTimer = 0.0f; m.fireCooldown = m.npcFireRate;
                    }
                    break;
                case WeaponFireMode::LockOn:
                    if (m.fireCooldown <= 0.0f && fabsf(faceDelt) < 90.0f) {
                        float len = Vector2Length(toT);
                        Vector2 dir = (len > 1.0f) ? Vector2Scale(toT, 1.0f / len) : Vector2{ 0,-1 };
                        Projectile p; p.position = e.transform.position;
                        p.velocity = { dir.x * m.npcProjSpeed, dir.y * m.npcProjSpeed };
                        p.maxLife = m.npcProjRange / m.npcProjSpeed;
                        p.damage = m.npcDamage; p.fromPlayer = false; p.ownerId = m.id;
                        p.isHoming = true; p.turnRate = 3.0f;
                        if (fireTargetIsPlayer) p.targetIsPlayer = true;
                        else                    p.targetId = fireNpcId;
                        _w->projectiles.push_back(p); m.fireCooldown = m.npcFireRate;
                    }
                    break;
                }
            }
        }
    }

    for (LootDrop& ld : _w->lootDrops) {
        if (ld.collected) continue;
        ld.lifetime -= dt;
        ld.pulseTimer += dt;
        if (ld.lifetime <= 0.0f) ld.collected = true;
    }
    auto isGone = [](const LootDrop& ld) { return ld.collected; };
    _w->lootDrops.erase(std::remove_if(_w->lootDrops.begin(), _w->lootDrops.end(), isGone),
        _w->lootDrops.end());

    for (MaterialDrop& md : _w->materialDrops) {
        if (md.collected) continue;
        md.lifetime -= dt;
        md.pulseTimer += dt;
        if (md.lifetime <= 0.0f) md.collected = true;
    }
    auto matGone = [](const MaterialDrop& md) { return md.collected; };
    _w->materialDrops.erase(std::remove_if(_w->materialDrops.begin(), _w->materialDrops.end(), matGone),
        _w->materialDrops.end());
}

void SpaceFlight::UpdateNpcCollisions() {
    // Block 1: player projectiles hit any non-escort NPC
    // (capital ships — m.hardpoints non-empty — are excluded here: they take
    // damage only through their own hardpoint hit-test below, never generic hull)
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || !p.fromPlayer) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || m.wingman || !m.hardpoints.empty()) continue;
            if (Vector2Distance(p.position, e.transform.position) < m.radius + 3.5f) {
                // Epic 9.2 (fighter capture): an Ion weapon subdues a fighter
                // that's already below the disable-hull threshold instead of
                // finishing it off — fighters have no hardpoints to disable
                // like capitals/stations, so this is the "hull-threshold +
                // ion-effect status" definition from the locked design.
                float hullPctBefore = (e.health.maxStats.hull > 0.0f)
                    ? e.health.currentHull / e.health.maxStats.hull : 1.0f;
                if (p.effect == WeaponEffect::Ion && m.faction != NpcFaction::Friendly &&
                    !m.disabled && hullPctBefore <= kIonDisableHullPct) {
                    p.alive = false;
                    m.disabled = true;
                    m.ionDisableTimer = (p.effectDuration > 0.0f) ? p.effectDuration : 6.0f;
                    m.aiState = NpcAiState::Disabled;
                    AddCommsMessage(m.shipTypeName + " ion-disabled! Approach to capture before it recovers.", true);
                    break;
                }

                p.alive = false;
                float dmg = p.damage;
                if (e.health.currentShield > 0.0f) {
                    float absorb = std::min(e.health.currentShield, dmg);
                    e.health.currentShield -= absorb;
                    dmg -= absorb;
                }
                e.health.currentHull = std::max(0.0f, e.health.currentHull - dmg);
                if (m.faction == NpcFaction::Neutral) m.retaliatingVsPlayer = true;
                if (m.aiState == NpcAiState::Patrol) m.aiState = NpcAiState::Chase;
                if (e.health.currentHull <= 0.0f) {
                    m.alive = false;
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    SpawnLootDrop(e.transform.position, m.faction);
                    AddCommsMessage(m.shipTypeName + " destroyed.");
                    ReputationRegistry::Adjust(m.npcFaction, -8.0f); // Epic 6.3: killing their ship costs standing
                    if (_hasActiveContract && _activeContract.type == ContractType::Bounty &&
                        _activeContract.targetFaction == m.npcFaction) {
                        _activeContract.killsDone++;
                    }
                }
                break;
            }
        }
    }

    // Block 2: NPC-fired projectiles hit any other NPC ship whose actual
    // (pairwise, DiplomaticRegistry) faction relation to the shooter is
    // Hostile — independent of either ship's relation to the player.
    //
    // Replaces the old pair of blocks that gated on NpcMeta::faction (the
    // 3-value Friendly/Neutral/Hostile bucket relative to the *player*):
    // two ships whose actual factions are mutually hostile to each other
    // (e.g. Automa Concord vs. Meridian Star Corps) but who happen to share
    // the same player-relative bucket (both Neutral-to-player, say) matched
    // neither block, so they'd correctly target and fire at each other via
    // the pairwise-aware AI logic above, yet their shots never applied
    // damage. The AI targeting code already used DiplomaticRegistry for
    // exactly this reason; this block now matches it.
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
        Faction shooterFaction = Faction::Merchant;
        bool    foundShooter   = false;
        for (size_t j = 0; j < _w->npcMeta.size(); ++j)
            if (_w->npcMeta[j].id == p.ownerId) { shooterFaction = _w->npcMeta[j].npcFaction; foundShooter = true; break; }
        if (!foundShooter) continue;

        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || m.id == p.ownerId || !m.hardpoints.empty()) continue;
            if (DiplomaticRegistry::Get(shooterFaction, m.npcFaction) != Relation::Hostile) continue;
            if (Vector2Distance(p.position, e.transform.position) < m.radius + 3.5f) {
                p.alive = false;
                float dmg = p.damage;
                if (e.health.currentShield > 0.0f) {
                    float absorb = std::min(e.health.currentShield, dmg);
                    e.health.currentShield -= absorb;
                    dmg -= absorb;
                }
                e.health.currentHull = std::max(0.0f, e.health.currentHull - dmg);
                if (m.faction == NpcFaction::Neutral && !m.wingman)
                    m.retaliationTargetId = p.ownerId;
                if (!m.wingman && m.aiState == NpcAiState::Patrol) m.aiState = NpcAiState::Chase;
                if (e.health.currentHull <= 0.0f) {
                    m.alive = false;
                    _w->npcFreeSlots.push_back(i);
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    SpawnLootDrop(e.transform.position, m.faction);
                    AddCommsMessage(m.wingman ? "WINGMAN destroyed." : m.shipTypeName + " destroyed.");
                }
                break;
            }
        }
    }

    // Block 3: hostile/retaliating NPC projectiles hit player — skip non-hostile non-retaliating NPC shots and station shots
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || p.fromPlayer) continue;
        if (p.ownerId != 0) {
            bool skip = false;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                if (_w->npcMeta[j].id == p.ownerId && _w->npcMeta[j].faction != NpcFaction::Hostile) {
                    if (!_w->npcMeta[j].retaliatingVsPlayer) skip = true;
                    break;
                }
            }
            if (!skip) {
                for (const SpaceStation& st : _w->stations)
                    if (st.id == p.ownerId) { skip = true; break; }
            }
            if (skip) continue;
        }
        if (!_stationServicesMenu.isOpen && !_seated &&
            Vector2Distance(p.position, _playerEntity.transform.position) < _playerMeta.radius) {
            p.alive = false;
            float dmg = p.damage;
            if (_playerEntity.health.currentShield > 0.0f) {
                float absorb = std::min(_playerEntity.health.currentShield, dmg);
                _playerEntity.health.currentShield -= absorb;
                dmg -= absorb;
            }
            _playerEntity.health.currentHull = std::max(0.0f, _playerEntity.health.currentHull - dmg);
            if (_hitCooldown <= 0.0f) _hitCooldown = 0.5f;
        }
    }

    // Block 4: Hostile NPC projectiles hitting Player Station Hardpoints
    // (player-built stations belong to the player's world — skip in background)
    for (Projectile& p : _w->projectiles) {
        if (_bgTick) break;
        if (!p.alive || p.fromPlayer) continue;

        for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
            if (!ps.alive) continue;

            const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
            float rad = def ? def->radius : 120.0f;

            bool hitStation = ResolveHardpointHit(ps.hardpoints, p.position, p.damage,
                [&](int i) { return GetHardpointPos(ps, i, rad); },
                "", " Destroyed!", true);
            if (hitStation) p.alive = false;

            // Station dies once every hardpoint is destroyed — no protected core.
            if (hitStation) {
                if (combat::AllHardpointsDestroyed(ps.hardpoints)) {
                    ps.alive = false;
                    AddCommsMessage("CRITICAL FAILURE: Station Lost.", true);
                }
                break; // Projectile consumed
            }
        }
    }

    // Block 5: in-world station projectiles hit NPCs and player
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || p.fromPlayer) continue;
        SpaceStation* stShooter = nullptr;
        for (SpaceStation& st : _w->stations)
            if (st.id == p.ownerId) { stShooter = &st; break; }
        if (!stShooter) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || !m.hardpoints.empty()) continue;
            if (Vector2Distance(p.position, e.transform.position) < m.radius + 3.5f) {
                p.alive = false;
                float dmg = p.damage;
                if (e.health.currentShield > 0.0f) {
                    float absorb = std::min(e.health.currentShield, dmg);
                    e.health.currentShield -= absorb;
                    dmg -= absorb;
                }
                e.health.currentHull = std::max(0.0f, e.health.currentHull - dmg);
                if (!m.wingman && m.aiState == NpcAiState::Patrol) m.aiState = NpcAiState::Chase;
                if (e.health.currentHull <= 0.0f) {
                    m.alive = false;
                    _w->npcFreeSlots.push_back(i);
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    SpawnLootDrop(e.transform.position, m.faction);
                    AddCommsMessage(m.shipTypeName + " destroyed.");
                }
                break;
            }
        }
        if (p.alive && !_stationServicesMenu.isOpen && !_seated &&
            Vector2Distance(p.position, _playerEntity.transform.position) < _playerMeta.radius) {
            p.alive = false;
            float dmg = p.damage;
            if (_playerEntity.health.currentShield > 0.0f) {
                float absorb = std::min(_playerEntity.health.currentShield, dmg);
                _playerEntity.health.currentShield -= absorb;
                dmg -= absorb;
            }
            _playerEntity.health.currentHull = std::max(0.0f, _playerEntity.health.currentHull - dmg);
            if (_hitCooldown <= 0.0f) _hitCooldown = 0.5f;
        }
    }

    // Block 6: any projectile hits in-world NPC station hardpoints
    for (Projectile& p : _w->projectiles) {
        if (!p.alive) continue;
        for (SpaceStation& st : _w->stations) {
            if (!st.alive || st.id == p.ownerId) continue;
            // Quick broad-phase: skip if not near the station at all
            if (Vector2Distance(p.position, st.position) > st.radius + 20.0f) continue;

            bool hitStation = ResolveHardpointHit(st.hardpoints, p.position, p.damage,
                [&](int i) { return GetNpcStationHardpointPos(st, i); },
                st.stationTypeId + " ", " destroyed.");
            if (hitStation) {
                p.alive = false;
                st.hull = std::max(0.0f, st.hull - p.damage); // keep overall health bar in sync

                // Trigger retaliation
                st.retaliating    = true;
                st.retaliateTimer = 8.0f;
                if (p.fromPlayer && p.ownerId == 0) {
                    st.retaliateAtPlayer = true;
                    st.retaliateAtNpcId  = 0;
                } else {
                    st.retaliateAtPlayer = false;
                    st.retaliateAtNpcId  = p.ownerId;
                }
            }

            if (hitStation) {
                // Station dies once every hardpoint is destroyed — no protected core.
                if (combat::AllHardpointsDestroyed(st.hardpoints) && st.alive) {
                    st.alive        = false;
                    st.rebuilding   = true;
                    st.rebuildTimer = kStationRebuildSeconds;
                    int dropCount = GetRandomValue(1, 3);
                    for (int d = 0; d < dropCount; ++d) {
                        Vector2 jitter = { (float)GetRandomValue(-40, 40), (float)GetRandomValue(-40, 40) };
                        SpawnLootDrop(Vector2Add(st.position, jitter), NpcFaction::Hostile);
                    }
                    net::Game().HostBroadcastStationDead(_w->systemId, st.id);
                    // Epic 11.1: a persistent, meaningful salvage reward — separate
                    // from the routine 1-3 LootDrop scatter above (locked decision:
                    // capitals/stations only, not routine fighter kills).
                    SpawnDerelictWreck(st.position, false, GetRandomValue(800, 1500), st.stationTypeId);
                    if (p.fromPlayer) {
                        ReputationRegistry::Adjust(st.faction, -15.0f); // Epic 6.3
                        if (_hasActiveContract && _activeContract.type == ContractType::Bounty &&
                            _activeContract.targetFaction == st.faction) {
                            _activeContract.killsDone++;
                        }
                    }
                } else if (st.alive && !st.disabled && combat::IsDisabled(st.hardpoints)) {
                    // Epic 9.1: every non-core hardpoint dead, station_core survives —
                    // unarmed and awaiting an instant-capture approach.
                    st.disabled = true;
                    AddCommsMessage(st.stationTypeId + " disabled! Approach to capture.", true);
                }
                break; // projectile consumed
            }
        }
    }

    // Any projectile hits capital ship hardpoints — same "no core, per-
    // hardpoint HP" model as NPC stations (Block 6) above, but the ship
    // moves/rotates so hardpoint world positions come from B.03's
    // GetCapitalHardpointWorldPos instead of a static ring. Blocks 1/2/5
    // above already exclude capitals (m.hardpoints non-empty) from the
    // generic ship-hull hit-test, so this is the only place capitals take
    // damage. Friendly-fire rule matches Block 6: no hostility gate, only
    // self-hit exclusion via ownerId.
    for (Projectile& p : _w->projectiles) {
        if (!p.alive) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || m.hardpoints.empty() || m.id == p.ownerId) continue;
            // Quick broad-phase: skip if not near the ship at all
            if (Vector2Distance(p.position, e.transform.position) > m.radius + 20.0f) continue;

            bool hitCapital = ResolveHardpointHit(m.hardpoints, p.position, p.damage,
                [&](int i) { return GetCapitalHardpointWorldPos(e.transform.position, e.transform.rotation, m.hardpoints[i].localOffset); },
                m.shipTypeName + " ", " destroyed.");
            if (hitCapital) {
                p.alive = false;
                if (!m.wingman && m.aiState == NpcAiState::Patrol) m.aiState = NpcAiState::Chase;

                // Capitals take damage from anything regardless of diplomatic
                // hostility (see this block's own header comment), so they
                // need the same retaliation-target override the fighter fire
                // scan uses for a non-Hostile attacker — otherwise a capital
                // with no diplomatically-Hostile target in a turret's range
                // never fires back at whatever is actually shooting it.
                // UpdateCapitalFire consults these same NpcMeta fields.
                if (p.fromPlayer) m.retaliatingVsPlayer = true;
                else if (p.ownerId != 0) m.retaliationTargetId = p.ownerId;
            }

            if (hitCapital) {
                if (combat::AllHardpointsDestroyed(m.hardpoints)) {
                    m.alive = false;
                    _w->npcFreeSlots.push_back(i);
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    int dropCount = GetRandomValue(1, 3);
                    for (int d = 0; d < dropCount; ++d) {
                        Vector2 jitter = { (float)GetRandomValue(-40, 40), (float)GetRandomValue(-40, 40) };
                        SpawnLootDrop(Vector2Add(e.transform.position, jitter), m.faction);
                    }
                    AddCommsMessage(m.shipTypeName + " destroyed.");
                    // Epic 11.1: capital wrecks pay out more than a station's —
                    // the bigger, rarer kill.
                    SpawnDerelictWreck(e.transform.position, true, GetRandomValue(2000, 4000), m.shipTypeName);
                    if (p.fromPlayer) {
                        ReputationRegistry::Adjust(m.npcFaction, -20.0f); // Epic 6.3
                        if (_hasActiveContract && _activeContract.type == ContractType::Bounty &&
                            _activeContract.targetFaction == m.npcFaction) {
                            _activeContract.killsDone++;
                        }
                    }
                } else if (!m.disabled && combat::IsDisabled(m.hardpoints)) {
                    // Epic 9.1: every non-core hardpoint dead, command_bridge survives —
                    // unarmed and awaiting an instant-capture approach.
                    m.disabled = true;
                    AddCommsMessage(m.shipTypeName + " disabled! Approach to capture.", true);
                }
                break; // projectile consumed
            }
        }
    }

    // Block 8: Hostile NPC projectiles hit remote client entities (host-authoritative).
    if (net::Game().IsHost() && !_remoteEntities.empty()) {
        std::vector<uint32_t> deadClients;
        for (Projectile& p : _w->projectiles) {
            if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
            bool fromHostile = false;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j)
                if (_w->npcMeta[j].id == p.ownerId && _w->npcMeta[j].faction == NpcFaction::Hostile) { fromHostile = true; break; }
            if (!fromHostile) continue;
            for (auto& [netId, re] : _remoteEntities) {
                if (re.id == 0) continue;
                if (!PeerInCurrentWorld(netId)) continue;  // peer is in another system
                if (_remoteDocked[netId]) continue;  // docked in a station menu — untargetable
                if (Vector2Distance(p.position, re.transform.position) < 18.0f + 3.5f) {
                    p.alive = false;
                    float dmg = p.damage;
                    if (re.health.currentShield > 0.0f) {
                        float absorb = std::min(re.health.currentShield, dmg);
                        re.health.currentShield -= absorb;
                        dmg -= absorb;
                    }
                    re.health.currentHull = std::max(0.0f, re.health.currentHull - dmg);
                    if (re.health.currentHull <= 0.0f) {
                        auto git = _remoteJoinGrace.find(netId);
                        if (git == _remoteJoinGrace.end() || git->second <= 0.0f)
                            deadClients.push_back(netId);
                    }
                    break;
                }
            }
        }
        for (uint32_t id : deadClients) {
            net::Game().HostSendPlayerDead(id);
            _remoteEntities.erase(id);
            _remoteFireCooldown.erase(id);
            _remoteJoinGrace.erase(id);
            _remoteDocked.erase(id);
        }
    }

    for (LootDrop& ld : _w->lootDrops) {
        if (ld.collected || _stationServicesMenu.isOpen) continue;
        if (Vector2Distance(_playerEntity.transform.position, ld.position) < _playerMeta.radius) {
            bool itemAdded = false;

            for (StorageItem& slot : _storageMenu.slots) {
                if (slot.type != StorageItemType::Empty) continue;
                slot.type = StorageItemType::Module;
                slot.module = ld.module;
                slot.displayName = ld.module.displayName;
                AddCommsMessage("MODULE ACQUIRED: " + ld.module.displayName, true);
                itemAdded = true;
                break;
            }

            if (itemAdded) {
                ld.collected = true;
            }
            else {
                AddCommsMessage("STORAGE FULL: Cannot collect module!", true);
            }
        }
    }

    for (MaterialDrop& md : _w->materialDrops) {
        if (md.collected || _stationServicesMenu.isOpen) continue;
        if (Vector2Distance(_playerEntity.transform.position, md.position) >= _playerMeta.radius) continue;
        bool added = false;
        for (StorageItem& slot : _storageMenu.slots) {
            if (slot.type == StorageItemType::Material &&
                slot.materialId == md.materialId &&
                slot.count < StorageMenu::MaxStack) {
                slot.count++;
                added = true;
                break;
            }
        }
        if (!added) {
            for (StorageItem& slot : _storageMenu.slots) {
                if (slot.type == StorageItemType::Empty) {
                    slot.type        = StorageItemType::Material;
                    slot.materialId  = md.materialId;
                    const MatDef* m = FindMaterial(md.materialId);
                    slot.displayName = m ? m->displayName : md.materialId;
                    slot.count       = 1;
                    added            = true;
                    break;
                }
            }
        }
        if (added) {
            const MatDef* m = FindMaterial(md.materialId);
            AddCommsMessage(std::string("MATERIAL ACQUIRED: ") + (m ? m->displayName : md.materialId), true);
            md.collected = true;
            AdvanceTutorialStep(TutorialStep::CollectMaterial);
        }
        else {
            AddCommsMessage("STORAGE FULL: Cannot collect material!", true);
        }
    }

    // Epic 11.1: derelict wrecks pay out credits directly on approach —
    // no storage-slot contention, since the reward is meant to always land.
    for (DerelictWreck& w : _w->derelictWrecks) {
        if (w.collected || _stationServicesMenu.isOpen) continue;
        if (Vector2Distance(_playerEntity.transform.position, w.position) >= _playerMeta.radius + w.radius) continue;
        w.collected = true;
        InventoryManager::Get().AddCredits(w.creditsReward);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "SALVAGED WRECK: %s (+%d credits)", w.sourceName.c_str(), w.creditsReward);
        AddCommsMessage(buf, true);
    }
    _w->derelictWrecks.erase(
        std::remove_if(_w->derelictWrecks.begin(), _w->derelictWrecks.end(),
                        [](const DerelictWreck& w) { return w.collected; }),
        _w->derelictWrecks.end());
}

ModuleDef SpaceFlight::GenerateDrop(ModuleGrade grade) {
    return ModuleRegistry::RandomDrop(grade);
}

void SpaceFlight::SpawnLootDrop(Vector2 pos, NpcFaction) {
    LootDrop ld;
    ld.position = pos;
    ld.module = ModuleRegistry::RandomDrop();
    _w->lootDrops.push_back(ld);
}

void SpaceFlight::SpawnMaterialDrop(Vector2 pos, const std::string& materialId) {
    MaterialDrop md;
    md.position   = pos;
    md.materialId = materialId;
    _w->materialDrops.push_back(md);
}

void SpaceFlight::SpawnDerelictWreck(Vector2 pos, bool isCapital, int creditsReward, const std::string& sourceName) {
    DerelictWreck w;
    w.position      = pos;
    w.isCapital     = isCapital;
    w.radius        = isCapital ? 70.0f : 45.0f;
    w.creditsReward = creditsReward;
    w.sourceName    = sourceName;
    _w->derelictWrecks.push_back(w);
}

const ecs::ShipDef* SpaceFlight::ResolveShipDefByHash(uint32_t shipNameHash) const {
    if (shipNameHash == 0) return nullptr;
    for (const ecs::ShipDef& def : ecs::ShipRegistry::AllShips())
        if (Fnv1a32(def.id) == shipNameHash) return &def;
    return nullptr;
}

// P8-T1: one ModuleType-or-0 byte per HardpointRig mount, in the same order
// HardpointRig::Resize() built them — see net::FighterHardpointSnapshot for
// why this is what a client reports (rather than full module ids/grades) and
// EncodeFighterLoadoutReport for the wire encoding.
static std::vector<uint8_t> EncodeLoadoutMounts(const HardpointRig& rig) {
    std::vector<uint8_t> out;
    out.reserve(rig.hardpoints.size());
    for (const Hardpoint& hp : rig.hardpoints) {
        bool equipped = hp.alive && !hp.slots.empty() && hp.slots[0].equipped.has_value();
        out.push_back(equipped ? static_cast<uint8_t>(hp.slots[0].equipped->type) + 1 : 0);
    }
    return out;
}

// Faction-tinted oval + per-hardpoint markers, shared by local NPC capitals
// (DrawNpcShips) and remote capitals (DrawRemotePlayers) so both draw paths
// stay in lockstep with GetCapitalHardpointWorldPos.
static void DrawCapitalBody(Vector2 pos, float rotation, float radius, NpcFaction faction,
                             const std::vector<Hardpoint>& hardpoints) {
    Color bodyTint = faction == NpcFaction::Hostile  ? Color{ 200,  60,  60, 255 }
                    : faction == NpcFaction::Friendly ? Color{  60, 200,  90, 255 }
                                                        : Color{  90, 150, 220, 255 };
    constexpr int kOvalSegments = 20;
    Vector2 pts[kOvalSegments + 2];
    pts[0] = pos;
    float a = radius, b = radius * 0.62f;
    float rotRad = rotation * DEG2RAD;
    float cr = cosf(rotRad), sr = sinf(rotRad);
    for (int k = 0; k <= kOvalSegments; ++k) {
        float ang = (float)k / (float)kOvalSegments * 2.0f * PI;
        float lx = sinf(ang) * a, ly = -cosf(ang) * b;
        pts[k + 1] = { pos.x + lx * cr - ly * sr, pos.y + lx * sr + ly * cr };
    }
    DrawTriangleFan(pts, kOvalSegments + 2,
        Color{ (unsigned char)(bodyTint.r / 4), (unsigned char)(bodyTint.g / 4), (unsigned char)(bodyTint.b / 4), 235 });
    DrawLineStrip(&pts[1], kOvalSegments + 1, bodyTint);

    for (const Hardpoint& hp : hardpoints) {
        Vector2 hpPos = GetCapitalHardpointWorldPos(pos, rotation, hp.localOffset);
        float   hpDrawRad = hp.isCore ? 16.0f : 12.0f;
        if (!hp.alive) {
            DrawCircleLinesV(hpPos, hpDrawRad, Color{ 80, 80, 80, 140 });
            DrawLine((int)(hpPos.x - hpDrawRad * 0.6f), (int)(hpPos.y - hpDrawRad * 0.6f),
                     (int)(hpPos.x + hpDrawRad * 0.6f), (int)(hpPos.y + hpDrawRad * 0.6f), Color{ 80, 80, 80, 140 });
            DrawLine((int)(hpPos.x - hpDrawRad * 0.6f), (int)(hpPos.y + hpDrawRad * 0.6f),
                     (int)(hpPos.x + hpDrawRad * 0.6f), (int)(hpPos.y - hpDrawRad * 0.6f), Color{ 80, 80, 80, 140 });
            continue;
        }
        float hpHullPct = hp.maxHull > 0.0f ? std::clamp(hp.hull / hp.maxHull, 0.0f, 1.0f) : 0.0f;
        Color ringCol = hpHullPct > 0.5f ? Color{ 48,188,68,255 }
            : hpHullPct > 0.25f ? Color{ 212,168,28,255 } : Color{ 208,42,32,255 };
        DrawCircleV(hpPos, hpDrawRad, Color{ 15, 25, 40, 240 });
        DrawCircleLinesV(hpPos, hpDrawRad, ringCol);
        if (hp.isCore) DrawCircleV(hpPos, hpDrawRad * 0.4f, Color{ 200, 160, 30, 255 });
    }
    // P2: composited module render replaces the old per-hardpoint content-type
    // dot — the hull-integrity ring/backdrop above stays as its own overlay pass.
    ecs::RenderSystem::DrawHardpointRig(pos, rotation, 1.0f, hardpoints, bodyTint, WHITE);
}

void SpaceFlight::DrawRemotePlayers() const {
    for (const auto& [id, re] : _remoteEntities) {
        if (re.id == 0) continue;
        if (!PeerInCurrentWorld(id)) continue;  // host: peer is in another system
        const Vector2& pos = re.transform.position;
        const float    rot = re.transform.rotation;
        auto capIt = _remoteCapitalHardpoints.find(id);
        if (capIt != _remoteCapitalHardpoints.end()) {
            const RemoteCapitalInfo& info = capIt->second;
            DrawCapitalBody(pos, rot, info.radius, info.faction, info.hardpoints);
            continue;
        }
        Texture2D* tex = re.sprite.texture;
        if (tex && tex->id > 0) {
            float tw = (float)tex->width, th = (float)tex->height;
            float ps = re.sprite.scale;   // set per-entity when the snapshot was first seen
            bool  flip = (tex == &_gargosTex);
            Rectangle src    = { 0.0f, 0.0f, tw, flip ? -th : th };
            Rectangle dst    = { pos.x, pos.y, tw * ps, th * ps };
            Vector2   origin = { tw * ps * 0.5f, th * ps * 0.5f };
            DrawTexturePro(*tex, src, dst, origin, rot, re.sprite.tint);
        } else {
            DrawCircleV(pos, 18.0f, re.sprite.tint);
        }
        // P8-T1: remote player equipped-module icons (placeholder squares —
        // see RenderSystem::DrawHardpointRig's P2-T5 note, no module art
        // exists yet). NPCs get no equivalent call — their loadout renders
        // correctly with zero sync, see _remoteFighterMounts' declaration.
        auto fmIt = _remoteFighterMounts.find(id);
        if (fmIt != _remoteFighterMounts.end())
            ecs::RenderSystem::DrawHardpointRig(pos, rot, re.sprite.scale, fmIt->second, WHITE, WHITE);
        // Name tag above ship (NPC IDs start at 1000; player IDs are < 1000)
        const char* tag = (re.id >= 1000) ? "NPC" : "PLAYER";
        Color tagCol    = (re.id >= 1000) ? Color{ 200, 120, 80, 200 } : Color{ 100, 200, 255, 200 };
        int tw2 = MeasureText(tag, 11);
        DrawText(tag, (int)(pos.x - tw2 * 0.5f), (int)(pos.y - 30.0f), 11, tagCol);
    }
}

void SpaceFlight::DrawNpcShips() const {
    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        const NpcMeta&    m = _w->npcMeta[i];
        const ecs::Entity& e = _w->entities[i];
        if (!m.alive) continue;

        // Epic 7.3: the Escort contract's own briefing text promises "the
        // marked <faction> trader" but nothing ever drew a marker — fixed
        // here, a pulsing ring + label over whichever NPC is escortNpcId,
        // same visual language as the derelict-wreck label below.
        if (_hasActiveContract && _activeContract.type == ContractType::Escort &&
            _activeContract.escortNpcId == m.id) {
            float pulse = sinf((float)GetTime() * 3.0f) * 0.5f + 0.5f;
            unsigned char a = (unsigned char)(120 + 100 * pulse);
            DrawCircleLines((int)e.transform.position.x, (int)e.transform.position.y,
                (int)(m.radius + 16.0f), Color{ 80, 200, 255, a });
            const char* label = "ESCORT TARGET";
            Vector2 ts = MeasureTextEx(_hudFontVal, label, 11.0f, 1.0f);
            DrawTextEx(_hudFontVal, label,
                { e.transform.position.x - ts.x / 2.0f, e.transform.position.y - m.radius - 26.0f },
                11.0f, 1.0f, Color{ 120, 220, 255, 230 });
        }

        const ecs::ShipDef* shipDef = ecs::ShipRegistry::ShipById(m.shipTypeId);

        // Gated on !m.hardpoints.empty() too, not just shipType: combat/collision
        // code (UpdateNpcCollisions, UpdateCapitalFire) treats "is a capital"
        // as "has a populated hardpoint list" — if a malformed ship def ever
        // produced a Capital with no hardpoints, that ship must fall through
        // to the normal fighter draw/hit-test path below rather than render as
        // an oval no code path can damage.
        if (shipDef && shipDef->shipType == ShipType::Capital && !m.hardpoints.empty()) {
            // Placeholder capital art: faction-tinted oval + hardpoint markers,
            // composited the same way DrawStations() draws station hardpoints.
            // Flat shapes aren't lit (see DrawAsteroid's non-textured fallback) —
            // only textured draws call _lighting.BeginLit/EndLit.
            DrawCapitalBody(e.transform.position, e.transform.rotation, m.radius, m.faction, m.hardpoints);
            continue;
        }

        Texture2D* texPtr = nullptr;
        if (m.shipTypeId == "gargos") {
            texPtr = const_cast<Texture2D*>(&_gargosTex);
        } else if (e.sprite.texture && e.sprite.texture->id > 0) {
            texPtr = e.sprite.texture;
        } else if (shipDef) {
            texPtr = ResourceManager::Load(shipDef->assetPath);
        }
        bool hasTexture = texPtr && texPtr->id > 0;
        float tw = hasTexture ? (float)texPtr->width  : 36.0f;
        float th = hasTexture ? (float)texPtr->height : 36.0f;

        float ps = shipDef ? shipDef->pixelScale : 1.0f;
        if (m.thrusting) {
            float flicker = 0.90f + 0.10f * sinf((float)GetTime() * 16.0f + (float)m.id);
            Vector2 exhOff = Vector2Rotate({ 0.0f, th * ps * 0.38f }, e.transform.rotation * DEG2RAD);
            Vector2 exhPos = Vector2Add(e.transform.position, exhOff);
            DrawCircleV(exhPos, 4.5f * flicker, Color{ 180, 160, 80, 130 });
            DrawCircleV(exhPos, 7.0f * flicker, Color{  90,  80, 40,  50 });
        }

        if (hasTexture) {
            bool      flip = (m.shipTypeId == "gargos");
            Rectangle src = { 0, 0, tw, flip ? -th : th };
            Rectangle dst = { e.transform.position.x, e.transform.position.y, tw * ps, th * ps };
            Vector2   origin = { tw * ps * 0.5f, th * ps * 0.5f };
            float     lightRange = _w->sun.active ? _w->sun.gravRange * 5.0f : 0.0f;
            Color     lit = _lighting.BeginLit(e.transform.position, { 0.0f, 0.0f }, lightRange);
            DrawTexturePro(*texPtr, src, dst, origin, e.transform.rotation, lit);
            _lighting.EndLit();
            // P2: composited module render (placeholder icons — see RenderSystem.h).
            ecs::RenderSystem::DrawHardpointRig(e.transform.position, e.transform.rotation, ps,
                                                 m.loadout.hardpoints, WHITE, WHITE);
        }
        else {
            float sz = m.radius;
            float r  = e.transform.rotation * DEG2RAD;
            float c  = cosf(r), s = sinf(r);
            auto  R  = [&](float ox, float oy) -> Vector2 {
                return { e.transform.position.x + ox * c - oy * s,
                         e.transform.position.y + ox * s + oy * c };
                };
            DrawTriangle(R(0, -sz), R(sz * 0.6f, sz * 0.55f), R(-sz * 0.6f, sz * 0.55f),
                Color{ 180, 180, 180, 255 });
            DrawTriangleLines(R(0, -sz), R(sz * 0.6f, sz * 0.55f), R(-sz * 0.6f, sz * 0.55f),
                Color{ 255, 255, 255, 120 });
        }
    }

    for (const LootDrop& ld : _w->lootDrops) {
        if (ld.collected) continue;
        float pulse = sinf(ld.pulseTimer * 4.0f) * 0.5f + 0.5f;
        auto  alpha8 = [](float p, int lo, int hi) -> unsigned char {
            return (unsigned char)(lo + (int)((hi - lo) * p));
            };
        DrawCircleV(ld.position, 5.0f,
            { alpha8(pulse, 140, 255), alpha8(pulse, 195, 255), 255,
              alpha8(pulse, 170, 240) });
        DrawCircleLines((int)ld.position.x, (int)ld.position.y, 13,
            { 100, 200, 255, alpha8(pulse, 100, 190) });
        Color gc = StorageMenu::GradeColor(ld.module.grade);
        gc.a = alpha8(pulse, 80, 160);
        DrawCircleLines((int)ld.position.x, (int)ld.position.y, 18, gc);
    }

    for (const MaterialDrop& md : _w->materialDrops) {
        if (md.collected) continue;
        float pulse = sinf(md.pulseTimer * 4.0f) * 0.5f + 0.5f;
        auto alpha8 = [](float p, int lo, int hi) -> unsigned char {
            return (unsigned char)(lo + (int)((hi - lo) * p));
        };
        const MatDef* mat = FindMaterial(md.materialId);
        Color core = mat ? mat->hudColor : Color{ 200, 180, 60, 255 };
        core.a = alpha8(pulse, 180, 255);
        DrawCircleV(md.position, 4.5f, core);
        DrawCircleLines((int)md.position.x, (int)md.position.y, 12,
            { core.r, core.g, core.b, alpha8(pulse, 100, 190) });
        DrawCircleLines((int)md.position.x, (int)md.position.y, 17,
            { core.r, core.g, core.b, alpha8(pulse, 50, 120) });
    }

    // Epic 11.1: derelict wrecks — dark hulk + a slow amber salvage-beacon
    // pulse (distinct from the faster blue/gold LootDrop/MaterialDrop pulse
    // above, since this is a rarer, more deliberate target) and a persistent
    // label since it never expires and isn't a targetable NpcMeta/SpaceStation.
    for (const DerelictWreck& w : _w->derelictWrecks) {
        if (w.collected) continue;
        float pulse = sinf((float)GetTime() * 2.0f) * 0.5f + 0.5f;
        auto  alpha8 = [](float p, int lo, int hi) -> unsigned char {
            return (unsigned char)(lo + (int)((hi - lo) * p));
        };
        float hullR = w.isCapital ? 34.0f : 22.0f;
        DrawCircleV(w.position, hullR, Color{ 40, 36, 30, 230 });
        DrawCircleLines((int)w.position.x, (int)w.position.y, (int)hullR, Color{ 90, 80, 60, 255 });
        DrawCircleLines((int)w.position.x, (int)w.position.y, (int)(hullR + 14.0f),
            Color{ 230, 170, 60, alpha8(pulse, 90, 200) });
        const char* label = "DERELICT WRECK";
        Vector2 ts = MeasureTextEx(_hudFontVal, label, 11.0f, 1.0f);
        DrawTextEx(_hudFontVal, label, { w.position.x - ts.x / 2.0f, w.position.y - hullR - 22.0f },
            11.0f, 1.0f, Color{ 230, 190, 110, 230 });
    }
}

static unsigned int s_nextAsteroidId = 1;

static Asteroid MakeAsteroid(Vector2 pos, int tier) {
    Asteroid a;
    a.id = s_nextAsteroidId++;
    a.position = pos;
    a.tier = tier;
    a.radius = AsteroidRadius(tier);
    a.health = AsteroidHealth(tier);
    float spd = AsteroidSpeed(tier) * (0.7f + (float)GetRandomValue(0, 60) / 100.0f);
    float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
    a.velocity = { cosf(ang) * spd, sinf(ang) * spd };
    a.rotation = (float)GetRandomValue(0, 360);
    a.rotSpeed = (float)GetRandomValue(-55, 55);
    return a;
}

// Shared material rarity weighting: used both for asteroid composition rolls
// and for mining-station auto-collection.
struct MaterialPoolEntry { const char* id; int minPct, maxPct, weight; };
static const MaterialPoolEntry kMaterialPool[] = {
    { "iron",      40, 80, 35 },
    { "carbon",    35, 70, 30 },
    { "silica",    30, 65, 25 },
    { "titanium",  20, 45, 20 },
    { "cobalt",    15, 40, 15 },
    { "tungsten",  10, 28, 10 },
    { "crystite",   8, 22,  7 },
    { "xenonite",   4, 14,  4 },
    { "voidstone",  2,  8,  1 },
};
static constexpr int kMaterialPoolSize = 9;

static void AssignAsteroidMaterials(Asteroid& a) {
    // Tier 2: 30% 1-mat, 45% 2-mat, 25% 3-mat
    // Tier 1: 60% 1-mat, 33% 2-mat,  7% 3-mat
    // Tier 0: 85% 1-mat, 15% 2-mat,  0% 3-mat
    int r = GetRandomValue(0, 99);
    int matCount;
    if      (a.tier == 2) matCount = (r < 30) ? 1 : (r < 75) ? 2 : 3;
    else if (a.tier == 1) matCount = (r < 60) ? 1 : (r < 93) ? 2 : 3;
    else                  matCount = (r < 85) ? 1 : 2;

    bool used[kMaterialPoolSize] = {};
    for (int m = 0; m < matCount; ++m) {
        int totalW = 0;
        for (int i = 0; i < kMaterialPoolSize; ++i) if (!used[i]) totalW += kMaterialPool[i].weight;
        int pick = GetRandomValue(0, totalW - 1), cumW = 0;
        for (int i = 0; i < kMaterialPoolSize; ++i) {
            if (used[i]) continue;
            cumW += kMaterialPool[i].weight;
            if (pick < cumW) {
                used[i] = true;
                a.materials.push_back({ kMaterialPool[i].id, GetRandomValue(kMaterialPool[i].minPct, kMaterialPool[i].maxPct) });
                break;
            }
        }
    }
}

// Picks a single material id by the same rarity weighting as asteroids.
static std::string RollMiningMaterialId() {
    int totalW = 0;
    for (int i = 0; i < kMaterialPoolSize; ++i) totalW += kMaterialPool[i].weight;
    int pick = GetRandomValue(0, totalW - 1), cumW = 0;
    for (int i = 0; i < kMaterialPoolSize; ++i) {
        cumW += kMaterialPool[i].weight;
        if (pick < cumW) return kMaterialPool[i].id;
    }
    return kMaterialPool[kMaterialPoolSize - 1].id;
}

void SpaceFlight::SpawnInitialAsteroids() {
    for (int i = 0; i < 8; ++i) {
        float ang = ((float)i / 8.0f) * 2.0f * PI;
        float dist = (float)GetRandomValue(500, 1200);
        Asteroid a = MakeAsteroid({ cosf(ang) * dist, sinf(ang) * dist }, 2);
        AssignAsteroidMaterials(a);
        _w->asteroids.push_back(std::move(a));
    }
}

static void DrawAsteroid(const Asteroid& a, const Texture2D* tex,
    const ecs::LightingSystem& lighting, float lightRange) {
    if (tex && tex->id > 0) {
        float tw = (float)tex->width, th = (float)tex->height;
        float diameter = a.radius * 2.0f;
        Rectangle src    = { 0.0f, 0.0f, tw, th };
        Rectangle dst    = { a.position.x, a.position.y, diameter, diameter };
        Vector2   origin = { diameter / 2.0f, diameter / 2.0f };
        Color     lit = lighting.BeginLit(a.position, { 0.0f, 0.0f }, lightRange);
        DrawTexturePro(*tex, src, dst, origin, a.rotation, lit);
        lighting.EndLit();
        return;
    }
    int sides = a.tier == 2 ? 8 : a.tier == 1 ? 7 : 6;
    Color outline = a.tier == 2 ? Color{ 150, 135, 110, 255 }
        : a.tier == 1 ? Color{ 125, 110,  90, 255 }
    : Color{ 105,  95,  80, 255 };
    Color inner = { (unsigned char)(outline.r / 2),
                      (unsigned char)(outline.g / 2),
                      (unsigned char)(outline.b / 2), 155 };
    DrawPoly(a.position, sides, a.radius, a.rotation, Color{ 30, 26, 21, 255 });
    DrawPolyLinesEx(a.position, sides, a.radius, a.rotation, 1.5f, outline);
    DrawPolyLinesEx(a.position, sides - 1, a.radius * 0.60f, a.rotation + 25.0f, 1.0f, inner);
}

static void DestroyAsteroid(Asteroid& a, std::vector<Asteroid>& spawns) {
    a.alive = false;
    if (a.tier > 0) {
        Vector2 off = { a.radius * 0.5f, 0.0f };
        Asteroid c1 = MakeAsteroid(Vector2Add(a.position, off), a.tier - 1);
        Asteroid c2 = MakeAsteroid(Vector2Subtract(a.position, off), a.tier - 1);
        for (const auto& mc : a.materials) {
            int cp = mc.percent / 2;
            if (cp > 0) {
                c1.materials.push_back({ mc.materialId, cp });
                c2.materials.push_back({ mc.materialId, cp });
            }
        }
        spawns.push_back(std::move(c1));
        spawns.push_back(std::move(c2));
    }
}

void SpaceFlight::UpdateCollisions() {
    std::vector<Asteroid> spawns;

    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        if (!_w->npcMeta[i].alive) continue;
        for (size_t j = i + 1; j < _w->npcMeta.size(); ++j) {
            if (!_w->npcMeta[j].alive) continue;

            float dist = Vector2Distance(_w->entities[i].transform.position, _w->entities[j].transform.position);
            float minDist = _w->npcMeta[i].radius + _w->npcMeta[j].radius;

            if (dist < minDist && dist > 0.01f) {
                Vector2 norm = Vector2Scale(Vector2Subtract(_w->entities[i].transform.position, _w->entities[j].transform.position), 1.0f / dist);
                float overlap = minDist - dist;

                _w->entities[i].transform.position = Vector2Add(_w->entities[i].transform.position, Vector2Scale(norm, overlap * 0.5f));
                _w->entities[j].transform.position = Vector2Subtract(_w->entities[j].transform.position, Vector2Scale(norm, overlap * 0.5f));

                float vRelN = Vector2DotProduct(Vector2Subtract(_w->entities[i].transform.velocity, _w->entities[j].transform.velocity), norm);
                if (vRelN < 0.0f) {
                    float bounceImpulse = -1.25f * vRelN;
                    _w->entities[i].transform.velocity = Vector2Add(_w->entities[i].transform.velocity, Vector2Scale(norm, bounceImpulse * 0.5f));
                    _w->entities[j].transform.velocity = Vector2Subtract(_w->entities[j].transform.velocity, Vector2Scale(norm, bounceImpulse * 0.5f));
                }
            }
        }
    }

    // Skipped while docked or seated in a turret (Epic 8) — a frozen,
    // invisible player shouldn't get rammed or physically pushed around by NPCs.
    if (!_stationServicesMenu.isOpen && !_seated) {
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive) continue;

            float dist = Vector2Distance(_playerEntity.transform.position, e.transform.position);
            float minDist = _playerMeta.radius + m.radius;

            if (dist < minDist && dist > 0.01f) {
                Vector2 norm = Vector2Scale(Vector2Subtract(_playerEntity.transform.position, e.transform.position), 1.0f / dist);
                float overlap = minDist - dist;

                _playerEntity.transform.position = Vector2Add(_playerEntity.transform.position, Vector2Scale(norm, overlap * 0.5f));
                e.transform.position = Vector2Subtract(e.transform.position, Vector2Scale(norm, overlap * 0.5f));

                float vRelN = Vector2DotProduct(Vector2Subtract(_playerEntity.transform.velocity, e.transform.velocity), norm);
                if (vRelN < 0.0f) {
                    float bounceImpulse = -1.25f * vRelN;
                    _playerEntity.transform.velocity = Vector2Add(_playerEntity.transform.velocity, Vector2Scale(norm, bounceImpulse * 0.5f));
                    e.transform.velocity = Vector2Subtract(e.transform.velocity, Vector2Scale(norm, bounceImpulse * 0.5f));
                }
            }
        }
    }

    // Only the host (or offline player) resolves hits; clients receive asteroid
    // state via server snapshots so local hit-detection would desync health.
    if (!net::Game().IsClient()) {
        for (Projectile& p : _w->projectiles) {
            if (!p.alive || !p.fromPlayer) continue;
            for (Asteroid& a : _w->asteroids) {
                if (!a.alive) continue;
                if (Vector2Distance(p.position, a.position) < a.radius + 3.5f) {
                    p.alive = false;
                    a.health -= (int)std::lround(p.damage);
                    if (a.health <= 0) {
                        for (const auto& mc : a.materials)
                            if (GetRandomValue(1, 100) <= mc.percent)
                                SpawnMaterialDrop(a.position, mc.materialId);
                        DestroyAsteroid(a, spawns);
                        if (!_bgTick) AdvanceTutorialStep(TutorialStep::DestroyAsteroid);
                    }
                    break;
                }
            }
        }
    }

    int n = (int)_w->asteroids.size();
    for (int i = 0; i < n; ++i) {
        Asteroid& a = _w->asteroids[i];
        if (!a.alive) continue;
        for (int j = i + 1; j < n; ++j) {
            Asteroid& b = _w->asteroids[j];
            if (!b.alive) continue;

            float dist = Vector2Distance(a.position, b.position);
            float minDist = a.radius + b.radius;
            if (dist >= minDist || dist < 0.01f) continue;

            Vector2 norm = Vector2Scale(Vector2Subtract(a.position, b.position), 1.0f / dist);
            float   massA = a.radius * a.radius;
            float   massB = b.radius * b.radius;
            float   totMass = massA + massB;
            float   overlap = minDist - dist;

            a.position = Vector2Add(a.position, Vector2Scale(norm, overlap * massB / totMass));
            b.position = Vector2Subtract(b.position, Vector2Scale(norm, overlap * massA / totMass));

            float vRelN = Vector2DotProduct(Vector2Subtract(a.velocity, b.velocity), norm);
            if (vRelN < 0.0f) {
                const float e = 0.65f;
                float       impulse = -(1.0f + e) * vRelN / (1.0f / massA + 1.0f / massB);
                a.velocity = Vector2Add(a.velocity, Vector2Scale(norm, impulse / massA));
                b.velocity = Vector2Subtract(b.velocity, Vector2Scale(norm, impulse / massB));

                if (a.tier > b.tier) {
                    b.health -= (int)std::lround(AsteroidDamage(a.tier));
                    if (b.health <= 0) {
                        for (const auto& mc : b.materials)
                            if (GetRandomValue(1, 100) <= mc.percent)
                                SpawnMaterialDrop(b.position, mc.materialId);
                        DestroyAsteroid(b, spawns);
                    }
                }
                else if (b.tier > a.tier) {
                    a.health -= (int)std::lround(AsteroidDamage(b.tier));
                    if (a.health <= 0) {
                        for (const auto& mc : a.materials)
                            if (GetRandomValue(1, 100) <= mc.percent)
                                SpawnMaterialDrop(a.position, mc.materialId);
                        DestroyAsteroid(a, spawns);
                        break;
                    }
                }
            }
        }
    }

    if (_hitCooldown <= 0.0f && !_stationServicesMenu.isOpen && !_seated) {
        for (Asteroid& a : _w->asteroids) {
            if (!a.alive) continue;
            if (Vector2Distance(_playerEntity.transform.position, a.position) < _playerMeta.radius + a.radius) {
                float dmg = AsteroidDamage(a.tier);
                if (_playerEntity.health.currentShield > 0.0f) {
                    float absorbed = std::min(_playerEntity.health.currentShield, dmg);
                    _playerEntity.health.currentShield -= absorbed;
                    dmg -= absorbed;
                }
                _playerEntity.health.currentHull = std::max(0.0f, _playerEntity.health.currentHull - dmg);
                _hitCooldown = 1.2f;
                Vector2 away = Vector2Subtract(_playerEntity.transform.position, a.position);
                float   len = Vector2Length(away);
                if (len > 0.01f)
                    _playerEntity.transform.velocity = Vector2Add(_playerEntity.transform.velocity, Vector2Scale(away, 160.0f / len));

                bool loadoutDirty = false;
                auto rollDestroy = [&](std::optional<ModuleDef>& slot) {
                    if (slot && GetRandomValue(0, 999) < 3) {
                        slot = std::nullopt;
                        loadoutDirty = true;
                    }
                    };
                for (auto* w : _loadout.WeaponSlots()) rollDestroy(w->equipped);
                for (auto* s : _loadout.ShieldSlots()) rollDestroy(s->equipped);
                for (auto* x : _loadout.AuxSlots())    rollDestroy(x->equipped);
                if (auto* engineSlot = _loadout.Engine())     rollDestroy(engineSlot->equipped);
                if (auto* hyperSlot  = _loadout.Hyperdrive()) rollDestroy(hyperSlot->equipped);
                if (_playerEntity.health.currentHull <= 0.0f) {
                    if (auto* armorSlot = _loadout.Armor(); armorSlot && armorSlot->equipped) {
                        armorSlot->equipped = std::nullopt;
                        loadoutDirty = true;
                    }
                }
                if (loadoutDirty) ApplyLoadout();
                break;
            }
        }
    }

    // Asteroid → NPC ship damage
    for (Asteroid& a : _w->asteroids) {
        if (!a.alive) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive) continue;
            if (m.asteroidHitCooldown > 0.0f) continue;
            if (Vector2Distance(e.transform.position, a.position) < m.radius + a.radius) {
                float dmg = AsteroidDamage(a.tier);
                if (e.health.currentShield > 0.0f) {
                    float absorb = std::min(e.health.currentShield, dmg);
                    e.health.currentShield -= absorb;
                    dmg -= absorb;
                }
                e.health.currentHull = std::max(0.0f, e.health.currentHull - dmg);
                m.asteroidHitCooldown = 1.2f;
                Vector2 away = Vector2Subtract(e.transform.position, a.position);
                float   len  = Vector2Length(away);
                if (len > 0.01f)
                    e.transform.velocity = Vector2Add(e.transform.velocity, Vector2Scale(away, 160.0f / len));
                if (e.health.currentHull <= 0.0f) {
                    m.alive = false;
                    _w->npcFreeSlots.push_back(i);
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    SpawnLootDrop(e.transform.position, m.faction);
                    AddCommsMessage(m.wingman ? "WINGMAN destroyed by asteroid." : "Ship destroyed by asteroid.");
                }
            }
        }
    }

    // Remote client entities hit by any fromPlayer projectile (host-authoritative).
    if (net::Game().IsHost() && !_remoteEntities.empty()) {
        std::vector<uint32_t> deadClients;
        for (Projectile& p : _w->projectiles) {
            if (!p.alive || !p.fromPlayer) continue;
            for (auto& [netId, re] : _remoteEntities) {
                if (re.id == 0) continue;
                if (p.ownerId == netId) continue;  // don't let a client's projectile hit themselves
                if (!PeerInCurrentWorld(netId)) continue;  // peer is in another system
                if (_remoteDocked[netId]) continue;  // docked in a station menu — untargetable
                if (Vector2Distance(p.position, re.transform.position) < 18.0f + 3.5f) {
                    p.alive = false;
                    re.health.currentHull = std::max(0.0f, re.health.currentHull - p.damage);
                    if (re.health.currentHull <= 0.0f) {
                        auto git = _remoteJoinGrace.find(netId);
                        if (git == _remoteJoinGrace.end() || git->second <= 0.0f)
                            deadClients.push_back(netId);
                    }
                    break;
                }
            }
        }
        for (uint32_t id : deadClients) {
            net::Game().HostSendPlayerDead(id);
            _remoteEntities.erase(id);
            _remoteFireCooldown.erase(id);
            _remoteJoinGrace.erase(id);
            _remoteDocked.erase(id);
        }
    }

    for (auto& a : spawns) _w->asteroids.push_back(std::move(a));
}

using namespace hudtheme;
static constexpr int HudH = 174;

// Simplified icon glyphs for the icon+label HUD buttons.
static void DrawHudHammerIcon(Vector2 c, float s, Color color) {
    DrawRectangleRec({ c.x - s * 0.55f, c.y - s * 0.65f, s * 1.1f, s * 0.5f }, color);
    DrawRectangleRec({ c.x - s * 0.12f, c.y - s * 0.15f, s * 0.24f, s * 0.85f }, color);
}
static void DrawHudRadarIcon(Vector2 c, float s, Color color) {
    DrawRing({ c.x, c.y - s * 0.15f }, s * 0.55f, s * 0.68f, 0.0f, 180.0f, 16, color);
    DrawLineEx({ c.x, c.y + s * 0.05f }, { c.x, c.y + s * 0.55f }, 2.0f, color);
    DrawLineEx({ c.x - s * 0.05f, c.y - s * 0.05f }, { c.x - s * 0.5f, c.y - s * 0.55f }, 2.0f, color);
    DrawCircleV({ c.x - s * 0.5f, c.y - s * 0.55f }, s * 0.09f, color);
}
// Down arrow dropping into a dock/hangar bracket — reads as "enter/dock".
static void DrawHudDockIcon(Vector2 c, float s, Color color) {
    DrawLineEx({ c.x, c.y - s * 0.55f }, { c.x, c.y + s * 0.05f }, 2.0f, color);
    DrawTriangle(
        { c.x - s * 0.28f, c.y + s * 0.05f },
        { c.x, c.y + s * 0.38f },
        { c.x + s * 0.28f, c.y + s * 0.05f },
        color);
    float by = c.y + s * 0.55f;
    DrawLineEx({ c.x - s * 0.5f, by - s * 0.28f }, { c.x - s * 0.5f, by }, 2.0f, color);
    DrawLineEx({ c.x + s * 0.5f, by - s * 0.28f }, { c.x + s * 0.5f, by }, 2.0f, color);
    DrawLineEx({ c.x - s * 0.5f, by }, { c.x + s * 0.5f, by }, 2.0f, color);
}
// Crosshair-in-a-ring — reads as "man a turret" (Epic 8).
static void DrawHudTurretIcon(Vector2 c, float s, Color color) {
    DrawRing(c, s * 0.38f, s * 0.5f, 0.0f, 360.0f, 20, color);
    DrawLineEx({ c.x - s * 0.62f, c.y }, { c.x - s * 0.3f, c.y }, 2.0f, color);
    DrawLineEx({ c.x + s * 0.3f, c.y },  { c.x + s * 0.62f, c.y }, 2.0f, color);
    DrawLineEx({ c.x, c.y - s * 0.62f }, { c.x, c.y - s * 0.3f }, 2.0f, color);
    DrawLineEx({ c.x, c.y + s * 0.3f },  { c.x, c.y + s * 0.62f }, 2.0f, color);
}

void SpaceFlight::UpdateTarget() {
    Vector2 mouse = GetMousePosition();
    Vector2 mw = GetScreenToWorld2D(mouse, _camera);

    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        const NpcMeta&    m = _w->npcMeta[i];
        const ecs::Entity& e = _w->entities[i];
        if (!m.alive) continue;
        if (CheckCollisionPointCircle(mw, e.transform.position, m.radius)) {
            _npcTargetId = m.id;
            _targetId = 0;
            _target.valid = true;
            _target.isNpc = true;
            _target.isStellar = false;
            _target.iconTex = nullptr;
            _target.npcFaction = m.faction;
            _target.role = m.role;
            _target.disabled = m.disabled; // Epic 9.1
            _target.worldPos = e.transform.position;
            _target.name = m.wingman
                ? ("WINGMAN " + m.shipTypeName)
                : (m.faction == NpcFaction::Hostile) ? ("HOSTILE " + m.shipTypeName)
                : (m.faction == NpcFaction::Neutral)  ? ("UNKNOWN " + m.shipTypeName)
                : ("FRIENDLY " + m.shipTypeName);
            _target.typeDesc = m.wingman ? "Escort wingman"
                : (m.faction == NpcFaction::Hostile) ? "Hostile fighter craft"
                : (m.faction == NpcFaction::Neutral)  ? "Unknown vessel"
                : "Friendly patrol craft";
            _target.hasFaction  = true;
            _target.gameFaction = m.npcFaction;
            _target.health = e.health.currentHull;
            _target.maxHealth = e.health.maxStats.hull;
            _target.distance = Vector2Distance(_playerEntity.transform.position, e.transform.position);
            _target.tier = -1;
            _target.isWingman = m.wingman;
            _target.kineticShield = e.health.currentShield;
            _target.maxKineticShield = e.health.maxStats.shield;
            _target.energyShield = m.energyShield;
            _target.maxEnergyShield = m.maxEnergyShield;
            return;
        }
    }

    for (const Asteroid& a : _w->asteroids) {
        if (!a.alive) continue;
        if (Vector2Distance(mw, a.position) < a.radius) {
            _targetId = a.id;
            _npcTargetId = 0;
            _target.valid = true;
            _target.isNpc = false;
            _target.isStellar = false;
            {
                const Texture2D* atex = a.tier == 2 ? &_asteroidTexLarge
                    : a.tier == 1 ? &_asteroidTexMedium : &_asteroidTexSmall;
                _target.iconTex = atex->id > 0 ? atex : nullptr;
            }
            _target.worldPos = a.position;
            _target.name = a.tier == 2 ? "LARGE ASTEROID"
                : a.tier == 1 ? "MEDIUM ASTEROID" : "SMALL ASTEROID";
            _target.typeDesc = a.tier == 2 ? "Dense mineral formation"
                : a.tier == 1 ? "Fragmented asteroid" : "Asteroid fragment";
            _target.health = (float)a.health;
            _target.maxHealth = (float)AsteroidHealth(a.tier);
            _target.distance = Vector2Distance(_playerEntity.transform.position, a.position);
            _target.tier = a.tier;
            _target.materialComps = a.materials;
            return;
        }
    }

    for (const SpaceStation& s : _w->stations) {
        if (!s.alive) continue;
        if (Vector2Distance(mw, s.position) < s.radius + 8.0f) {
            _targetId = s.id;
            _npcTargetId = 0;
            _target.valid = true;
            _target.isNpc = false;
            _target.isStellar = true;
            _target.iconTex = _stationBaseTex.id > 0 ? &_stationBaseTex : nullptr;
            _target.worldPos = s.position;
            {
                const StationTypeDef* stType = StationTypeRegistry::ById(s.stationTypeId);
                _target.name    = stType ? stType->displayName : "Space Station";
            }
            _target.typeDesc    = "Orbital platform";
            _target.hasFaction  = true;
            _target.gameFaction = s.faction;
            _target.npcFaction  = RelationToNpcFaction(ReputationRegistry::PlayerRelation(s.faction));
            _target.disabled    = s.disabled; // Epic 9.1
            _target.health = s.hull;
            _target.maxHealth = s.maxHull;
            _target.distance = Vector2Distance(_playerEntity.transform.position, s.position);
            _target.tier = -1;
            return;
        }
    }

    for (const SpacePlanet& p : _w->planets) {
        if (Vector2Distance(mw, p.position) < p.radius + 8.0f) {
            _targetId = p.id;
            _npcTargetId = 0;
            _target.valid = true;
            _target.isNpc = false;
            _target.isStellar = true;
            _target.iconTex = _planetBaseTex.id > 0 ? &_planetBaseTex : nullptr;
            _target.worldPos = p.position;
            _target.name = "PLANET";
            _target.typeDesc = "Terrestrial body";
            _target.health = 0.0f;
            _target.maxHealth = 1.0f;
            _target.distance = Vector2Distance(_playerEntity.transform.position, p.position);
            _target.tier = -1;
            return;
        }
    }

    if (_target.valid) {
        if (_target.isNpc) {
            bool found = false;
            for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
                const NpcMeta&    m = _w->npcMeta[i];
                const ecs::Entity& e = _w->entities[i];
                if (m.id == _npcTargetId && m.alive) {
                    _target.health = e.health.currentHull;
                    _target.worldPos = e.transform.position;
                    _target.distance = Vector2Distance(_playerEntity.transform.position, e.transform.position);
                    _target.kineticShield = e.health.currentShield;
                    _target.energyShield = m.energyShield;
                    found = true;
                    break;
                }
            }
            if (!found) { _target = TargetInfo{}; _npcTargetId = 0; }
        }
        else if (!_target.isStellar) {
            bool found = false;
            for (const Asteroid& a : _w->asteroids) {
                if (a.id == _targetId && a.alive) {
                    _target.health = (float)a.health;
                    _target.worldPos = a.position;
                    _target.distance = Vector2Distance(_playerEntity.transform.position, a.position);
                    _target.materialComps = a.materials;
                    found = true;
                    break;
                }
            }
            if (!found) { _target = TargetInfo{}; _targetId = 0; }
        }
    }
}

void SpaceFlight::DrawHUD() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int hx = 12, hy = sh - HudH - 6;
    int hw = sw - hx * 2;
    Vector2 mouse = GetMousePosition();

    static constexpr int CenterW = 190;
    int lDiv = hx + (hw - CenterW) / 2;
    int rDiv = lDiv + CenterW;

    DrawHudBracketPanel({ (float)hx, (float)hy, (float)hw, (float)HudH }, HudBg, HudBorder, 18.0f, 2.0f);
    DrawRectangle(lDiv, hy + 10, 1, HudH - 20, HudDiv);
    DrawRectangle(rDiv, hy + 10, 1, HudH - 20, HudDiv);

    auto DrawStatusRing = [](Vector2 c, float iR, float oR,
        float pct, Color fill, Color bg) {
            DrawRing(c, iR, oR, -90.0f, 270.0f, 64, bg);
            if (pct > 0.005f) {
                Color glow = fill; glow.a = 55;
                DrawRing(c, iR - 2.0f, oR + 2.0f, -90.0f, -90.0f + 360.0f * pct, 64, glow);
                DrawRing(c, iR, oR, -90.0f, -90.0f + 360.0f * pct, 64, fill);
            }
            for (int t = 0; t < 4; ++t) {
                float ang = (-90.0f + 90.0f * t) * DEG2RAD;
                Vector2 dir = { cosf(ang), sinf(ang) };
                DrawLineEx({ c.x + dir.x * (iR - 2.0f), c.y + dir.y * (iR - 2.0f) },
                    { c.x + dir.x * (oR + 2.0f), c.y + dir.y * (oR + 2.0f) },
                    1.0f, Color{ 10,14,18,200 });
            }
        };
    auto DrawHalfRing = [](Vector2 c, float iR, float oR,
        float pct, Color fill, Color bg, bool left) {
            float s = left ? 90.0f : -90.0f;
            DrawRing(c, iR, oR, s, s + 180.0f, 32, bg);
            if (pct > 0.005f) {
                Color glow = fill; glow.a = 55;
                DrawRing(c, iR - 2.0f, oR + 2.0f, s, s + 180.0f * pct, 32, glow);
                DrawRing(c, iR, oR, s, s + 180.0f * pct, 32, fill);
            }
        };
    auto Rot2D = [](Vector2 v, float deg) -> Vector2 {
        float r = deg * DEG2RAD;
        return { v.x * cosf(r) - v.y * sinf(r), v.x * sinf(r) + v.y * cosf(r) };
        };

    const float sAreaR = 28.0f;
    const float sHpIn = 30.0f, sHpOut = 37.0f;
    const float sShIn = 39.0f, sShOut = 46.0f;
    Vector2 sc = { (float)((lDiv + rDiv) / 2), (float)(hy + HudH / 2 - 6) };

    DrawCircleV(sc, sShOut + 1.0f, Color{ 6, 10, 6, 230 });
    DrawCircleLines((int)sc.x, (int)sc.y, (int)(sShOut + 3.0f), Color{ 90,150,190,90 });

    float hullPct = _playerEntity.health.currentHull / _playerEntity.health.maxStats.hull;
    Color hullCol = hullPct > 0.5f ? HudGood
        : hullPct > 0.25f ? HudCaution
        : HudCritical;
    if (hullPct <= 0.25f) {
        float pulse = 0.55f + 0.45f * sinf((float)GetTime() * 6.0f);
        hullCol.a = (unsigned char)(150 + 90 * pulse);
    }
    DrawStatusRing(sc, sHpIn, sHpOut, hullPct, hullCol, Color{ 22,32,22,200 });

    float ksPct = _playerEntity.health.maxStats.shield > 0.0f
        ? _playerEntity.health.currentShield / _playerEntity.health.maxStats.shield : 0.0f;
    float esPct = _playerMeta.maxEnergyShield > 0.0f
        ? _playerMeta.energyShield / _playerMeta.maxEnergyShield : 0.0f;
    DrawHalfRing(sc, sShIn, sShOut, ksPct, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
    DrawHalfRing(sc, sShIn, sShOut, esPct, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
    DrawCircleLines((int)sc.x, (int)sc.y, (int)sAreaR, Color{ 30,55,30,160 });

    {
        char pctBuf[8];
        std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", hullPct * 100.0f);
        Vector2 hpTs = MeasureTextEx(_hudFontVal, pctBuf, 15.0f, 1.0f);
        DrawTextEx(_hudFontVal, pctBuf, { sc.x - hpTs.x / 2.0f, sc.y + sShOut + 6.0f }, 15.0f, 1.0f, hullCol);

        if (_playerEntity.health.maxStats.shield > 0.0f) {
            std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", ksPct * 100.0f);
            Vector2 ksTs = MeasureTextEx(_hudFontVal, pctBuf, 14.0f, 1.0f);
            DrawTextEx(_hudFontVal, pctBuf, { sc.x - sShOut - ksTs.x - 5.0f, sc.y - ksTs.y / 2.0f },
                14.0f, 1.0f, Color{ 255,210,60,255 });
        }
        if (_playerMeta.maxEnergyShield > 0.0f) {
            std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", esPct * 100.0f);
            Vector2 esTs = MeasureTextEx(_hudFontVal, pctBuf, 14.0f, 1.0f);
            DrawTextEx(_hudFontVal, pctBuf, { sc.x + sShOut + 5.0f, sc.y - esTs.y / 2.0f },
                14.0f, 1.0f, Color{ 60,180,220,255 });
        }
    }

    Texture2D* shipTexPtr = _playerShipTex;
    if (shipTexPtr && shipTexPtr->id > 0) {
        const Texture2D& shipTex = *shipTexPtr;
        float tw = (float)shipTex.width;
        float th = (float)shipTex.height;
        float scale = (sAreaR * 1.75f) / std::max(tw, th);
        Rectangle src = { 0.0f, 0.0f, tw, th };
        Rectangle dst = { sc.x, sc.y, tw * scale, th * scale };
        Vector2   origin = { tw * scale / 2.0f, th * scale / 2.0f };
        DrawTexturePro(shipTex, src, dst, origin, _playerEntity.transform.rotation, WHITE);
        if (_playerMeta.thrusting) {
            Vector2 exh = Vector2Add(sc, Rot2D({ 0.0f, th * scale * 0.38f }, _playerEntity.transform.rotation));
            DrawCircleV(exh, 2.5f, Color{ 255,160,60,110 });
        }
    }
    else {
        const float iSz = sAreaR * 0.55f;
        Vector2 tip = Vector2Add(sc, Rot2D({ 0.0f,      -iSz }, _playerEntity.transform.rotation));
        Vector2 lft = Vector2Add(sc, Rot2D({ -iSz * 0.6f,  iSz * 0.55f }, _playerEntity.transform.rotation));
        Vector2 rgt = Vector2Add(sc, Rot2D({ iSz * 0.6f,  iSz * 0.55f }, _playerEntity.transform.rotation));
        DrawTriangle(tip, rgt, lft, Color{ 60,140,230,255 });
        DrawTriangleLines(tip, rgt, lft, Color{ 140,200,255,255 });
    }
    const char* snc = _playerMeta.displayName.c_str();
    Vector2 sncTs = MeasureTextEx(_hudFontUi, snc, 11.0f, 1.0f);
    DrawTextEx(_hudFontUi, snc, { sc.x - sncTs.x / 2.0f, (float)(hy + HudH - 17) }, 11.0f, 1.0f, HudLabel);

    const float tAreaR = 26.0f;
    const float tHpIn = 28.0f, tHpOut = 35.0f;
    const float tShIn = 37.0f, tShOut = 44.0f;
    Vector2 tc = { (float)(hx + 60), (float)(hy + HudH / 2 - 6) };
    DrawCircleV(tc, tShOut + 1.0f, Color{ 6, 8, 14, 230 });
    DrawCircleLines((int)tc.x, (int)tc.y, (int)(tShOut + 3.0f), Color{ 90,150,190,90 });

    const bool hasSensors = _hasSensors;

    if (_target.valid) {
        if (_target.isNpc) {
            float tHpPct = _target.health / _target.maxHealth;
            Color tHpCol = tHpPct > 0.5f ? HudGood
                : tHpPct > 0.25f ? HudCaution
                : HudCritical;
            DrawStatusRing(tc, tHpIn, tHpOut, tHpPct, tHpCol, Color{ 22,22,32,200 });
            float tKsPct = (_target.maxKineticShield > 0.0f)
                ? _target.kineticShield / _target.maxKineticShield : 0.0f;
            float tEsPct = (_target.maxEnergyShield > 0.0f)
                ? _target.energyShield / _target.maxEnergyShield : 0.0f;
            DrawHalfRing(tc, tShIn, tShOut, tKsPct, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
            DrawHalfRing(tc, tShIn, tShOut, tEsPct, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
            bool isHostileNpc = (_target.npcFaction == NpcFaction::Hostile);
            bool isNeutralNpc = (_target.npcFaction == NpcFaction::Neutral);
            Color tgtRing = isHostileNpc ? Color{ 120,30,30,180 }
                : isNeutralNpc ? Color{ 100,90,20,180 } : Color{ 30,80,30,180 };
            DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, tgtRing);

            // Percentage readouts stacked below the ring — the target cluster sits
            // close to the screen edge, so flanking labels (as on the player ring)
            // would clip; a centered stack always has room to grow.
            if (hasSensors || _target.isWingman) {
                char pctBuf[8];
                std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", tHpPct * 100.0f);
                Vector2 hpTs = MeasureTextEx(_hudFontVal, pctBuf, 14.0f, 1.0f);
                DrawTextEx(_hudFontVal, pctBuf, { tc.x - hpTs.x / 2.0f, tc.y + tShOut + 6.0f }, 14.0f, 1.0f, tHpCol);
            }
            if (_target.isWingman && (_target.maxKineticShield > 0.0f || _target.maxEnergyShield > 0.0f)) {
                char shBuf[24];
                std::snprintf(shBuf, sizeof(shBuf), "%.0f%% / %.0f%%", tKsPct * 100.0f, tEsPct * 100.0f);
                Vector2 shTs = MeasureTextEx(_hudFontVal, shBuf, 12.0f, 1.0f);
                DrawTextEx(_hudFontVal, shBuf, { tc.x - shTs.x / 2.0f, tc.y + tShOut + 23.0f }, 12.0f, 1.0f, HudLabel);
            }
            Texture2D* npcTexPtr = nullptr;
            for (size_t ni = 0; ni < _w->npcMeta.size(); ++ni) {
                if (_w->npcMeta[ni].id == _npcTargetId && _w->npcMeta[ni].alive) {
                    if (_w->npcMeta[ni].shipTypeId == "gargos")
                        npcTexPtr = const_cast<Texture2D*>(&_gargosTex);
                    else if (_w->entities[ni].sprite.texture && _w->entities[ni].sprite.texture->id > 0)
                        npcTexPtr = _w->entities[ni].sprite.texture;
                    else if (const auto* sd = ecs::ShipRegistry::ShipById(_w->npcMeta[ni].shipTypeId))
                        npcTexPtr = ResourceManager::Load(sd->assetPath);
                    break;
                }
            }
            const Texture2D& npcTex = npcTexPtr ? *npcTexPtr : Texture2D{};
            if (npcTex.id > 0) {
                float tw = (float)npcTex.width, th = (float)npcTex.height;
                float sc2 = (tAreaR * 1.8f) / std::max(tw, th);
                Color tint = isHostileNpc ? Color{ 255,160,160,255 }
                    : isNeutralNpc ? Color{ 255,240,160,255 } : Color{ 160,255,190,255 };
                Rectangle isrc = { 0, 0, tw, th };
                Rectangle idst = { tc.x, tc.y, tw * sc2, th * sc2 };
                DrawTexturePro(npcTex, isrc, idst, { tw * sc2 * 0.5f, th * sc2 * 0.5f }, 0.0f, tint);
            }
            if (hasSensors || _target.isWingman) {
                float dx = tc.x + tShOut + 12, dy = (float)(hy + 12);
                DrawTextEx(_hudFontUi, _target.name.c_str(), { dx, dy }, 13.0f, 1.0f, HudValue); dy += 18;
                DrawTextEx(_hudFontVal, _target.typeDesc.c_str(), { dx, dy }, 12.0f, 1.0f, HudLabel); dy += 16;
                char db[64];
                std::snprintf(db, sizeof(db), "DIST  %.0f u", _target.distance);
                DrawTextEx(_hudFontVal, db, { dx, dy }, 12.0f, 1.0f, HudValue); dy += 16;
                std::snprintf(db, sizeof(db), "HP  %.0f / %.0f", _target.health, _target.maxHealth);
                DrawTextEx(_hudFontVal, db, { dx, dy }, 12.0f, 1.0f, HudValue); dy += 16;
                if (hasSensors && _target.hasFaction) {
                    char fb[48];
                    std::snprintf(fb, sizeof(fb), "FACTION  %s", FactionName(_target.gameFaction));
                    DrawTextEx(_hudFontVal, fb, { dx, dy }, 12.0f, 1.0f, Color{ 180, 210, 255, 255 }); dy += 16;
                    Relation stand = ReputationRegistry::PlayerRelation(_target.gameFaction);
                    char sb[48];
                    std::snprintf(sb, sizeof(sb), "STANDING  %s", PlayerRelationLabel(stand));
                    DrawTextEx(_hudFontVal, sb, { dx, dy }, 12.0f, 1.0f, Color{ 180, 210, 255, 255 }); dy += 16;
                }
                if (hasSensors && _target.role != NpcRole::None) {
                    char rb[48];
                    std::snprintf(rb, sizeof(rb), "ROLE  %s", NpcRoleName(_target.role));
                    DrawTextEx(_hudFontVal, rb, { dx, dy }, 12.0f, 1.0f, Color{ 180, 210, 255, 255 }); dy += 16;
                }
                if (hasSensors && _target.disabled) {
                    DrawTextEx(_hudFontVal, "STATUS  DISABLED - APPROACH TO CAPTURE", { dx, dy }, 12.0f, 1.0f, Color{ 255, 200, 60, 255 }); dy += 16;
                }
                if (_target.isWingman && _target.maxKineticShield > 0.0f) {
                    std::snprintf(db, sizeof(db), "KS  %.0f / %.0f",
                        _target.kineticShield, _target.maxKineticShield);
                    DrawTextEx(_hudFontVal, db, { dx, dy }, 12.0f, 1.0f, Color{ 255,210,60,255 }); dy += 16;
                }
                if (_target.isWingman && _target.maxEnergyShield > 0.0f) {
                    std::snprintf(db, sizeof(db), "ES  %.0f / %.0f",
                        _target.energyShield, _target.maxEnergyShield);
                    DrawTextEx(_hudFontVal, db, { dx, dy }, 12.0f, 1.0f, Color{ 60,180,220,255 });
                }
            }
        }
        else if (_target.isStellar) {
            // Stations get a faction-relation tint (same rule as NPC ships);
            // planets have no faction data so they keep the original blue look.
            bool isHostileStn = _target.hasFaction && (_target.npcFaction == NpcFaction::Hostile);
            bool isNeutralStn = _target.hasFaction && (_target.npcFaction == NpcFaction::Neutral);
            Color stnRing = !_target.hasFaction ? Color{ 30,50,100,180 }
                : isHostileStn ? Color{ 120,30,30,180 }
                : isNeutralStn ? Color{ 100,90,20,180 } : Color{ 30,80,30,180 };
            Color stnTint = !_target.hasFaction ? WHITE
                : isHostileStn ? Color{ 255,160,160,255 }
                : isNeutralStn ? Color{ 255,240,160,255 } : Color{ 160,255,190,255 };

            DrawStatusRing(tc, tHpIn, tHpOut, 0.0f, Color{ 48,88,188,255 }, Color{ 22,22,32,200 });
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
            DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, stnRing);
            if (_target.iconTex && _target.iconTex->id > 0) {
                float tw = (float)_target.iconTex->width, th = (float)_target.iconTex->height;
                float sc2 = (tAreaR * 1.85f) / std::max(tw, th);
                DrawTexturePro(*_target.iconTex,
                    { 0, 0, tw, th }, { tc.x, tc.y, tw * sc2, th * sc2 },
                    { tw * sc2 * 0.5f, th * sc2 * 0.5f }, 0.0f, stnTint);
            }
            if (hasSensors) {
                float dx = tc.x + tShOut + 12, dy = (float)(hy + 12);
                DrawTextEx(_hudFontUi, _target.name.c_str(), { dx, dy }, 13.0f, 1.0f, HudValue); dy += 18;
                DrawTextEx(_hudFontVal, _target.typeDesc.c_str(), { dx, dy }, 12.0f, 1.0f, HudLabel); dy += 16;
                if (_target.hasFaction) {
                    char fb[48];
                    std::snprintf(fb, sizeof(fb), "FACTION  %s", FactionName(_target.gameFaction));
                    DrawTextEx(_hudFontVal, fb, { dx, dy }, 12.0f, 1.0f, Color{ 180, 210, 255, 255 }); dy += 16;
                    Relation stand = ReputationRegistry::PlayerRelation(_target.gameFaction);
                    char sb[48];
                    std::snprintf(sb, sizeof(sb), "STANDING  %s", PlayerRelationLabel(stand));
                    DrawTextEx(_hudFontVal, sb, { dx, dy }, 12.0f, 1.0f, Color{ 180, 210, 255, 255 }); dy += 16;
                }
                if (_target.disabled) {
                    DrawTextEx(_hudFontVal, "STATUS  DISABLED - APPROACH TO CAPTURE", { dx, dy }, 12.0f, 1.0f, Color{ 255, 200, 60, 255 });
                }
            }
        }
        else {
            float tHpPct = _target.health / _target.maxHealth;
            Color tHpCol = tHpPct > 0.5f ? HudGood
                : tHpPct > 0.25f ? HudCaution
                : HudCritical;
            DrawStatusRing(tc, tHpIn, tHpOut, hasSensors ? tHpPct : 0.0f, tHpCol, Color{ 22,22,32,200 });
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
            DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, Color{ 30,30,55,180 });
            if (_target.iconTex && _target.iconTex->id > 0) {
                float tw = (float)_target.iconTex->width, th = (float)_target.iconTex->height;
                float sc2 = (tAreaR * 1.3f) / std::max(tw, th);
                DrawTexturePro(*_target.iconTex,
                    { 0, 0, tw, th }, { tc.x, tc.y, tw * sc2, th * sc2 },
                    { tw * sc2 * 0.5f, th * sc2 * 0.5f }, 0.0f, WHITE);
            } else {
                int   sides = _target.tier == 2 ? 8 : _target.tier == 1 ? 7 : 6;
                float spin = (float)(GetTime() * 22.0);
                DrawPoly(tc, sides, tAreaR * 0.65f, spin, Color{ 30,26,21,255 });
                DrawPolyLinesEx(tc, sides, tAreaR * 0.65f, spin, 1.0f, Color{ 130,115,90,255 });
            }
            if (hasSensors) {
                char pctBuf[8];
                std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", tHpPct * 100.0f);
                Vector2 hpTs = MeasureTextEx(_hudFontVal, pctBuf, 14.0f, 1.0f);
                DrawTextEx(_hudFontVal, pctBuf, { tc.x - hpTs.x / 2.0f, tc.y + tShOut + 6.0f }, 14.0f, 1.0f, tHpCol);
            }
            if (hasSensors) {
                float dx = tc.x + tShOut + 12, dy = (float)(hy + 12);
                DrawTextEx(_hudFontUi, _target.name.c_str(), { dx, dy }, 13.0f, 1.0f, HudValue); dy += 18;
                DrawTextEx(_hudFontVal, _target.typeDesc.c_str(), { dx, dy }, 12.0f, 1.0f, HudLabel); dy += 16;
                char db[64];
                std::snprintf(db, sizeof(db), "DIST  %.0f u", _target.distance);
                DrawTextEx(_hudFontVal, db, { dx, dy }, 12.0f, 1.0f, HudValue); dy += 16;
                std::snprintf(db, sizeof(db), "HP  %.0f / %.0f", _target.health, _target.maxHealth);
                DrawTextEx(_hudFontVal, db, { dx, dy }, 12.0f, 1.0f, HudValue); dy += 16;
                for (const auto& mc : _target.materialComps) {
                    const MatDef* mat = FindMaterial(mc.materialId);
                    char mb[48];
                    std::snprintf(mb, sizeof(mb), "%-10s %d%%",
                        mat ? mat->displayName : mc.materialId.c_str(), mc.percent);
                    DrawTextEx(_hudFontVal, mb, { dx, dy }, 11.0f, 1.0f, mat ? mat->hudColor : HudValue);
                    dy += 14;
                }
            }
        }
    }
    else {
        DrawStatusRing(tc, tHpIn, tHpOut, 0.0f, HudGood, Color{ 22,22,32,200 });
        DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
        DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
        DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, Color{ 30,30,55,110 });
        Vector2 noTs = MeasureTextEx(_hudFontUi, "NO", 10.0f, 1.0f);
        Vector2 tgtTs = MeasureTextEx(_hudFontUi, "TARGET", 10.0f, 1.0f);
        DrawTextEx(_hudFontUi, "NO", { tc.x - noTs.x / 2.0f, tc.y - 10.0f }, 10.0f, 1.0f, Color{ 80,80,100,200 });
        DrawTextEx(_hudFontUi, "TARGET", { tc.x - tgtTs.x / 2.0f, tc.y + 2.0f }, 10.0f, 1.0f, Color{ 80,80,100,200 });
    }

    bool showTargetData = hasSensors || (_target.valid && _target.isNpc && _target.isWingman);
    int weapX = (int)(tc.x + tShOut) + 10 + (showTargetData ? 168 : 12);
    int wy = hy + 10;

    DrawTextEx(_hudFontUi, "WEAPON", { (float)weapX, (float)wy }, 11.0f, 1.0f, HudLabel); wy += 14;
    if (_playerMeta.canFire) {
        int barW = std::min(lDiv - weapX - 10, 140);
        if (_playerMeta.weaponFireMode == WeaponFireMode::Charge) {
            float chargePct = _playerMeta.ChargePct();
            bool  full = chargePct >= 1.0f;
            Color cFill = full ? HudGood : Color{ 60,150,220,255 };
            DrawRectangle(weapX, wy, barW, 10, Color{ 20,28,20,200 });
            DrawRectangle(weapX, wy, (int)(barW * chargePct), 10, cFill);
            DrawRectangleLinesEx({ (float)weapX,(float)wy,(float)barW,10.0f }, 1.0f, HudDiv);
            wy += 12;
            const char* cl = full ? "FULL CHARGE" : chargePct > 0.01f ? "CHARGING" : "HOLD TO CHARGE";
            DrawTextEx(_hudFontVal, cl, { (float)weapX, (float)wy }, 11.0f, 1.0f,
                full ? HudGood : Color{ 100,175,220,255 });
            wy += 16;
        }
        else if (_playerMeta.weaponFireMode == WeaponFireMode::LockOn) {
            bool locked = (_lockTargetId != 0);
            DrawTextEx(_hudFontVal, locked ? "TARGET LOCKED" : "CLICK TO LOCK",
                { (float)weapX, (float)wy + 2.0f }, 11.0f, 1.0f,
                locked ? HudCritical : Color{ 120,120,140,220 });
            wy += 16;
            float readyPct = 1.0f - _playerMeta.FireCooldownPct();
            DrawRectangle(weapX, wy, barW, 6, Color{ 20,28,20,200 });
            DrawRectangle(weapX, wy, (int)(barW * readyPct), 6,
                readyPct >= 1.0f ? HudGood : HudCaution);
            DrawRectangleLinesEx({ (float)weapX,(float)wy,(float)barW,6.0f }, 1.0f, HudDiv);
            wy += 10;
        }
        else {
            float readyPct = 1.0f - _playerMeta.FireCooldownPct();
            bool  isReady = readyPct >= 1.0f;
            Color wFill = isReady ? HudGood : HudCaution;
            if (isReady) {
                float pulse = 0.6f + 0.4f * sinf((float)GetTime() * 5.0f);
                wFill.a = (unsigned char)(170 + 85 * pulse);
            }
            DrawRectangle(weapX, wy, barW, 10, Color{ 20,28,20,200 });
            DrawRectangle(weapX, wy, (int)(barW * readyPct), 10, wFill);
            DrawRectangleLinesEx({ (float)weapX,(float)wy,(float)barW,10.0f }, 1.0f, HudDiv);
            wy += 12;
            DrawTextEx(_hudFontVal, isReady ? "READY" : "CHARGING", { (float)weapX, (float)wy }, 11.0f, 1.0f,
                isReady ? HudGood : HudCaution);
            wy += 16;
        }
    }
    else {
        DrawTextEx(_hudFontVal, "NO WEAPON", { (float)weapX, (float)wy + 2.0f }, 11.0f, 1.0f, HudCritical); wy += 28;
    }

    DrawTextEx(_hudFontUi, "SLOTS", { (float)weapX, (float)wy }, 10.0f, 1.0f, HudLabel); wy += 12;
    {
        auto weaponSlots = _loadout.WeaponSlots();
        for (int i = 0; i < _playerMeta.weaponSlots; ++i) {
            bool hasWeapon = (i < (int)weaponSlots.size() && weaponSlots[i]->equipped);
            // An armed slot (equipped + toggled on via its number key) fires
            // with the group; a disabled or empty slot is dimmed.
            bool isOn = hasWeapon && IsWeaponEnabled(i);
            Rectangle slot = { (float)(weapX + i * 38), (float)wy, 32.0f, 26.0f };
            DrawHudChamferRect(slot, 5.0f,
                isOn ? Color{ 20,38,20,230 } : Color{ 14,22,14,200 },
                isOn ? HudGood : HudDiv,
                isOn ? 2.0f : 1.0f);
            char kh[2] = { (char)('1' + i), 0 };
            if (i == 9) kh[0] = '0';
            DrawText(kh, (int)slot.x + 3, (int)slot.y + 3, 7, Color{ 60,100,60,175 });
            if (hasWeapon) {
                const ModuleDef& wm = *weaponSlots[i]->equipped;
                const char* abbr = (wm.weapon.fireMode == WeaponFireMode::LockOn) ? "MIS"
                    : (wm.weapon.fireMode == WeaponFireMode::Charge) ? "CHG"
                    : (wm.weapon.damageType == DamageType::Energy) ? "ENR"
                    : (wm.weapon.effect == WeaponEffect::EMP) ? "EMP"
                    : (wm.weapon.effect == WeaponEffect::Ion) ? "ION"
                    : "KIN";
                DrawText(abbr, (int)(slot.x + 5), (int)(slot.y + 13), 9,
                    isOn ? Color{ 100,210,100,220 } : Color{ 70,90,70,180 });
            }
            else {
                DrawText("--", (int)(slot.x + 9), (int)(slot.y + 7), 10, Color{ 50,80,50,175 });
            }
        }
    }

    Rectangle enterBtn, buildBtn, commsBtn, seatBtn;
    ComputeHudButtons(sw, sh, enterBtn, buildBtn, commsBtn, seatBtn);
    bool nearStation = IsNearEnterableStation();
    bool nearPlanet  = nearStation || IsNearPlanet();
    bool hovEnter    = nearPlanet && CheckCollisionPointRec(mouse, enterBtn);

    float breathe = 0.6f + 0.4f * sinf((float)GetTime() * 3.0f);

    // ENTER — dock-arrow icon, label below (contextual, breathes when actionable)
    {
        Color enterBg = nearPlanet ? (hovEnter ? Color{ 30,70,90,230 } : Color{ 12,25,35,200 })
            : Color{ 16,16,16,150 };
        Color enterBdr = nearPlanet ? Color{ 60,160,220,200 } : HudDiv;
        if (nearPlanet && !hovEnter) enterBdr.a = (unsigned char)(140 + 100 * breathe);
        Color enterFg = nearPlanet ? (hovEnter ? WHITE : Color{ 60,160,220,255 })
            : Color{ 50,55,60,160 };
        DrawHudChamferRect(enterBtn, 6.0f, enterBg, enterBdr, 1.5f);
        DrawHudDockIcon({ enterBtn.x + enterBtn.width / 2.0f, enterBtn.y + enterBtn.height * 0.38f }, 16.0f, enterFg);
        const char* enterLbl = "ENTER";
        Vector2 enterTs = MeasureTextEx(_hudFontUi, enterLbl, 9.0f, 1.0f);
        DrawTextEx(_hudFontUi, enterLbl,
            { enterBtn.x + (enterBtn.width - enterTs.x) / 2.0f, enterBtn.y + enterBtn.height - enterTs.y - 3.0f },
            9.0f, 1.0f, enterFg);
    }

    // BUILD — hammer icon, label below
    {
        bool hovBuild = CheckCollisionPointRec(mouse, buildBtn);
        Color bg = hovBuild ? Color{ 20,50,110,230 } : Color{ 10,22,50,200 };
        Color bdr = Color{ 40,100,200,200 };
        Color fg = hovBuild ? WHITE : Color{ 80,150,230,255 };
        DrawHudChamferRect(buildBtn, 6.0f, bg, bdr, 1.5f);
        DrawHudHammerIcon({ buildBtn.x + buildBtn.width / 2.0f, buildBtn.y + buildBtn.height * 0.38f }, 16.0f, fg);
        const char* buildLbl = "BUILD";
        Vector2 buildTs = MeasureTextEx(_hudFontUi, buildLbl, 9.0f, 1.0f);
        DrawTextEx(_hudFontUi, buildLbl,
            { buildBtn.x + (buildBtn.width - buildTs.x) / 2.0f, buildBtn.y + buildBtn.height - buildTs.y - 3.0f },
            9.0f, 1.0f, fg);
    }

    // COMMS — radar dish icon, label below. Epic 13: also actionable with a
    // station targeted (hail for its contract board without needing to dock).
    {
        bool commsAvailable = (_npcTargetId != 0) ||
            (_target.valid && _target.isStellar && _target.hasFaction);
        bool hovComms = commsAvailable && CheckCollisionPointRec(mouse, commsBtn);
        Color bg = commsAvailable ? (hovComms ? Color{ 20,60,80,230 } : Color{ 10,28,40,200 })
            : Color{ 16,16,16,150 };
        Color bdr = commsAvailable ? Color{ 30,100,160,200 } : HudDiv;
        Color fg = commsAvailable ? (hovComms ? WHITE : Color{ 50,140,190,255 })
            : Color{ 45,50,55,150 };
        DrawHudChamferRect(commsBtn, 6.0f, bg, bdr, 1.5f);
        DrawHudRadarIcon({ commsBtn.x + commsBtn.width / 2.0f, commsBtn.y + commsBtn.height * 0.38f }, 16.0f, fg);
        const char* commsLbl = "COMMS";
        Vector2 commsTs = MeasureTextEx(_hudFontUi, commsLbl, 9.0f, 1.0f);
        DrawTextEx(_hudFontUi, commsLbl,
            { commsBtn.x + (commsBtn.width - commsTs.x) / 2.0f, commsBtn.y + commsBtn.height - commsTs.y - 3.0f },
            9.0f, 1.0f, fg);
    }

    // SEAT — turret-crosshair icon (Epic 8). Always actionable while seated
    // (doubles as EXIT); otherwise only when a friendly capital hardpoint is
    // in range.
    {
        unsigned int seatNpcId; int seatHpIdx; Vector2 seatPos;
        // Epic 8 is host/singleplayer-only for now: only the host resolves
        // hit detection (UpdateCollisions/UpdateNpcCollisions are skipped
        // entirely for clients), so a client's local turret shots would
        // fly but never damage anything — gate seating out for clients
        // rather than ship a control that silently does nothing.
        bool seatAvailable = !net::Game().IsClient() && (_seated || FindNearestFriendlySeat(seatNpcId, seatHpIdx, seatPos));
        bool hovSeat = seatAvailable && CheckCollisionPointRec(mouse, seatBtn);
        Color bg = seatAvailable ? (hovSeat ? Color{ 70,40,20,230 } : Color{ 35,20,10,200 })
            : Color{ 16,16,16,150 };
        Color bdr = seatAvailable ? Color{ 200,120,50,200 } : HudDiv;
        Color fg = seatAvailable ? (hovSeat ? WHITE : Color{ 220,150,80,255 })
            : Color{ 45,50,55,150 };
        DrawHudChamferRect(seatBtn, 6.0f, bg, bdr, 1.5f);
        DrawHudTurretIcon({ seatBtn.x + seatBtn.width / 2.0f, seatBtn.y + seatBtn.height * 0.38f }, 16.0f, fg);
        const char* seatLbl = _seated ? "EXIT SEAT" : "MAN TURRET";
        Vector2 seatTs = MeasureTextEx(_hudFontUi, seatLbl, 9.0f, 1.0f);
        DrawTextEx(_hudFontUi, seatLbl,
            { seatBtn.x + (seatBtn.width - seatTs.x) / 2.0f, seatBtn.y + seatBtn.height - seatTs.y - 3.0f },
            9.0f, 1.0f, fg);
    }

    (void)rDiv;

    Vector2 escMapTs = MeasureTextEx(_hudFontVal, "[ESC] MAP", 11.0f, 1.0f);
    DrawTextEx(_hudFontVal, "[ESC] MAP", { (float)(hx + hw) - escMapTs.x - 8.0f, (float)(hy + HudH - 15) },
        11.0f, 1.0f, HudLabel);

    // Epic 12.1: received-comms panel toggle, always available once there's a
    // log to read.
    Vector2 logHintTs = MeasureTextEx(_hudFontVal, "[L] LOG", 11.0f, 1.0f);
    DrawTextEx(_hudFontVal, "[L] LOG", { (float)(hx + hw) - logHintTs.x - 8.0f, (float)(hy + HudH - 30) },
        11.0f, 1.0f, HudLabel);

    // Epic 12.2: persistent skip control, visible for the whole tutorial, not
    // just a one-time start prompt.
    if (_tutorialActive) {
        Vector2 skipTs = MeasureTextEx(_hudFontVal, "[T] SKIP TUTORIAL", 11.0f, 1.0f);
        DrawTextEx(_hudFontVal, "[T] SKIP TUTORIAL", { (float)(hx + hw) - skipTs.x - 8.0f, (float)(hy + HudH - 45) },
            11.0f, 1.0f, HudCaution);
    }

    bool menuOpen = (_storageMenu.isOpen || _modulesMenu.isOpen || _galaxyMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _commsLogOpen || _enterPopupOpen || _stationServicesMenu.isOpen || _localMapOpen ||
        _buildMenu.isOpen || _stationModMenu.isOpen || _miningMenu.isOpen || _placementConfirmOpen);
    if (!menuOpen && mouse.y < hy) {
        int cs = 8;
        DrawLine((int)mouse.x - cs, (int)mouse.y, (int)mouse.x + cs, (int)mouse.y, Color{ 255,255,255,155 });
        DrawLine((int)mouse.x, (int)mouse.y - cs, (int)mouse.x, (int)mouse.y + cs, Color{ 255,255,255,155 });
        DrawCircleLines((int)mouse.x, (int)mouse.y, 5, Color{ 255,255,255,95 });
    }
}

void SpaceFlight::DrawLocalMap() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 1, 3, 1, 255 });

    const float mapR = std::min((float)sw, (float)sh) * 0.38f;
    const Vector2 mc = { (float)(sw / 2), (float)(sh / 2 + 14) };
    const float mapRange = 2500.0f;
    const float scale = mapR / mapRange;

    auto W2M = [&](Vector2 wp) -> Vector2 {
        Vector2 off = Vector2Subtract(wp, _playerEntity.transform.position);
        return { mc.x + off.x * scale, mc.y + off.y * scale };
        };
    auto Rot2D = [](Vector2 v, float deg) -> Vector2 {
        float r = deg * DEG2RAD;
        return { v.x * cosf(r) - v.y * sinf(r), v.x * sinf(r) + v.y * cosf(r) };
        };

    const float ringDists[] = { 500.f, 1000.f, 1500.f, 2000.f, 2500.f };
    for (float rd : ringDists) {
        float sr = rd * scale;
        if (sr > mapR + 1.0f) break;
        DrawCircleLines((int)mc.x, (int)mc.y, sr, Color{ 18,50,18,130 });
        char lbl[32]; std::snprintf(lbl, sizeof(lbl), "%.0f", rd);
        DrawText(lbl, (int)(mc.x + sr + 4), (int)mc.y - 7, 11, Color{ 35,90,35,160 });
    }
    DrawLineEx({ mc.x - mapR, mc.y }, { mc.x + mapR, mc.y }, 1.0f, Color{ 22,55,22,100 });
    DrawLineEx({ mc.x, mc.y - mapR }, { mc.x, mc.y + mapR }, 1.0f, Color{ 22,55,22,100 });

    DrawCircleV(mc, mapR, Color{ 0, 5, 0, 55 });

    // Sun on local map
    if (_w->sun.active) {
        Vector2 sunMapPos = W2M({ 0.0f, 0.0f });
        float   sunMapR   = std::max(6.0f, _w->sun.radius * scale);
        float   gravMapR  = _w->sun.gravRange * scale;
        const Color& sc = _w->sun.coreColor;
        // Gravity zone ring
        if (gravMapR < mapR + 1.0f)
            DrawCircleLines((int)sunMapPos.x, (int)sunMapPos.y, gravMapR,
                { sc.r, sc.g, sc.b, 35 });
        // Glow layers
        DrawCircleV(sunMapPos, sunMapR * 2.8f, { sc.r, sc.g, sc.b, 18 });
        DrawCircleV(sunMapPos, sunMapR * 1.8f, { sc.r, sc.g, sc.b, 40 });
        DrawCircleV(sunMapPos, sunMapR,         sc);
        DrawCircleV(sunMapPos, sunMapR * 0.45f, { 255, 255, 255, 220 });
        // Label if sun is within map bounds
        if (sunMapPos.x > 0 && sunMapPos.x < sw && sunMapPos.y > 0 && sunMapPos.y < sh) {
            const char* slbl = _w->sun.typeId.c_str();
            DrawText(slbl, (int)(sunMapPos.x - MeasureText(slbl, 10) / 2),
                (int)(sunMapPos.y + sunMapR + 3), 10, { sc.r, sc.g, sc.b, 200 });
        }
    }

    for (const Asteroid& a : _w->asteroids) {
        if (!a.alive) continue;
        Vector2 mapPos = W2M(a.position);
        float   distPx = Vector2Distance(mapPos, mc);

        if (distPx > mapR) {
            Vector2 dir = Vector2Normalize(Vector2Subtract(mapPos, mc));
            Vector2 edgePt = Vector2Add(mc, Vector2Scale(dir, mapR - 5.0f));
            Color   ec = a.tier == 2 ? Color{ 255,200,60,210 }
                : a.tier == 1 ? Color{ 80,200,255,210 }
            : Color{ 200,200,200,190 };
            DrawCircleV(edgePt, 4.0f, ec);
            continue;
        }
        int   sides = a.tier == 2 ? 8 : a.tier == 1 ? 7 : 6;
        float drawR = std::max(5.0f, a.radius * scale);
        Color fill = a.tier == 2 ? Color{ 30,26,21,255 }
            : a.tier == 1 ? Color{ 24,20,16,255 }
        : Color{ 18,15,12,255 };
        Color outline = a.tier == 2 ? Color{ 150,130,100,240 }
            : a.tier == 1 ? Color{ 125,110, 90,240 }
        : Color{ 105, 95, 80,240 };
        DrawPoly(mapPos, sides, drawR, a.rotation, fill);
        DrawPolyLinesEx(mapPos, sides, drawR, a.rotation, 1.5f, outline);
    }

    const float ssz = 10.0f;
    Vector2 stip = Vector2Add(mc, Rot2D({ 0.0f,       -ssz }, _playerEntity.transform.rotation));
    Vector2 slft = Vector2Add(mc, Rot2D({ -ssz * 0.6f,  ssz * 0.55f }, _playerEntity.transform.rotation));
    Vector2 srgt = Vector2Add(mc, Rot2D({ ssz * 0.6f,  ssz * 0.55f }, _playerEntity.transform.rotation));
    DrawTriangle(stip, srgt, slft, Color{ 60,140,230,255 });
    DrawTriangleLines(stip, srgt, slft, Color{ 140,200,255,255 });
    float spd = Vector2Length(_playerEntity.transform.velocity);
    if (spd > 5.0f) {
        Vector2 velEnd = Vector2Add(mc, Vector2Scale(_playerEntity.transform.velocity, scale * 2.5f));
        float   vDist = Vector2Distance(velEnd, mc);
        if (vDist > mapR - 4.0f)
            velEnd = Vector2Add(mc, Vector2Scale(
                Vector2Normalize(Vector2Subtract(velEnd, mc)), mapR - 4.0f));
        DrawLineEx(mc, velEnd, 1.5f, Color{ 80,150,255,200 });
    }

    DrawCircleLines((int)mc.x, (int)mc.y, mapR, Color{ 40,160,40,210 });
    DrawCircleLines((int)mc.x, (int)mc.y, mapR + 1.5f, Color{ 20, 80,20,120 });

    const char* title = "LOCAL SPACE";
    DrawText(title, (sw - MeasureText(title, 26)) / 2, 14, 26, Color{ 68,162,68,255 });
    DrawRectangle((sw - 400) / 2, 50, 400, 1, Color{ 34,98,34,170 });

    char infoBuf[64];
    std::snprintf(infoBuf, sizeof(infoBuf), "X %.0f  Y %.0f",
        _playerEntity.transform.position.x, _playerEntity.transform.position.y);
    DrawText(infoBuf, sw - MeasureText(infoBuf, 13) - 18, 18, 13, HudValue);
    std::snprintf(infoBuf, sizeof(infoBuf), "SPD %.0f u/s", spd);
    DrawText(infoBuf, sw - MeasureText(infoBuf, 13) - 18, 36, 13, HudValue);

    Vector2 mouse = GetMousePosition();
    if (Vector2Distance(mouse, mc) < mapR) {
        char coordBuf[64];
        std::snprintf(coordBuf, sizeof(coordBuf), "X %.0f  Y %.0f",
            _playerEntity.transform.position.x + (mouse.x - mc.x) / scale,
            _playerEntity.transform.position.y + (mouse.y - mc.y) / scale);
        int tw = MeasureText(coordBuf, 13);
        DrawRectangle((int)mouse.x + 12, (int)mouse.y - 14, tw + 10, 20, Color{ 0,8,0,200 });
        DrawText(coordBuf, (int)mouse.x + 17, (int)mouse.y - 10, 13, Color{ 80,200,80,220 });
    }

    int lx = sw - 190, ly = sh - 96;
    DrawText("LEGEND", lx, ly, 13, HudLabel); ly += 18;
    DrawCircleV({ (float)(lx + 7), (float)(ly + 6) }, 5.0f, Color{ 255,200,60,220 });
    DrawText("Large asteroid", lx + 18, ly, 12, HudValue); ly += 16;
    DrawCircleV({ (float)(lx + 7), (float)(ly + 6) }, 3.5f, Color{ 80,200,255,220 });
    DrawText("Medium asteroid", lx + 18, ly, 12, HudValue); ly += 16;
    DrawCircleV({ (float)(lx + 7), (float)(ly + 6) }, 2.5f, Color{ 200,200,200,200 });
    DrawText("Small asteroid", lx + 18, ly, 12, HudValue);

    Rectangle backBtn = { 18.0f, 16.0f, 120.0f, 38.0f };
    bool hovBack = CheckCollisionPointRec(mouse, backBtn);
    DrawRectangleRec(backBtn, hovBack ? Color{ 50,95,50,230 } : Color{ 12,28,12,220 });
    DrawRectangleLinesEx(backBtn, 1.0f, Color{ 40,160,40,200 });
    const char* backLbl = "< BACK";
    DrawText(backLbl,
        (int)(backBtn.x + (backBtn.width - MeasureText(backLbl, 16)) / 2.0f),
        (int)(backBtn.y + (backBtn.height - 16) / 2.0f),
        16, WHITE);
}

void SpaceFlight::ApplyLoadout() {
    const ecs::ShipDef* defPtr = ecs::ShipRegistry::ShipById(_playerMeta.defId);
    if (!defPtr) return;
    const ecs::ShipDef& def = *defPtr;
    _playerShipTex = def.designArray.empty()
        ? ResourceManager::Load(def.assetPath)
        : SpriteCache::Bake(def.designArray, SKYBLUE, WHITE);

    float prevKinetic = _playerEntity.health.currentShield;
    float prevEnergy  = _playerMeta.energyShield;

    _playerEntity.health.maxStats.hull = def.baseStats.hull;
    _playerMeta.thrust    = 0.0f;
    _playerMeta.turnSpeed = 0.0f;
    _playerMeta.fireRate  = 0.0f;
    _playerMeta.projSpeed = 0.0f;
    _playerMeta.projRange = 0.0f;
    _playerEntity.health.maxStats.shield = 0.0f;
    _playerMeta.maxEnergyShield      = 0.0f;
    _playerMeta.kineticRechargeRate  = 0.0f;
    _playerMeta.energyRechargeRate   = 0.0f;
    _playerMeta.canMove       = false;
    _playerMeta.canFire       = false;
    _playerMeta.hasTurret     = false;
    _playerMeta.weaponFireMode= WeaponFireMode::Standard;
    _playerMeta.weaponDamage  = 10.0f;
    _playerMeta.chargeTime    = 1.0f;
    _playerMeta.burstCount    = 1;
    _playerMeta.spreadAngle   = 0.0f;
    _playerMeta.weaponTurnRate= 0.0f;
    _playerMeta.weaponEffect         = WeaponEffect::None;
    _playerMeta.weaponEffectDuration = 0.0f;

    if (auto* armorSlot = _loadout.Armor(); armorSlot && armorSlot->equipped)
        _playerEntity.health.maxStats.hull += armorSlot->equipped->armor.hullBonus;
    _playerEntity.health.currentHull = std::min(_playerEntity.health.currentHull,
                                                 _playerEntity.health.maxStats.hull);

    for (const auto* shSlot : _loadout.ShieldSlots()) {
        const auto& sh = shSlot->equipped;
        if (!sh) continue;
        if (sh->shield.shieldType == ShieldType::Kinetic) {
            _playerEntity.health.maxStats.shield += sh->shield.capacity;
            _playerMeta.kineticRechargeRate      += sh->shield.rechargeRate;
        }
        else {
            _playerMeta.maxEnergyShield    += sh->shield.capacity;
            _playerMeta.energyRechargeRate += sh->shield.rechargeRate;
        }
    }
    _playerEntity.health.currentShield = std::min(prevKinetic, _playerEntity.health.maxStats.shield);
    _playerMeta.energyShield           = std::min(prevEnergy,  _playerMeta.maxEnergyShield);

    if (auto* engineSlot = _loadout.Engine(); engineSlot && engineSlot->equipped && !engineSlot->equipped->engine.isHyperdrive) {
        _playerMeta.thrust    += engineSlot->equipped->engine.thrustBonus;
        _playerMeta.turnSpeed += engineSlot->equipped->engine.turnSpeedBonus;
        _playerMeta.canMove    = true;
    }

    auto weaponSlots = _loadout.WeaponSlots();
    // Keep the per-slot firing arrays sized to the current weapon slots,
    // preserving any existing enable flags; slots default to enabled.
    _weaponEnabled.resize(weaponSlots.size(), true);
    _weaponCooldown.resize(weaponSlots.size(), 0.0f);
    _weaponCharge.resize(weaponSlots.size(), 0.0f);

    // Pick a representative weapon for the HUD's WEAPON readiness/charge panel:
    // the first enabled+equipped slot, or any equipped slot if none are enabled.
    // Actual firing iterates every enabled+equipped slot independently (Update).
    _primaryWeapon = -1;
    for (int i = 0; i < (int)weaponSlots.size(); ++i)
        if (weaponSlots[i]->equipped && IsWeaponEnabled(i)) { _primaryWeapon = i; break; }
    if (_primaryWeapon < 0)
        for (int i = 0; i < (int)weaponSlots.size(); ++i)
            if (weaponSlots[i]->equipped) { _primaryWeapon = i; break; }

    if (_primaryWeapon >= 0 && weaponSlots[_primaryWeapon]->equipped) {
        const WeaponStats& ws = weaponSlots[_primaryWeapon]->equipped->weapon;
        _playerMeta.fireRate       = ws.fireRate;
        _playerMeta.projSpeed      = ws.projSpeed;
        _playerMeta.projRange      = ws.projRange;
        _playerMeta.hasTurret      = ws.isTurret;
        _playerMeta.canFire        = true;
        _playerMeta.weaponFireMode = ws.fireMode;
        _playerMeta.weaponDamage   = ws.damage;
        _playerMeta.chargeTime     = ws.chargeTime;
        _playerMeta.burstCount     = ws.burstCount;
        _playerMeta.spreadAngle    = ws.spreadAngle;
        _playerMeta.weaponTurnRate = (ws.projType == WeaponProjType::Seeking) ? 3.0f : 0.0f;
        _playerMeta.weaponEffect         = ws.effect;
        _playerMeta.weaponEffectDuration = ws.effectDuration;
    }

    _hasSensors = false;
    for (const auto* axSlot : _loadout.AuxSlots()) {
        const auto& ax = axSlot->equipped;
        if (ax && ax->auxiliary.hasSensors) { _hasSensors = true; break; }
    }

    // Galaxy-map fog reveal radius — deliberately independent of _hasSensors/
    // combat sensorRange above (see AuxStats::mapSensorRange's comment). No
    // baseline: a ship with nothing equipped here sees nothing beyond its
    // home system, by design — see GalaxyMap's undiscovered-system gating.
    _mapSensorRange = 0.0f;
    _mapSensorTier  = 0;
    for (const auto* axSlot : _loadout.AuxSlots()) {
        const auto& ax = axSlot->equipped;
        if (ax && ax->auxiliary.mapSensorRange > _mapSensorRange) {
            _mapSensorRange = ax->auxiliary.mapSensorRange;
            _mapSensorTier  = (int)ax->grade + 1;
        }
    }

    _hyperdriveRange = 0.0f;
    if (auto* hyperSlot = _loadout.Hyperdrive(); hyperSlot && hyperSlot->equipped && hyperSlot->equipped->engine.isHyperdrive)
        _hyperdriveRange = hyperSlot->equipped->engine.hyperdriveRange;

    // P3: recompute power budget on every loadout edit, then apply the
    // overload penalty (unchanged from the pre-P3 constants — just finally
    // wired to something).
    _loadout.RecalculateLoad();
    if (_loadout.IsOverloaded()) {
        _playerMeta.thrust   *= HardpointRig::kOverloadThrustFactor;
        _playerMeta.fireRate *= HardpointRig::kOverloadCooldownMult;
    }

    // P8-T1: tell the host what's equipped now — only a client's own process
    // knows this; the host already knows its own player + every NPC's
    // loadout directly and needs no report. No-op host/offline (see
    // ClientSendFighterLoadoutReport).
    if (net::Game().IsClient())
        net::Game().ClientSendFighterLoadoutReport(EncodeLoadoutMounts(_loadout));
}

void SpaceFlight::ApplyNpcLoadout(ecs::Entity& entity, NpcMeta& meta) {
    float baseHull = 100.0f;
    if (const ecs::ShipDef* def = ecs::ShipRegistry::ShipById(meta.shipTypeId))
        baseHull = def->baseStats.hull;
    entity.health.maxStats.hull = baseHull;
    meta.npcThrust        = 0.0f;
    meta.npcDamage        = 0.0f;
    meta.npcFireRate      = 1.4f;
    meta.npcHasWeapon     = false;
    entity.health.maxStats.shield = 0.0f;
    meta.maxEnergyShield  = 0.0f;
    meta.kineticRechargeRate = 0.0f;
    meta.energyRechargeRate  = 0.0f;

    if (auto* armorSlot = meta.loadout.Armor(); armorSlot && armorSlot->equipped)
        entity.health.maxStats.hull += armorSlot->equipped->armor.hullBonus;
    entity.health.currentHull = std::min(entity.health.currentHull, entity.health.maxStats.hull);

    for (const auto* shSlot : meta.loadout.ShieldSlots()) {
        const auto& sh = shSlot->equipped;
        if (!sh) continue;
        if (sh->shield.shieldType == ShieldType::Kinetic) {
            entity.health.maxStats.shield += sh->shield.capacity;
            meta.kineticRechargeRate      += sh->shield.rechargeRate;
        }
        else {
            meta.maxEnergyShield   += sh->shield.capacity;
            meta.energyRechargeRate += sh->shield.rechargeRate;
        }
    }
    entity.health.currentShield = std::min(entity.health.currentShield, entity.health.maxStats.shield);
    meta.energyShield           = std::min(meta.energyShield, meta.maxEnergyShield);

    auto npcWeaponSlots = meta.loadout.WeaponSlots();
    if (!npcWeaponSlots.empty() && npcWeaponSlots[0]->equipped) {
        const WeaponStats& ws = npcWeaponSlots[0]->equipped->weapon;
        meta.npcHasWeapon   = true;
        meta.npcDamage      = ws.damage;
        meta.npcFireRate    = ws.fireRate;
        meta.npcWeaponMode  = ws.fireMode;
        meta.npcProjType    = ws.projType;
        meta.npcChargeTime  = ws.chargeTime;
        meta.npcBurstCount  = ws.burstCount > 0 ? ws.burstCount : 1;
        meta.npcSpreadAngle = ws.spreadAngle;
        meta.npcProjSpeed   = ws.projSpeed;
        meta.npcProjRange   = ws.projRange;
    }

    if (auto* engineSlot = meta.loadout.Engine(); engineSlot && engineSlot->equipped && !engineSlot->equipped->engine.isHyperdrive)
        meta.npcThrust = engineSlot->equipped->engine.thrustBonus;

    // Capital ships get a flat, slow, ship-defined speed/turn rate instead of
    // the fighter engine-module roll above — mass/momentum (thrust vs. total
    // module mass) is a deferred follow-up milestone for ALL moving craft.
    // TODO(mass-momentum): replace this flat override with thrust-vs-mass —
    // see project_capital_ships.md; applies to every ship, not just capitals.
    if (const ecs::ShipDef* def = ecs::ShipRegistry::ShipById(meta.shipTypeId);
        def && def->shipType == ShipType::Capital) {
        meta.npcThrust   = def->baseStats.thrust;
        meta.npcTurnRate = def->turnSpeed;
        // Hold at a much longer standoff range than fighters so it lets its
        // turrets do the work instead of closing to fighter brawling range.
        // (preferredRange is derived from attackRange by the caller right
        // after ApplyNpcLoadout returns — see SpawnNpcShips et al.)
        meta.attackRange = std::max(meta.attackRange, 900.0f);
    }

    // Role-based combat posture (Epic 2.4/2.5 — Military and Raider are the
    // first two officer roles to get distinct combat behavior; Trader/
    // Industrialist/Explorer keep the stock ranges since they aren't
    // combat-focused roles). See also the per-role Flee threshold in
    // UpdateNpcShips's Attack-state case.
    if (meta.role == NpcRole::Military) {
        meta.aggroRange *= 1.3f; // disciplined patrols spot and engage threats sooner
    } else if (meta.role == NpcRole::Raider) {
        meta.aggroRange  *= 1.15f;
        meta.attackRange *= 1.1f; // presses the attack rather than holding at range
    }

    // P3: recompute power budget on every loadout edit. meta.loadout is only
    // ever populated for fighters (capitals use meta.hardpoints instead, kept
    // in sync per-tick by UpdateNpcShips) — for a capital this rig is always
    // empty, so IsOverloaded() is trivially false and the capital thrust/turn
    // override just above already wins regardless.
    meta.loadout.RecalculateLoad();
    if (meta.loadout.IsOverloaded()) {
        meta.npcThrust   *= HardpointRig::kOverloadThrustFactor;
        meta.npcFireRate *= HardpointRig::kOverloadCooldownMult;
    }
}

// ── World state save/load helpers ─────────────────────────────────────────────

SaveManager::GameState SpaceFlight::BuildWorldState() const {
    using SM = SaveManager;
    SM::GameState gs;

    // Player ship
    gs.shipTypeId = _playerMeta.defId;
    gs.hull       = _playerEntity.health.currentHull;
    gs.maxHull    = _playerEntity.health.maxStats.hull;
    gs.posX       = _playerEntity.transform.position.x;
    gs.posY       = _playerEntity.transform.position.y;
    gs.velX       = _playerEntity.transform.velocity.x;
    gs.velY       = _playerEntity.transform.velocity.y;
    gs.rotation   = _playerEntity.transform.rotation;

    // Player loadout
    for (const auto* w : _loadout.WeaponSlots())
        gs.weaponIds.push_back(w->equipped ? w->equipped->id : std::string{});
    { auto* a = _loadout.Armor();      gs.armorId      = (a && a->equipped) ? a->equipped->id : std::string{}; }
    { auto* e = _loadout.Engine();     gs.engineId     = (e && e->equipped) ? e->equipped->id : std::string{}; }
    { auto* h = _loadout.Hyperdrive(); gs.hyperdriveId = (h && h->equipped) ? h->equipped->id : std::string{}; }
    for (const auto* s : _loadout.ShieldSlots())
        gs.shieldIds.push_back(s->equipped ? s->equipped->id : std::string{});
    for (const auto* a : _loadout.AuxSlots())
        gs.auxIds.push_back(a->equipped ? a->equipped->id : std::string{});

    // Storage
    for (const auto& slot : _storageMenu.slots) {
        SM::StorageSave ss;
        ss.type        = static_cast<int>(slot.type);
        ss.displayName = slot.displayName;
        ss.materialId  = slot.materialId;
        ss.moduleId    = (slot.type == StorageItemType::Module) ? slot.module.id : std::string{};
        ss.count       = slot.count;
        gs.storage.push_back(std::move(ss));
    }

    // Asteroids
    for (const auto& a : _w->asteroids) {
        SM::AsteroidSave as;
        as.id       = a.id;
        as.posX     = a.position.x; as.posY    = a.position.y;
        as.velX     = a.velocity.x; as.velY    = a.velocity.y;
        as.rotation = a.rotation;   as.rotSpeed= a.rotSpeed;
        as.health   = a.health;     as.tier    = a.tier;
        as.alive    = a.alive;
        for (const auto& m : a.materials) {
            SM::MaterialEntry me;
            me.materialId = m.materialId;
            me.percent    = m.percent;
            as.materials.push_back(std::move(me));
        }
        gs.asteroids.push_back(std::move(as));
    }

    // NPCs
    gs.nextNpcId = _w->nextNpcId;
    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        const NpcMeta&    nm = _w->npcMeta[i];
        const ecs::Entity& ne = _w->entities[i];
        SM::NpcSave ns;
        ns.id             = nm.id;
        ns.posX           = ne.transform.position.x; ns.posY = ne.transform.position.y;
        ns.velX           = ne.transform.velocity.x; ns.velY = ne.transform.velocity.y;
        ns.rotation       = ne.transform.rotation;
        ns.hull           = ne.health.currentHull;   ns.maxHull = ne.health.maxStats.hull;
        ns.radius         = nm.radius;
        ns.alive          = nm.alive;
        ns.faction        = static_cast<int>(nm.faction);
        ns.aiState        = static_cast<int>(nm.aiState);
        ns.waypointX      = nm.waypoint.x;  ns.waypointY   = nm.waypoint.y;
        ns.waypointSet    = nm.waypointSet;
        ns.fireCooldown   = nm.fireCooldown; ns.aggroRange  = nm.aggroRange;
        ns.attackRange    = nm.attackRange;
        ns.kineticShield  = ne.health.currentShield; ns.energyShield = nm.energyShield;
        ns.npcChargeTimer = nm.npcChargeTimer;
        ns.hasGreeted     = nm.hasGreeted;  ns.hasAnnounced = nm.hasAnnounced;
        ns.wingman        = nm.wingman;     ns.wingmanSlot  = nm.wingmanSlot;
        ns.escortTargetId = nm.escortTargetId;
        ns.shipTypeId     = nm.shipTypeId;
        { auto v = nm.loadout.WeaponSlots(); ns.weaponId = (!v.empty() && v[0]->equipped) ? v[0]->equipped->id : std::string{}; }
        { auto* a = nm.loadout.Armor();      ns.armorId  = (a && a->equipped) ? a->equipped->id : std::string{}; }
        { auto v = nm.loadout.ShieldSlots(); ns.shieldId = (!v.empty() && v[0]->equipped) ? v[0]->equipped->id : std::string{}; }
        { auto* e = nm.loadout.Engine();     ns.engineId = (e && e->equipped) ? e->equipped->id : std::string{}; }
        gs.npcs.push_back(std::move(ns));
    }

    // Sun
    gs.sunTypeId = _w->sun.typeId;
    gs.sunRadius = _w->sun.radius;

    // Planets (with orbital state)
    for (const auto& p : _w->planets) {
        SM::PlanetSave ps;
        ps.posX        = p.position.x; ps.posY        = p.position.y;
        ps.radius      = p.radius;     ps.id          = p.id;
        ps.orbitRadius = p.orbitRadius; ps.orbitAngle  = p.orbitAngle;
        ps.orbitSpeed  = p.orbitSpeed;
        gs.planets.push_back(ps);
    }

    // Stations
    for (const auto& s : _w->stations) {
        SM::StationSave ss;
        ss.posX = s.position.x; ss.posY = s.position.y;
        ss.radius = s.radius;   ss.id   = s.id;
        gs.stations.push_back(ss);
    }

    // Player-built stations
    for (const auto& ps : FleetManager::Get().PlayerStations) {
        SM::PlayerStationSave pss;
        pss.id           = ps.id;
        pss.stationDefId = ps.stationDefId;
        pss.displayName  = ps.displayName;
        pss.posX         = ps.position.x; pss.posY = ps.position.y;
        pss.alive        = ps.alive;
        pss.miningTimer  = ps.miningTimer;
        for (const auto& hp : ps.hardpoints) {
            SM::HardpointSave hps;
            hps.id      = hp.id;
            hps.hull    = hp.hull;
            hps.maxHull = hp.maxHull;
            hps.alive   = hp.alive;
            hps.shedPriority = hp.shedPriority;
            for (const auto& slot : hp.slots) {
                SM::HardpointSlotSave sls;
                sls.moduleId = slot.equipped ? slot.equipped->id : std::string{};
                hps.slots.push_back(std::move(sls));
            }
            pss.hardpoints.push_back(std::move(hps));
        }
        for (const auto& item : ps.storage) {
            SM::StorageSave stg;
            stg.type        = static_cast<int>(item.type);
            stg.displayName = item.displayName;
            stg.materialId  = item.materialId;
            stg.moduleId    = item.module.id;
            stg.count       = item.count;
            pss.storage.push_back(std::move(stg));
        }
        pss.economyStock = ps.economy.stock;
        gs.playerStations.push_back(std::move(pss));
    }

    // Loot drops
    for (const auto& l : _w->lootDrops) {
        SM::LootSave ls;
        ls.posX      = l.position.x; ls.posY      = l.position.y;
        ls.lifetime  = l.lifetime;   ls.pulseTimer = l.pulseTimer;
        ls.collected = l.collected;  ls.moduleId   = l.module.id;
        gs.lootDrops.push_back(std::move(ls));
    }

    // Material drops
    for (const auto& m : _w->materialDrops) {
        SM::MatDropSave ms;
        ms.posX       = m.position.x; ms.posY       = m.position.y;
        ms.lifetime   = m.lifetime;   ms.pulseTimer  = m.pulseTimer;
        ms.collected  = m.collected;  ms.materialId  = m.materialId;
        gs.matDrops.push_back(std::move(ms));
    }

    gs.discoveredIds        = _discoveredIds;
    gs.currentSystemId      = _currentSystemId;
    gs.discoveredSystemIds  = _discoveredSystemIds;
    gs.gameSeed             = _gameSeed;
    gs.currentGalaxyId      = _currentGalaxyId;
    gs.visitedGalaxyIds     = _visitedGalaxyIds;
    gs.hasWorldState = true;
    return gs;
}

void SpaceFlight::ApplyWorldState(const SaveManager::GameState& gs) {
    // Asteroids
    _w->asteroids.clear();
    for (const auto& as : gs.asteroids) {
        Asteroid a;
        a.id       = as.id;
        a.position = { as.posX, as.posY };
        a.velocity = { as.velX, as.velY };
        a.rotation = as.rotation;
        a.rotSpeed = as.rotSpeed;
        a.radius   = AsteroidRadius(as.tier);
        a.health   = as.health;
        a.tier     = as.tier;
        a.alive    = as.alive;
        for (const auto& m : as.materials) {
            MaterialChance mc;
            mc.materialId = m.materialId;
            mc.percent    = m.percent;
            a.materials.push_back(std::move(mc));
        }
        _w->asteroids.push_back(std::move(a));
    }

    // NPCs
    _w->entities.clear();
    _w->npcMeta.clear();
    _w->npcFreeSlots.clear();
    _w->nextNpcId = gs.nextNpcId;
    for (const auto& ns : gs.npcs) {
        ecs::Entity ne;
        NpcMeta     nm;
        ne.id                         = ns.id;
        ne.transform.position         = { ns.posX, ns.posY };
        ne.transform.velocity         = { ns.velX, ns.velY };
        ne.transform.rotation         = ns.rotation;
        ne.health.currentHull         = ns.hull;
        ne.health.maxStats.hull       = ns.maxHull;
        nm.radius                     = ns.radius;
        ne.transform.radius           = ns.radius;
        nm.alive                      = ns.alive;
        nm.id                         = ns.id;
        nm.faction                    = static_cast<NpcFaction>(ns.faction);
        nm.aiState                    = static_cast<NpcAiState>(ns.aiState);
        nm.waypoint                   = { ns.waypointX, ns.waypointY };
        nm.waypointSet                = ns.waypointSet;
        nm.fireCooldown               = ns.fireCooldown;
        nm.aggroRange                 = ns.aggroRange;
        nm.attackRange                = ns.attackRange;
        ne.health.currentShield       = ns.kineticShield;
        nm.energyShield               = ns.energyShield;
        nm.npcChargeTimer             = ns.npcChargeTimer;
        nm.hasGreeted                 = ns.hasGreeted;
        nm.hasAnnounced               = ns.hasAnnounced;
        nm.wingman                    = ns.wingman;
        nm.wingmanSlot                = ns.wingmanSlot;
        nm.escortTargetId             = ns.escortTargetId;
        nm.shipTypeId                 = ns.shipTypeId;
        {
            const ecs::ShipDef* sd = ecs::ShipRegistry::ShipById(nm.shipTypeId);
            nm.shipTypeName = sd ? sd->displayName
                                  : (ns.shipTypeId == "gargos" ? "Gargos" : "AR-3 Saber");
            nm.npcFaction = sd ? FactionFromPaletteId(sd->paletteId) : Faction::Merchant;
            // Capital hardpoint state (per-hardpoint HP/alive) isn't part of
            // the save format yet — rebuild a fresh, undamaged hardpoint set
            // from the ship def so the ship still renders/fires correctly
            // after loading, rather than silently falling back to the
            // pre-capital-ships single-point fighter behavior.
            if (sd && sd->shipType == ShipType::Capital)
                nm.hardpoints = BuildCapitalHardpoints(*sd);
        }
        nm.preferredRange             = nm.attackRange * 0.75f;
        nm.loadout.Resize(NpcMeta::WSlots, NpcMeta::ShSlots, 0);
        { auto v = nm.loadout.WeaponSlots(); if (!v.empty()) v[0]->equipped = ModuleById(ns.weaponId); }
        if (auto* a = nm.loadout.Armor()) a->equipped = ModuleById(ns.armorId);
        { auto v = nm.loadout.ShieldSlots(); if (!v.empty()) v[0]->equipped = ModuleById(ns.shieldId); }
        if (auto* e = nm.loadout.Engine()) e->equipped = ModuleById(ns.engineId);
        float savedHull = ns.hull, savedMaxHull = ns.maxHull;
        ApplyNpcLoadout(ne, nm);
        ne.health.currentHull     = savedHull;
        ne.health.maxStats.hull   = savedMaxHull;
        _w->entities.push_back(std::move(ne));
        _w->npcMeta.push_back(std::move(nm));
    }

    // Planets (restore orbital state)
    _w->planets.clear();
    for (const auto& ps : gs.planets) {
        SpacePlanet p;
        p.position    = { ps.posX, ps.posY };
        p.radius      = ps.radius;
        p.id          = ps.id;
        p.orbitRadius = ps.orbitRadius;
        p.orbitAngle  = ps.orbitAngle;
        p.orbitSpeed  = ps.orbitSpeed;
        _w->planets.push_back(p);
    }

    // Rebuild sun physics from saved data
    if (!gs.sunTypeId.empty()) {
        const StarTypeDef* def = StarRegistry::ById(gs.sunTypeId);
        if (def && _w->sun.active) {
            float savedR     = (gs.sunRadius > 0.f) ? gs.sunRadius
                                                     : (def->minRadius + def->maxRadius) * 0.5f;
            _w->sun.radius      = savedR;
            _w->sun.gravRange   = savedR * def->gravRangeMult;
            _w->sun.gravStrength= def->gravStrength;
        }
    }

    // Stations
    _w->stations.clear();
    for (const auto& ss : gs.stations) {
        SpaceStation st;
        st.position = { ss.posX, ss.posY };
        st.radius   = ss.radius;
        st.id       = ss.id;
        st.faction  = static_cast<Faction>(ss.id % static_cast<int>(Faction::COUNT));
        const auto& stTypes = StationTypeRegistry::All();
        const StationTypeDef& typeDef = stTypes[st.id % stTypes.size()];
        st.stationTypeId = typeDef.id;
        BuildNpcStationHardpoints(st);
        // SaveGameState doesn't carry economy stock yet (follow-up once the
        // save system's schema is revisited) — reseed fresh on load rather
        // than leaving every good at 0 stock forever.
        SeedStationEconomy(st.economy);
        _w->stations.push_back(std::move(st));
    }

    // Player-built stations
    FleetManager::Get().PlayerStations.clear();
    unsigned int maxPlayerStationId = 0;
    for (const auto& pss : gs.playerStations) {
        if (pss.stationDefId.empty()) continue;
        PlayerStation& ps = FleetManager::Get().SpawnStation(pss.stationDefId, { pss.posX, pss.posY });
        ps.id          = pss.id;
        ps.displayName = pss.displayName;
        ps.alive       = pss.alive;
        ps.miningTimer = pss.miningTimer;
        maxPlayerStationId = std::max(maxPlayerStationId, pss.id);

        // Overlay saved per-hardpoint hull/alive/equipped-module state onto
        // the freshly rebuilt hardpoint layout (index-aligned; same
        // stationDefId ⇒ same slot layout as when this was saved).
        for (size_t hi = 0; hi < ps.hardpoints.size() && hi < pss.hardpoints.size(); ++hi) {
            Hardpoint& hp = ps.hardpoints[hi];
            const SaveManager::HardpointSave& hps = pss.hardpoints[hi];
            hp.hull    = hps.hull;
            hp.maxHull = hps.maxHull;
            hp.alive   = hps.alive;
            hp.shedPriority = hps.shedPriority;
            for (size_t si = 0; si < hp.slots.size() && si < hps.slots.size(); ++si) {
                const std::string& modId = hps.slots[si].moduleId;
                hp.slots[si].equipped = modId.empty() ? std::nullopt : ModuleById(modId);
            }
        }

        // Overlay cargo hold (mining stations) — index-aligned onto the
        // fixed-size hold SpawnStation already allocated.
        for (size_t si = 0; si < ps.storage.size() && si < pss.storage.size(); ++si) {
            const SaveManager::StorageSave& s = pss.storage[si];
            StorageItem&           item = ps.storage[si];
            item.type        = static_cast<StorageItemType>(s.type);
            item.displayName = s.displayName;
            item.materialId  = s.materialId;
            item.count       = s.count;
            if (item.type == StorageItemType::Module) {
                if (auto mod = ModuleById(s.moduleId)) item.module = *mod;
            }
        }

        if (!pss.economyStock.empty()) ps.economy.stock = pss.economyStock;
    }
    if (maxPlayerStationId >= FleetManager::Get().NextStationId)
        FleetManager::Get().NextStationId = maxPlayerStationId + 1;

    // Loot drops
    _w->lootDrops.clear();
    for (const auto& ls : gs.lootDrops) {
        auto mod = ModuleById(ls.moduleId);
        if (!mod) continue;
        LootDrop l;
        l.position  = { ls.posX, ls.posY };
        l.lifetime  = ls.lifetime;
        l.pulseTimer= ls.pulseTimer;
        l.collected = ls.collected;
        l.module    = *mod;
        _w->lootDrops.push_back(std::move(l));
    }

    // Material drops
    _w->materialDrops.clear();
    for (const auto& ms : gs.matDrops) {
        MaterialDrop m;
        m.position  = { ms.posX, ms.posY };
        m.lifetime  = ms.lifetime;
        m.pulseTimer= ms.pulseTimer;
        m.collected = ms.collected;
        m.materialId= ms.materialId;
        _w->materialDrops.push_back(std::move(m));
    }

    // Clear transient targeting/UI state that no longer maps to saved world
    _target    = TargetInfo{};
    _targetId  = 0;
    _npcTargetId = 0;
}

void SpaceFlight::OnEnter() {
    ModuleRegistry::Init();

    // Fresh session: drop any prior worlds and start pointed at galaxy 1 /
    // system 1. If a save load changes _currentGalaxyId/_currentSystemId
    // below, the world map is re-keyed there.
    _worlds.clear();
    _currentGalaxyId = 1;
    _currentSystemId = 1;
    _w = &GetOrCreateWorld(_currentSystemId);
    _peerSystem.clear();
    _peerFaction.clear();
    _peerFactionDiscovered.clear();
    _bgTick = false;
    _fuel = kMaxFuel; // Epic 4: fresh session starts with a full tank

    auto& cfg = FleetManager::Get().PlayerShip;
    kPlayerFaction = cfg.PlayerFaction;
    ReputationRegistry::ResetForPlayerFaction(kPlayerFaction); // Epic 6.1: seed standing from the static matrix

    SaveManager::GameState gs;
    bool didLoad = SaveManager::Get().HasPendingLoad();

    auto InitPlayerFromDef = [&](const ecs::ShipDef& def) {
        _playerMeta = PlayerMeta{};
        _playerMeta.defId           = def.id;
        _playerMeta.displayName     = def.displayName;
        _playerMeta.radius          = def.radius;
        _playerMeta.shipType        = def.shipType;
        _playerMeta.weaponSlots     = def.weaponSlots;
        _playerMeta.armorSlots      = def.armorSlots;
        _playerMeta.shieldSlots     = def.shieldSlots;
        _playerMeta.engineSlots     = def.engineSlots;
        _playerMeta.hyperdriveSlots = def.hyperdriveSlots;
        _playerMeta.auxSlots        = def.auxSlots;
        _playerEntity = ecs::Entity{};
        _playerEntity.health.maxStats.hull  = def.baseStats.hull;
        _playerEntity.health.currentHull    = def.baseStats.hull;
        _playerEntity.transform.radius      = def.radius;
    };

    if (didLoad) {
        gs = SaveManager::Get().ConsumePendingLoad();
        if (const auto* d = ecs::ShipRegistry::ShipById(gs.shipTypeId)) InitPlayerFromDef(*d);
        _playerEntity.transform.position = { gs.posX, gs.posY };
        _playerEntity.transform.velocity = { gs.velX, gs.velY };
        _playerEntity.transform.rotation = gs.rotation;
        cfg.HullIntegrity = gs.hull;
        cfg.MaxHull       = gs.maxHull;
        cfg.ShipTypeId    = gs.shipTypeId;
    }
    else {
        if (const auto* d = ecs::ShipRegistry::ShipById(cfg.ShipTypeId)) InitPlayerFromDef(*d);
        _playerEntity.health.currentHull    = cfg.HullIntegrity;
        _playerEntity.health.maxStats.hull  = cfg.MaxHull;
        _playerEntity.transform.position    = { 0.0f, 0.0f };
    }

    // Epic 12: a fresh (non-loaded) session starts the tutorial; loading an
    // existing save skips it outright — a returning player doesn't need it,
    // and SaveManager has no "has completed tutorial" field to check instead.
    _tutorialActive   = !didLoad;
    _tutorialStep     = TutorialStep::Move;
    _tutorialStartPos = _playerEntity.transform.position;
    _commsLogOpen     = false;
    if (_tutorialActive) {
        AddCommsMessage("TUTORIAL: Fly at least 300 units from your starting position.", true);
    }

    _camera = Camera2D{};
    _camera.target = _playerEntity.transform.position;
    _camera.offset = { (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f };
    _cameraZoom = 1.0f;
    _camera.zoom = _cameraZoom;

    _w->projectiles.clear();
    _w->asteroids.clear();
    _hitCooldown = 0.0f;
    _target = TargetInfo{};
    _targetId = 0;
    _localMapOpen = false;

    // All weapon slots start enabled; ApplyLoadout sizes these to the ship.
    _weaponEnabled.assign(_weaponEnabled.size(), true);
    std::fill(_weaponCooldown.begin(), _weaponCooldown.end(), 0.0f);
    std::fill(_weaponCharge.begin(), _weaponCharge.end(), 0.0f);
    _primaryWeapon = -1;
    _enterPopupOpen      = false;
    _stationServicesMenu.isOpen = false;
    _dockedStationId       = 0;
    _dockedIsPlayerStation = false;
    _inPlacementMode     = false;
    _placementConfirmOpen= false;
    _placingStationDefId.clear();
    _npcTargetId = 0;
    _w->respawnTimer = 20.0f;
    _w->entities.clear();
    _w->npcMeta.clear();
    _w->lootDrops.clear();
    _w->materialDrops.clear();
    _w->derelictWrecks.clear();
    _w->hasActiveDistress = false;
    _playerDead = false;
    _deathTimer = 0.0f;
    _commsLog.clear();
    _commsMenuOpen = false;
    _commsMenuPhase = 0;
    _commsMenuNpcId = 0;
    _commsMenuNpcName = {};
    _commsMenuNpcText = {};
    _commsMenuIsStation = false;
    _commsMenuStationId = 0;
    _commsMenuIsDistress = false;
    _escortMenuOpen = false;
    _escortMenuSelId = 0;
    _escortModuleNpcId = 0;
    _loadout.Resize(_playerMeta.weaponSlots, _playerMeta.shieldSlots, _playerMeta.auxSlots);
    _discoveredIds.clear();
    _currentSystemId = 1;
    _discoveredSystemIds.clear();
    _visitedGalaxyIds.assign({ 1 });

    // ── Universe/galaxy seed: derive (New Game) or restore (Load) before
    // anything touches StarSystemRegistry — every system's position/seed is
    // derived from the current galaxy's seed, which is itself derived from
    // this one master (universe) seed via UniverseRegistry. currentGalaxyId
    // defaults to 1 (home galaxy), which UniverseRegistry::Generate(1) always
    // resolves back to _gameSeed unmodified, so single-galaxy saves/behavior
    // are unaffected.
    if (didLoad) {
        _gameSeed        = gs.gameSeed != 0 ? gs.gameSeed : 1u;
        _currentGalaxyId = gs.currentGalaxyId != 0 ? gs.currentGalaxyId : 1u;
    } else {
        _gameSeed = cfg.GalaxySeedInput.empty()
            ? (uint32_t)GetRandomValue(1, 2147483647)
            : StarSystemRegistry::HashSeedString(cfg.GalaxySeedInput);
    }
    UniverseRegistry::Init(_gameSeed);
    StarSystemRegistry::Init(UniverseRegistry::Generate(_currentGalaxyId).seed);
    // Bug fix (2026-07-08): the home system (id==1) used to always report
    // Faction::Republic regardless of the player's actual chosen faction —
    // StarSystemRegistry is a free-standing header with no access to
    // kPlayerFaction, so this is the one place that can correct it. Host/
    // offline only: StarSystemRegistry's override map is per-process static
    // state, not network-synced, so a client independently calling this with
    // its own (possibly different) faction would make its local view of
    // system 1's owner diverge from the host's and other clients' — the
    // actual in-game station's faction reaches clients via the normal
    // station-sync snapshot instead, so clients don't need this override.
    if (!net::Game().IsClient()) {
        StarSystemRegistry::SetControlled(1, kPlayerFaction);
    }

    // ── Loadout: restore from save, or fall back to default starter kit ───────
    if (didLoad && gs.hasWorldState && !gs.engineId.empty()) {
        { auto v = _loadout.WeaponSlots(); for (int i = 0; i < (int)v.size() && i < (int)gs.weaponIds.size(); ++i) v[i]->equipped = ModuleById(gs.weaponIds[i]); }
        if (auto* a = _loadout.Armor())      a->equipped = ModuleById(gs.armorId);
        if (auto* e = _loadout.Engine())     e->equipped = ModuleById(gs.engineId);
        if (auto* h = _loadout.Hyperdrive()) h->equipped = ModuleById(gs.hyperdriveId);
        { auto v = _loadout.ShieldSlots(); for (int i = 0; i < (int)v.size() && i < (int)gs.shieldIds.size(); ++i) v[i]->equipped = ModuleById(gs.shieldIds[i]); }
        { auto v = _loadout.AuxSlots(); for (int i = 0; i < (int)v.size() && i < (int)gs.auxIds.size(); ++i) v[i]->equipped = ModuleById(gs.auxIds[i]); }
        _discoveredIds        = gs.discoveredIds;
        _currentSystemId      = gs.currentSystemId;
        _discoveredSystemIds  = gs.discoveredSystemIds;
        if (!gs.visitedGalaxyIds.empty()) _visitedGalaxyIds = gs.visitedGalaxyIds;
    }
    else {
        if (auto* e = _loadout.Engine()) e->equipped = Engine_Thruster_I();
        if (auto* a = _loadout.Armor())  a->equipped = Armor_HullPatch();
        { auto v = _loadout.WeaponSlots(); if (!v.empty()) v[0]->equipped = Weapon_PulseCannon_I(); }
        { auto v = _loadout.ShieldSlots(); if (!v.empty()) v[0]->equipped = Shield_KineticBarrier_I(); }
        // No hyperdrive or aux module equipped by default — the player must
        // craft or buy a hyperdrive/sensors at a station before warping.
    }
    // Reset hull before ApplyLoadout so a dead-state from a prior session doesn't
    // carry over via its min(currentHull, maxHull) clamp.
    _playerEntity.health.currentHull = _playerEntity.health.maxStats.hull;
    ApplyLoadout();

    // ApplyLoadout resets maxHull from ship def + armor bonus; re-apply saved hull
    // values so the loaded state is faithfully restored.
    if (didLoad) {
        _playerEntity.health.currentHull   = gs.hull;
        _playerEntity.health.maxStats.hull = gs.maxHull;
    }
    // Guard against a save that recorded hull=0 (e.g. died before save flushed).
    if (_playerEntity.health.currentHull <= 0.0f)
        _playerEntity.health.currentHull = _playerEntity.health.maxStats.hull;

    // Always mark the current system as discovered
    if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), _currentSystemId)
            == _discoveredSystemIds.end())
        _discoveredSystemIds.push_back(_currentSystemId);

    // Always mark the current galaxy as visited
    if (std::find(_visitedGalaxyIds.begin(), _visitedGalaxyIds.end(), _currentGalaxyId)
            == _visitedGalaxyIds.end())
        _visitedGalaxyIds.push_back(_currentGalaxyId);

    // A loaded save may have moved us to another system/galaxy — re-key the world map.
    if (_w->systemId != _currentSystemId || _w->galaxyId != _currentGalaxyId) {
        _worlds.clear();
        _w = &GetOrCreateWorld(_currentSystemId);
    }

    // ── Storage: restore from save, or seed with default starter modules ──────
    _storageMenu.Open(8);
    _storageMenu.Close();

    if (didLoad && gs.hasWorldState && !gs.storage.empty()) {
        int slotCount = (int)std::min(gs.storage.size(), _storageMenu.slots.size());
        for (int i = 0; i < slotCount; ++i) {
            const auto& ss = gs.storage[i];
            StorageItem& slot = _storageMenu.slots[i];
            slot.type        = static_cast<StorageItemType>(ss.type);
            slot.displayName = ss.displayName;
            slot.materialId  = ss.materialId;
            slot.count       = ss.count;
            if (slot.type == StorageItemType::Module) {
                auto mod = ModuleById(ss.moduleId);
                slot.module = mod ? *mod : ModuleDef{};
            }
        }
    }
    else {
        // No starter modules seeded into storage — the player must craft or
        // buy a hyperdrive (and other module types) at a station.
    }

    if (_planetBaseTex.id > 0)     { UnloadTexture(_planetBaseTex);     _planetBaseTex     = {}; }
    if (_stationBaseTex.id > 0)   { UnloadTexture(_stationBaseTex);   _stationBaseTex   = {}; }
    if (_gargosTex.id > 0)        { UnloadTexture(_gargosTex);        _gargosTex        = {}; }
    if (_sunTex.id > 0)           { UnloadTexture(_sunTex);           _sunTex           = {}; }
    if (_asteroidTexLarge.id > 0) { UnloadTexture(_asteroidTexLarge); _asteroidTexLarge = {}; }
    if (_asteroidTexMedium.id > 0){ UnloadTexture(_asteroidTexMedium);_asteroidTexMedium= {}; }
    if (_asteroidTexSmall.id > 0) { UnloadTexture(_asteroidTexSmall); _asteroidTexSmall = {}; }
    if (_hudFontUi.texture.id  > 0) { UnloadFont(_hudFontUi);  _hudFontUi  = {}; }
    if (_hudFontVal.texture.id > 0) { UnloadFont(_hudFontVal); _hudFontVal = {}; }
    _planetBaseTex    = LoadTexture("assets/planets/planet1.png");
    _stationBaseTex   = LoadTexture("assets/stations/space_station1.png");
    _gargosTex        = LoadTexture("assets/ships/gargos.png");
    _sunTex           = LoadTexture("assets/sun/sun.png");
    _asteroidTexLarge = LoadTexture("assets/asteroids/large_asteroid.png");
    _asteroidTexMedium= LoadTexture("assets/asteroids/medium_asteroid.png");
    _asteroidTexSmall = LoadTexture("assets/asteroids/small_asteroid.png");
    if (_planetBaseTex.id    > 0) SetTextureFilter(_planetBaseTex,    TEXTURE_FILTER_BILINEAR);
    if (_stationBaseTex.id   > 0) SetTextureFilter(_stationBaseTex,   TEXTURE_FILTER_BILINEAR);
    if (_gargosTex.id        > 0) SetTextureFilter(_gargosTex,        TEXTURE_FILTER_BILINEAR);
    if (_sunTex.id           > 0) SetTextureFilter(_sunTex,           TEXTURE_FILTER_BILINEAR);
    if (_asteroidTexLarge.id > 0) SetTextureFilter(_asteroidTexLarge, TEXTURE_FILTER_BILINEAR);
    if (_asteroidTexMedium.id> 0) SetTextureFilter(_asteroidTexMedium,TEXTURE_FILTER_BILINEAR);
    if (_asteroidTexSmall.id > 0) SetTextureFilter(_asteroidTexSmall, TEXTURE_FILTER_BILINEAR);

    _hudFontUi = LoadFontEx("assets/fonts/Orbitron/Orbitron-VariableFont_wght.ttf", 40, nullptr, 0);
    if (_hudFontUi.texture.id == 0) _hudFontUi = GetFontDefault();
    else { GenTextureMipmaps(&_hudFontUi.texture); SetTextureFilter(_hudFontUi.texture, TEXTURE_FILTER_TRILINEAR); }
    _hudFontVal = LoadFontEx("assets/fonts/Exo_2/Exo2-VariableFont_wght.ttf", 40, nullptr, 0);
    if (_hudFontVal.texture.id == 0) _hudFontVal = GetFontDefault();
    else { GenTextureMipmaps(&_hudFontVal.texture); SetTextureFilter(_hudFontVal.texture, TEXTURE_FILTER_TRILINEAR); }

    // ── World entities: restore or spawn fresh ────────────────────────────────
    _w->sun = SpaceSun{};
    if (didLoad && gs.hasWorldState) {
        ApplyWorldState(gs);
        // Re-derive sun visuals/physics from saved data
        if (!gs.sunTypeId.empty()) {
            const StarTypeDef* def = StarRegistry::ById(gs.sunTypeId);
            if (def) {
                float savedR     = (gs.sunRadius > 0.f) ? gs.sunRadius
                                                         : (def->minRadius + def->maxRadius) * 0.5f;
                _w->sun.typeId      = def->id;
                _w->sun.radius      = savedR;
                _w->sun.gravRange   = savedR * def->gravRangeMult;
                _w->sun.gravStrength= def->gravStrength;
                _w->sun.coreColor   = def->coreColor;
                _w->sun.innerGlow   = def->innerGlowColor;
                _w->sun.outerGlow   = def->outerGlowColor;
                _w->sun.active      = true;
                BakeSunCorona();
            }
        }
    }
    else if (net::Game().IsHost()) {
        // Same seed rule as offline (derived from the galaxy master seed) so a
        // given galaxy always reproduces the same home system; joining clients
        // still receive the seed explicitly via WorldSync.
        auto homeSys = StarSystemRegistry::ById(_currentSystemId);
        _w->seed     = homeSys ? homeSys->seed : (uint32_t)GetRandomValue(100000, 999999);
        _worldSynced = true;
        _playerEntity.id                 = 1;   // non-zero so snapshots include it
        _playerEntity.network.networkId  = 1;
        _playerEntity.network.isLocalPlayer = true;
        SpawnPlanetsAndStations(_w->seed);
        SpawnInitialAsteroids();
        SpawnNpcShips();
        _playerEntity.transform.position = _w->playerSpawnPos;
        _camera.target = _playerEntity.transform.position;
    }
    else if (net::Game().IsClient()) {
        // Client waits for WorldSync before spawning anything.
        // Show "Syncing..." overlay until WorldSync arrives (see Update).
        _worldSynced = false;
        _remoteEntities.clear();
        _remoteCapitalHardpoints.clear();
        _remoteFighterMounts.clear();
        _remotePlayerStations.clear();
        uint32_t localId = net::Game().LocalNetworkId();
        _playerEntity.id                 = localId;  // non-zero so it can be identified
        _playerEntity.network.networkId  = localId;
        _playerEntity.network.isLocalPlayer = true;
    }
    else {
        // Offline — derive the home system's content seed from the galaxy
        // seed (same rule warp targets use) so a given galaxy seed always
        // reproduces the same starting system, not a random one each launch.
        auto homeSys = StarSystemRegistry::ById(_currentSystemId);
        _w->seed = homeSys ? homeSys->seed : 0;
        SpawnPlanetsAndStations(_w->seed);  // sets _w->sun and _w->playerSpawnPos
        SpawnInitialAsteroids();
        SpawnNpcShips();
        _playerEntity.transform.position = _w->playerSpawnPos;
        _camera.target = _playerEntity.transform.position;
    }
    PrewarmSpriteCache();
    InitStars();
    _lighting.Init();
}

// ── Warp cinematic ───────────────────────────────────────────────────────────
// Phases: TurnToFace (rotate to travel direction) -> FlyOut (accelerate off
// screen, trailing particles) -> FadeOut (screen to black; the actual system
// switch happens the instant the screen is fully black) -> FlyIn (ship dashes
// from just behind the spawn point into position while the screen fades back
// in). SpaceFlight::Update() short-circuits into UpdateWarpSequence() for the
// whole duration, so none of the normal input/AI/combat logic runs.
static constexpr float kWarpTurnTime      = 0.45f;
static constexpr float kWarpFlyOutTime    = 0.55f;
static constexpr float kWarpFadeOutTime   = 0.35f;
static constexpr float kWarpFlyInTime     = 0.60f;
static constexpr float kWarpFadeInRamp    = 0.30f;  // portion of FlyIn spent fading back in
static constexpr float kWarpFlySpeed      = 3200.0f; // u/s — well beyond normal thrust top speed
static constexpr float kWarpArriveOffset  = 1400.0f; // how far "behind" spawn the FlyIn dash starts
static constexpr float kLocalWarpFlyTime  = 0.50f;   // in-system dash: turn then zoom straight there

void SpaceFlight::SpawnWarpParticle(Vector2 pos, Vector2 dir) {
    float spread = ((float)GetRandomValue(-100, 100) / 100.0f);
    Vector2 perp = { -dir.y, dir.x };
    WarpParticle p;
    p.pos     = Vector2Add(pos, Vector2Scale(perp, spread * 14.0f));
    p.vel     = Vector2Scale(dir, -(kWarpFlySpeed * 0.9f + (float)GetRandomValue(0, 400)));
    p.maxLife = 0.35f + (float)GetRandomValue(0, 20) / 100.0f;
    p.life    = p.maxLife;
    _warpParticles.push_back(p);
}

void SpaceFlight::BeginWarpSequence(unsigned int targetSystemId) {
    auto curSys = StarSystemRegistry::ById(_currentSystemId);
    auto tgtSys = StarSystemRegistry::ById(targetSystemId);
    Vector2 dir = { 0.0f, -1.0f };
    float   jumpDist = 0.0f;
    if (curSys && tgtSys) {
        Vector2 delta = Vector2Subtract(tgtSys->galacticPos, curSys->galacticPos);
        jumpDist = Vector2Length(delta);
        if (jumpDist > 0.01f) dir = Vector2Scale(delta, 1.0f / jumpDist);
    }

    // Epic 4: hard-gate the jump on fuel — no soft warning, the drive simply
    // won't engage. Any queued beacon-chain hops are abandoned too, since a
    // hop mid-chain running dry leaves the player stranded wherever the last
    // successful hop put them (locked decision: "blocks warp until refueled/
    // rescued", not a partial-chain fallback).
    float cost = JumpFuelCost(jumpDist);
    if (cost > _fuel + 0.01f) {
        AddCommsMessage("HYPERDRIVE: Insufficient fuel for this jump. Refuel at a station.");
        _warpChainQueue.clear();
        // Epic 11.2: a failed jump for lack of fuel is exactly the "stranded"
        // condition Epic 4.3 flagged as needing a rescue hook — send one
        // distress call per stranding (a later failed jump before this
        // resolves doesn't re-send/reset the rescue-ETA timer).
        if (!_w->hasActiveDistress) {
            DistressCall dc;
            dc.type          = DistressType::Stranded;
            dc.position       = _playerEntity.transform.position;
            dc.windowSeconds  = (float)GetRandomValue(45, 90);
            _w->activeDistress    = dc;
            _w->hasActiveDistress = true;
            AddCommsMessage("DISTRESS CALL SENT: awaiting a passing vessel to deliver emergency fuel.");
        }
        return;
    }
    _fuel = std::max(0.0f, _fuel - cost);

    // Epic 12.4: warping away always lifts home-station protection, even if
    // the tutorial isn't finished (the fallback for a player who skips by
    // leaving) — completes step 8 if that's genuinely where the player is,
    // otherwise just ends the tutorial quietly with no completion message.
    if (_tutorialActive) {
        if (_tutorialStep == TutorialStep::Warp) AdvanceTutorialStep(TutorialStep::Warp);
        else _tutorialActive = false;
    }

    _warpTargetSystemId  = targetSystemId;
    _warpTargetGalaxyId  = 0; // same-galaxy warp
    _warpDir             = dir;
    _warpStartRot        = _playerEntity.transform.rotation;
    _warpTargetRot       = atan2f(dir.y, dir.x) * RAD2DEG + 90.0f;
    _warpFadeAlpha       = 0.0f;
    _warpParticles.clear();
    _warpPhaseTimer       = 0.0f;
    _warpPhaseAfterTurn   = WarpPhase::FlyOut;
    _warpPhase            = WarpPhase::TurnToFace;
    _playerMeta.thrusting = false;
}

// Cross-galaxy warp: arrives at targetGalaxyId's home system (id 1, always
// populated — see UniverseRegistry's id==1 convention). The turn direction is
// cosmetic — current/target galaxies live in different in-game coordinate
// spaces, so there's no single "vector toward the destination" the way an
// in-galaxy warp has; using the universe-space delta between the two
// galaxies' icon positions still gives a deterministic, seed-consistent
// facing rather than an arbitrary one.
void SpaceFlight::BeginGalaxyWarpSequence(unsigned int targetGalaxyId) {
    Vector2 curPos = UniverseRegistry::Generate(_currentGalaxyId).position;
    Vector2 tgtPos = UniverseRegistry::Generate(targetGalaxyId).position;
    Vector2 delta  = Vector2Subtract(tgtPos, curPos);
    float   len    = Vector2Length(delta);
    Vector2 dir    = (len > 0.01f) ? Vector2Scale(delta, 1.0f / len) : Vector2{ 0.0f, -1.0f };

    // Epic 4: same fuel gate as an in-galaxy hop (see BeginWarpSequence) —
    // cross-galaxy jumps only reach a drive's own targetable range anyway
    // (GalaxyMap gates which galaxies are even selectable), so the same
    // distance-vs-range cost formula applies without adjustment.
    float cost = JumpFuelCost(len);
    if (cost > _fuel + 0.01f) {
        AddCommsMessage("HYPERDRIVE: Insufficient fuel for this jump. Refuel at a station.");
        // Epic 11.2: same stranding-distress-call hook as BeginWarpSequence.
        if (!_w->hasActiveDistress) {
            DistressCall dc;
            dc.type          = DistressType::Stranded;
            dc.position       = _playerEntity.transform.position;
            dc.windowSeconds  = (float)GetRandomValue(45, 90);
            _w->activeDistress    = dc;
            _w->hasActiveDistress = true;
            AddCommsMessage("DISTRESS CALL SENT: awaiting a passing vessel to deliver emergency fuel.");
        }
        return;
    }
    _fuel = std::max(0.0f, _fuel - cost);

    // Epic 12.4: same warp-away tutorial hook as BeginWarpSequence.
    if (_tutorialActive) {
        if (_tutorialStep == TutorialStep::Warp) AdvanceTutorialStep(TutorialStep::Warp);
        else _tutorialActive = false;
    }

    _warpTargetSystemId  = 1; // every galaxy's home/arrival system
    _warpTargetGalaxyId  = targetGalaxyId;
    _warpDir             = dir;
    _warpStartRot        = _playerEntity.transform.rotation;
    _warpTargetRot       = atan2f(dir.y, dir.x) * RAD2DEG + 90.0f;
    _warpFadeAlpha       = 0.0f;
    _warpParticles.clear();
    _warpPhaseTimer       = 0.0f;
    _warpPhaseAfterTurn   = WarpPhase::FlyOut;
    _warpPhase            = WarpPhase::TurnToFace;
    _playerMeta.thrusting = false;
    _warpChainQueue.clear(); // cross-galaxy jumps are never part of a beacon chain
}

void SpaceFlight::BeginLocalWarp(Vector2 targetPos) {
    Vector2 delta = Vector2Subtract(targetPos, _playerEntity.transform.position);
    float   len   = Vector2Length(delta);
    Vector2 dir   = (len > 0.01f) ? Vector2Scale(delta, 1.0f / len) : Vector2{ 0.0f, -1.0f };

    _warpLocalTarget      = targetPos;
    _warpDir              = dir;
    _warpStartRot         = _playerEntity.transform.rotation;
    _warpTargetRot        = atan2f(dir.y, dir.x) * RAD2DEG + 90.0f;
    _warpFadeAlpha        = 0.0f;
    _warpParticles.clear();
    _warpPhaseTimer       = 0.0f;
    _warpPhaseAfterTurn   = WarpPhase::LocalFly;
    _warpPhase            = WarpPhase::TurnToFace;
    _playerMeta.thrusting = false;
    _warpChainQueue.clear(); // in-system dashes are never part of a beacon chain
}

void SpaceFlight::CommitWarpWorldSwitch(unsigned int targetSystemId, unsigned int targetGalaxyId) {
    bool crossingGalaxy = (targetGalaxyId != 0 && targetGalaxyId != _currentGalaxyId);
    if (crossingGalaxy) {
        // Re-point StarSystemRegistry at the new galaxy before anything below
        // looks up targetSystemId — it only means something in this galaxy's
        // id space. _worlds/GetOrCreateWorld pick up _currentGalaxyId
        // automatically, so a later re-visit to the old galaxy still resumes
        // its systems as left (see the _worlds comment in SpaceFlight.h).
        _currentGalaxyId = targetGalaxyId;
        StarSystemRegistry::Init(UniverseRegistry::Generate(_currentGalaxyId).seed);
        // Per-galaxy discovery/fog-of-war isn't remembered across a visit
        // (unlike mutable world state) — see _currentGalaxyId's comment.
        _discoveredSystemIds.clear();
        // Unlike discovery, "visited" is remembered forever (see
        // _visitedGalaxyIds's comment) — this is what lets the Universe tier
        // map color it blue on every future look, not just this visit.
        if (std::find(_visitedGalaxyIds.begin(), _visitedGalaxyIds.end(), targetGalaxyId)
                == _visitedGalaxyIds.end())
            _visitedGalaxyIds.push_back(targetGalaxyId);
    }

    auto sys = StarSystemRegistry::ById(targetSystemId);
    if (!sys) return;

    // Wingmen don't follow across systems: demote them to regular patrol in the
    // world being left behind (it persists now, so they'd otherwise chase a
    // player position in a different system's coordinate space forever).
    for (NpcMeta& m : _w->npcMeta) {
        if (m.alive && m.wingman) {
            m.wingman     = false;
            m.wingmanSlot = -1;
            m.aiState     = NpcAiState::Patrol;
            m.waypointSet = false;
        }
    }

    _currentSystemId = targetSystemId;
    if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), targetSystemId)
            == _discoveredSystemIds.end()) {
        _discoveredSystemIds.push_back(targetSystemId);
        // Discovery pooling: the host is always a member of its own faction's
        // party, so its own new discoveries go out to every connected peer
        // sharing kPlayerFaction — the mirror image of the pendingWarpNotifies
        // handling above, which pools a *peer's* discoveries the same way.
        if (net::Game().IsHost()) {
            std::vector<uint32_t> targets;
            for (const auto& [otherId, otherFaction] : _peerFaction)
                if (otherFaction == kPlayerFaction) targets.push_back(otherId);
            if (!targets.empty())
                net::Game().HostBroadcastSystemDiscovered(targets, targetSystemId);
        }
    }
    _discoveredIds.clear();

    // Targeting state points at the old world's NPCs; ids restart per world.
    _target       = TargetInfo{};
    _targetId     = 0;
    _npcTargetId  = 0;
    _lockTargetId = 0;

    // Attach to the destination world: freshly generated on first visit,
    // resumed exactly as left (dead stations, mined asteroids...) on a revisit.
    _w = &EnsureWorldGenerated(targetSystemId);
    BakeSunCorona();  // EnsureWorldGenerated restores the previous world's corona
}

// Client: build (or rebuild) the local world for the system the host says
// we're in. Regenerates content from the seed, fast-forwards planet orbits to
// the host's simulation age, and applies the station diff so damage/deaths
// that predate our arrival are visible. NPCs, asteroid state and projectiles
// need nothing here — they reconcile from the first Snapshot that follows.
void SpaceFlight::ApplyWorldSyncClient(const net::WorldSyncData& ws) {
    // The host is authoritative for which galaxy is current; the client just
    // follows along. Per-galaxy discovery state doesn't carry across a galaxy
    // change (same rule as CommitWarpWorldSwitch's cross-galaxy path).
    if (ws.galaxyId != _currentGalaxyId) _discoveredSystemIds.clear();
    _currentSystemId = ws.systemId;
    _gameSeed        = ws.gameSeed;
    _currentGalaxyId = ws.galaxyId;
    if (std::find(_visitedGalaxyIds.begin(), _visitedGalaxyIds.end(), ws.galaxyId)
            == _visitedGalaxyIds.end())
        _visitedGalaxyIds.push_back(ws.galaxyId);
    UniverseRegistry::Init(_gameSeed);
    StarSystemRegistry::Init(UniverseRegistry::Generate(_currentGalaxyId).seed);

    // Clients only ever track the system they're in — start it clean.
    _worlds.clear();
    _w = &GetOrCreateWorld(ws.systemId);
    _w->seed = ws.worldSeed;
    SpawnPlanetsAndStations(ws.worldSeed);
    SpawnInitialAsteroids();
    UpdateOrbits(ws.worldAge);   // fast-forward to the host's orbit phase
    _w->age = ws.worldAge;
    for (const net::StationStateSync& ss : ws.stations) {
        for (SpaceStation& st : _w->stations) {
            if (st.id != ss.id) continue;
            st.hull  = ss.hull;
            st.alive = ss.alive != 0;
            break;
        }
    }
    // Offset the spawn so players arriving at the same system don't overlap.
    _w->playerSpawnPos.x += 200.0f;

    if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), _currentSystemId)
            == _discoveredSystemIds.end())
        _discoveredSystemIds.push_back(_currentSystemId);

    // Cross-system leftovers: remote ships/projectiles and target locks.
    _remoteEntities.clear();
    _remoteCapitalHardpoints.clear();
    _remoteFighterMounts.clear();
    _remotePlayerStations.clear();
    _remoteProjectiles.clear();
    _target       = TargetInfo{};
    _targetId     = 0;
    _npcTargetId  = 0;
    _lockTargetId = 0;
    _discoveredIds.clear();

    _worldSynced = true;
}

void SpaceFlight::UpdateWarpSequence(float dt) {
    _warpPhaseTimer += dt;

    // Particles keep drifting/fading through every phase once spawned.
    for (size_t i = 0; i < _warpParticles.size(); ) {
        WarpParticle& p = _warpParticles[i];
        p.life -= dt;
        p.pos   = Vector2Add(p.pos, Vector2Scale(p.vel, dt));
        if (p.life <= 0.0f) { _warpParticles[i] = _warpParticles.back(); _warpParticles.pop_back(); }
        else ++i;
    }

    switch (_warpPhase) {
    case WarpPhase::TurnToFace: {
        float t = std::min(_warpPhaseTimer / kWarpTurnTime, 1.0f);
        _playerEntity.transform.rotation = LerpAngleDeg(_warpStartRot, _warpTargetRot, t);
        if (t >= 1.0f) {
            if (_warpPhaseAfterTurn == WarpPhase::LocalFly) {
                _warpFlyInStart = _playerEntity.transform.position;
                _playerEntity.transform.velocity = Vector2Scale(_warpDir, kWarpFlySpeed);
            }
            _warpPhase      = _warpPhaseAfterTurn;
            _warpPhaseTimer = 0.0f;
        }
        break;
    }
    case WarpPhase::FlyOut: {
        float t     = std::min(_warpPhaseTimer / kWarpFlyOutTime, 1.0f);
        float speed = Vector2Length(_playerEntity.transform.velocity) * (1.0f - t) + kWarpFlySpeed * t;
        _playerEntity.transform.velocity = Vector2Scale(_warpDir, speed);
        _playerEntity.transform.position = Vector2Add(_playerEntity.transform.position,
            Vector2Scale(_playerEntity.transform.velocity, dt));
        _camera.target = _playerEntity.transform.position;
        for (int i = 0; i < 3; ++i)
            SpawnWarpParticle(_playerEntity.transform.position, _warpDir);
        if (t >= 1.0f) {
            _warpPhase      = WarpPhase::FadeOut;
            _warpPhaseTimer = 0.0f;
        }
        break;
    }
    case WarpPhase::FadeOut: {
        float t = std::min(_warpPhaseTimer / kWarpFadeOutTime, 1.0f);
        _warpFadeAlpha = t;
        if (t >= 1.0f) {
            if (net::Game().IsClient()) {
                // The host owns the destination's live state — announce the
                // warp and hold on black until its WorldSync arrives.
                net::Game().ClientSendWarpNotify(_warpTargetSystemId);
                _remoteEntities.clear();
                _remoteCapitalHardpoints.clear();
                _remoteFighterMounts.clear();
                _remotePlayerStations.clear();
                _remoteProjectiles.clear();
                _worldSynced    = false;   // also stops Input sends while in limbo
                _warpPhase      = WarpPhase::AwaitSync;
                _warpPhaseTimer = 0.0f;
                break;
            }
            CommitWarpWorldSwitch(_warpTargetSystemId, _warpTargetGalaxyId);
            _warpParticles.clear();
            _warpFlyInStart = Vector2Subtract(_w->playerSpawnPos, Vector2Scale(_warpDir, kWarpArriveOffset));
            _playerEntity.transform.position = _warpFlyInStart;
            _playerEntity.transform.velocity = Vector2Scale(_warpDir, kWarpFlySpeed);
            _playerEntity.transform.rotation = _warpTargetRot;
            _camera.target = _playerEntity.transform.position;
            _warpPhase      = WarpPhase::FlyIn;
            _warpPhaseTimer = 0.0f;
        }
        break;
    }
    case WarpPhase::AwaitSync: {
        // Client-only: black screen until the host's WorldSync for the
        // destination arrives (net::Game().Poll() runs at the top of Update).
        _warpFadeAlpha = 1.0f;
        if (net::Game().pendingServerClosing || !net::Game().IsConnected()) {
            // Host vanished mid-warp — bail out the same way the main loop does.
            net::Game().pendingServerClosing = false;
            net::Game().Shutdown();
            GameManager::Get().TransitionTo(GameMode::MainMenu);
            _warpPhase = WarpPhase::None;
            _warpChainQueue.clear(); // abandon any remaining hops — bailing to the main menu
            break;
        }
        if (net::Game().pendingWorldSync.has_value()) {
            net::WorldSyncData ws = *net::Game().pendingWorldSync;
            net::Game().pendingWorldSync.reset();
            ApplyWorldSyncClient(ws);
            _warpParticles.clear();
            _warpFlyInStart = Vector2Subtract(_w->playerSpawnPos, Vector2Scale(_warpDir, kWarpArriveOffset));
            _playerEntity.transform.position = _warpFlyInStart;
            _playerEntity.transform.velocity = Vector2Scale(_warpDir, kWarpFlySpeed);
            _playerEntity.transform.rotation = _warpTargetRot;
            _camera.target = _playerEntity.transform.position;
            _warpPhase      = WarpPhase::FlyIn;
            _warpPhaseTimer = 0.0f;
        }
        break;
    }
    case WarpPhase::FlyIn: {
        float t     = std::min(_warpPhaseTimer / kWarpFlyInTime, 1.0f);
        float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // ease-out cubic
        _playerEntity.transform.position = Vector2Lerp(_warpFlyInStart, _w->playerSpawnPos, eased);
        _warpFadeAlpha = 1.0f - std::min(_warpPhaseTimer / kWarpFadeInRamp, 1.0f);
        _camera.target = _playerEntity.transform.position;
        if (t < 0.6f)
            for (int i = 0; i < 3; ++i)
                SpawnWarpParticle(_playerEntity.transform.position, _warpDir);
        if (t >= 1.0f) {
            _playerEntity.transform.position = _w->playerSpawnPos;
            _playerEntity.transform.velocity = { 0.0f, 0.0f };
            _camera.target  = _playerEntity.transform.position;
            _warpFadeAlpha  = 0.0f;
            _warpParticles.clear();
            if (!_warpChainQueue.empty()) {
                // Beacon chain: pop the next waypoint and re-trigger the full
                // TurnToFace->FlyOut->FadeOut->FlyIn cinematic from here —
                // BeginWarpSequence reads _currentSystemId (already updated by
                // CommitWarpWorldSwitch above) as the "from" point, so each
                // hop's turn direction is relative to the system just entered.
                unsigned int nextId = _warpChainQueue.front();
                _warpChainQueue.erase(_warpChainQueue.begin());
                BeginWarpSequence(nextId);
            } else {
                _warpPhase      = WarpPhase::None;
                _warpPhaseTimer = 0.0f;
            }
        }
        break;
    }
    case WarpPhase::LocalFly: {
        float t     = std::min(_warpPhaseTimer / kLocalWarpFlyTime, 1.0f);
        float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // ease-out cubic
        _playerEntity.transform.position = Vector2Lerp(_warpFlyInStart, _warpLocalTarget, eased);
        _camera.target = _playerEntity.transform.position;
        for (int i = 0; i < 3; ++i)
            SpawnWarpParticle(_playerEntity.transform.position, _warpDir);
        if (t >= 1.0f) {
            _playerEntity.transform.position = _warpLocalTarget;
            _playerEntity.transform.velocity = { 0.0f, 0.0f };
            _camera.target  = _playerEntity.transform.position;
            _warpParticles.clear();
            _warpPhase      = WarpPhase::None;
            _warpPhaseTimer = 0.0f;
        }
        break;
    }
    default: break;
    }
}

// World-space warp particle trail — must be drawn inside BeginMode2D/EndMode2D.
void SpaceFlight::DrawWarpParticles() const {
    for (const WarpParticle& p : _warpParticles) {
        float a = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        Vector2 tail = Vector2Subtract(p.pos, Vector2Scale(p.vel, 0.03f));
        Color   col  = { 160, 220, 255, (unsigned char)(a * 230.0f) };
        DrawLineEx(tail, p.pos, 2.0f, col);
    }
}

void SpaceFlight::Update(float dt) {
    net::Game().Poll(dt);   // pump ENet every frame (no-op when Offline)
    ReputationRegistry::Tick(dt); // Epic 6.2: decay every faction's player-standing score toward neutral
    TickActiveContract(dt);       // Epic 7: contract timers/quota/escort-alive checks

    // ── Warp cinematic: fully blocks player interaction until it completes ────
    if (_warpPhase != WarpPhase::None) {
        UpdateWarpSequence(dt);
        return;
    }

    // ── Host: peer lifecycle (joins, disconnects, warps) ──────────────────────
    if (net::Game().IsHost()) {
        // New peers join into the host's current system.
        for (uint32_t peerId : net::Game().newPeerIds) {
            _peerSystem[peerId] = _currentSystemId;
            net::Game().HostSendWorldSync(peerId, BuildWorldSync(*_w));
        }
        net::Game().newPeerIds.clear();

        // Hello arrives as a follow-up packet, usually a tick or more after
        // the ENet connect event that populated newPeerIds above — so a
        // peer's faction (and the faction-scoped discovery catch-up it
        // unlocks) is learned here, separately and slightly later.
        for (const auto& [peerId, faction] : net::Game().newPeerFactions) {
            _peerFaction[peerId] = faction;
            const std::vector<unsigned int>& pool =
                (faction == kPlayerFaction) ? _discoveredSystemIds
                                            : _peerFactionDiscovered[(uint8_t)faction];
            net::Game().HostSendDiscoverySync(peerId, pool);
        }
        net::Game().newPeerFactions.clear();

        // Disconnected peers: drop their state so their system can freeze.
        for (uint32_t peerId : net::Game().disconnectedPeerIds) {
            _remoteEntities.erase(peerId);
            _remoteFireCooldown.erase(peerId);
            _remoteJoinGrace.erase(peerId);
            _remoteDocked.erase(peerId);
            _peerSystem.erase(peerId);
            _peerFaction.erase(peerId);
        }
        net::Game().disconnectedPeerIds.clear();

        // Peers that warped: attach them to the destination world (spinning it
        // up from its seed on first visit) and send its live state.
        for (const auto& [peerId, sysId] : net::Game().pendingWarpNotifies) {
            if (!StarSystemRegistry::ById(sysId)) continue;
            SystemWorld& world = EnsureWorldGenerated(sysId);
            _peerSystem[peerId] = sysId;
            _remoteEntities.erase(peerId);    // re-created at the arrival position
            _remoteJoinGrace[peerId] = 5.0f;  // arrival grace, same as a fresh join
            net::Game().HostSendWorldSync(peerId, BuildWorldSync(world));

            // Discovery pooling (party = same faction): merge sysId into the
            // discovering peer's faction's shared set and tell every OTHER
            // connected peer sharing that faction — the peer that just
            // arrived already knows, so it's excluded from the broadcast.
            // Defaults to the host's own faction if Hello hasn't arrived yet
            // (rare race right after a fresh join); harmless either way since
            // the peer's real faction gets corrected the moment Hello lands.
            auto factionIt = _peerFaction.find(peerId);
            Faction pf = (factionIt != _peerFaction.end()) ? factionIt->second : kPlayerFaction;
            bool isHostFaction = (pf == kPlayerFaction);
            std::vector<unsigned int>& pool =
                isHostFaction ? _discoveredSystemIds : _peerFactionDiscovered[(uint8_t)pf];
            bool isNew = std::find(pool.begin(), pool.end(), sysId) == pool.end();
            if (isNew) pool.push_back(sysId);

            if (isNew) {
                std::vector<uint32_t> targets;
                for (const auto& [otherId, otherFaction] : _peerFaction)
                    if (otherId != peerId && otherFaction == pf) targets.push_back(otherId);
                if (!targets.empty())
                    net::Game().HostBroadcastSystemDiscovered(targets, sysId);
            }
        }
        net::Game().pendingWarpNotifies.clear();

        // Apply client input commands → update _remoteEntities and spawn
        // server-side projectiles into the sender's own system.
        for (auto& [id, fc] : _remoteFireCooldown) fc -= dt;
        for (auto& [id, g]  : _remoteJoinGrace)    g  -= dt;

        for (const auto& cmd : net::Game().pendingInputs) {
            if (cmd.networkId == 0) continue;
            auto& re = _remoteEntities[cmd.networkId];
            if (re.id == 0) {
                re.id                   = cmd.networkId;
                re.sprite.texture       = _playerShipTex;
                // Peer's real ship type isn't sent over the wire; render at the
                // host's own ship scale (same texture is used, tinted blue).
                const ecs::ShipDef* def = ecs::ShipRegistry::ShipById(_playerMeta.defId);
                re.sprite.scale         = def ? def->pixelScale : 1.0f;
                re.sprite.tint          = { 100, 200, 255, 255 };
                re.health.currentHull   = 100.0f;
                re.health.maxStats.hull = 100.0f;
                re.transform.position   = { cmd.posX, cmd.posY };
                _remoteJoinGrace[cmd.networkId] = 5.0f;  // invincible for 5 s at spawn
            }
            re.transform.position = { cmd.posX, cmd.posY };
            re.transform.rotation = cmd.aimRotation;
            re.network.networkId  = cmd.networkId;
            _remoteDocked[cmd.networkId] = (cmd.docked != 0);

            // Spawn a server-side projectile when the client fires — into the
            // world the firing client is actually in.
            if (cmd.firing && _remoteFireCooldown[cmd.networkId] <= 0.0f) {
                _remoteFireCooldown[cmd.networkId] = 0.25f;
                float fRad = (cmd.aimRotation - 90.0f) * DEG2RAD;
                Vector2 fireDir = { cosf(fRad), sinf(fRad) };
                Projectile p;
                p.position   = re.transform.position;
                p.velocity   = { fireDir.x * 700.0f, fireDir.y * 700.0f };
                p.lifetime   = 0.0f;
                p.maxLife    = 1000.0f / 700.0f;
                p.damage     = 10.0f;
                p.alive      = true;
                p.fromPlayer = true;
                p.ownerId    = cmd.networkId;
                auto sysIt = _peerSystem.find(cmd.networkId);
                SystemWorld& fireWorld = (sysIt != _peerSystem.end())
                    ? GetOrCreateWorld(sysIt->second) : *_w;
                fireWorld.projectiles.push_back(p);
            }
        }
        net::Game().pendingInputs.clear();

        // Drain client build/placement requests (Epic C, host-authoritative):
        // only the host ever creates the object; it then reaches every peer
        // through the normal Snapshot broadcast below. Credits/materials were
        // already validated+spent locally by the requesting client.
        for (const auto& req : net::Game().pendingBuildStationRequests) {
            // Player stations aren't system-tagged today (pre-existing gap,
            // see [[project-multiplayer]] known limitations) — SpawnStation
            // is already system-agnostic, so this matches existing behavior.
            FleetManager::Get().SpawnStation(req.stationDefId, { req.posX, req.posY });
        }
        net::Game().pendingBuildStationRequests.clear();

        for (const auto& req : net::Game().pendingPlaceShipRequests) {
            unsigned int sysId = _currentSystemId;
            auto sysIt = _peerSystem.find(req.requesterId);
            if (sysIt != _peerSystem.end()) sysId = sysIt->second;
            // The requesting CLIENT's own faction, not the host's kPlayerFaction —
            // see PlaceFriendlyShip's header comment. Falls back to the host's
            // own faction if the requester's Hello hasn't been processed yet
            // (shouldn't happen in practice — a peer must be connected and
            // past Hello to have sent a PlaceShipRequest at all).
            Faction placerFaction = kPlayerFaction;
            auto factionIt = _peerFaction.find(req.requesterId);
            if (factionIt != _peerFaction.end()) placerFaction = factionIt->second;
            PlaceFriendlyShip(GetOrCreateWorld(sysId), req.shipDefId, { req.posX, req.posY }, placerFaction);
        }
        net::Game().pendingPlaceShipRequests.clear();

        // Simulate every other occupied system; unoccupied worlds stay frozen
        // in memory (state preserved, no ticking) until someone returns. Worlds
        // left behind in a previously-visited galaxy are always skipped here —
        // _peerSystem only ever tracks systems within _currentGalaxyId, since a
        // cross-galaxy warp moves the whole party together.
        for (auto& [key, worldPtr] : _worlds) {
            if (worldPtr.get() == _w) continue;   // main Update path ticks this one
            if (WorldKeyGalaxyId(key) != _currentGalaxyId) continue;
            unsigned int sysId = WorldKeySystemId(key);
            bool occupied = false;
            for (const auto& [peerId, ps] : _peerSystem)
                if (ps == sysId) { occupied = true; break; }
            if (occupied) TickBackgroundWorld(dt, *worldPtr);
        }

        // ~20 Hz: send each occupied system's snapshot to exactly its occupants.
        _netTickAccum += dt;
        if (_netTickAccum >= 0.05f) {
            _netTickAccum = 0.0f;

            // Player-built stations aren't system-tagged today (pre-existing
            // gap — see [[project-multiplayer]] known limitations), so the
            // same full list is broadcast to every occupied system's
            // snapshot, matching how DrawPlayerStations() already shows them
            // regardless of the local player's current system.
            std::vector<net::PlayerStationSnapshot> stationSnaps;
            std::vector<net::PlayerStationHardpointSnapshot> stationHpSnaps;
            for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
                if (!ps.alive) continue;
                net::PlayerStationSnapshot ss;
                ss.id           = ps.id;
                ss.stationDefId = ps.stationDefId;
                ss.posX         = ps.position.x;
                ss.posY         = ps.position.y;
                ss.alive        = 1;
                stationSnaps.push_back(ss);
                for (size_t h = 0; h < ps.hardpoints.size(); ++h) {
                    const Hardpoint& hp = ps.hardpoints[h];
                    net::PlayerStationHardpointSnapshot shs;
                    shs.stationId = ps.id;
                    shs.hpIndex   = static_cast<uint8_t>(h);
                    shs.hull      = static_cast<uint16_t>(hp.hull);
                    shs.alive     = hp.alive ? 1 : 0;
                    stationHpSnaps.push_back(shs);
                }
            }

            for (auto& [key, worldPtr] : _worlds) {
                if (WorldKeyGalaxyId(key) != _currentGalaxyId) continue; // frozen, previously-visited galaxy
                unsigned int sysId = WorldKeySystemId(key);
                SystemWorld& world = *worldPtr;

                std::vector<uint32_t> occupants;
                for (const auto& [peerId, ps] : _peerSystem)
                    if (ps == sysId) occupants.push_back(peerId);
                if (occupants.empty()) continue;

                std::vector<ecs::Entity> broadcastList;
                broadcastList.reserve(world.entities.size() + 1 + _remoteEntities.size());
                for (size_t i = 0; i < world.entities.size(); ++i) {
                    if (!world.npcMeta[i].alive) continue;
                    ecs::Entity npcCopy = world.entities[i];
                    npcCopy.network.shipNameHash = Fnv1a32(world.npcMeta[i].shipTypeId);
                    broadcastList.push_back(npcCopy);
                }
                // P8-T1: per-mount fighter loadout rows — only PLAYER ships
                // need these. NPC fighter loadouts are deterministic from the
                // shared world seed (both peers roll identically at spawn)
                // and never change afterward (no per-hardpoint combat damage
                // exists for fighters), so clients already render them
                // correctly with zero sync, same reasoning Epic C already
                // established for random NPC capitals' initial fit.
                std::vector<net::FighterHardpointSnapshot> fighterHpSnaps;
                auto addFighterRows = [&](uint32_t netId, const std::vector<uint8_t>& mounts) {
                    for (size_t i = 0; i < mounts.size() && i < 255; ++i)
                        fighterHpSnaps.push_back({ netId, static_cast<uint8_t>(i), mounts[i] });
                };
                // Docked (in a station menu) players are omitted from the
                // broadcast entirely — clients evict anything absent from a
                // snapshot, so this is how a docked ship disappears for peers.
                if (sysId == _currentSystemId && !_playerDead && !_stationServicesMenu.isOpen) {
                    ecs::Entity pCopy = _playerEntity;
                    pCopy.network.isLocalPlayer = false;
                    broadcastList.push_back(pCopy);
                    addFighterRows(pCopy.network.networkId, EncodeLoadoutMounts(_loadout));
                }
                // Remote entities in this system, so its occupants see each other.
                for (const auto& [netId, re] : _remoteEntities) {
                    if (re.id == 0 || _remoteDocked[netId]) continue;
                    auto it = _peerSystem.find(netId);
                    if (it != _peerSystem.end() && it->second == sysId) {
                        broadcastList.push_back(re);
                        // netId < 1000 is a player (NPCs start at 1000, see
                        // the id-space convention used elsewhere in this
                        // file, e.g. the client's own snap.networkId < 1000
                        // branch) — only players relay a loadout report.
                        if (netId < 1000) {
                            auto lit = net::Game().peerFighterLoadouts.find(netId);
                            if (lit != net::Game().peerFighterLoadouts.end())
                                addFighterRows(netId, lit->second);
                        }
                    }
                }

                std::vector<net::AsteroidSnapshot> asteroidSnaps;
                asteroidSnaps.reserve(world.asteroids.size());
                for (const auto& a : world.asteroids) {
                    if (!a.alive) continue;
                    net::AsteroidSnapshot as;
                    as.id       = a.id;
                    as.posX     = a.position.x;
                    as.posY     = a.position.y;
                    as.velX     = a.velocity.x;
                    as.velY     = a.velocity.y;
                    as.rotation = a.rotation;
                    as.health   = static_cast<int8_t>(a.health);
                    as.tier     = static_cast<int8_t>(a.tier);
                    asteroidSnaps.push_back(as);
                }

                std::vector<net::ProjectileSnapshot> projSnaps;
                projSnaps.reserve(world.projectiles.size());
                for (const Projectile& p : world.projectiles) {
                    if (!p.alive) continue;
                    net::ProjectileSnapshot ps;
                    ps.posX = p.position.x;
                    ps.posY = p.position.y;
                    ps.velX = p.velocity.x;
                    ps.velY = p.velocity.y;
                    projSnaps.push_back(ps);
                }

                std::vector<net::CapitalHardpointSnapshot> capSnaps;
                for (size_t i = 0; i < world.npcMeta.size(); ++i) {
                    const NpcMeta& m = world.npcMeta[i];
                    if (!m.alive || m.hardpoints.empty()) continue;
                    for (size_t h = 0; h < m.hardpoints.size(); ++h) {
                        const Hardpoint& hp = m.hardpoints[h];
                        net::CapitalHardpointSnapshot cs;
                        cs.capitalId = m.id;
                        cs.hpIndex   = static_cast<uint8_t>(h);
                        cs.hull      = static_cast<uint16_t>(hp.hull);
                        cs.alive     = hp.alive ? 1 : 0;
                        capSnaps.push_back(cs);
                    }
                }

                net::Game().HostSendSnapshot(occupants, sysId,
                                             broadcastList, asteroidSnaps, projSnaps, capSnaps,
                                             stationSnaps, stationHpSnaps, fighterHpSnaps);
            }
        }
    }

    // ── Client: handle server-push events ────────────────────────────────────
    if (net::Game().IsClient()) {
        if (net::Game().pendingServerClosing) {
            net::Game().pendingServerClosing = false;
            net::Game().Shutdown();
            GameManager::Get().TransitionTo(GameMode::MainMenu);
            return;
        }
        if (net::Game().pendingPlayerDead) {
            net::Game().pendingPlayerDead = false;
            _playerEntity.health.currentHull = 0.0f;
        }
        for (const auto& [sysId, deadId] : net::Game().pendingStationDeads) {
            if (sysId != _currentSystemId) continue;  // other systems sync on arrival
            for (auto& st : _w->stations)
                if (st.id == deadId) {
                    st.alive        = false;
                    st.rebuilding   = true;
                    st.rebuildTimer = kStationRebuildSeconds;
                    break;
                }
        }
        net::Game().pendingStationDeads.clear();

        // Discovery pooling: merge in whatever the host says our faction's
        // party has already found — a one-time bulk catch-up (DiscoverySync,
        // sent right after our Hello reveals our faction) plus any live
        // SystemDiscovered updates from faction-mates warping since then.
        if (net::Game().pendingDiscoverySync.has_value()) {
            for (unsigned int id : *net::Game().pendingDiscoverySync)
                if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), id)
                        == _discoveredSystemIds.end())
                    _discoveredSystemIds.push_back(id);
            net::Game().pendingDiscoverySync.reset();
        }
        for (unsigned int id : net::Game().pendingSystemDiscoveries) {
            if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), id)
                    == _discoveredSystemIds.end())
                _discoveredSystemIds.push_back(id);
        }
        net::Game().pendingSystemDiscoveries.clear();
    }

    // ── Client: apply WorldSync then lerp remote snapshots ────────────────────
    if (net::Game().IsClient()) {
        if (net::Game().pendingWorldSync.has_value()) {
            auto ws = *net::Game().pendingWorldSync;
            net::Game().pendingWorldSync.reset();
            // NPC positions come from server snapshots; don't simulate them locally.
            ApplyWorldSyncClient(ws);
            _playerEntity.transform.position = _w->playerSpawnPos;
            _camera.target = _w->playerSpawnPos;
        }

        // On each new snapshot: correct entity positions, sync asteroids, evict stale.
        uint32_t localId = net::Game().LocalNetworkId();
        // Drop snapshots that describe a system we're not in — packets for the
        // previous system can still be in flight right after a warp.
        if (net::Game().snapshotDirty &&
            (!_worldSynced || net::Game().latestSnapshotSystemId != _currentSystemId)) {
            net::Game().snapshotDirty = false;   // consume and discard
        }
        if (net::Game().snapshotDirty) {
            net::Game().snapshotDirty = false;

            // ── Remote entities: snap toward server position, take server velocity ──
            for (const auto& snap : net::Game().latestSnapshots) {
                if (snap.networkId == localId || snap.networkId == 0) continue;
                auto& re = _remoteEntities[snap.networkId];
                if (re.id == 0) {
                    re.id    = snap.networkId;
                    re.sprite.scale = 1.0f;
                    if (snap.networkId < 1000) {
                        // Remote player — draw with local ship texture tinted blue.
                        const ecs::ShipDef* def = ecs::ShipRegistry::ShipById(_playerMeta.defId);
                        re.sprite.texture = _playerShipTex;
                        re.sprite.tint    = { 100, 200, 255, 255 };
                        re.sprite.scale   = def ? def->pixelScale : 1.0f;
                        // P8-T1: mount layout mirrors the LOCAL player's own
                        // current HardpointRig shape, matching the hull-skin
                        // convention right above (remote players already
                        // render with our own ship texture, not their real
                        // one) — see _remoteFighterMounts' declaration.
                        HardpointRig rig;
                        rig.Resize(_playerMeta.weaponSlots, _playerMeta.shieldSlots, _playerMeta.auxSlots);
                        _remoteFighterMounts[snap.networkId] = std::move(rig.hardpoints);
                    } else if (snap.shipNameHash == kGargosShipHash) {
                        re.sprite.texture = const_cast<Texture2D*>(&_gargosTex);
                        re.sprite.tint    = WHITE;
                        re.sprite.scale   = 1.0f;
                    } else if (const ecs::ShipDef* def = ResolveShipDefByHash(snap.shipNameHash)) {
                        // NPC — resolve its real sprite from the type hash carried in the snapshot.
                        re.sprite.texture = ResourceManager::Load(def->assetPath);
                        re.sprite.tint    = WHITE;
                        re.sprite.scale   = def->pixelScale;
                        if (def->shipType == ShipType::Capital) {
                            RemoteCapitalInfo info;
                            info.hardpoints = BuildCapitalHardpoints(*def);
                            info.radius     = def->radius;
                            Faction capFaction = FactionFromPaletteId(def->paletteId);
                            info.faction = RelationToNpcFaction(ReputationRegistry::PlayerRelation(capFaction));
                            _remoteCapitalHardpoints[snap.networkId] = std::move(info);
                        }
                    } else {
                        // Unknown/unmatched ship type — fall back to a red circle.
                        re.sprite.texture = nullptr;
                        re.sprite.tint    = { 230, 90, 70, 240 };
                    }
                    re.transform.position = snap.position;
                } else {
                    // Blend 50% toward the authoritative position to correct dead-reckoning drift.
                    re.transform.position.x += (snap.position.x - re.transform.position.x) * 0.5f;
                    re.transform.position.y += (snap.position.y - re.transform.position.y) * 0.5f;
                }
                re.transform.velocity = snap.velocity;
                re.transform.rotation = snap.rotation;
            }

            // ── Asteroid sync: update existing, create new, evict destroyed ──────
            for (const auto& as : net::Game().latestAsteroidSnapshots) {
                Asteroid* found = nullptr;
                for (auto& a : _w->asteroids) { if (a.id == as.id) { found = &a; break; } }
                if (found) {
                    found->position = { as.posX, as.posY };
                    found->velocity = { as.velX, as.velY };
                    found->rotation = as.rotation;
                    found->health   = static_cast<int>(as.health);
                    found->alive    = true;
                } else {
                    Asteroid na;
                    na.id       = as.id;
                    na.position = { as.posX, as.posY };
                    na.velocity = { as.velX, as.velY };
                    na.rotation = as.rotation;
                    na.health   = static_cast<int>(as.health);
                    na.tier     = static_cast<int>(as.tier);
                    na.radius   = AsteroidRadius(na.tier);
                    na.alive    = true;
                    _w->asteroids.push_back(na);
                }
            }
            // Kill asteroids absent from this snapshot (destroyed on server).
            for (auto& a : _w->asteroids) {
                if (!a.alive) continue;
                bool inSnap = false;
                for (const auto& as : net::Game().latestAsteroidSnapshots)
                    if (as.id == a.id) { inSnap = true; break; }
                if (!inSnap) a.alive = false;
            }

            // ── Evict remote entities absent from this snapshot ──────────────────
            for (auto it = _remoteEntities.begin(); it != _remoteEntities.end(); ) {
                bool inSnap = false;
                for (const auto& snap : net::Game().latestSnapshots)
                    if (snap.networkId == it->first) { inSnap = true; break; }
                it = inSnap ? std::next(it) : _remoteEntities.erase(it);
            }
            for (auto it = _remoteCapitalHardpoints.begin(); it != _remoteCapitalHardpoints.end(); ) {
                it = _remoteEntities.count(it->first) ? std::next(it) : _remoteCapitalHardpoints.erase(it);
            }
            for (auto it = _remoteFighterMounts.begin(); it != _remoteFighterMounts.end(); ) {
                it = _remoteEntities.count(it->first) ? std::next(it) : _remoteFighterMounts.erase(it);
            }

            // ── Capital hardpoint sync: patch hull/alive on remote capitals ──────
            for (const auto& cs : net::Game().latestCapitalSnapshots) {
                auto it = _remoteCapitalHardpoints.find(cs.capitalId);
                if (it == _remoteCapitalHardpoints.end()) continue;
                auto& hardpoints = it->second.hardpoints;
                if (cs.hpIndex < hardpoints.size()) {
                    hardpoints[cs.hpIndex].hull  = cs.hull;
                    hardpoints[cs.hpIndex].alive = (cs.alive != 0);
                }
            }

            // ── P8-T1: fighter loadout sync — patch equipped-module type on
            // remote players' mount rig. moduleType 0 = empty mount.
            for (const auto& fs : net::Game().latestFighterHardpointSnapshots) {
                auto it = _remoteFighterMounts.find(fs.networkId);
                if (it == _remoteFighterMounts.end()) continue;
                auto& hardpoints = it->second;
                if (fs.hpIndex >= hardpoints.size()) continue;
                Hardpoint& hp = hardpoints[fs.hpIndex];
                if (hp.slots.empty()) continue;
                if (fs.moduleType == 0) {
                    hp.slots[0].equipped = std::nullopt;
                } else {
                    ModuleDef md;
                    md.type = static_cast<ModuleType>(fs.moduleType - 1);
                    hp.slots[0].equipped = md;
                }
            }

            // ── Projectile sync: replace list from server snapshot ─────────────
            _remoteProjectiles = net::Game().latestProjectileSnapshots;

            // ── Player-station sync: create/update remote stations, evict gone ones,
            // patch hardpoint hull/alive (Epic C MP sync, [[tasks-multiplayer]]) ──
            for (const auto& ss : net::Game().latestPlayerStationSnapshots) {
                PlayerStation& rs = _remotePlayerStations[ss.id];
                rs.id       = ss.id;
                rs.stationDefId = ss.stationDefId;
                rs.position = { ss.posX, ss.posY };
                rs.alive    = (ss.alive != 0);
                if (rs.displayName.empty()) {
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ss.stationDefId);
                    rs.displayName = def ? def->displayName : ss.stationDefId;
                }
                if (rs.hardpoints.empty()) {
                    if (const PlayerStationDef* def = PlayerStationRegistry::ById(ss.stationDefId)) {
                        for (const StationHardpointDef& hd : def->hardpoints) {
                            Hardpoint hp;
                            hp.id          = hd.id;
                            hp.displayName = hd.displayName;
                            hp.isCore      = hd.isCore;
                            hp.maxHull     = hd.maxHull;
                            hp.hull        = hd.maxHull;
                            rs.hardpoints.push_back(hp);
                        }
                    }
                }
            }
            for (auto it = _remotePlayerStations.begin(); it != _remotePlayerStations.end(); ) {
                bool inSnap = false;
                for (const auto& ss : net::Game().latestPlayerStationSnapshots)
                    if (ss.id == it->first) { inSnap = true; break; }
                it = inSnap ? std::next(it) : _remotePlayerStations.erase(it);
            }
            for (const auto& shs : net::Game().latestPlayerStationHardpointSnapshots) {
                auto it = _remotePlayerStations.find(shs.stationId);
                if (it == _remotePlayerStations.end()) continue;
                auto& hardpoints = it->second.hardpoints;
                if (shs.hpIndex < hardpoints.size()) {
                    hardpoints[shs.hpIndex].hull  = shs.hull;
                    hardpoints[shs.hpIndex].alive = (shs.alive != 0);
                }
            }
        }

        // ── Dead reckoning: advance remote entity and projectile positions ───────
        for (auto& [id, re] : _remoteEntities) {
            re.transform.position.x += re.transform.velocity.x * dt;
            re.transform.position.y += re.transform.velocity.y * dt;
        }
        for (auto& rp : _remoteProjectiles) {
            rp.posX += rp.velX * dt;
            rp.posY += rp.velY * dt;
        }
    }

    // Block all game logic until the client has received the world from the host.
    if (!_worldSynced) return;

    // ── DEBUG CHEAT: Press F8 to get Mining Station Materials ───────────────────
    if (IsKeyPressed(KEY_F8)) {
        // Local helper function mimicking BuildMenu's AddToStorage logic
        auto addDebugMaterial = [this](const std::string& id, const std::string& name, int amount) {
            // 1. Try to find an existing slot to stack onto
            for (StorageItem& slot : _storageMenu.slots) {
                if (slot.type == StorageItemType::Material && slot.materialId == id) {
                    slot.count = std::min(slot.count + amount, StorageMenu::MaxStack);
                    return;
                }
            }
            // 2. Otherwise, find a clean empty slot
            for (StorageItem& slot : _storageMenu.slots) {
                if (slot.type == StorageItemType::Empty) {
                    slot.type = StorageItemType::Material;
                    slot.materialId = id;
                    slot.displayName = name;
                    slot.count = amount;
                    return;
                }
            }
            };

        // Inject the exact recipe criteria required by BuildableRegistry
        addDebugMaterial("hull_frame", "Hull Frame", 8);
        addDebugMaterial("circuit_board", "Circuit Board", 4);
        addDebugMaterial("titanium_alloy", "Titanium Alloy", 3);
        addDebugMaterial("weapons_rack", "Weapons Rack", 5);
        addDebugMaterial("power_cell", "Power Cell", 5);
        InventoryManager::Get().AddCredits(50000);

        // Flash a status verification onto the UI logging feed
        AddCommsMessage("DEBUG: Added Mining Station materials and 50,000 credits to storage.", true);
    }

    if (_playerEntity.health.currentHull <= 0.0f) {
        _playerDead = true;
        _deathTimer += dt;
        static constexpr float kTypeDelay  = 0.5f;
        static constexpr float kTypeSpeed  = 12.0f;
        static constexpr int   kMsgLen     = 13;   // "YOU PERISHED!"
        static constexpr float kBtnDelay   = kTypeDelay + kMsgLen / kTypeSpeed + 0.4f;
        if (_deathTimer >= kBtnDelay && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int sw = GetScreenWidth(), sh = GetScreenHeight();
            Rectangle respawnBtn = { (float)(sw / 2 - 110), (float)(sh / 2 + 52),  220.0f, 44.0f };
            Rectangle menuBtn    = { (float)(sw / 2 - 110), (float)(sh / 2 + 104), 220.0f, 44.0f };
            if (CheckCollisionPointRec(GetMousePosition(), respawnBtn)) {
                _playerDead = false;
                _deathTimer = 0.0f;
                for (auto* w : _loadout.WeaponSlots()) w->equipped = std::nullopt;
                for (auto* s : _loadout.ShieldSlots()) s->equipped = std::nullopt;
                for (auto* a : _loadout.AuxSlots())    a->equipped = std::nullopt;
                if (auto* a = _loadout.Armor())      a->equipped = Armor_HullPatch();
                if (auto* e = _loadout.Engine())     e->equipped = Engine_Thruster_I();
                if (auto* h = _loadout.Hyperdrive()) h->equipped = std::nullopt;
                { auto v = _loadout.WeaponSlots(); if (!v.empty()) v[0]->equipped = Weapon_PulseCannon_I(); }
                ApplyLoadout();
                // When the player dies and needs to respawn:
                float spawnDist = _w->sun.gravRange + 800.0f;

                // 1. Calculate a brand new safe coordinate based on the CURRENT world state
                _w->playerSpawnPos = GetSafeSpawnPosition(_w, spawnDist, kEnemySpawnMargin);

                // 2. Teleport the player to the newly secured location
                _playerEntity.transform.position = _w->playerSpawnPos;

                _playerEntity.health.currentHull = _playerEntity.health.maxStats.hull;
                _playerEntity.transform.velocity = { 0.0f, 0.0f };
                _camera.target = _w->playerSpawnPos;
                HideCursor();
            } else if (CheckCollisionPointRec(GetMousePosition(), menuBtn)) {
                if (net::Game().IsHost()) net::Game().BroadcastServerClosing();
                net::Game().Shutdown();
                GameManager::Get().TransitionTo(GameMode::MainMenu);
            }
        }
        return;
    }

    // ── ADDED: INPUT GUARD SUPPRESSION ───────────────────────────────────────
    static bool blockFireUntilRelease = false;

    if (_storageMenu.isOpen || _modulesMenu.isOpen || _galaxyMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _commsLogOpen || _enterPopupOpen || _localMapOpen ||
        _buildMenu.isOpen || _stationModMenu.isOpen || _miningMenu.isOpen || _placementConfirmOpen ||
        _stationServicesMenu.isOpen) {
        blockFireUntilRelease = true;
    }

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        blockFireUntilRelease = false;
    }
    // ─────────────────────────────────────────────────────────────────────────

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (_enterPopupOpen) {
            _enterPopupOpen = false;
        }
        else if (_stationServicesMenu.isOpen) {
            _stationServicesMenu.Close();
        }
        else if (_buildMenu.isOpen) {
            _buildMenu.Close();
        }
        else if (_stationModMenu.isOpen) {
            _stationModMenu.Close();
        }
        else if (_miningMenu.isOpen) {
            _miningMenu.Close();
        }
        else if (_inPlacementMode) {
            _inPlacementMode = false;
            _placingStationDefId.clear();
        }
        else if (_placementConfirmOpen) {
            _placementConfirmOpen = false;
        }
        else if (_commsMenuOpen) {
            _commsMenuOpen = false;
        }
        else if (_ranksMenuOpen) {
            _ranksMenuOpen = false;
        }
        else if (_escortMenuOpen) {
            _escortMenuOpen = false;
        }
        else if (_commsLogOpen) {
            _commsLogOpen = false;
        }
        else if (_localMapOpen) {
            _localMapOpen = false;
        }
        else if (_galaxyMap.isOpen) {
            if (!_galaxyMap.IsPickerOpen()) _galaxyMap.Close();
        }
        else {
            _galaxyMap.Open();
        }
    }

    // Epic 12.1/12.2: LOG toggles the received-comms panel; SKIP TUTORIAL
    // ends the tutorial early. Both no-op while some other menu already has
    // input (L still closes the log panel itself, per the toggle).
    {
        bool otherMenuOpen = _storageMenu.isOpen || _modulesMenu.isOpen || _galaxyMap.isOpen ||
            _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _enterPopupOpen || _localMapOpen ||
            _buildMenu.isOpen || _stationModMenu.isOpen || _miningMenu.isOpen || _placementConfirmOpen ||
            _stationServicesMenu.isOpen;
        if (IsKeyPressed(KEY_L) && (!otherMenuOpen || _commsLogOpen)) {
            _commsLogOpen = !_commsLogOpen;
        }
        if (IsKeyPressed(KEY_T) && _tutorialActive && !otherMenuOpen && !_commsLogOpen) {
            SkipTutorial();
        }
    }

    if (_localMapOpen) {
        Rectangle backBtn = { 18.0f, 16.0f, 120.0f, 38.0f };
        if (CheckCollisionPointRec(GetMousePosition(), backBtn) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            _localMapOpen = false;
        }
        return;
    }

    if (_galaxyMap.isOpen) {
        // Feed current world + galaxy data to the map
        {
            SystemMapData mapData;
            mapData.playerPos       = _playerEntity.transform.position;
            mapData.hyperdriveRange = _hyperdriveRange;
            mapData.fuel            = _fuel;
            mapData.maxFuel         = kMaxFuel;
            mapData.mapSensorRange  = _mapSensorRange;
            mapData.mapSensorTier   = _mapSensorTier;
            for (const NpcMeta& n : _w->npcMeta) if (n.alive && n.wingman) mapData.wingmanCount++;
            for (const SpacePlanet& p : _w->planets) {
                bool disc = std::find(_discoveredIds.begin(), _discoveredIds.end(), p.id) != _discoveredIds.end();
                mapData.blips.push_back({ p.id, p.position, p.radius, true, disc });
            }
            for (const SpaceStation& s : _w->stations) {
                if (!s.alive) continue;
                bool disc = std::find(_discoveredIds.begin(), _discoveredIds.end(), s.id) != _discoveredIds.end();
                mapData.blips.push_back({ s.id, s.position, s.radius, false, disc });
            }
            // Player-built stations (space stations, mining stations, defense
            // platforms, ...) — always "discovered" since the player built them.
            for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
                if (!ps.alive) continue;
                const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                float rad = def ? def->radius : 120.0f;
                mapData.blips.push_back({ ps.id, ps.position, rad, false, true });
            }
            mapData.hasSun  = _w->sun.active;
            mapData.sunCore = _w->sun.coreColor;
            mapData.sunGlow = _w->sun.outerGlow;
            mapData.currentSystemId     = _currentSystemId;
            mapData.discoveredSystemIds = _discoveredSystemIds;
            mapData.currentGalaxyId     = _currentGalaxyId;
            mapData.visitedGalaxyIds    = _visitedGalaxyIds;
            auto curSys = StarSystemRegistry::ById(_currentSystemId);
            mapData.currentSystemPos = curSys ? curSys->galacticPos : Vector2{};
            _galaxyMap.SetMapData(mapData);
        }

        MapAction action = _galaxyMap.Update(dt);

        if (action == MapAction::WarpTo) {
            _galaxyMap.Close();
            BeginLocalWarp(_galaxyMap.WarpTarget());
            return;
        }
        if (action == MapAction::WarpToSystem) {
            unsigned int targetId = _galaxyMap.WarpTargetId();
            std::vector<unsigned int> chain = _galaxyMap.WarpChain();
            _galaxyMap.Close();
            if (StarSystemRegistry::ById(targetId)) {
                // chain.size() > 1 is a beacon-chained warp (see GalaxyMap::
                // ComputeChainPath): hop through the intermediate discovered
                // systems first, playing a full cinematic per hop, then arrive
                // at targetId. A direct in-range warp reports a 1-element (or
                // empty, for older callers) chain, so this is a no-op then.
                _warpChainQueue.assign(chain.size() > 1 ? chain.begin() + 1 : chain.end(),
                                        chain.end());
                BeginWarpSequence(chain.empty() ? targetId : chain.front());
            }
            return;
        }
        if (action == MapAction::WarpToGalaxy) {
            unsigned int targetGalaxyId = _galaxyMap.WarpTargetGalaxyId();
            _galaxyMap.Close();
            // StarSystemRegistry/_currentGalaxyId is per-process global state, not
            // per-peer — a client switching it would only affect their own view (never
            // the host's), so clients simply can't initiate this. A host CAN switch it
            // safely only when nobody else is connected: any peer currently tracked in
            // _peerSystem lives in a system keyed to the galaxy being left behind, and
            // once _currentGalaxyId moves on, this file's per-tick loops stop
            // ticking/syncing that (now foreign-galaxy) world, silently stranding them.
            // Full multiplayer support (forcing connected peers to follow, or handing
            // off) is a bigger follow-on piece, not part of this phase.
            bool otherPeersConnected = net::Game().IsHost() && !_peerSystem.empty();
            if (targetGalaxyId != 0 && targetGalaxyId != _currentGalaxyId &&
                !net::Game().IsClient() && !otherPeersConnected)
                BeginGalaxyWarpSequence(targetGalaxyId);  // cinematic; actual switch happens mid-sequence
            return;
        }
        if (action == MapAction::OpenModules) {
            _galaxyMap.Close();
            _modulesMenu.Open(&_loadout, &_storageMenu.slots,
                _playerMeta.weaponSlots, _playerMeta.armorSlots,
                _playerMeta.shieldSlots, _playerMeta.engineSlots,
                _playerMeta.hyperdriveSlots, _playerMeta.auxSlots,
                &_playerEntity.health);
            return;
        }
        if (action == MapAction::OpenStorage) {
            _galaxyMap.Close();
            _storageMenu.Open((int)_storageMenu.slots.size());
            return;
        }
        if (action == MapAction::OpenEscorts) {
            int wingCount = 0;
            for (const NpcMeta& n : _w->npcMeta) if (n.alive && n.wingman) wingCount++;
            if (wingCount > 0) {
                _galaxyMap.Close();
                _escortMenuOpen = true;
                _escortMenuSelId = 0;
                for (const NpcMeta& n : _w->npcMeta)
                    if (n.alive && n.wingman) { _escortMenuSelId = n.id; break; }
            }
            return;
        }
        if (action == MapAction::OpenRanks) {
            _galaxyMap.Close();
            _ranksMenuOpen = true;
            return;
        }
        if (action == MapAction::GoMainMenu) {
            if (net::Game().IsHost()) net::Game().BroadcastServerClosing();
            net::Game().Shutdown();
            GameManager::Get().TransitionTo(GameMode::MainMenu);
            return;
        }
        if (action == MapAction::SaveToFile) {
            auto gs = BuildWorldState();
            SaveManager::Get().SaveGameToPath(gs,
                _galaxyMap.SavePath(), _galaxyMap.SaveDisplayName());
            _galaxyMap.AckSave();
        }
        if (action == MapAction::LoadGame) {
            SaveManager::GameState gs;
            if (SaveManager::Get().LoadGame(_galaxyMap.LoadFilename(), gs)) {
                if (gs.shipTypeId != _playerMeta.defId) {
                    if (const auto* defPtr = ecs::ShipRegistry::ShipById(gs.shipTypeId)) {
                        const ecs::ShipDef& def    = *defPtr;
                        _playerMeta.defId           = def.id;
                        _playerMeta.displayName     = def.displayName;
                        _playerMeta.radius          = def.radius;
                        _playerMeta.shipType        = def.shipType;
                        _playerMeta.weaponSlots     = def.weaponSlots;
                        _playerMeta.armorSlots      = def.armorSlots;
                        _playerMeta.shieldSlots     = def.shieldSlots;
                        _playerMeta.engineSlots     = def.engineSlots;
                        _playerMeta.hyperdriveSlots = def.hyperdriveSlots;
                        _playerMeta.auxSlots        = def.auxSlots;
                        _playerEntity.health.maxStats.hull = def.baseStats.hull;
                        _playerEntity.transform.radius     = def.radius;
                    }
                }
                _playerEntity.transform.position = { gs.posX, gs.posY };
                _playerEntity.transform.velocity = { gs.velX, gs.velY };
                _playerEntity.transform.rotation = gs.rotation;
                _camera.target = _playerEntity.transform.position;
                _w->projectiles.clear();
                _hitCooldown = 0.0f;
                _target = TargetInfo{};
                _targetId = 0;
                auto& cfg = FleetManager::Get().PlayerShip;
                cfg.HullIntegrity = gs.hull;
                cfg.MaxHull       = gs.maxHull;
                cfg.ShipTypeId    = gs.shipTypeId;

                // Restore loadout
                _loadout.Resize(_playerMeta.weaponSlots, _playerMeta.shieldSlots, _playerMeta.auxSlots);
                _discoveredIds.clear();
                _discoveredSystemIds.clear();
                _currentSystemId = 1;
                _currentGalaxyId = 1;
                _visitedGalaxyIds.assign({ 1 });
                _gameSeed        = gs.gameSeed != 0 ? gs.gameSeed : 1u;
                UniverseRegistry::Init(_gameSeed);
                if (gs.hasWorldState) _currentGalaxyId = gs.currentGalaxyId != 0 ? gs.currentGalaxyId : 1u;
                StarSystemRegistry::Init(UniverseRegistry::Generate(_currentGalaxyId).seed);
                if (gs.hasWorldState && !gs.engineId.empty()) {
                    { auto v = _loadout.WeaponSlots(); for (int i = 0; i < (int)v.size() && i < (int)gs.weaponIds.size(); ++i) v[i]->equipped = ModuleById(gs.weaponIds[i]); }
                    if (auto* a = _loadout.Armor())      a->equipped = ModuleById(gs.armorId);
                    if (auto* e = _loadout.Engine())     e->equipped = ModuleById(gs.engineId);
                    if (auto* h = _loadout.Hyperdrive()) h->equipped = ModuleById(gs.hyperdriveId);
                    { auto v = _loadout.ShieldSlots(); for (int i = 0; i < (int)v.size() && i < (int)gs.shieldIds.size(); ++i) v[i]->equipped = ModuleById(gs.shieldIds[i]); }
                    { auto v = _loadout.AuxSlots(); for (int i = 0; i < (int)v.size() && i < (int)gs.auxIds.size(); ++i) v[i]->equipped = ModuleById(gs.auxIds[i]); }
                    _discoveredIds       = gs.discoveredIds;
                    _currentSystemId     = gs.currentSystemId;
                    _discoveredSystemIds = gs.discoveredSystemIds;
                    if (!gs.visitedGalaxyIds.empty()) _visitedGalaxyIds = gs.visitedGalaxyIds;
                }
                if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), _currentSystemId)
                        == _discoveredSystemIds.end())
                    _discoveredSystemIds.push_back(_currentSystemId);
                if (std::find(_visitedGalaxyIds.begin(), _visitedGalaxyIds.end(), _currentGalaxyId)
                        == _visitedGalaxyIds.end())
                    _visitedGalaxyIds.push_back(_currentGalaxyId);
                // Loading replaces the whole world set; re-key to the saved system.
                _worlds.clear();
                _w = &GetOrCreateWorld(_currentSystemId);
                ApplyLoadout();
                // Re-apply saved hull values (ApplyLoadout resets maxHull from def+armor)
                _playerEntity.health.currentHull   = gs.hull;
                _playerEntity.health.maxStats.hull  = gs.maxHull;

                // Restore storage
                if (gs.hasWorldState && !gs.storage.empty()) {
                    int slotCount = (int)std::min(gs.storage.size(), _storageMenu.slots.size());
                    for (int i = 0; i < slotCount; ++i) {
                        const auto& ss = gs.storage[i];
                        StorageItem& slot = _storageMenu.slots[i];
                        slot.type        = static_cast<StorageItemType>(ss.type);
                        slot.displayName = ss.displayName;
                        slot.materialId  = ss.materialId;
                        slot.count       = ss.count;
                        if (slot.type == StorageItemType::Module) {
                            auto mod = ModuleById(ss.moduleId);
                            slot.module = mod ? *mod : ModuleDef{};
                        }
                    }
                }

                // Restore or spawn world entities
                _w->sun = SpaceSun{};
                if (gs.hasWorldState) {
                    ApplyWorldState(gs);
                    if (!gs.sunTypeId.empty()) {
                        const StarTypeDef* def = StarRegistry::ById(gs.sunTypeId);
                        if (def) {
                            float savedR     = (gs.sunRadius > 0.f) ? gs.sunRadius
                                                                     : (def->minRadius + def->maxRadius) * 0.5f;
                            _w->sun.typeId      = def->id;
                            _w->sun.radius      = savedR;
                            _w->sun.gravRange   = savedR * def->gravRangeMult;
                            _w->sun.gravStrength= def->gravStrength;
                            _w->sun.coreColor   = def->coreColor;
                            _w->sun.innerGlow   = def->innerGlowColor;
                            _w->sun.outerGlow   = def->outerGlowColor;
                            _w->sun.active      = true;
                            BakeSunCorona();
                        }
                    }
                }
                else {
                    _w->asteroids.clear();
                    _w->entities.clear();
                    _w->npcMeta.clear();
                    _w->lootDrops.clear();
                    _w->materialDrops.clear();
                    _w->derelictWrecks.clear();
                    _w->hasActiveDistress = false;
                    auto homeSys = StarSystemRegistry::ById(_currentSystemId);
                    SpawnPlanetsAndStations(homeSys ? homeSys->seed : 0);
                    SpawnInitialAsteroids();
                    SpawnNpcShips();
                    _playerEntity.transform.position = _w->playerSpawnPos;
                    _camera.target = _playerEntity.transform.position;
                }
                InitStars();
            }
            _galaxyMap.Close();
        }
        return;
    }

    if (_enterPopupOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PopH = 130;
        int py2 = sh2 / 2 - PopH / 2;
        Rectangle okBtn = { (float)(sw2 / 2 - 60), (float)(py2 + PopH - 46), 120.0f, 32.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), okBtn))
            _enterPopupOpen = false;
        return;
    }

    if (_stationServicesMenu.isOpen) {
        // Epic 12.2 step 6: a credits increase while the dock menu is open is
        // treated as "sold an item" — cheaper than threading a dedicated
        // signal through StationServicesMenu for a single tutorial checkbox.
        int creditsBeforeMenu = InventoryManager::Get().Credits;
        _stationServicesMenu.Update();
        if (InventoryManager::Get().Credits > creditsBeforeMenu)
            AdvanceTutorialStep(TutorialStep::Sell);

        if (_stationServicesMenu.isOpen) {
            // World keeps simulating around the docked player — only player
            // input/movement/camera/net-input stay frozen (this whole branch
            // returns before reaching that code further down).
            TickWorldWhileDocked(dt);
            SendClientInput(/*docked=*/true, /*firing=*/false);

            // If the station the player is inside was destroyed by that
            // simulation this frame, the player dies with it — reuse the
            // existing death path (checked at the top of Update()) by
            // zeroing hull; force-close the menu directly (not Close(),
            // which only closes from the Main sub-screen) so next frame's
            // death check isn't hidden behind a still-open menu.
            bool stationStillAlive = false;
            if (_dockedIsPlayerStation) {
                for (const PlayerStation& ps : FleetManager::Get().PlayerStations)
                    if (ps.id == _dockedStationId) { stationStillAlive = ps.alive; break; }
            } else {
                for (const SpaceStation& st : _w->stations)
                    if (st.id == _dockedStationId) { stationStillAlive = st.alive; break; }
            }
            if (!stationStillAlive) {
                _playerEntity.health.currentHull = 0.0f;
                _stationServicesMenu.isOpen       = false;
                _dockedStationId       = 0;
                _dockedIsPlayerStation = false;
            }
        } else {
            _dockedStationId       = 0;
            _dockedIsPlayerStation = false;
        }
        return;
    }

    // ── Build menu ────────────────────────────────────────────────────────────
    if (_buildMenu.isOpen) {
        _buildMenu.Update();

        // 1. Live Placement: User selected a station and clicks outside the menu
        std::string activeHoverId = _buildMenu.GetSelectedStationId();
        if (!activeHoverId.empty() && !_buildMenu.IsMouseOverMenu() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            _placementPos = GetScreenToWorld2D(GetMousePosition(), _camera);
            _placingStationDefId = activeHoverId;
            _placementConfirmOpen = true;
            _buildMenu.Close(); // Close menu to focus safely on the confirmation popup
        }

        // 2. Button Placement: User clicked the explicit "PLACE" button inside the menu
        else if (!_buildMenu.pendingBuildId.empty()) {
            _inPlacementMode = true;
            _placingStationDefId = _buildMenu.pendingBuildId;
            _buildMenu.pendingBuildId.clear();
        }

        return; // Keep input focused on menu block while active
    }

    if (_stationModMenu.isOpen) {
        _stationModMenu.Update();

        // 1. Live Placement: Clicked a ship and pulled cursor outside menu bounds
        std::string activeHoverId = _stationModMenu.GetSelectedShipId();
        if (!activeHoverId.empty() && !_stationModMenu.IsMouseOverMenu() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            _shipPlacementPos = GetScreenToWorld2D(GetMousePosition(), _camera);
            _placingShipDefId = activeHoverId;
            _shipPlacementConfirmOpen = true;
            _stationModMenu.Close();
        }
        // 2. Button Placement: Clicked "PLACE SHIP" explicitly inside menu
        else if (!_stationModMenu.pendingShipBuildId.empty()) {
            _inShipPlacementMode = true;
            _placingShipDefId = _stationModMenu.pendingShipBuildId;
            _stationModMenu.pendingShipBuildId.clear();
        }
        return;
    }

    if (_miningMenu.isOpen) {
        bool stillOpen = _miningMenu.Update();
        if (!stillOpen && _miningMenu.openModulesRequested) {
            _miningMenu.openModulesRequested = false;
            for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
                if (ps.id == _miningMenuId) {
                    _stationModMenuId = ps.id;
                    _stationModMenu.Open(&ps, &_storageMenu.slots);
                    break;
                }
            }
        }
        return;
    }

    if (_shipPlacementConfirmOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PopW = 340, PopH = 150;
        int px2 = sw2 / 2 - PopW / 2, py2 = sh2 / 2 - PopH / 2;
        Vector2 m2 = GetMousePosition();
        Rectangle yesBtn = { (float)(px2 + 30),        (float)(py2 + PopH - 50), 120.0f, 32.0f };
        Rectangle noBtn = { (float)(px2 + PopW - 150),  (float)(py2 + PopH - 50), 120.0f, 32.0f };

        bool isCapital = false;
        if (const auto* hoveredDef = ecs::ShipRegistry::ShipById(_placingShipDefId))
            isCapital = hoveredDef->shipType == ShipType::Capital;
        bool canAffordCapital = !isCapital || InventoryManager::Get().Credits >= kCapitalShipBuildCost;

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(m2, yesBtn) && canAffordCapital) {
                if (isCapital) InventoryManager::Get().SpendCredits(kCapitalShipBuildCost);

                // Credits/materials are spent locally above regardless of role
                // (co-op trust model). Only object CREATION is host-authoritative:
                // a client sends a request and waits for the host's Snapshot to
                // show it; the host (and single-player) keep creating directly.
                // See [[tasks-multiplayer]] Epic C.
                if (net::Game().IsClient()) {
                    net::Game().ClientSendPlaceShipRequest(_placingShipDefId, _shipPlacementPos.x, _shipPlacementPos.y);
                } else {
                    PlaceFriendlyShip(*_w, _placingShipDefId, _shipPlacementPos, kPlayerFaction);
                }

                _shipPlacementConfirmOpen = false;
                _inShipPlacementMode = false;
                _placingShipDefId.clear();
            }
            else if (CheckCollisionPointRec(m2, noBtn)) {
                _shipPlacementConfirmOpen = false;
                _inShipPlacementMode = false;
                _placingShipDefId.clear();
            }
        }
        return;
    }

    // ── ADD: Ship Placement Mode ──
    if (_inShipPlacementMode) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            _inShipPlacementMode = false;
            _placingShipDefId.clear();
        }
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            _shipPlacementPos = GetScreenToWorld2D(GetMousePosition(), _camera);
            _shipPlacementConfirmOpen = true;
        }
    }

    // ── Placement confirmation popup box ──────────────────────────────────────
    if (_placementConfirmOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PopW = 340, PopH = 150;
        int px2 = sw2 / 2 - PopW / 2, py2 = sh2 / 2 - PopH / 2;
        Vector2 m2 = GetMousePosition();
        Rectangle yesBtn = { (float)(px2 + 30),        (float)(py2 + PopH - 50), 120.0f, 32.0f };
        Rectangle noBtn = { (float)(px2 + PopW - 150),(float)(py2 + PopH - 50), 120.0f, 32.0f };

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(m2, yesBtn)) {
                // Find the active recipe costs across registry items
                const BuildableDef* foundDef = nullptr;
                for (const auto& bd : BuildableRegistry::All()) {
                    if (bd.type == BuildableType::Station && bd.stationDefId == _placingStationDefId) {
                        foundDef = &bd;
                        break;
                    }
                }

                // Deduct the materials from active inventory slots
                if (foundDef) {
                    for (const auto& ing : foundDef->itemCost) {
                        int toRemove = ing.amount;
                        for (StorageItem& s : _storageMenu.slots) {
                            if (s.type == StorageItemType::Material && s.materialId == ing.itemId) {
                                if (s.count >= toRemove) {
                                    s.count -= toRemove;
                                    toRemove = 0;
                                }
                                else {
                                    toRemove -= s.count;
                                    s.count = 0;
                                }
                                if (s.count == 0) s = StorageItem{};
                                if (toRemove == 0) break;
                            }
                        }
                    }
                }

                // Materials are spent locally above regardless of role. Object
                // CREATION is host-authoritative: a client sends a request and
                // waits for the host's Snapshot to show it; host/single-player
                // keep creating directly. See [[tasks-multiplayer]] Epic C.
                if (net::Game().IsClient()) {
                    net::Game().ClientSendBuildStationRequest(_placingStationDefId, _placementPos.x, _placementPos.y);
                } else {
                    FleetManager::Get().SpawnStation(_placingStationDefId, _placementPos);
                }
                _placementConfirmOpen = false;
                _inPlacementMode = false;
                _placingStationDefId.clear();
            }
            else if (CheckCollisionPointRec(m2, noBtn)) {
                // Return safely back to selection view on decline
                _placementConfirmOpen = false;
                _inPlacementMode = false;
                _placingStationDefId.clear();
                _buildMenu.Open(&_storageMenu.slots);
            }
        }
        return;
    }

    // ── Placement mode: ghost preview updating ───────────────────────────────
    if (_inPlacementMode) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            _inPlacementMode = false;
            _placingStationDefId.clear();
            _buildMenu.Open(&_storageMenu.slots); // Re-open menu on cancel
        }
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            _placementPos = GetScreenToWorld2D(GetMousePosition(), _camera);
            _placementConfirmOpen = true;
        }
        // Fall through naturally so the player can still fly/maneuver while planning
    } 

    // ── Station module menu ────────────────────────────────────────────────────
    if (_stationModMenu.isOpen) {
        _stationModMenu.Update();
        return;
    }

    // ── Placement confirmation ────────────────────────────────────────────────
    if (_placementConfirmOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PopW = 340, PopH = 150;
        int px2 = sw2 / 2 - PopW / 2, py2 = sh2 / 2 - PopH / 2;
        Vector2 m2 = GetMousePosition();
        Rectangle yesBtn = { (float)(px2 + 30),        (float)(py2 + PopH - 50), 120.0f, 32.0f };
        Rectangle noBtn  = { (float)(px2 + PopW - 150),(float)(py2 + PopH - 50), 120.0f, 32.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(m2, yesBtn)) {
                if (net::Game().IsClient()) {
                    net::Game().ClientSendBuildStationRequest(_placingStationDefId, _placementPos.x, _placementPos.y);
                } else {
                    FleetManager::Get().SpawnStation(_placingStationDefId, _placementPos);
                }
                _placementConfirmOpen = false;
                _inPlacementMode      = false;
                _placingStationDefId.clear();
            }
            else if (CheckCollisionPointRec(m2, noBtn)) {
                _placementConfirmOpen = false;
                // Keep placement mode active so player can pick another spot
            }
        }
        return;
    }

    // ── Placement mode: ghost preview, click to confirm location ─────────────
    if (_inPlacementMode) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            _inPlacementMode = false;
            _placingStationDefId.clear();
        }
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            _placementPos         = GetScreenToWorld2D(GetMousePosition(), _camera);
            _placementConfirmOpen = true;
        }
        // Still allow ship movement below — don't return
    }

    // ── Right-click on player station to open module menu ─────────────────────
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && !_inPlacementMode) {
        Vector2 worldMouse = GetScreenToWorld2D(GetMousePosition(), _camera);
        for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
            if (!ps.alive) continue;
            const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
            float rad = def ? def->radius : 120.0f;
            if (Vector2Distance(worldMouse, ps.position) < rad) {
                if (ps.stationDefId == "mining_station") {
                    _miningMenuId = ps.id;
                    _miningMenu.Open(&ps, &_storageMenu.slots);
                } else {
                    _stationModMenuId = ps.id;
                    _stationModMenu.Open(&ps, &_storageMenu.slots);
                }
                return;
            }
        }
    }

    if (_commsMenuOpen) {
        // Epic 13: station hail (remote contract board) is a separate,
        // simpler flow — no NPC roll/response phases, just an offer list.
        if (_commsMenuIsStation) {
            bool stationAlive = false;
            for (const SpaceStation& s : _w->stations)
                if (s.id == _commsMenuStationId && s.alive) { stationAlive = true; break; }
            if (!stationAlive) { _commsMenuOpen = false; return; }

            int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
            static constexpr int CW = 560, CH = 360;
            int mcx = sw2 / 2 - CW / 2, mcy = sh2 / 2 - CH / 2;
            Rectangle backBtn = { (float)(mcx + 20), (float)(mcy + CH - 52), 120.0f, 34.0f };
            Vector2   m2 = GetMousePosition();
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(m2, backBtn)) {
                    _commsMenuOpen = false;
                }
                else if (!_hasActiveContract) {
                    static constexpr float RowH = 70.0f, RowGap = 10.0f;
                    float listY = (float)mcy + 60.0f;
                    for (size_t oi = 0; oi < _contractOffers.size(); ++oi) {
                        Rectangle row  = { (float)mcx + 20.0f, listY + oi * (RowH + RowGap),
                                           (float)CW - 40.0f, RowH };
                        Rectangle btn  = { row.x + row.width - 120.0f, row.y + row.height / 2.0f - 16.0f,
                                           100.0f, 32.0f };
                        if (!CheckCollisionPointRec(m2, btn)) continue;

                        Contract& offer = _contractOffers[oi];
                        if (offer.type == ContractType::Courier) {
                            StationEconomy* econ = FindStationEconomy(_commsMenuStationId, false);
                            if (!econ || econ->GetStock(offer.goodId) < offer.amount) break;
                            econ->RemoveStock(offer.goodId, offer.amount);
                        }
                        _activeContract    = offer;
                        _hasActiveContract = true;
                        AddCommsMessage("CONTRACT ACCEPTED (remote hail): " + offer.title, true);
                        break;
                    }
                }
            }
            return;
        }

        bool npcAlive = false;
        for (const NpcMeta& n : _w->npcMeta)
            if (n.id == _commsMenuNpcId && n.alive) { npcAlive = true; break; }
        if (!npcAlive) { _commsMenuOpen = false; return; }

        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int CW = 500, CH = 210;
        int mcx = sw2 / 2 - CW / 2, mcy = sh2 / 2 - CH / 2;
        Rectangle backBtn = { (float)(mcx + 20),       (float)(mcy + CH - 52), 120.0f, 34.0f };
        Rectangle joinBtn = { (float)(mcx + CW - 180), (float)(mcy + CH - 52), 160.0f, 34.0f };
        Vector2   m2 = GetMousePosition();
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(m2, backBtn)) {
                _commsMenuOpen = false;
            }
            else if (_commsMenuPhase == 0 && !_commsMenuIsCapital && CheckCollisionPointRec(m2, joinBtn)) {
                if (_commsMenuIsDistress) {
                    // Epic 13: acknowledging the distress call doesn't change
                    // the survive-the-window payout in TickDistressCalls at
                    // all — it's a flat goodwill bump for actively responding
                    // rather than the reward just happening ambiently.
                    if (_w->hasActiveDistress &&
                        _w->activeDistress.type == DistressType::ShipUnderAttack &&
                        _w->activeDistress.npcId == _commsMenuNpcId) {
                        _w->activeDistress.acknowledged = true;
                        ReputationRegistry::Adjust(_w->activeDistress.issuerFaction, 5.0f);
                        AddCommsMessage("DISTRESS ACKNOWLEDGED: assistance pledged to " +
                                        std::string(FactionName(_w->activeDistress.issuerFaction)) + ".", true);
                    }
                    _commsMenuNpcText = DistressAckLines[GetRandomValue(0, 2)];
                    _commsMenuPhase = 1;
                }
                else {
                    int roll = GetRandomValue(0, 99);
                    for (size_t ci = 0; ci < _w->npcMeta.size(); ++ci) {
                        NpcMeta& npc = _w->npcMeta[ci];
                        if (npc.id != _commsMenuNpcId || !npc.alive) continue;
                        // Capital-class craft can't join the escort wing — the
                        // wing AI would apply skewed fighter attributes to them.
                        if (!npc.hardpoints.empty()) {
                            _commsMenuNpcText = "Capital-class vessels don't fly escort. Request denied.";
                            _commsMenuPhase = 1;
                            break;
                        }
                        bool isFriendly = (npc.faction == NpcFaction::Friendly);
                        bool isNeutral  = (npc.faction == NpcFaction::Neutral);
                        int acceptChance = isFriendly ? 75 : isNeutral ? 50 : 25;
                        bool accepted = (roll < acceptChance);
                        if (accepted) {
                            int wingCount = 0;
                            for (const NpcMeta& w : _w->npcMeta)
                                if (w.alive && w.wingman) wingCount++;
                            if (wingCount >= 4) {
                                _commsMenuNpcText = "Wing is full. Dismiss an escort first.";
                            }
                            else {
                                bool usedSlots[4] = {};
                                for (const NpcMeta& w : _w->npcMeta)
                                    if (w.alive && w.wingman && w.wingmanSlot >= 0 && w.wingmanSlot < 4)
                                        usedSlots[w.wingmanSlot] = true;
                                int newSlot = 0;
                                for (int s = 0; s < 4; ++s)
                                    if (!usedSlots[s]) { newSlot = s; break; }
                                npc.wingman     = true;
                                npc.wingmanSlot = newSlot;
                                npc.faction = NpcFaction::Friendly;
                                npc.aiState = NpcAiState::Escort;
                                npc.waypointSet = false;
                                _commsMenuNpcText = isFriendly
                                    ? JoinAcceptLines[GetRandomValue(0, 2)]
                                    : HostileJoinLines[GetRandomValue(0, 1)];
                            }
                        }
                        else {
                            if (isFriendly) {
                                _commsMenuNpcText = FriendlyRefusalLines[GetRandomValue(0, 1)];
                            }
                            else {
                                _commsMenuNpcText = HostileRefusalLines[GetRandomValue(0, 1)];
                                npc.aiState = NpcAiState::Chase;
                            }
                        }
                        break;
                    }
                    _commsMenuPhase = 1;
                }
            }
        }
        return;
    }

    if (_modulesMenu.isOpen) {
        bool changed = _modulesMenu.Update();
        if (changed) {
            if (_escortModuleNpcId != 0) {
                for (size_t ci = 0; ci < _w->npcMeta.size(); ++ci)
                    if (_w->npcMeta[ci].id == _escortModuleNpcId) {
                        ApplyNpcLoadout(_w->entities[ci], _w->npcMeta[ci]);
                        break;
                    }
            }
            else {
                ApplyLoadout();
                AdvanceTutorialStep(TutorialStep::EquipModule);
            }
        }
        if (!_modulesMenu.isOpen) _escortModuleNpcId = 0;
        return;
    }

    if (_escortMenuOpen) {
        std::vector<size_t> wingmanIdxs;
        for (size_t ci = 0; ci < _w->npcMeta.size(); ++ci)
            if (_w->npcMeta[ci].alive && _w->npcMeta[ci].wingman) wingmanIdxs.push_back(ci);
        if (wingmanIdxs.empty()) { _escortMenuOpen = false; return; }

        bool selValid = false;
        for (size_t ci : wingmanIdxs)
            if (_w->npcMeta[ci].id == _escortMenuSelId) { selValid = true; break; }
        if (!selValid) _escortMenuSelId = _w->npcMeta[wingmanIdxs[0]].id;

        size_t selIdx = SIZE_MAX;
        for (size_t ci : wingmanIdxs)
            if (_w->npcMeta[ci].id == _escortMenuSelId) { selIdx = ci; break; }
        if (selIdx == SIZE_MAX) { _escortMenuOpen = false; return; }

        int sw2 = GetScreenWidth();
        Vector2 m2 = GetMousePosition();

        Rectangle backBtn = { 18.0f, 16.0f, 120.0f, 38.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m2, backBtn)) {
            _escortMenuOpen = false;
            return;
        }

        static constexpr int ICON_W = 150, ICON_H = 50, ICON_GAP = 16;
        int totalW = (int)wingmanIdxs.size() * ICON_W + ((int)wingmanIdxs.size() - 1) * ICON_GAP;
        int iconStartX = sw2 / 2 - totalW / 2;
        for (int i = 0; i < (int)wingmanIdxs.size(); ++i) {
            Rectangle ir = { (float)(iconStartX + i * (ICON_W + ICON_GAP)), 70.0f,
                              (float)ICON_W, (float)ICON_H };
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m2, ir)) {
                _escortMenuSelId = _w->npcMeta[wingmanIdxs[i]].id;
                selIdx = wingmanIdxs[i];
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Rectangle editBtn = { 30.0f, 260.0f, 180.0f, 38.0f };
            Rectangle dismissBtn = { 230.0f, 260.0f, 180.0f, 38.0f };
            if (CheckCollisionPointRec(m2, editBtn)) {
                _escortModuleNpcId = _w->npcMeta[selIdx].id;
                _modulesMenu.Open(&_w->npcMeta[selIdx].loadout, &_storageMenu.slots,
                    NpcMeta::WSlots, NpcMeta::ArSlots,
                    NpcMeta::ShSlots, NpcMeta::EnSlots, 0,
                    &_w->entities[selIdx].health);
            }
            else if (CheckCollisionPointRec(m2, dismissBtn)) {
                _w->npcMeta[selIdx].wingman     = false;
                _w->npcMeta[selIdx].wingmanSlot = -1;
                _w->npcMeta[selIdx].aiState     = NpcAiState::Patrol;
                _w->npcMeta[selIdx].waypointSet = false;
                _escortMenuSelId = 0;
                bool anyLeft = false;
                for (const NpcMeta& nn : _w->npcMeta)
                    if (nn.alive && nn.wingman) { _escortMenuSelId = nn.id; anyLeft = true; break; }
                if (!anyLeft) { _escortMenuOpen = false; return; }
            }
        }
        return;
    }

    if (_ranksMenuOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PW = 500, PH = 340;
        int px = sw2 / 2 - PW / 2, py = sh2 / 2 - PH / 2;
        Rectangle closeBtn = { (float)(px + PW / 2 - 60), (float)(py + PH - 46), 120.0f, 32.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), closeBtn))
            _ranksMenuOpen = false;
        return;
    }

    // Epic 12.1: received-comms panel — same centered-overlay convention as
    // the ranks menu above.
    if (_commsLogOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PW = 520, PH = 320;
        int px = sw2 / 2 - PW / 2, py = sh2 / 2 - PH / 2;
        Rectangle closeBtn = { (float)(px + PW / 2 - 60), (float)(py + PH - 46), 120.0f, 32.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), closeBtn))
            _commsLogOpen = false;
        return;
    }

    if (_storageMenu.isOpen) {
        _storageMenu.Update();
        return;
    }

    _camera.offset = { (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f };

    if (_playerEntity.health.maxStats.shield > 0.0f)
        _playerEntity.health.currentShield = std::min(
            _playerEntity.health.currentShield + _playerMeta.kineticRechargeRate * dt,
            _playerEntity.health.maxStats.shield);
    if (_playerMeta.maxEnergyShield > 0.0f)
        _playerMeta.energyShield = std::min(
            _playerMeta.energyShield + _playerMeta.energyRechargeRate * dt,
            _playerMeta.maxEnergyShield);

    // Number keys toggle each weapon slot on/off (1-9 → slots 1-9, 0 → slot 10).
    // Every enabled+equipped weapon fires together, so the player can arm all,
    // some, or none of their weapons at once.
    auto toggleWeapon = [&](int slot) {
        if (slot < 0 || slot >= _playerMeta.weaponSlots) return;
        if (slot >= (int)_weaponEnabled.size()) _weaponEnabled.resize(slot + 1, true);
        _weaponEnabled[slot] = !_weaponEnabled[slot];
        _lockTargetId = 0;
        ApplyLoadout(); // refresh the HUD's representative (primary) weapon
    };
    for (int k = 0; k < 9 && k < _playerMeta.weaponSlots; ++k)
        if (IsKeyPressed(KEY_ONE + k)) toggleWeapon(k);
    if (IsKeyPressed(KEY_ZERO) && _playerMeta.weaponSlots > 9) toggleWeapon(9);

    // ── HUD button clicks ─────────────────────────────────────────────────────
    Vector2 mousePos = GetMousePosition();
    int hy = GetScreenHeight() - HudH - 6;

    bool clickedHudBtn = _storageMenu.isOpen || _modulesMenu.isOpen || _galaxyMap.isOpen || _ranksMenuOpen || (mousePos.y >= hy);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Rectangle enterBtn, buildBtn, commsBtn, seatBtn;
        ComputeHudButtons(GetScreenWidth(), GetScreenHeight(), enterBtn, buildBtn, commsBtn, seatBtn);
        Vector2 m = GetMousePosition();

        // Epic 8: SEAT button toggles manning/unmanning a friendly capital's
        // turret. Checked ahead of the other buttons since it's a toggle
        // (both entry and exit share the same rect) rather than an
        // enable/disable-gated single action.
        {
            unsigned int seatNpcId; int seatHpIdx; Vector2 seatPos;
            // Epic 8 is host/singleplayer-only for now: only the host resolves
        // hit detection (UpdateCollisions/UpdateNpcCollisions are skipped
        // entirely for clients), so a client's local turret shots would
        // fly but never damage anything — gate seating out for clients
        // rather than ship a control that silently does nothing.
        bool seatAvailable = !net::Game().IsClient() && (_seated || FindNearestFriendlySeat(seatNpcId, seatHpIdx, seatPos));
            if (seatAvailable && CheckCollisionPointRec(m, seatBtn)) {
                if (_seated) {
                    // Unseat: park the player's ship at the hardpoint's last
                    // known position (found again below since _seatedNpcId
                    // may have moved/died since the frame started).
                    Vector2 exitPos = _playerEntity.transform.position;
                    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
                        if (_w->npcMeta[i].id != _seatedNpcId || !_w->npcMeta[i].alive) continue;
                        if (_seatedHardpointIdx >= 0 && _seatedHardpointIdx < (int)_w->npcMeta[i].hardpoints.size()) {
                            exitPos = GetCapitalHardpointWorldPos(_w->entities[i].transform.position,
                                _w->entities[i].transform.rotation,
                                _w->npcMeta[i].hardpoints[_seatedHardpointIdx].localOffset);
                        }
                        break;
                    }
                    _playerEntity.transform.position = exitPos;
                    _playerEntity.transform.velocity = { 0.0f, 0.0f };
                    _seated = false;
                    _seatedNpcId = 0;
                    _seatedHardpointIdx = -1;
                } else {
                    _seated = true;
                    _seatedNpcId = seatNpcId;
                    _seatedHardpointIdx = seatHpIdx;
                    _playerEntity.transform.position = seatPos;
                    _playerEntity.transform.velocity = { 0.0f, 0.0f };
                }
                clickedHudBtn = true;
            }
        }
        EnterableStation es = FindEnterableStation();
        if ((es.found || IsNearPlanet()) && CheckCollisionPointRec(m, enterBtn)) {
            if (es.found) {
                _dockedStationId       = es.id;
                _dockedIsPlayerStation = es.isPlayerStation;
                AdvanceTutorialStep(TutorialStep::Dock);

                // Epic 7.2: docking at the active Courier contract's
                // destination auto-delivers the cargo before the menu opens.
                if (_hasActiveContract && _activeContract.type == ContractType::Courier &&
                    _activeContract.destStationId == es.id &&
                    _activeContract.destWorldKey == WorldKey(_currentGalaxyId, _currentSystemId)) {
                    StationEconomy* destEcon = FindStationEconomy(es.id, es.isPlayerStation);
                    if (destEcon) destEcon->AddStock(_activeContract.goodId, _activeContract.amount);
                    CompleteActiveContract();
                }

                Faction stationFaction = FindStationFaction(es.id, es.isPlayerStation);
                _contractOffers = GenerateContractOffers(stationFaction, es.id, es.isPlayerStation);
                _stationServicesMenu.Open(&_playerEntity, &_storageMenu.slots,
                                           FindStationEconomy(es.id, es.isPlayerStation),
                                           &_fuel, kMaxFuel, stationFaction,
                                           &_contractOffers, &_activeContract, &_hasActiveContract);
            } else {
                _enterPopupOpen = true;
            }
            clickedHudBtn = true;
        }
        else if (CheckCollisionPointRec(m, buildBtn)) {
            _buildMenu.Open(&_storageMenu.slots);
            clickedHudBtn = true;
        }
        else if (CheckCollisionPointRec(m, commsBtn) &&
                 (_npcTargetId != 0 || (_target.valid && _target.isStellar && _target.hasFaction))) {
            _commsMenuOpen  = true;
            _commsMenuPhase = 0;
            if (_npcTargetId != 0) {
                _commsMenuIsStation  = false;
                _commsMenuNpcId      = _npcTargetId;
                // Epic 13: hailing the specific ship broadcasting an active
                // ShipUnderAttack distress call swaps the recruit-hail flow
                // below for an ACKNOWLEDGE action instead.
                _commsMenuIsDistress = _w->hasActiveDistress &&
                    _w->activeDistress.type == DistressType::ShipUnderAttack &&
                    _w->activeDistress.npcId == _npcTargetId &&
                    !_w->activeDistress.acknowledged;
                _commsMenuIsCapital = false;
                for (const NpcMeta& npc : _w->npcMeta) {
                    if (npc.id != _npcTargetId || !npc.alive) continue;
                    _commsMenuIsCapital = !npc.hardpoints.empty();
                    if (npc.faction == NpcFaction::Friendly) {
                        _commsMenuNpcName = "FRIENDLY " + npc.shipTypeName;
                        _commsMenuNpcText = FriendlyLines[GetRandomValue(0, 4)];
                    }
                    else if (npc.faction == NpcFaction::Neutral) {
                        _commsMenuNpcName = "UNKNOWN " + npc.shipTypeName;
                        _commsMenuNpcText = FriendlyLines[GetRandomValue(0, 4)];
                    }
                    else {
                        _commsMenuNpcName = "HOSTILE " + npc.shipTypeName;
                        _commsMenuNpcText = HostileLines[GetRandomValue(0, 3)];
                    }
                    if (_commsMenuIsDistress) _commsMenuNpcText = DistressHailLines[GetRandomValue(0, 2)];
                    break;
                }
            }
            else {
                // Epic 13: hailing a station opens its contract board from
                // range, reusing the exact same GenerateContractOffers /
                // _activeContract flow docking already goes through —
                // hailing is just a second entry point into it.
                _commsMenuIsStation  = true;
                _commsMenuIsDistress = false;
                _commsMenuIsCapital  = false;
                _commsMenuStationId  = _targetId;
                Faction stFaction = FindStationFaction(_targetId, false);
                _commsMenuNpcName = std::string(FactionName(stFaction)) + " " + _target.name;
                _commsMenuNpcText = _hasActiveContract
                    ? "We've nothing further for you while that job's active."
                    : "Comms open. State your business.";
                if (!_hasActiveContract) {
                    _contractOffers = GenerateContractOffers(stFaction, _targetId, false);
                }
            }
            clickedHudBtn = true;
        }
    }

    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), _camera);

    unsigned int passLockId = 0;
    Vector2      passLockPos = {};
    auto lockCheckWeaponSlots = _loadout.WeaponSlots();
    bool anyLockOnArmed = false;
    for (int i = 0; i < (int)lockCheckWeaponSlots.size(); ++i)
        if (lockCheckWeaponSlots[i]->equipped && IsWeaponEnabled(i) &&
            lockCheckWeaponSlots[i]->equipped->weapon.fireMode == WeaponFireMode::LockOn) {
            anyLockOnArmed = true; break;
        }
    if (anyLockOnArmed) {
        if (!clickedHudBtn && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            _lockTargetId = 0;
            // NPC ships (non-escort) take priority
            for (size_t li = 0; li < _w->npcMeta.size(); ++li) {
                const NpcMeta& n = _w->npcMeta[li];
                if (!n.alive || n.wingman) continue;
                if (Vector2Distance(mouseWorld, _w->entities[li].transform.position) < n.radius + 8.0f) {
                    _lockTargetId = n.id; break;
                }
            }
            // Then asteroids
            if (_lockTargetId == 0) {
                for (const Asteroid& a : _w->asteroids) {
                    if (a.alive && Vector2Distance(mouseWorld, a.position) < a.radius) {
                        _lockTargetId = a.id; break;
                    }
                }
            }
        }
        if (_lockTargetId != 0) {
            bool found = false;
            for (size_t li = 0; li < _w->npcMeta.size(); ++li)
                if (_w->npcMeta[li].id == _lockTargetId && _w->npcMeta[li].alive) { _lockTargetPos = _w->entities[li].transform.position; found = true; break; }
            if (!found)
                for (const Asteroid& a : _w->asteroids)
                    if (a.id == _lockTargetId && a.alive) { _lockTargetPos = a.position; found = true; break; }
            if (!found) _lockTargetId = 0;
        }
        passLockId = _lockTargetId;
        passLockPos = _lockTargetPos;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) _lockTargetId = 0;
    }
    else {
        _lockTargetId = 0;
    }

    // ── Player ship update ────────────────────────────────────────────────────
    if (_seated) {
        UpdateSeatedTurret(dt, mouseWorld, clickedHudBtn);
    } else {
        const bool fireEnabled = !clickedHudBtn && !blockFireUntilRelease;
        auto& pos  = _playerEntity.transform.position;
        auto& vel  = _playerEntity.transform.velocity;
        float& rot = _playerEntity.transform.rotation;

        if (_playerMeta.canMove) {
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  rot -= _playerMeta.turnSpeed * dt;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) rot += _playerMeta.turnSpeed * dt;
            _playerMeta.thrusting = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
            if (_playerMeta.thrusting) {
                float fwdRad = (rot - 90.0f) * DEG2RAD;
                Vector2 fwd  = { cosf(fwdRad), sinf(fwdRad) };
                vel.x += fwd.x * _playerMeta.thrust * dt;
                vel.y += fwd.y * _playerMeta.thrust * dt;
            }
        } else {
            _playerMeta.thrusting = false;
        }

        float activeDrag  = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 14.0f : 2.0f;
        float dragFactor  = expf(-activeDrag * dt);
        vel.x *= dragFactor;
        vel.y *= dragFactor;

        pos.x += vel.x * dt;
        pos.y += vel.y * dt;

        // Per-slot weapon firing: every enabled+equipped weapon fires on its
        // own cooldown/charge timer, so the player can arm all, some, or none
        // of their weapons via the number keys (see _weaponEnabled). Cooldowns
        // keep ticking even while firing is blocked (menu open), matching the
        // old single-weapon behavior.
        auto  fireSlots = _loadout.WeaponSlots();
        if ((int)_weaponCooldown.size() < (int)fireSlots.size()) {
            _weaponCooldown.resize(fireSlots.size(), 0.0f);
            _weaponCharge.resize(fireSlots.size(), 0.0f);
            _weaponEnabled.resize(fireSlots.size(), true);
        }
        float cdMult = _loadout.IsOverloaded() ? HardpointRig::kOverloadCooldownMult : 1.0f;

        Vector2 toAim  = Vector2Subtract(mouseWorld, pos);
        float   aimLen = Vector2Length(toAim);
        float   fwdRad = (rot - 90.0f) * DEG2RAD;
        Vector2 fwd    = { cosf(fwdRad), sinf(fwdRad) };
        Vector2 aimDir = (aimLen > 1.0f) ? Vector2Scale(toAim, 1.0f / aimLen) : fwd;

        const bool canFire = fireEnabled && !net::Game().IsClient();

        for (int wi = 0; wi < (int)fireSlots.size(); ++wi) {
            float& cd     = _weaponCooldown[wi];
            float& charge = _weaponCharge[wi];
            if (cd > 0.0f) cd -= dt;

            const bool armed = fireSlots[wi]->equipped && IsWeaponEnabled(wi);
            if (!armed) { charge = 0.0f; continue; }

            const WeaponStats& ws = fireSlots[wi]->equipped->weapon;
            if (ws.fireMode != WeaponFireMode::Charge) charge = 0.0f;
            if (!canFire) continue;

            float fireRate  = ws.fireRate * cdMult;
            float projSpeed = ws.projSpeed;
            float ttl       = projSpeed > 0.0f ? ws.projRange / projSpeed : 0.0f;

            switch (ws.fireMode) {
            case WeaponFireMode::Standard: {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && cd <= 0.0f) {
                    bool inArc = ws.isTurret || (Vector2DotProduct(fwd, aimDir) > 0.0f);
                    if (inArc) {
                        cd = fireRate;
                        Projectile p;
                        p.position = pos;
                        p.velocity = { aimDir.x * projSpeed, aimDir.y * projSpeed };
                        p.lifetime = 0.0f; p.maxLife = ttl; p.damage = ws.damage; p.alive = true;
                        p.effect = ws.effect; p.effectDuration = ws.effectDuration;
                        _w->projectiles.push_back(p);
                    }
                }
                break;
            }
            case WeaponFireMode::Charge: {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                    charge = std::min(charge + dt, ws.chargeTime);
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && charge > 0.005f && cd <= 0.0f) {
                    bool inArc = ws.isTurret || (Vector2DotProduct(fwd, aimDir) > 0.0f);
                    if (inArc) {
                        float ratio    = ws.chargeTime > 0.0f ? charge / ws.chargeTime : 1.0f;
                        int   numProj  = std::max(1, (int)std::ceil(ws.burstCount * ratio));
                        float halfSprd = (ws.spreadAngle * 0.5f) * DEG2RAD;
                        float step     = (numProj > 1) ? (ws.spreadAngle * DEG2RAD) / (numProj - 1) : 0.0f;
                        for (int bi = 0; bi < numProj; ++bi) {
                            float a = (numProj > 1) ? -halfSprd + step * bi : 0.0f;
                            float c = cosf(a), s = sinf(a);
                            Vector2 d = { aimDir.x * c - aimDir.y * s, aimDir.x * s + aimDir.y * c };
                            Projectile p;
                            p.position = pos;
                            p.velocity = { d.x * projSpeed, d.y * projSpeed };
                            p.lifetime = 0.0f; p.maxLife = ttl; p.damage = ws.damage; p.alive = true;
                            p.effect = ws.effect; p.effectDuration = ws.effectDuration;
                            _w->projectiles.push_back(p);
                        }
                        cd = fireRate;
                    }
                    charge = 0.0f;
                }
                break;
            }
            case WeaponFireMode::LockOn: {
                if (passLockId != 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && cd <= 0.0f) {
                    Vector2 toTarget = Vector2Subtract(passLockPos, pos);
                    float   tLen     = Vector2Length(toTarget);
                    if (tLen > 1.0f) {
                        Vector2 tDir = Vector2Scale(toTarget, 1.0f / tLen);
                        Projectile p;
                        p.position = pos;
                        p.velocity = { tDir.x * projSpeed, tDir.y * projSpeed };
                        p.lifetime = 0.0f; p.maxLife = ttl; p.damage = ws.damage; p.alive = true;
                        p.isHoming = true;
                        p.targetId = passLockId;
                        p.turnRate = (ws.projType == WeaponProjType::Seeking) ? 3.0f : 0.0f;
                        p.effect = ws.effect; p.effectDuration = ws.effectDuration;
                        _w->projectiles.push_back(p);
                        cd = fireRate;
                    }
                }
                break;
            }
            default: break;
            }
        }

        // Mirror the representative (primary) weapon's timers into _playerMeta
        // so the HUD's WEAPON readiness/charge panel reflects a live weapon.
        if (_primaryWeapon >= 0 && _primaryWeapon < (int)_weaponCooldown.size()) {
            _playerMeta._fireCooldown = _weaponCooldown[_primaryWeapon];
            _playerMeta._chargeTimer  = _weaponCharge[_primaryWeapon];
        } else {
            _playerMeta._fireCooldown = 0.0f;
            _playerMeta._chargeTimer  = 0.0f;
        }
    }

    AdvanceProjectilesAndAsteroids(dt);

    _w->age += dt;
    UpdateOrbits(dt);
    UpdateNpcShips(dt);
    ApplySunGravity(dt);

    if (_hitCooldown > 0.0f) _hitCooldown -= dt;

    UpdatePlayerStations(dt);
    UpdateWorldStationFire(dt);
    UpdateCapitalFire(dt);

    if (!net::Game().IsClient()) {
        UpdateCollisions();
        UpdateNpcCollisions();
        UpdateCollisions();
        UpdateNpcCollisions();
        if (!_stationServicesMenu.isOpen && !_seated) UpdateCaptureProximity(); // Epic 9.1
    }

    TickDiscovery();

    if (!net::Game().IsClient()) {
        CullAndRespawnAround(dt, _playerEntity.transform.position);
    } else {
        // Clients don't own world content (snapshots reconcile it), but local
        // station-fire visuals and snapshot-killed asteroids still need erasing.
        auto isDead = [](const auto& e) { return !e.alive; };
        _w->projectiles.erase(std::remove_if(_w->projectiles.begin(), _w->projectiles.end(), isDead), _w->projectiles.end());
        _w->asteroids.erase(std::remove_if(_w->asteroids.begin(), _w->asteroids.end(), isDead), _w->asteroids.end());
    }

    UpdateTarget();
    TickTutorial();

    bool anyMenuOpen = _storageMenu.isOpen || _modulesMenu.isOpen || _galaxyMap.isOpen ||
                       _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _commsLogOpen ||
                       _enterPopupOpen || _stationServicesMenu.isOpen || _localMapOpen ||
                       _buildMenu.isOpen || _stationModMenu.isOpen || _miningMenu.isOpen || _placementConfirmOpen;
    if (!anyMenuOpen) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            _cameraZoom = std::clamp(_cameraZoom * powf(1.1f, wheel), 0.25f, 4.0f);
    }
    _camera.zoom = _cameraZoom;

    _camera.target.x += (_playerEntity.transform.position.x - _camera.target.x) * 6.0f * dt;
    _camera.target.y += (_playerEntity.transform.position.y - _camera.target.y) * 6.0f * dt;

    // ── Client: send current position + aim to host every frame ───────────────
    SendClientInput(/*docked=*/false,
        (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !blockFireUntilRelease));
}

void SpaceFlight::SendClientInput(bool docked, bool firing) {
    if (!(net::Game().IsClient() && _worldSynced)) return;
    static uint32_t s_inputSeq = 0;
    net::InputCommand cmd;
    cmd.networkId   = net::Game().LocalNetworkId();
    cmd.aimRotation = _playerEntity.transform.rotation;
    cmd.posX        = _playerEntity.transform.position.x;
    cmd.posY        = _playerEntity.transform.position.y;
    cmd.sequence    = s_inputSeq++;
    cmd.firing      = firing ? 1 : 0;
    cmd.docked      = docked ? 1 : 0;
    net::Game().ClientSendInput(cmd);
}

void SpaceFlight::TickStationMining(PlayerStation& ps, float dt) {
    if (ps.stationDefId != "mining_station" || ps.storage.empty()) return;

    // Find the Material Probe installed in any aux slot (only the Mining
    // Drill hardpoint has one, per station_defs.json).
    const ModuleDef* probe   = nullptr;
    const Hardpoint* probeHp = nullptr;
    for (const Hardpoint& hp : ps.hardpoints) {
        if (!hp.alive) continue;
        for (const auto* a : hp.AuxSlots()) {
            if (a->equipped.has_value() && a->equipped->id == "aux_material_probe") { probe = &(*a->equipped); probeHp = &hp; break; }
        }
        if (probe) break;
    }
    if (!probe) return;   // no probe installed — station collects nothing

    // P3: a shed drill hardpoint collects nothing this tick; a throttled one
    // just collects slower (interval scaled below) rather than stopping outright.
    if (probeHp->shed) return;

    ps.miningTimer -= dt;
    if (ps.miningTimer > 0.0f) return;

    // Higher-grade probes collect faster; power throttle slows it back down;
    // P5: a Manufacturing facility adjacent to this hardpoint's Mining
    // facility speeds collection further (Mining<->Manufacturing throughput
    // bonus — see RecalculateAdjacency). No shipped station_defs.json type
    // has both facilities on one station today, so this multiplier is
    // presently always 1.0 in practice; wired and ready regardless.
    int   gradeIdx  = static_cast<int>(probe->grade);   // 0=Common .. 6=Mythic
    float interval  = std::max(2.0f, 9.0f - gradeIdx * 1.0f);
    interval       /= std::max(probeHp->throttle, 0.05f);
    interval       /= std::max(probeHp->adjacencyRateMult, 0.05f);
    ps.miningTimer  = interval;

    std::string matId = RollMiningMaterialId();
    for (StorageItem& slot : ps.storage) {
        if (slot.type == StorageItemType::Material && slot.materialId == matId &&
            slot.count < StorageMenu::MaxStack) {
            slot.count++;
            return;
        }
    }
    for (StorageItem& slot : ps.storage) {
        if (slot.type == StorageItemType::Empty) {
            slot.type        = StorageItemType::Material;
            slot.materialId   = matId;
            const MatDef* m   = FindMaterial(matId);
            slot.displayName  = m ? m->displayName : matId;
            slot.count        = 1;
            return;
        }
    }
    // Storage full — the timer already reset, so collection simply retries
    // (and keeps failing) each interval until a slot frees up.
}

// Per-PlayerStation upkeep: armor-derived hardpoint max-hull, mining tick,
// and autonomous fire at the nearest hostile NPC. Self-contained (no player
// input dependency), so it runs identically whether the player is flying
// around or docked inside a station menu (TickWorldWhileDocked).
void SpaceFlight::UpdatePlayerStations(float dt) {
    for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
        if (!ps.alive) continue;
        for (Hardpoint& hp : ps.hardpoints) {
            float baseHull = 100.0f; // Base health without modules
            const ModuleSlot* armorSlot = hp.Armor();
            float bonus = (armorSlot && armorSlot->equipped.has_value()) ? armorSlot->equipped->armor.hullBonus : 0.0f;
            float newMax = baseHull + bonus;

            if (hp.hull > newMax) hp.hull = newMax; // Cap current health if armor is removed
            hp.maxHull = newMax;
        }

        // P3/P5: recompute power budget once per tick (same as capitals
        // above). Adjacency first — see the capital-tick comment above for why.
        RecalculateAdjacency(ps.hardpoints);
        ps.powerBudget = RecalculatePowerBudget(ps.hardpoints, HardpointRig::kStationBaseCapacity);

        TickStationMining(ps, dt);

        // ── ADDED: Player Station Autonomous Firing ─────────────────────────
        // (This MUST be inside the ps loop so we know which station is firing)

        // 1. Find the closest living hostile to the station
        float closestDist = FLT_MAX;
        Vector2 targetPos = { 0, 0 };
        unsigned int targetId = 0;
        size_t targetIdx = 0;

        for (size_t li = 0; li < _w->npcMeta.size(); ++li) {
            const NpcMeta& npc = _w->npcMeta[li];
            if (!npc.alive || npc.faction != NpcFaction::Hostile) continue;
            // Nearest-target selection uses the raw center as a distance
            // proxy (fine); the actual fire/aim position is redirected to an
            // alive hardpoint below so shots don't converge on a dead one.
            float d = Vector2Distance(ps.position, _w->entities[li].transform.position);
            if (d < closestDist) {
                closestDist = d;
                targetPos = _w->entities[li].transform.position;
                targetId = npc.id;
                targetIdx = li;
            }
        }
        if (targetId != 0) targetPos = GetNpcAimPos(*_w, targetIdx);

        // 2. If a hostile is detected, let armed hardpoints open fire
        if (targetId != 0) {
            const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
            float rad = def ? def->radius : 120.0f;

            for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
                Hardpoint& hp = ps.hardpoints[i];

                // Skip dead hardpoints, or hardpoints with no weapons installed
                // FIXED: Added to check the first weapon slot specifically
                const ModuleSlot* wSlot = hp.FirstWeapon();
                if (!hp.alive || !wSlot || !wSlot->equipped.has_value() || hp.shed) continue;

                // Process weapon cooldown
                if (hp.fireCooldown > 0.0f) {
                    hp.fireCooldown -= dt;
                    continue;
                }

                // Check if the target is within the weapon's maximum range
                const WeaponStats& ws = wSlot->equipped->weapon;
                if (closestDist <= ws.projRange) {
                    Vector2 hpPos = GetHardpointPos(ps, i, rad);
                    Vector2 toT = Vector2Subtract(targetPos, hpPos);

                    // Calculate the base angle to the target
                    float aimAng = atan2f(toT.x, -toT.y) * RAD2DEG;

                    // Determine how many projectiles to fire
                    int burstCount = (ws.fireMode == WeaponFireMode::Charge && ws.burstCount > 0) ? ws.burstCount : 1;

                    for (int b = 0; b < burstCount; ++b) {
                        // Calculate the spread offset for this specific projectile in the arc
                        float spOff = 0.0f;
                        if (burstCount > 1) {
                            spOff = ws.spreadAngle * ((float)b / (burstCount - 1) - 0.5f);
                        }

                        float fRad = (aimAng + spOff) * DEG2RAD;
                        Vector2 dir = { sinf(fRad), -cosf(fRad) };

                        // Spawn the projectile
                        Projectile p;
                        p.position = hpPos;
                        p.velocity = { dir.x * ws.projSpeed, dir.y * ws.projSpeed };
                        p.maxLife = ws.projRange / ws.projSpeed;
                        p.damage = ws.damage;
                        p.fromPlayer = true; // Set to true so station bullets don't hurt the player!
                        p.ownerId = ps.id;

                        // Allow station missiles to home in on the target
                        if (ws.fireMode == WeaponFireMode::LockOn) {
                            p.isHoming = true;
                            p.turnRate = 3.0f;
                            p.targetId = targetId;
                        }

                        _w->projectiles.push_back(p);
                    }

                    // Reset the hardpoint's cooldown (incorporate charge time so it doesn't rapid-fire)
                    hp.fireCooldown = ws.fireRate + ws.chargeTime;
                }
            }
        }
    }
}

// Marks planets/world-stations within range of the player as discovered.
void SpaceFlight::TickDiscovery() {
    static constexpr float DiscoveryRange = 400.0f;
    for (const SpacePlanet& p : _w->planets) {
        if (Vector2Distance(_playerEntity.transform.position, p.position) < p.radius + DiscoveryRange) {
            if (std::find(_discoveredIds.begin(), _discoveredIds.end(), p.id) == _discoveredIds.end())
                _discoveredIds.push_back(p.id);
        }
    }
    for (const SpaceStation& s : _w->stations) {
        if (!s.alive) continue;
        if (Vector2Distance(_playerEntity.transform.position, s.position) < s.radius + DiscoveryRange) {
            if (std::find(_discoveredIds.begin(), _discoveredIds.end(), s.id) == _discoveredIds.end())
                _discoveredIds.push_back(s.id);
        }
    }
}

// World-sim tick run while the player is docked inside _stationServicesMenu:
// everything Update()'s normal tail runs (NPCs, projectiles, station/capital
// fire, collisions, discovery, cull/respawn) except player input, movement,
// camera, and target-lock — the player isn't flying or fighting right now.
void SpaceFlight::TickWorldWhileDocked(float dt) {
    AdvanceProjectilesAndAsteroids(dt);

    _w->age += dt;
    UpdateOrbits(dt);
    UpdateNpcShips(dt);
    ApplySunGravity(dt);

    if (_hitCooldown > 0.0f) _hitCooldown -= dt;

    UpdatePlayerStations(dt);
    UpdateWorldStationFire(dt);
    UpdateCapitalFire(dt);

    if (!net::Game().IsClient()) {
        UpdateCollisions();
        UpdateNpcCollisions();
        UpdateCollisions();
        UpdateNpcCollisions();
    }

    TickDiscovery();

    if (!net::Game().IsClient()) {
        CullAndRespawnAround(dt, _playerEntity.transform.position);
    } else {
        auto isDead = [](const auto& e) { return !e.alive; };
        _w->projectiles.erase(std::remove_if(_w->projectiles.begin(), _w->projectiles.end(), isDead), _w->projectiles.end());
        _w->asteroids.erase(std::remove_if(_w->asteroids.begin(), _w->asteroids.end(), isDead), _w->asteroids.end());
    }
}

// In-world (NPC faction) station firing, per hardpoint. Operates on `_w`, so
// it serves both the player's system and background-world ticks.
void SpaceFlight::UpdateWorldStationFire(float dt) {
    for (SpaceStation& st : _w->stations) {
        if (!st.alive) continue;
        if (st.retaliateTimer > 0.0f) st.retaliateTimer -= dt;

        // Compute max weapon range across all armed hardpoints
        float maxRange = 500.0f;
        for (const Hardpoint& hp : st.hardpoints) {
            const ModuleSlot* wSlot = hp.FirstWeapon();
            if (wSlot && wSlot->equipped.has_value())
                maxRange = std::max(maxRange, wSlot->equipped->weapon.projRange);
        }

        // Find closest valid target within range — shared template so any
        // future hostile-capable unit (ships, turrets, ...) can reuse the
        // exact same "nearest hostile in range" rule stations use here.
        combat::HostileTarget pick = combat::FindNearestHostileTarget(
            *_w, _playerEntity, st.faction, st.position, maxRange, st.id,
            !_stationServicesMenu.isOpen && !_seated);

        Vector2      fireTarget        = pick.position;
        bool         hasFireTarget     = pick.valid;
        float        bestDist          = pick.valid ? Vector2Distance(st.position, pick.position) : maxRange;
        unsigned int fireTargetId      = pick.id;
        bool         fireTargetIsPlayer= (pick.kind == combat::HostileTargetKind::Player);

        // Redirect aim from the target's raw center to one of its own alive
        // hardpoints — otherwise fire keeps converging on st.position/ship
        // center (often exactly where a now-dead core/hardpoint sat).
        if (pick.valid && pick.kind == combat::HostileTargetKind::Station) {
            for (const SpaceStation& tst : _w->stations)
                if (tst.id == pick.id) { fireTarget = GetStationAimPos(tst); break; }
        } else if (pick.valid && pick.kind == combat::HostileTargetKind::Npc) {
            for (size_t j = 0; j < _w->npcMeta.size(); ++j)
                if (_w->npcMeta[j].id == pick.id) { fireTarget = GetNpcAimPos(*_w, j); break; }
        }

        if (st.retaliating && st.retaliateTimer > 0.0f) {
            if (st.retaliateAtPlayer) {
                float d = Vector2Distance(st.position, _playerEntity.transform.position);
                if (!hasFireTarget || d < bestDist) {
                    bestDist = d; fireTarget = _playerEntity.transform.position;
                    hasFireTarget = true; fireTargetId = 0; fireTargetIsPlayer = true;
                }
            } else if (st.retaliateAtNpcId != 0) {
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (_w->npcMeta[j].id == st.retaliateAtNpcId && _w->npcMeta[j].alive) {
                        float d = Vector2Distance(st.position, _w->entities[j].transform.position);
                        if (!hasFireTarget || d < bestDist) {
                            bestDist = d; fireTarget = GetNpcAimPos(*_w, j);
                            hasFireTarget = true; fireTargetId = _w->npcMeta[j].id; fireTargetIsPlayer = false;
                        }
                        break;
                    }
                }
            }
            if (st.retaliateTimer <= 0.0f) {
                st.retaliating = false; st.retaliateAtPlayer = false; st.retaliateAtNpcId = 0;
            }
        }

        if (!hasFireTarget) continue;

        // Fire from each armed hardpoint independently
        for (int hi = 0; hi < (int)st.hardpoints.size(); ++hi) {
            Hardpoint& hp = st.hardpoints[hi];
            ModuleSlot* wSlot = hp.FirstWeapon();
            if (!hp.alive || !wSlot || !wSlot->equipped.has_value()) continue;
            if (hp.fireCooldown > 0.0f) { hp.fireCooldown -= dt; continue; }

            const WeaponStats& ws = wSlot->equipped->weapon;
            if (bestDist > ws.projRange) continue;

            Vector2 hpPos = GetNpcStationHardpointPos(st, hi);
            Vector2 toT   = Vector2Subtract(fireTarget, hpPos);
            float   len   = Vector2Length(toT);
            if (len < 1.0f) { hp.fireCooldown = ws.fireRate; continue; }
            Vector2 dir = Vector2Scale(toT, 1.0f / len);

            int shots = (ws.projType == WeaponProjType::Burst || ws.projType == WeaponProjType::Spread)
                        ? ws.burstCount : 1;
            float spread = ws.spreadAngle;
            float aimAng = atan2f(toT.x, -toT.y) * RAD2DEG;
            for (int b = 0; b < shots; ++b) {
                float spOff = (shots > 1) ? spread * ((float)b / (shots - 1) - 0.5f) : 0.0f;
                float fRad  = (aimAng + spOff) * DEG2RAD;
                Vector2 fd  = { sinf(fRad), -cosf(fRad) };
                Projectile sp;
                sp.position   = hpPos;
                sp.velocity   = { fd.x * ws.projSpeed, fd.y * ws.projSpeed };
                sp.maxLife    = ws.projRange / ws.projSpeed;
                sp.damage     = ws.damage;
                sp.fromPlayer = false;
                sp.ownerId    = st.id;
                if (ws.fireMode == WeaponFireMode::LockOn) {
                    sp.isHoming = true; sp.turnRate = 3.0f;
                    sp.targetId = fireTargetId; sp.targetIsPlayer = fireTargetIsPlayer;
                }
                _w->projectiles.push_back(sp);
            }
            hp.fireCooldown = ws.fireRate + ws.chargeTime;
        }
    }
}

// Capital ships fire differently from stations: each weapon hardpoint picks
// its OWN nearest hostile independently (no single shared ship target), so
// one capital can engage several enemies at once with different batteries.
// Per-shot projectile spawning mirrors UpdateWorldStationFire verbatim.
void SpaceFlight::UpdateCapitalFire(float dt) {
    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        NpcMeta& m = _w->npcMeta[i];
        if (!m.alive || m.hardpoints.empty()) continue;
        Vector2 shipPos = _w->entities[i].transform.position;
        float   shipRot = _w->entities[i].transform.rotation;

        for (int hIdx = 0; hIdx < (int)m.hardpoints.size(); ++hIdx) {
            Hardpoint& hp = m.hardpoints[hIdx];
            ModuleSlot* wSlot = hp.FirstWeapon();
            if (!hp.alive || !wSlot || !wSlot->equipped.has_value() || hp.shed) continue;
            // Epic 8: the player fires this hardpoint directly while seated
            // in it — UpdateSeatedTurret manages its cooldown/firing instead.
            if (_seated && m.id == _seatedNpcId && hIdx == _seatedHardpointIdx) continue;
            if (hp.fireCooldown > 0.0f) { hp.fireCooldown -= dt; continue; }

            const WeaponStats& ws = wSlot->equipped->weapon;
            Vector2 hpPos = GetCapitalHardpointWorldPos(shipPos, shipRot, hp.localOffset);

            combat::HostileTarget pick = combat::FindNearestHostileTarget(
                *_w, _playerEntity, m.npcFaction, hpPos, ws.projRange, 0,
                !_stationServicesMenu.isOpen && !_seated);

            // A capital can take damage from an attacker it isn't
            // diplomatically Hostile to (see UpdateNpcCollisions' capital
            // hit block), so it must also be able to retaliate against one —
            // mirrors the retaliatingVsPlayer/retaliationTargetId override
            // the fighter fire-target scan applies for the same reason.
            float bestDist = pick.valid ? Vector2Distance(hpPos, pick.position) : ws.projRange;
            if (m.retaliatingVsPlayer && !_stationServicesMenu.isOpen && !_seated) {
                float d = Vector2Distance(hpPos, _playerEntity.transform.position);
                if (d <= ws.projRange && (!pick.valid || d < bestDist)) {
                    bestDist = d;
                    pick = { combat::HostileTargetKind::Player, 0, _playerEntity.transform.position, true };
                }
            }
            if (m.retaliationTargetId != 0) {
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                        float d = Vector2Distance(hpPos, _w->entities[j].transform.position);
                        if (d <= ws.projRange && (!pick.valid || d < bestDist)) {
                            bestDist = d;
                            pick = { combat::HostileTargetKind::Npc, m.retaliationTargetId, _w->entities[j].transform.position, true };
                        }
                        break;
                    }
                }
            }
            if (!pick.valid) continue;

            // Redirect aim to one of the target's own alive hardpoints rather
            // than its raw center (see UpdateWorldStationFire for the same fix).
            Vector2 aimPos = pick.position;
            if (pick.kind == combat::HostileTargetKind::Station) {
                for (const SpaceStation& tst : _w->stations)
                    if (tst.id == pick.id) { aimPos = GetStationAimPos(tst); break; }
            } else if (pick.kind == combat::HostileTargetKind::Npc) {
                for (size_t j = 0; j < _w->npcMeta.size(); ++j)
                    if (_w->npcMeta[j].id == pick.id) { aimPos = GetNpcAimPos(*_w, j); break; }
            }

            Vector2 toT = Vector2Subtract(aimPos, hpPos);
            float   len = Vector2Length(toT);
            if (len < 1.0f) { hp.fireCooldown = ws.fireRate; continue; }

            int shots = (ws.projType == WeaponProjType::Burst || ws.projType == WeaponProjType::Spread)
                        ? ws.burstCount : 1;
            float spread = ws.spreadAngle;
            float aimAng = atan2f(toT.x, -toT.y) * RAD2DEG;
            for (int b = 0; b < shots; ++b) {
                float spOff = (shots > 1) ? spread * ((float)b / (shots - 1) - 0.5f) : 0.0f;
                float fRad  = (aimAng + spOff) * DEG2RAD;
                Vector2 fd  = { sinf(fRad), -cosf(fRad) };
                Projectile sp;
                sp.position   = hpPos;
                sp.velocity   = { fd.x * ws.projSpeed, fd.y * ws.projSpeed };
                sp.maxLife    = ws.projRange / ws.projSpeed;
                sp.damage     = ws.damage;
                sp.fromPlayer = false;
                sp.ownerId    = m.id;
                if (ws.fireMode == WeaponFireMode::LockOn) {
                    sp.isHoming = true; sp.turnRate = 3.0f;
                    sp.targetId = pick.id; sp.targetIsPlayer = (pick.kind == combat::HostileTargetKind::Player);
                }
                _w->projectiles.push_back(sp);
            }
            hp.fireCooldown = ws.fireRate + ws.chargeTime;
        }
    }
}

// Epic 8: manning a capital's hardpoint. Movement/aiming/firing here
// deliberately mirror UpdateCapitalFire's own shot-spawning (same burst/
// spread/homing handling) rather than the player's own ship weapon code —
// hardpoint weapons don't have the player ship's charge-fire distinction,
// so "hold left mouse, fire on cooldown" is the correct model to copy.
void SpaceFlight::UpdateSeatedTurret(float dt, Vector2 mouseWorld, bool clickedHudBtn) {
    for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
        NpcMeta& m = _w->npcMeta[i];
        if (m.id != _seatedNpcId) continue;
        if (!m.alive || _seatedHardpointIdx < 0 || _seatedHardpointIdx >= (int)m.hardpoints.size() ||
            !m.hardpoints[_seatedHardpointIdx].alive) {
            // Capital destroyed, or this specific hardpoint died since
            // seating — auto-eject, leaving the player parked exactly where
            // the seat last was rather than snapping back to some stale
            // pre-seat position.
            _seated = false;
            _seatedNpcId = 0;
            _seatedHardpointIdx = -1;
            AddCommsMessage("Turret lost — ejected.", true);
            return;
        }

        ecs::Entity&    e  = _w->entities[i];
        Hardpoint& hp = m.hardpoints[_seatedHardpointIdx];
        Vector2 hpPos = GetCapitalHardpointWorldPos(e.transform.position, e.transform.rotation, hp.localOffset);

        _playerEntity.transform.position = hpPos;
        _playerEntity.transform.velocity = { 0.0f, 0.0f };
        _playerEntity.transform.rotation = e.transform.rotation;

        if (hp.fireCooldown > 0.0f) hp.fireCooldown -= dt;
        ModuleSlot* wSlot = hp.FirstWeapon();
        if (clickedHudBtn || !wSlot || !wSlot->equipped.has_value()) return;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || hp.fireCooldown > 0.0f) return;

        const WeaponStats& ws  = wSlot->equipped->weapon;
        Vector2             toT = Vector2Subtract(mouseWorld, hpPos);
        float               len = Vector2Length(toT);
        if (len < 1.0f) return;

        int   shots  = (ws.projType == WeaponProjType::Burst || ws.projType == WeaponProjType::Spread)
                       ? ws.burstCount : 1;
        float spread = ws.spreadAngle;
        float aimAng = atan2f(toT.x, -toT.y) * RAD2DEG;
        for (int b = 0; b < shots; ++b) {
            float spOff = (shots > 1) ? spread * ((float)b / (shots - 1) - 0.5f) : 0.0f;
            float fRad  = (aimAng + spOff) * DEG2RAD;
            Vector2 fd  = { sinf(fRad), -cosf(fRad) };
            Projectile sp;
            sp.position   = hpPos;
            sp.velocity   = { fd.x * ws.projSpeed, fd.y * ws.projSpeed };
            sp.maxLife    = ws.projRange / ws.projSpeed;
            sp.damage     = ws.damage;
            sp.fromPlayer = true;
            // Owned by the capital being manned (matches UpdateCapitalFire's own
            // AI-fired convention), not 0 (the normal player-ship convention) —
            // the capital-hardpoint hit-test block's self-exclusion is keyed off
            // m.id == p.ownerId, so ownerId=0 left every hardpoint on the manned
            // capital, including the one just fired from, eligible to take a hit
            // from its own outgoing shot on the very next collision pass.
            sp.ownerId    = m.id;
            sp.effect         = ws.effect; // Epic 9.2: a friendly capital's own Ion Cannon can capture fighters too
            sp.effectDuration = ws.effectDuration;
            _w->projectiles.push_back(sp);
        }
        hp.fireCooldown = ws.fireRate + ws.chargeTime;
        return;
    }

    // The seated NPC no longer exists at all (e.g. despawned/culled) — same
    // auto-eject fallback as the died-in-place case above.
    _seated = false;
    _seatedNpcId = 0;
    _seatedHardpointIdx = -1;
    AddCommsMessage("Turret lost — ejected.", true);
}

// Advance projectiles (movement, lifetime, homing) and drift asteroids for
// the world `_w` points at. Shared by the active system and background ticks.
void SpaceFlight::AdvanceProjectilesAndAsteroids(float dt) {
    for (Projectile& p : _w->projectiles) {
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.lifetime += dt;
        if (p.lifetime >= p.maxLife) p.alive = false;
    }

    for (Projectile& p : _w->projectiles) {
        if (!p.alive || !p.isHoming) continue;
        Vector2 tPos = {};
        bool    found = false;
        if (p.targetIsPlayer) {
            // In a background world the player is parked far away; homing at
            // that point would fling the projectile out of the system.
            if (!_bgTick) { tPos = _playerEntity.transform.position; found = true; }
        }
        else if (p.targetId != 0) {
            for (const Asteroid& a : _w->asteroids)
                if (a.id == p.targetId && a.alive) { tPos = a.position; found = true; break; }
            if (!found)
                for (size_t li = 0; li < _w->npcMeta.size(); ++li)
                    if (_w->npcMeta[li].id == p.targetId && _w->npcMeta[li].alive) { tPos = GetNpcAimPos(*_w, li); found = true; break; }
            if (!found)
                for (const SpaceStation& st : _w->stations)
                    if (st.id == p.targetId && st.alive) { tPos = GetStationAimPos(st); found = true; break; }
        }
        if (!found) { p.isHoming = false; continue; }
        float speed = Vector2Length(p.velocity);
        if (speed < 1.0f) continue;
        float curAng = atan2f(p.velocity.y, p.velocity.x);
        Vector2 diff = Vector2Subtract(tPos, p.position);
        float   tAng = atan2f(diff.y, diff.x);
        float   delta = tAng - curAng;
        while (delta > PI) delta -= 2.0f * PI;
        while (delta < -PI) delta += 2.0f * PI;
        float turn = std::clamp(delta, -p.turnRate * dt, p.turnRate * dt);
        float newAng = curAng + turn;
        p.velocity = { cosf(newAng) * speed, sinf(newAng) * speed };
    }

    for (Asteroid& a : _w->asteroids) {
        a.position.x += a.velocity.x * dt;
        a.position.y += a.velocity.y * dt;
        a.rotation   += a.rotSpeed   * dt;
    }
}

// Despawn far-away asteroids/NPCs and repopulate the space around `anchor` —
// the local player in the active world, or a remote occupant in a background
// world. Also erases dead projectiles/asteroids for the ticked world.
void SpaceFlight::CullAndRespawnAround(float dt, Vector2 anchor) {
    // Compute the half-diagonal of the visible world area so spawns never
    // appear inside the camera frustum regardless of zoom level. Cull
    // distances below are derived from this too (floored at the old fixed
    // values) — at low zoom the visible area can exceed a fixed cull radius,
    // which used to destroy every asteroid/NPC the instant it spawned just
    // outside the view (zoomed all the way out, nothing could ever spawn).
    float halfW     = (float)GetScreenWidth()  / (2.0f * _cameraZoom);
    float halfH     = (float)GetScreenHeight() / (2.0f * _cameraZoom);
    float viewEdge  = sqrtf(halfW * halfW + halfH * halfH) + 150.0f;

    float asteroidCullDist = std::max(2800.0f, viewEdge + 1800.0f);
    float npcCullDist      = std::max(3000.0f, viewEdge + 1800.0f);

    for (Asteroid& a : _w->asteroids)
        if (a.alive && Vector2Distance(anchor, a.position) > asteroidCullDist)
            a.alive = false;
    for (size_t ci = 0; ci < _w->npcMeta.size(); ++ci)
        if (_w->npcMeta[ci].alive && !_w->npcMeta[ci].wingman &&
            Vector2Distance(anchor, _w->entities[ci].transform.position) > npcCullDist) {
            _w->npcMeta[ci].alive = false;
            _w->npcFreeSlots.push_back(ci);
        }

    auto isDead = [](const auto& e) { return !e.alive; };
    _w->projectiles.erase(std::remove_if(_w->projectiles.begin(), _w->projectiles.end(), isDead), _w->projectiles.end());
    _w->asteroids.erase(std::remove_if(_w->asteroids.begin(), _w->asteroids.end(), isDead), _w->asteroids.end());
    // NPC slots are NOT erased — dead slots are held in _w->npcFreeSlots for reuse.

    static constexpr int MaxAsteroids = 40;
    static constexpr int MaxNpcShips = 20;
    _w->respawnTimer -= dt;
    if (_w->respawnTimer <= 0.0f) {
        _w->respawnTimer = 5.0f;

        int liveAsteroids = (int)std::count_if(_w->asteroids.begin(), _w->asteroids.end(),
            [](const Asteroid& a) { return a.alive; });
        for (int s = 0; s < 2 && liveAsteroids < MaxAsteroids; ++s, ++liveAsteroids) {
            float ang     = (float)GetRandomValue(0, 359) * DEG2RAD;
            float minDist = std::max(1100.0f, viewEdge);
            float dist    = minDist + (float)GetRandomValue(0, 800);
            Vector2 pos = { anchor.x + cosf(ang) * dist,
                            anchor.y + sinf(ang) * dist };
            Asteroid ra = MakeAsteroid(pos, GetRandomValue(0, 2));
            AssignAsteroidMaterials(ra);
            _w->asteroids.push_back(std::move(ra));
        }
        int liveNpcs = (int)std::count_if(_w->npcMeta.begin(), _w->npcMeta.end(),
            [](const NpcMeta& m) { return m.alive; });
        // Epic 12.3: suppress hostile rolls while the tutorial protects the home system.
        bool suppressHostile = _tutorialActive && _w->galaxyId == 1 && _w->systemId == 1;
        for (int s = 0; s < 2 && liveNpcs < MaxNpcShips; ++s, ++liveNpcs) {
            Vector2 pos = { 0.0f, 0.0f };

            // 50-attempt retry loop for dynamic spawns
            for (int attempt = 0; attempt < 50; ++attempt) {
                float ang = (float)GetRandomValue(0, 359) * DEG2RAD;
                float minDist = std::max(1200.0f, viewEdge);
                float dist = minDist + (float)GetRandomValue(0, 800);

                pos = { anchor.x + cosf(ang) * dist,
                        anchor.y + sinf(ang) * dist };

                // Validate that this off-screen location isn't right on top of a station or existing enemy
                if (SpawnPosSafeFromStations(pos, _w->stations, kEnemySpawnMargin) &&
                    SpawnPosSafeFromNpcs(pos, _w->npcMeta, _w->entities, 400.0f)) {
                    break; // Found a safe spot!
                }
            }

            auto [ne, nm] = MakeNpcEntity(_w->nextNpcId++, pos, suppressHostile);
            ApplyNpcLoadout(ne, nm);
            nm.preferredRange = nm.attackRange * 0.75f;
            ne.health.currentHull = ne.health.maxStats.hull;
            ne.network.networkId  = ne.id;   // expose to snapshots
            if (!_w->npcFreeSlots.empty()) {
                size_t slot = _w->npcFreeSlots.back(); _w->npcFreeSlots.pop_back();
                _w->entities[slot] = std::move(ne); _w->npcMeta[slot] = std::move(nm);
            } else {
                _w->entities.push_back(std::move(ne)); _w->npcMeta.push_back(std::move(nm));
            }
        }
    }
}

// Simulate one frame of a system the local player is NOT in. Reuses the same
// per-world simulation functions as the active system by re-pointing `_w`.
// The local player is parked far outside the world for the duration, so AI,
// collisions and sun gravity treat them as absent (every player interaction in
// those functions is distance-gated); culling/respawn anchors on one of the
// system's remote occupants instead.
void SpaceFlight::TickBackgroundWorld(float dt, SystemWorld& world) {
    SystemWorld* prevWorld = _w;
    _w      = &world;
    _bgTick = true;

    const Vector2 savedPos = _playerEntity.transform.position;
    const Vector2 savedVel = _playerEntity.transform.velocity;
    _playerEntity.transform.position = { 1.0e9f, 1.0e9f };
    _playerEntity.transform.velocity = { 0.0f, 0.0f };

    world.age += dt;
    AdvanceProjectilesAndAsteroids(dt);
    UpdateOrbits(dt);
    UpdateNpcShips(dt);
    ApplySunGravity(dt);
    UpdateWorldStationFire(dt);
    UpdateCapitalFire(dt);
    UpdateCollisions();
    UpdateNpcCollisions();

    // Anchor culling/repopulation on a player who's actually here.
    for (const auto& [netId, re] : _remoteEntities) {
        if (re.id == 0) continue;
        auto it = _peerSystem.find(netId);
        if (it != _peerSystem.end() && it->second == world.systemId) {
            CullAndRespawnAround(dt, re.transform.position);
            break;
        }
    }

    _playerEntity.transform.position = savedPos;
    _playerEntity.transform.velocity = savedVel;
    _bgTick = false;
    _w      = prevWorld;
}

void SpaceFlight::Draw() {
    bool menuOpen = (_storageMenu.isOpen || _modulesMenu.isOpen || _galaxyMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _commsLogOpen || _enterPopupOpen || _stationServicesMenu.isOpen || _localMapOpen ||
        _buildMenu.isOpen || _stationModMenu.isOpen || _miningMenu.isOpen || _placementConfirmOpen);
    Vector2 mouse = GetMousePosition();
    int hy = GetScreenHeight() - HudH - 6;

    // ModulesMenu and StorageMenu manage the cursor themselves to avoid flicker
    bool selfManagedCursor = _modulesMenu.isOpen || _storageMenu.isOpen;
    if (_inPlacementMode && !_placementConfirmOpen) {
        ShowCursor();  // show cursor in placement mode so player can aim
    }
    else if (!menuOpen && mouse.y < hy) {
        HideCursor();
    }
    else if (!selfManagedCursor) {
        ShowCursor();
    }

    DrawBackground();

    BeginMode2D(_camera);

    DrawSun();
    DrawPlanets();
    DrawStations();
    DrawPlayerStations();

    // ── Determine what preview to draw ──
    std::string previewId = _placingStationDefId;
    if (previewId.empty() && _buildMenu.isOpen && !_buildMenu.IsMouseOverMenu()) {
        previewId = _buildMenu.GetSelectedStationId();
    }

    if (!previewId.empty() && !_placementConfirmOpen) {
        Vector2 worldMouse = GetScreenToWorld2D(GetMousePosition(), _camera);
        const PlayerStationDef* def = PlayerStationRegistry::ById(previewId);
        float rad = def ? def->radius : 120.0f;

        if (_stationBaseTex.id > 0 && previewId != "mining_station") {
            float size = rad * 2.0f;
            Rectangle src = { 0.0f, 0.0f, (float)_stationBaseTex.width, (float)_stationBaseTex.height };
            Rectangle dst = { worldMouse.x, worldMouse.y, size, size };
            Vector2 origin = { size * 0.5f, size * 0.5f };
            DrawTexturePro(_stationBaseTex, src, dst, origin, 0.0f, Color{ 255, 255, 255, 140 });
        }
        else {
            Rectangle box = { worldMouse.x - rad, worldMouse.y - rad, rad * 2.0f, rad * 2.0f };
            DrawRectangleRec(box, Color{ 60, 130, 220, 80 });
            DrawRectangleLinesEx(box, 1.5f, Color{ 80, 160, 255, 180 });
        }
    } // ◄◄ NOTICE THIS CLOSING BRACE - The ship preview MUST be outside it!

    // ── Draw Ship Preview Ghost ──
    std::string previewShipId = _placingShipDefId;
    if (previewShipId.empty() && _stationModMenu.isOpen && !_stationModMenu.IsMouseOverMenu()) {
        previewShipId = _stationModMenu.GetSelectedShipId();
    }

    if (!previewShipId.empty() && !_shipPlacementConfirmOpen) {
        Vector2 worldMouse = GetScreenToWorld2D(GetMousePosition(), _camera);
        const ecs::ShipDef* previewDef = ecs::ShipRegistry::ShipById(previewShipId);
        Texture2D* previewTexPtr = nullptr;
        if (previewDef) previewTexPtr = ResourceManager::Load(previewDef->assetPath);
        const Texture2D& tex = (previewShipId == "gargos") ? _gargosTex
            : (previewTexPtr ? *previewTexPtr : Texture2D{});

        if (tex.id > 0) {
            float ps = previewDef ? previewDef->pixelScale : 1.0f;
            float tw = (float)tex.width * ps, th = (float)tex.height * ps;
            DrawTexturePro(tex, { 0, 0, (float)tex.width, (float)tex.height },
                           { worldMouse.x, worldMouse.y, tw, th }, { tw / 2, th / 2 }, 0.0f, Color{ 255, 255, 255, 140 });
        }
        else {
            DrawCircleV(worldMouse, 20.0f, Color{ 60, 130, 220, 80 });
            DrawCircleLinesV(worldMouse, 20.0f, Color{ 80, 160, 255, 180 });
        }
    }

    {
        float asteroidLightRange = _w->sun.active ? _w->sun.gravRange * 5.0f : 0.0f;
        for (const Asteroid& a : _w->asteroids) {
            if (!a.alive) continue;
            const Texture2D* atex = a.tier == 2 ? &_asteroidTexLarge
                                  : a.tier == 1 ? &_asteroidTexMedium
                                  :               &_asteroidTexSmall;
            DrawAsteroid(a, atex, _lighting, asteroidLightRange);
        }
    }

    DrawNpcShips();
    DrawRemotePlayers();

    for (const Projectile& p : _w->projectiles) {
        float age = p.lifetime / p.maxLife;
        auto  a8 = (unsigned char)((1.0f - age) * 255.0f);
        if (p.isHoming) {
            DrawCircleV(p.position, 5.0f, Color{ 220, 60, 220, a8 });
            DrawCircleV(p.position, 9.0f, Color{ 180, 40, 180, (unsigned char)(a8 / 3) });
        }
        else {
            DrawCircleV(p.position, 3.5f, Color{ 255, 220, 100, a8 });
            DrawCircleV(p.position, 6.0f, Color{ 255, 160,  40, (unsigned char)(a8 / 3) });
        }
    }
    // Draw server-authoritative projectiles on client (host renders its own _w->projectiles above).
    for (const net::ProjectileSnapshot& rp : _remoteProjectiles) {
        DrawCircleV({ rp.posX, rp.posY }, 3.5f, Color{ 255, 220, 100, 220 });
        DrawCircleV({ rp.posX, rp.posY }, 6.0f, Color{ 255, 160,  40,  55 });
    }

    if (!_stationServicesMenu.isOpen && !_seated &&
        (_hitCooldown <= 0.0f || (int)(_hitCooldown * 8) % 2 == 0)) {
        const Vector2& pos = _playerEntity.transform.position;
        const float rot = _playerEntity.transform.rotation;
        const ecs::ShipDef* pDefPtr = ecs::ShipRegistry::ShipById(_playerMeta.defId);
        Texture2D* pTexPtr = _playerShipTex;

        if (pTexPtr && pTexPtr->id > 0) {
            float tw = (float)pTexPtr->width, th = (float)pTexPtr->height;
            float ps = pDefPtr ? pDefPtr->pixelScale : 1.0f;
            if (_playerMeta.thrusting) {
                float flicker = 0.92f + 0.08f * sinf((float)GetTime() * 16.0f);
                Vector2 exhOff = Vector2Rotate({ 0.0f, th * ps * 0.38f }, rot * DEG2RAD);
                Vector2 exhPos = Vector2Add(pos, exhOff);
                DrawCircleV(exhPos, 4.5f * flicker, Color{ 80, 255, 120, 140 });
                DrawCircleV(exhPos, 7.0f * flicker,  Color{ 20, 200,  60,  55 });
            }
            Rectangle src    = { 0.0f, 0.0f, tw, th };
            Rectangle dst    = { pos.x, pos.y, tw * ps, th * ps };
            Vector2   origin = { tw * ps / 2.0f, th * ps / 2.0f };
            float     lightRange = _w->sun.active ? _w->sun.gravRange * 5.0f : 0.0f;
            Color     lit = _lighting.BeginLit(pos, { 0.0f, 0.0f }, lightRange);
            DrawTexturePro(*pTexPtr, src, dst, origin, rot, lit);
            _lighting.EndLit();
            // P2: composited module render (placeholder icons — see RenderSystem.h).
            ecs::RenderSystem::DrawHardpointRig(pos, rot, ps, _loadout.hardpoints, WHITE, WHITE);
        } else {
            float r  = rot * DEG2RAD;
            float cr = cosf(r), sr = sinf(r);
            float sz = _playerMeta.radius;
            auto  R  = [&](Vector2 v) -> Vector2 {
                return Vector2Add(pos, { v.x * cr - v.y * sr, v.x * sr + v.y * cr });
            };
            Vector2 tip   = R({  0.0f,       -sz         });
            Vector2 left  = R({ -sz * 0.6f,   sz * 0.55f });
            Vector2 right = R({  sz * 0.6f,   sz * 0.55f });
            if (_playerMeta.thrusting) {
                float flicker = 0.92f + 0.08f * sinf((float)GetTime() * 16.0f);
                Vector2 exhOff = Vector2Rotate({ 0.0f, sz * 0.55f }, rot * DEG2RAD);
                Vector2 exhPos = Vector2Add(pos, exhOff);
                DrawCircleV(exhPos, 4.5f * flicker, Color{ 80, 255, 120, 140 });
                DrawCircleV(exhPos, 7.0f * flicker,  Color{ 20, 200,  60,  55 });
            }
            DrawTriangle(tip, right, left, Color{ 60, 140, 230, 255 });
            DrawTriangleLines(tip, right, left, Color{ 140, 200, 255, 255 });
        }
    }

    DrawWarpParticles();

    EndMode2D();

    if (_warpFadeAlpha > 0.0f) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2,
            Color{ 0, 0, 0, (unsigned char)(std::clamp(_warpFadeAlpha, 0.0f, 1.0f) * 255.0f) });
    }

    if (_lockTargetId != 0 && !menuOpen) {
        Vector2 sp = GetWorldToScreen2D(_lockTargetPos, _camera);
        float   rsz = 22.0f;
        DrawRectangleLinesEx({ sp.x - rsz, sp.y - rsz, rsz * 2.0f, rsz * 2.0f },
            1.5f, Color{ 220, 60, 220, 210 });
        const char* lbl = "LOCKED";
        DrawText(lbl, (int)(sp.x - MeasureText(lbl, 10) / 2), (int)(sp.y + rsz + 3),
            10, Color{ 220, 60, 220, 210 });
    }

    if (_localMapOpen) {
        DrawLocalMap();
        return;
    }

    if (_modulesMenu.isOpen) {
        _modulesMenu.Draw();
        return;
    }
    if (_storageMenu.isOpen) {
        _storageMenu.Draw();
        return;
    }

    DrawHUD();

    if (_escortMenuOpen) {
        int sw2 = GetScreenWidth();
        DrawRectangle(0, 0, sw2, GetScreenHeight(), Color{ 1, 4, 1, 255 });
        const char* title = "ESCORT WING";
        DrawText(title, sw2 / 2 - MeasureText(title, 24) / 2, 18, 24, Color{ 68,162,68,255 });
        DrawRectangle(sw2 / 2 - 200, 52, 400, 1, Color{ 34,98,34,170 });

        Vector2 m2 = GetMousePosition();
        Rectangle backBtn = { 18.0f, 16.0f, 120.0f, 38.0f };
        bool hovBack = CheckCollisionPointRec(m2, backBtn);
        DrawRectangleRec(backBtn, hovBack ? Color{ 50,95,50,230 } : Color{ 12,28,12,220 });
        DrawRectangleLinesEx(backBtn, 1.0f, Color{ 40,160,40,200 });
        const char* bl = "< BACK";
        DrawText(bl, (int)(backBtn.x + (backBtn.width - MeasureText(bl, 14)) / 2),
            (int)(backBtn.y + (backBtn.height - 14) / 2), 14, WHITE);

        std::vector<size_t> wingmenIdxs;
        for (size_t di = 0; di < _w->npcMeta.size(); ++di)
            if (_w->npcMeta[di].alive && _w->npcMeta[di].wingman) wingmenIdxs.push_back(di);
        if (wingmenIdxs.empty()) return;

        size_t selIdx = wingmenIdxs[0];
        for (size_t di : wingmenIdxs)
            if (_w->npcMeta[di].id == _escortMenuSelId) { selIdx = di; break; }
        const NpcMeta* sel = &_w->npcMeta[selIdx];

        static constexpr int ICON_W = 150, ICON_H = 50, ICON_GAP = 16;
        int totalIconW = (int)wingmenIdxs.size() * ICON_W + ((int)wingmenIdxs.size() - 1) * ICON_GAP;
        int iconStartX = sw2 / 2 - totalIconW / 2;
        for (int i = 0; i < (int)wingmenIdxs.size(); ++i) {
            Rectangle ir = { (float)(iconStartX + i * (ICON_W + ICON_GAP)), 70.0f,
                              (float)ICON_W, (float)ICON_H };
            bool selThis = (_w->npcMeta[wingmenIdxs[i]].id == _escortMenuSelId);
            bool hovThis = CheckCollisionPointRec(m2, ir);
            DrawRectangleRec(ir, selThis ? Color{ 20,55,20,240 } : (hovThis ? Color{ 18,42,18,220 } : Color{ 10,20,10,200 }));
            DrawRectangleLinesEx(ir, selThis ? 2.0f : 1.0f,
                selThis ? Color{ 80,200,80,255 } : Color{ 34,98,34,160 });
            std::string iconLblS = _w->npcMeta[wingmenIdxs[i]].shipTypeName + "  #" + std::to_string(_w->npcMeta[wingmenIdxs[i]].id);
            const char* iconLbl = iconLblS.c_str();
            DrawText(iconLbl, (int)(ir.x + (ir.width - MeasureText(iconLbl, 12)) / 2),
                (int)(ir.y + 8), 12, selThis ? WHITE : Color{ 100,180,100,220 });
            float hp = _w->entities[wingmenIdxs[i]].health.currentHull / _w->entities[wingmenIdxs[i]].health.maxStats.hull;
            int bx = (int)ir.x + 8, by = (int)(ir.y + 30), bw = ICON_W - 16;
            DrawRectangle(bx, by, bw, 5, Color{ 20,32,20,200 });
            DrawRectangle(bx, by, (int)(bw * hp), 5,
                hp > 0.5f ? Color{ 60,200,60,220 } : hp > 0.25f ? Color{ 200,160,30,220 } : Color{ 200,50,30,220 });
        }

        DrawRectangle(30, 130, sw2 - 60, 1, Color{ 34,98,34,160 });

        std::string selLblS = "ESCORT: " + sel->shipTypeName + "  #" + std::to_string(sel->id);
        DrawText(selLblS.c_str(), 30, 142, 14, Color{ 68,162,68,255 });
        float selHull    = _w->entities[selIdx].health.currentHull;
        float selMaxHull = _w->entities[selIdx].health.maxStats.hull;
        float hpPct = selHull / selMaxHull;
        char hpLbl[64];
        std::snprintf(hpLbl, sizeof(hpLbl), "Hull  %.0f / %.0f", selHull, selMaxHull);
        Color hpCol = hpPct > 0.5f ? Color{ 60,200,60,230 }
        : hpPct > 0.25f ? Color{ 200,160,30,230 } : Color{ 200,50,30,230 };
        DrawText(hpLbl, 30, 162, 12, hpCol);

        DrawText("MODULES:", 30, 195, 12, Color{ 68,162,68,200 });
        static const ModuleType modTypes[] = { ModuleType::Weapon, ModuleType::Armor,
                                               ModuleType::Shield, ModuleType::Engine };
        static const char* modLabels[] = { "W", "A", "S", "E" };
        const std::optional<ModuleDef>* modPtrs[4] = {};
        auto selWeaponSlots = sel->loadout.WeaponSlots();
        auto selShieldSlots = sel->loadout.ShieldSlots();
        const ModuleSlot* selArmor  = sel->loadout.Armor();
        const ModuleSlot* selEngine = sel->loadout.Engine();
        modPtrs[0] = !selWeaponSlots.empty() ? &selWeaponSlots[0]->equipped : nullptr;
        modPtrs[1] = selArmor  ? &selArmor->equipped  : nullptr;
        modPtrs[2] = !selShieldSlots.empty() ? &selShieldSlots[0]->equipped : nullptr;
        modPtrs[3] = selEngine ? &selEngine->equipped : nullptr;
        int mx = 30;
        for (int i = 0; i < 4; ++i) {
            const char* mname = (modPtrs[i] && modPtrs[i]->has_value())
                ? (*modPtrs[i])->displayName.c_str() : "--";
            Color lc = StorageMenu::TypeColor(modTypes[i]);
            DrawRectangle(mx, 212, 200, 26, Color{ 10,18,10,200 });
            DrawRectangleLinesEx({ (float)mx, 212.0f, 200.0f, 26.0f }, 1.0f, Color{ 34,98,34,140 });
            DrawText(modLabels[i], mx + 4, 217, 11, { lc.r, lc.g, lc.b, 200 });
            DrawText(mname, mx + 18, 217, 11, Color{ 180,210,180,230 });
            mx += 210;
        }

        Rectangle editBtn = { 30.0f, 260.0f, 180.0f, 38.0f };
        Rectangle dismissBtn = { 230.0f, 260.0f, 180.0f, 38.0f };
        bool hovEdit = CheckCollisionPointRec(m2, editBtn);
        bool hovDismiss = CheckCollisionPointRec(m2, dismissBtn);
        DrawRectangleRec(editBtn, hovEdit ? Color{ 30,80,50,230 } : Color{ 12,35,20,200 });
        DrawRectangleLinesEx(editBtn, 1.0f, Color{ 40,160,80,200 });
        const char* elbl = "EDIT MODULES";
        DrawText(elbl, (int)(editBtn.x + (editBtn.width - MeasureText(elbl, 13)) / 2),
            (int)(editBtn.y + 13), 13, hovEdit ? WHITE : Color{ 80,210,130,220 });
        DrawRectangleRec(dismissBtn, hovDismiss ? Color{ 80,25,25,230 } : Color{ 28,10,10,200 });
        DrawRectangleLinesEx(dismissBtn, 1.0f, Color{ 160,45,45,200 });
        const char* dlbl = "DISMISS ESCORT";
        DrawText(dlbl, (int)(dismissBtn.x + (dismissBtn.width - MeasureText(dlbl, 13)) / 2),
            (int)(dismissBtn.y + 13), 13, hovDismiss ? WHITE : Color{ 210,70,70,220 });
        return;
    }

    if (_commsMenuOpen && _commsMenuIsStation) {
        // Epic 13: station hail — a condensed remote view of the contract
        // board, same offers/accept flow as StationServicesMenu's Contracts
        // screen, just reachable without docking.
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0, 0, 0, 140 });
        static constexpr int CW = 560, CH = 360;
        int mcx = sw2 / 2 - CW / 2, mcy = sh2 / 2 - CH / 2;
        DrawRectangle(mcx, mcy, CW, CH, Color{ 8, 14, 22, 245 });
        DrawRectangleLinesEx({ (float)mcx, (float)mcy, (float)CW, (float)CH },
            1.5f, Color{ 40, 130, 200, 220 });

        std::string hdrStr = "COMMS  --  " + _commsMenuNpcName;
        const char* hdr = hdrStr.c_str();
        DrawText(hdr, mcx + CW / 2 - MeasureText(hdr, 14) / 2, mcy + 14, 14,
            Color{ 60, 160, 220, 255 });
        DrawRectangle(mcx + 16, mcy + 36, CW - 32, 1, Color{ 40, 100, 160, 160 });

        if (_hasActiveContract) {
            DrawText(_commsMenuNpcText.c_str(), mcx + 24, mcy + 60, 14,
                Color{ 190, 215, 245, 230 });
        }
        else if (_contractOffers.empty()) {
            const char* msg = "NO CONTRACTS AVAILABLE";
            DrawText(msg, mcx + CW / 2 - MeasureText(msg, 14) / 2, mcy + 60, 14,
                Color{ 220, 180, 80, 230 });
        }
        else {
            static constexpr float RowH = 70.0f, RowGap = 10.0f;
            float listY = (float)mcy + 60.0f;
            Vector2 mHov = GetMousePosition();
            for (size_t oi = 0; oi < _contractOffers.size(); ++oi) {
                const Contract& c = _contractOffers[oi];
                Rectangle row = { (float)mcx + 20.0f, listY + oi * (RowH + RowGap),
                                   (float)CW - 40.0f, RowH };
                DrawRectangle((int)row.x, (int)row.y, (int)row.width, (int)row.height,
                    Color{ 4, 10, 18, 200 });
                DrawRectangleLinesEx(row, 1.0f, Color{ 20, 60, 100, 180 });
                DrawText(c.title.c_str(), (int)(row.x + 14), (int)(row.y + 10), 14,
                    Color{ 190, 215, 245, 230 });
                DrawText(c.briefing.c_str(), (int)(row.x + 14), (int)(row.y + 32), 11,
                    Color{ 130, 160, 190, 210 });
                char rew[48];
                std::snprintf(rew, sizeof(rew), "+%d CR", c.rewardCredits);
                DrawText(rew, (int)(row.x + 14), (int)(row.y + 52), 12, Color{ 80, 200, 100, 220 });

                Rectangle btn = { row.x + row.width - 120.0f, row.y + row.height / 2.0f - 16.0f, 100.0f, 32.0f };
                bool hovAccept = CheckCollisionPointRec(mHov, btn);
                DrawRectangleRec(btn, hovAccept ? Color{ 40, 90, 50, 230 } : Color{ 14, 28, 18, 200 });
                DrawRectangleLinesEx(btn, 1.0f, Color{ 40, 160, 80, 200 });
                const char* albl = "ACCEPT";
                DrawText(albl, (int)(btn.x + (btn.width - MeasureText(albl, 12)) / 2),
                    (int)(btn.y + 10), 12, hovAccept ? WHITE : Color{ 80, 200, 100, 220 });
            }
        }

        DrawRectangle(mcx + 16, mcy + CH - 64, CW - 32, 1, Color{ 40, 100, 160, 160 });
        Vector2   m2b = GetMousePosition();
        Rectangle backBtn2 = { (float)(mcx + 20), (float)(mcy + CH - 52), 120.0f, 34.0f };
        bool      hovBack2 = CheckCollisionPointRec(m2b, backBtn2);
        DrawRectangleRec(backBtn2, hovBack2 ? Color{ 40, 80, 100, 230 } : Color{ 12, 28, 40, 200 });
        DrawRectangleLinesEx(backBtn2, 1.0f, Color{ 40, 130, 200, 200 });
        const char* blbl2 = "< BACK";
        DrawText(blbl2, (int)(backBtn2.x + (backBtn2.width - MeasureText(blbl2, 12)) / 2),
            (int)(backBtn2.y + 11), 12,
            hovBack2 ? WHITE : Color{ 120, 185, 240, 220 });
    }
    else if (_commsMenuOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0, 0, 0, 140 });
        static constexpr int CW = 500, CH = 210;
        int mcx = sw2 / 2 - CW / 2, mcy = sh2 / 2 - CH / 2;
        DrawRectangle(mcx, mcy, CW, CH, Color{ 8, 14, 22, 245 });
        DrawRectangleLinesEx({ (float)mcx, (float)mcy, (float)CW, (float)CH },
            1.5f, Color{ 40, 130, 200, 220 });

        std::string hdrStr = "COMMS  --  " + _commsMenuNpcName;
        const char* hdr = hdrStr.c_str();
        DrawText(hdr, mcx + CW / 2 - MeasureText(hdr, 14) / 2, mcy + 14, 14,
            Color{ 60, 160, 220, 255 });
        DrawRectangle(mcx + 16, mcy + 36, CW - 32, 1, Color{ 40, 100, 160, 160 });

        DrawRectangle(mcx + 16, mcy + 44, CW - 32, 90, Color{ 4, 10, 18, 200 });
        DrawRectangleLinesEx({ (float)(mcx + 16), (float)(mcy + 44), (float)(CW - 32), 90.0f },
            1.0f, Color{ 20, 60, 100, 180 });
        DrawText(_commsMenuNpcText.c_str(), mcx + 24, mcy + 64, 14,
            Color{ 190, 215, 245, 230 });

        DrawRectangle(mcx + 16, mcy + 144, CW - 32, 1, Color{ 40, 100, 160, 160 });

        Vector2   m2 = GetMousePosition();
        Rectangle backBtn = { (float)(mcx + 20),       (float)(mcy + CH - 52), 120.0f, 34.0f };
        bool      hovBack = CheckCollisionPointRec(m2, backBtn);
        DrawRectangleRec(backBtn, hovBack ? Color{ 40, 80, 100, 230 } : Color{ 12, 28, 40, 200 });
        DrawRectangleLinesEx(backBtn, 1.0f, Color{ 40, 130, 200, 200 });
        const char* blbl = "< BACK";
        DrawText(blbl, (int)(backBtn.x + (backBtn.width - MeasureText(blbl, 12)) / 2),
            (int)(backBtn.y + 11), 12,
            hovBack ? WHITE : Color{ 120, 185, 240, 220 });

        if (_commsMenuPhase == 0 && !_commsMenuIsCapital) {
            Rectangle joinBtn = { (float)(mcx + CW - 180), (float)(mcy + CH - 52), 160.0f, 34.0f };
            bool hovJoin = CheckCollisionPointRec(m2, joinBtn);
            DrawRectangleRec(joinBtn, hovJoin ? Color{ 40, 90, 50, 230 } : Color{ 14, 28, 18, 200 });
            DrawRectangleLinesEx(joinBtn, 1.0f, Color{ 40, 160, 80, 200 });
            const char* jlbl = _commsMenuIsDistress ? "ACKNOWLEDGE" : "REQUEST JOIN";
            DrawText(jlbl, (int)(joinBtn.x + (joinBtn.width - MeasureText(jlbl, 12)) / 2),
                (int)(joinBtn.y + 11), 12,
                hovJoin ? WHITE : Color{ 80, 200, 100, 220 });
        }
    }

    if (_enterPopupOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0, 0, 0, 160 });
        static constexpr int PopW = 340, PopH = 130;
        int px2 = sw2 / 2 - PopW / 2, py2 = sh2 / 2 - PopH / 2;
        DrawRectangle(px2, py2, PopW, PopH, Color{ 10, 18, 28, 240 });
        DrawRectangleLinesEx({ (float)px2, (float)py2, (float)PopW, (float)PopH },
            1.5f, Color{ 40, 130, 200, 220 });
        const char* msg = "In planet gameplay coming soon.";
        DrawText(msg, sw2 / 2 - MeasureText(msg, 14) / 2, py2 + 30, 14,
            Color{ 180, 215, 255, 240 });
        Rectangle okBtn = { (float)(sw2 / 2 - 60), (float)(py2 + PopH - 46), 120.0f, 32.0f };
        bool hovOk = CheckCollisionPointRec(GetMousePosition(), okBtn);
        DrawRectangleRec(okBtn, hovOk ? Color{ 30, 80, 120, 230 } : Color{ 15, 40, 65, 200 });
        DrawRectangleLinesEx(okBtn, 1.0f, Color{ 40, 130, 200, 200 });
        DrawText("OK", (int)(okBtn.x + (okBtn.width - MeasureText("OK", 12)) / 2),
            (int)(okBtn.y + 10), 12, hovOk ? WHITE : Color{ 120, 185, 240, 220 });
    }

    _stationServicesMenu.Draw();

    if (_ranksMenuOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0, 0, 0, 140 });
        static constexpr int PW = 500, PH = 340;
        int px = sw2 / 2 - PW / 2, py = sh2 / 2 - PH / 2;
        DrawRectangle(px, py, PW, PH, HudBg);
        DrawRectangleLinesEx({ (float)px, (float)py, (float)PW, (float)PH }, 1.5f, HudBorder);

        const char* hdr = "FACTION STANDING";
        DrawText(hdr, px + PW / 2 - MeasureText(hdr, 14) / 2, py + 14, 14, HudLabel);
        DrawRectangle(px + 16, py + 36, PW - 32, 1, HudDiv);

        int rowY = py + 46;
        for (const FactionDef& f : FactionRegistry::All()) {
            if (f.ranks.empty()) continue;
            int idx = 0; // always first rank for now
            const char* rankName = f.ranks[idx].c_str();
            DrawText(f.displayName.c_str(), px + 20, rowY, 12, HudValue);
            DrawText(rankName, px + PW - 20 - MeasureText(rankName, 12), rowY, 12, HudLabel);
            rowY += 26;
        }

        DrawRectangle(px + 16, py + PH - 58, PW - 32, 1, HudDiv);

        Vector2 m2 = GetMousePosition();
        Rectangle closeBtn = { (float)(px + PW / 2 - 60), (float)(py + PH - 46), 120.0f, 32.0f };
        bool hovClose = CheckCollisionPointRec(m2, closeBtn);
        DrawRectangleRec(closeBtn, hovClose ? Color{ 50,95,50,230 } : Color{ 12,22,12,200 });
        DrawRectangleLinesEx(closeBtn, 1.0f, HudDiv);
        const char* clbl = "CLOSE";
        DrawText(clbl, (int)(closeBtn.x + (closeBtn.width - MeasureText(clbl, 12)) / 2),
            (int)(closeBtn.y + 10), 12, hovClose ? WHITE : HudLabel);
    }

    // Epic 12.1: received-comms panel — surfaces the existing _commsLog feed
    // (also where tutorial hints land) rather than a new message system.
    if (_commsLogOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0, 0, 0, 140 });
        static constexpr int PW = 520, PH = 320;
        int px = sw2 / 2 - PW / 2, py = sh2 / 2 - PH / 2;
        DrawRectangle(px, py, PW, PH, HudBg);
        DrawRectangleLinesEx({ (float)px, (float)py, (float)PW, (float)PH }, 1.5f, HudBorder);

        const char* hdr = "RECEIVED COMMS";
        DrawText(hdr, px + PW / 2 - MeasureText(hdr, 14) / 2, py + 14, 14, HudLabel);
        DrawRectangle(px + 16, py + 36, PW - 32, 1, HudDiv);

        int rowY = py + 48;
        if (_commsLog.empty()) {
            DrawText("No messages yet.", px + 20, rowY, 12, HudLabel);
        } else {
            for (const CommsEntry& e : _commsLog) {
                DrawText(e.text.c_str(), px + 20, rowY, 12, e.fromPlayer ? HudValue : HudLabel);
                rowY += 24;
            }
        }

        DrawRectangle(px + 16, py + PH - 58, PW - 32, 1, HudDiv);

        Vector2 m2 = GetMousePosition();
        Rectangle closeBtn = { (float)(px + PW / 2 - 60), (float)(py + PH - 46), 120.0f, 32.0f };
        bool hovClose = CheckCollisionPointRec(m2, closeBtn);
        DrawRectangleRec(closeBtn, hovClose ? Color{ 50,95,50,230 } : Color{ 12,22,12,200 });
        DrawRectangleLinesEx(closeBtn, 1.0f, HudDiv);
        const char* clbl = "CLOSE";
        DrawText(clbl, (int)(closeBtn.x + (closeBtn.width - MeasureText(clbl, 12)) / 2),
            (int)(closeBtn.y + 10), 12, hovClose ? WHITE : HudLabel);
    }

    // ── Build menu ────────────────────────────────────────────────────────────
    if (_buildMenu.isOpen)       _buildMenu.Draw();
    if (_stationModMenu.isOpen)  _stationModMenu.Draw();
    if (_miningMenu.isOpen)      _miningMenu.Draw();

    // ── Placement mode HUD overlay ────────────────────────────────────────────
    if (_inPlacementMode && !_placementConfirmOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        const PlayerStationDef* def = PlayerStationRegistry::ById(_placingStationDefId);
        const char* sname = def ? def->displayName.c_str() : "Station";
        char hintbuf[128];
        std::snprintf(hintbuf, sizeof(hintbuf),
            "Placing: %s  —  Click to confirm location  |  [ESC] Cancel", sname);
        int tw = MeasureText(hintbuf, 13);
        DrawRectangle(sw2/2 - tw/2 - 12, 8, tw + 24, 28, Color{8,14,28,220});
        DrawRectangleLinesEx({(float)(sw2/2 - tw/2 - 12), 8.0f, (float)(tw+24), 28.0f},
            1.0f, Color{40,100,200,180});
        DrawText(hintbuf, sw2/2 - tw/2, 15, 13, Color{180,215,255,240});
    }

    // ── Placement confirmation popup ──────────────────────────────────────────
    if (_placementConfirmOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{0,0,0,140});
        static constexpr int PopW = 340, PopH = 150;
        int px2 = sw2/2 - PopW/2, py2 = sh2/2 - PopH/2;
        DrawRectangle(px2, py2, PopW, PopH, Color{8,14,30,245});
        DrawRectangleLinesEx({(float)px2,(float)py2,(float)PopW,(float)PopH},
            1.5f, Color{40,100,200,220});
        const PlayerStationDef* def = PlayerStationRegistry::ById(_placingStationDefId);
        const char* sname = def ? def->displayName.c_str() : "Station";
        char msgbuf[128];
        std::snprintf(msgbuf, sizeof(msgbuf), "Build %s here?", sname);
        DrawText(msgbuf, sw2/2 - MeasureText(msgbuf,14)/2, py2 + 24, 14, Color{190,220,255,240});
        DrawText("This will consume the required items.",
            sw2/2 - MeasureText("This will consume the required items.",11)/2,
            py2 + 48, 11, Color{110,140,180,200});

        Vector2 m2 = GetMousePosition();
        Rectangle yesBtn = {(float)(px2+30),        (float)(py2+PopH-50), 120.0f, 32.0f};
        Rectangle noBtn  = {(float)(px2+PopW-150),  (float)(py2+PopH-50), 120.0f, 32.0f};

        bool hovY = CheckCollisionPointRec(m2, yesBtn);
        bool hovN = CheckCollisionPointRec(m2, noBtn);

        DrawRectangleRec(yesBtn, hovY ? Color{20,80,40,230} : Color{12,40,20,200});
        DrawRectangleLinesEx(yesBtn, 1.0f, Color{40,160,80,200});
        DrawText("YES", (int)(yesBtn.x+(yesBtn.width-MeasureText("YES",12))/2),
            (int)(yesBtn.y+10), 12, hovY ? WHITE : Color{80,200,100,220});

        DrawRectangleRec(noBtn, hovN ? Color{80,20,20,230} : Color{40,12,12,200});
        DrawRectangleLinesEx(noBtn, 1.0f, Color{160,40,40,200});
        DrawText("NO", (int)(noBtn.x+(noBtn.width-MeasureText("NO",12))/2),
            (int)(noBtn.y+10), 12, hovN ? WHITE : Color{200,80,80,220});
    }

    if (_shipPlacementConfirmOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0,0,0,140 });
        static constexpr int PopW = 340, PopH = 150;
        int px2 = sw2 / 2 - PopW / 2, py2 = sh2 / 2 - PopH / 2;
        DrawRectangle(px2, py2, PopW, PopH, Color{ 8,14,30,245 });
        DrawRectangleLinesEx({ (float)px2,(float)py2,(float)PopW,(float)PopH }, 1.5f, Color{ 40,100,200,220 });

        std::string dName = "Ship";
        bool isCapital = false;
        if (const auto* ship = ecs::ShipRegistry::ShipById(_placingShipDefId)) {
            dName = ship->displayName;
            isCapital = ship->shipType == ShipType::Capital;
        }
        bool canAfford = !isCapital || InventoryManager::Get().Credits >= kCapitalShipBuildCost;

        char msgbuf[128];
        std::snprintf(msgbuf, sizeof(msgbuf), "Build %s here?", dName.c_str());
        DrawText(msgbuf, sw2 / 2 - MeasureText(msgbuf, 14) / 2, py2 + 24, 14, Color{ 190,220,255,240 });
        if (isCapital) {
            std::snprintf(msgbuf, sizeof(msgbuf), "Cost: %d credits", kCapitalShipBuildCost);
            DrawText(msgbuf, sw2 / 2 - MeasureText(msgbuf, 11) / 2, py2 + 48, 11,
                canAfford ? Color{ 110,140,180,200 } : Color{ 200,80,80,220 });
        } else {
            DrawText("Friendly ships defend the sector autonomously.",
                sw2 / 2 - MeasureText("Friendly ships defend the sector autonomously.", 11) / 2,
                py2 + 48, 11, Color{ 110,140,180,200 });
        }

        Vector2 m2 = GetMousePosition();
        Rectangle yesBtn = { (float)(px2 + 30),        (float)(py2 + PopH - 50), 120.0f, 32.0f };
        Rectangle noBtn = { (float)(px2 + PopW - 150),  (float)(py2 + PopH - 50), 120.0f, 32.0f };
        bool hovY = CheckCollisionPointRec(m2, yesBtn) && canAfford;
        bool hovN = CheckCollisionPointRec(m2, noBtn);

        DrawRectangleRec(yesBtn, !canAfford ? Color{ 30,30,30,200 } : hovY ? Color{ 20,80,40,230 } : Color{ 12,40,20,200 });
        DrawRectangleLinesEx(yesBtn, 1.0f, !canAfford ? Color{ 90,90,90,180 } : Color{ 40,160,80,200 });
        DrawText("YES", (int)(yesBtn.x + (yesBtn.width - MeasureText("YES", 12)) / 2),
            (int)(yesBtn.y + 10), 12, !canAfford ? Color{ 110,110,110,200 } : hovY ? WHITE : Color{ 80,200,100,220 });

        DrawRectangleRec(noBtn, hovN ? Color{ 80,20,20,230 } : Color{ 40,12,12,200 });
        DrawRectangleLinesEx(noBtn, 1.0f, Color{ 160,40,40,200 });
        DrawText("NO", (int)(noBtn.x + (noBtn.width - MeasureText("NO", 12)) / 2),
            (int)(noBtn.y + 10), 12, hovN ? WHITE : Color{ 200,80,80,220 });
    }

    if (_galaxyMap.isOpen) _galaxyMap.Draw();

    // ── Client sync overlay ───────────────────────────────────────────────────
    if (!_worldSynced) {
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        DrawRectangle(0, 0, sw, sh, { 0, 0, 0, 200 });
        const char* msg = "Syncing with host...";
        float fs = 28.0f;
        float tw = MeasureText(msg, (int)fs);
        DrawText(msg, (sw - (int)tw) / 2, sh / 2 - (int)(fs / 2), (int)fs, { 165, 192, 220, 255 });
        return;
    }

    if (_playerDead) {
        ShowCursor();
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        float overlayA = std::min(_deathTimer / 0.5f, 1.0f);
        DrawRectangle(0, 0, sw, sh, { 0, 0, 0, (unsigned char)(overlayA * 190) });

        static constexpr const char* kMsg      = "YOU PERISHED!";
        static constexpr int         kMsgLen   = 13;
        static constexpr float       kTypeDelay = 0.5f;
        static constexpr float       kTypeSpeed = 12.0f;
        static constexpr float       kBtnDelay  = kTypeDelay + kMsgLen / kTypeSpeed + 0.4f;

        int charsShown = (int)std::min((float)kMsgLen,
            std::max(0.0f, (_deathTimer - kTypeDelay) * kTypeSpeed));

        if (charsShown > 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.*s", charsShown, kMsg);
            int fsz = 72;
            int tx  = sw / 2 - MeasureText(kMsg, fsz) / 2;
            int ty  = sh / 2 - fsz / 2;
            DrawText(buf, tx + 3, ty + 3, fsz, { 80,  0,  0, 255 });
            DrawText(buf, tx,     ty,     fsz, { 220, 20, 20, 255 });
        }

        if (_deathTimer >= kBtnDelay) {
            float btnFade = std::min((_deathTimer - kBtnDelay) / 0.35f, 1.0f);
            unsigned char ba = (unsigned char)(btnFade * 255);

            // Respawn button
            Rectangle respawnBtn = { (float)(sw / 2 - 110), (float)(sh / 2 + 52), 220.0f, 44.0f };
            bool hovR = CheckCollisionPointRec(GetMousePosition(), respawnBtn);
            DrawRectangleRec(respawnBtn, { 5, 40, 80, (unsigned char)(btnFade * (hovR ? 210 : 130)) });
            DrawRectangleLinesEx(respawnBtn, 1.5f, { 30, 140, 220, ba });
            const char* lblR = "RESPAWN";
            DrawText(lblR,
                (int)(respawnBtn.x + (respawnBtn.width  - MeasureText(lblR, 18)) / 2),
                (int)(respawnBtn.y + (respawnBtn.height - 18) / 2),
                18, { hovR ? (unsigned char)180 : (unsigned char)140,
                      hovR ? (unsigned char)220 : (unsigned char)180,
                      (unsigned char)255, ba });

            // Main Menu button
            Rectangle menuBtn = { (float)(sw / 2 - 110), (float)(sh / 2 + 104), 220.0f, 44.0f };
            bool hovM = CheckCollisionPointRec(GetMousePosition(), menuBtn);
            DrawRectangleRec(menuBtn, { 60, 5, 5, (unsigned char)(btnFade * (hovM ? 210 : 130)) });
            DrawRectangleLinesEx(menuBtn, 1.5f, { 200, 30, 30, ba });
            const char* lblM = "MAIN MENU";
            DrawText(lblM,
                (int)(menuBtn.x + (menuBtn.width  - MeasureText(lblM, 18)) / 2),
                (int)(menuBtn.y + (menuBtn.height - 18) / 2),
                18, { hovM ? (unsigned char)255 : (unsigned char)210, 70, 70, ba });
        }
    }
}

void SpaceFlight::OnExit() {
    auto& cfg = FleetManager::Get().PlayerShip;
    cfg.HullIntegrity = _playerEntity.health.currentHull;
    cfg.MaxHull       = _playerEntity.health.maxStats.hull;
    _w->projectiles.clear();
    _w->asteroids.clear();
    _w->entities.clear();
    _w->npcMeta.clear();
    _w->lootDrops.clear();
    _w->materialDrops.clear();
    _w->derelictWrecks.clear();
    _w->hasActiveDistress = false;
    _commsLog.clear();
    _remoteEntities.clear();
    _remoteCapitalHardpoints.clear();
    _remoteFighterMounts.clear();
    _remotePlayerStations.clear();
    _remoteFireCooldown.clear();
    _remoteJoinGrace.clear();
    _remoteProjectiles.clear();
    _peerSystem.clear();
    _npcTargetId = 0;
    _playerEntity = ecs::Entity{};
    _playerMeta   = PlayerMeta{};
    _w->planets.clear();
    _w->stations.clear();
    if (_planetBaseTex.id    > 0) { UnloadTexture(_planetBaseTex);    _planetBaseTex    = {}; }
    if (_stationBaseTex.id   > 0) { UnloadTexture(_stationBaseTex);   _stationBaseTex   = {}; }
    if (_gargosTex.id        > 0) { UnloadTexture(_gargosTex);        _gargosTex        = {}; }
    if (_sunTex.id           > 0) { UnloadTexture(_sunTex);           _sunTex           = {}; }
    if (_sunCorona.id        > 0) { UnloadTexture(_sunCorona);        _sunCorona        = {}; }
    if (_asteroidTexLarge.id > 0) { UnloadTexture(_asteroidTexLarge); _asteroidTexLarge = {}; }
    if (_asteroidTexMedium.id> 0) { UnloadTexture(_asteroidTexMedium);_asteroidTexMedium= {}; }
    if (_asteroidTexSmall.id > 0) { UnloadTexture(_asteroidTexSmall); _asteroidTexSmall = {}; }
    if (_hudFontUi.texture.id  > 0) { UnloadFont(_hudFontUi);  _hudFontUi  = {}; }
    if (_hudFontVal.texture.id > 0) { UnloadFont(_hudFontVal); _hudFontVal = {}; }
    _lighting.Shutdown();
} 
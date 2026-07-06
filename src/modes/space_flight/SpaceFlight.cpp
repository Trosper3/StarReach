#include "SpaceFlight.h"
#include "raylib.h"
#include "raymath.h"
#include "core/FleetManager.h"
#include "core/GameManager.h"
#include "core/Module.h"
#include "core/SaveManager.h"
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
#include "engine/SpriteCache.h"
#include "core/ShipRegistry.h"
#include "data/MaterialDefs.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
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

static bool SpawnPosSafeFromStations(Vector2 pos, const std::vector<SpaceStation>& stations, float margin) {
    for (const SpaceStation& s : stations) {
        if (!s.alive) continue;
        if (DiplomaticRegistry::Get(s.faction, kPlayerFaction) != Relation::Hostile) continue;
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
// Right section of the HUD now just holds ENTER (top bar) with BUILD and
// COMMS stacked vertically as icon buttons below it.
static void ComputeHudButtons(int sw, int sh,
    Rectangle& enterBtn, Rectangle& buildBtn, Rectangle& commsBtn) {
    static constexpr int HudH2 = 174;
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

static Vector2 GetNpcStationHardpointPos(const SpaceStation& st, int hpIndex) {
    if (hpIndex < 0 || hpIndex >= (int)st.hardpoints.size()) return st.position;
    const HardpointState& hp = st.hardpoints[hpIndex];
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

    HardpointState core;
    core.id          = "core";
    core.displayName = "Core";
    core.isCore      = true;
    core.maxHull     = 250.0f;
    core.hull        = core.maxHull;
    core.arSlots     = 1;
    core.armor       = ModuleRegistry::Random(ModuleType::Armor, ModuleRegistry::RollGrade());
    totalHull += core.maxHull;
    st.hardpoints.push_back(core);

    HardpointState dock;
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
        HardpointState wb;
        wb.id          = "weapon_battery_" + std::to_string(i + 1);
        wb.displayName = "Weapon Battery " + std::to_string(i + 1);
        wb.maxHull     = 120.0f;
        wb.hull        = wb.maxHull;
        wb.wSlots      = 1;
        wb.weapons.push_back(ModuleRegistry::Random(ModuleType::Weapon, ModuleRegistry::RollGrade()));
        totalHull += wb.maxHull;
        st.hardpoints.push_back(wb);
    }

    st.maxHull = totalHull;
    st.hull    = totalHull;
}

static Vector2 GetHardpointPos(const PlayerStation& ps, int hpIndex, float stationRadius) {
    if (hpIndex < 0 || hpIndex >= (int)ps.hardpoints.size()) return ps.position;
    const HardpointState& hp = ps.hardpoints[hpIndex];

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
        const HardpointState& hp = ps.hardpoints[i];
        if (!hp.alive || hp.isCore) continue;
        return GetHardpointPos(ps, i, rad);
    }
    for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
        if (ps.hardpoints[i].alive) return GetHardpointPos(ps, i, rad);
    }
    return ps.position;
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
            const HardpointState& hp = s.hardpoints[i];
            if (!hp.alive) continue;
            Vector2 hpPos = GetNpcStationHardpointPos(s, i);
            float hpDrawRad = hp.isCore ? 18.0f : 14.0f;

            float hpHullPct = hp.maxHull > 0.0f ? std::clamp(hp.hull / hp.maxHull, 0.0f, 1.0f) : 0.0f;
            Color ringCol = hpHullPct > 0.5f ? Color{ 48,188,68,255 }
                : hpHullPct > 0.25f ? Color{ 212,168,28,255 } : Color{ 208,42,32,255 };
            DrawCircleV(hpPos, hpDrawRad, Color{ 15, 25, 40, 240 });
            DrawCircleLinesV(hpPos, hpDrawRad, ringCol);

            bool hasWeapon = !hp.weapons.empty() && hp.weapons[0].has_value();
            Color iconCol = hp.isCore       ? Color{ 200, 160,  30, 255 }
                : hp.isDockingBay ? Color{ 140, 220, 255, 255 }
                : hasWeapon       ? Color{ 220,  90,  60, 255 }
                                  : Color{ 100, 180, 255, 200 };
            DrawCircleV(hpPos, hpDrawRad * 0.4f, iconCol);
        }
    }
}

void SpaceFlight::DrawPlayerStations() const {
    const auto& stations = FleetManager::Get().PlayerStations;
    for (const PlayerStation& ps : stations) {
        if (!ps.alive) continue;
        const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
        float rad = def ? def->radius : 120.0f;

        // Count alive / total hardpoints for damage tinting
        int totalHp = (int)ps.hardpoints.size();
        int aliveHp = 0;
        for (const HardpointState& hp : ps.hardpoints)
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

        // PLAYER label
        const char* ownlbl = "[YOURS]";
        DrawText(ownlbl,
            (int)(ps.position.x - MeasureText(ownlbl, 9) / 2),
            (int)(ps.position.y - rad - 28), 9, Color{ 80,200,100,200 });

        // ── Draw Physical Hardpoints ──────────────────────────────────────────────
        for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
            const HardpointState& hp = ps.hardpoints[i];
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

            // Draw Core / Standard Indicator inside the hardpoint
            Color iconCol = hp.isCore ? Color{ 200, 160, 30, 255 } : Color{ 100, 180, 255, 200 };
            DrawCircleV(hpPos, hpDrawRad * 0.4f, iconCol);

            // Render small text label near hovered hardpoints if needed here
        }
    }
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

    int stationCount = GetRandomValue(1, 10);
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
            st.faction  = static_cast<Faction>(st.id % static_cast<int>(Faction::COUNT));
            const auto& stTypes = StationTypeRegistry::All();
            const StationTypeDef& typeDef = stTypes[st.id % stTypes.size()];
            st.stationTypeId = typeDef.id;
            BuildNpcStationHardpoints(st);
            _w->stations.push_back(std::move(st));
        }
    }
    float spawnDist = _w->sun.gravRange + 800.0f;
    _w->playerSpawnPos = GetSafeSpawnPosition(_w, spawnDist, kEnemySpawnMargin);
}

SystemWorld& SpaceFlight::GetOrCreateWorld(unsigned int systemId) {
    auto it = _worlds.find(systemId);
    if (it == _worlds.end()) {
        auto w = std::make_unique<SystemWorld>();
        w->systemId = systemId;
        it = _worlds.emplace(systemId, std::move(w)).first;
    }
    return *it->second;
}

// Returns the world for systemId, generating its content from the registry
// seed on first visit. Generation runs with `_w` temporarily pointed at the
// new world (every spawn helper operates through `_w`); afterwards `_w` is
// restored and the displayed sun corona re-baked, since SpawnPlanetsAndStations
// overwrote the shared _sunCorona texture with the new world's sun.
SystemWorld& SpaceFlight::EnsureWorldGenerated(unsigned int systemId) {
    bool existed = _worlds.find(systemId) != _worlds.end();
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

    // Player
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
static constexpr float NpcTurnRate = 155.0f;
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

void SpaceFlight::AddCommsMessage(const std::string& text, bool fromPlayer) {
    if (_bgTick) return;  // chatter from systems the player isn't in
    CommsEntry e;  e.text = text;  e.fromPlayer = fromPlayer;
    _commsLog.push_back(e);
    if ((int)_commsLog.size() > 5) _commsLog.erase(_commsLog.begin());
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
    const Vector2& pos = _playerEntity.transform.position;
    for (const SpaceStation& s : _w->stations) {
        if (!s.alive) continue;
        if (Vector2Distance(pos, s.position) >= s.radius + 50.0f) continue;
        if (DiplomaticRegistry::Get(s.faction, kPlayerFaction) == Relation::Hostile) continue;
        return true;
    }
    for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
        if (!ps.alive) continue;
        const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
        float rad = def ? def->radius : 120.0f;
        if (Vector2Distance(pos, ps.position) < rad + 50.0f) return true;
    }
    return false;
}

static std::pair<ecs::Entity, NpcMeta> MakeNpcEntity(unsigned int npcId, Vector2 pos) {
    // 1. AUTO-INJECT: Pick a random ship definition directly from the JSON Registry
    const auto& allShips = ecs::ShipRegistry::AllShips();
    int randIdx = GetRandomValue(0, allShips.size() - 1);
    const ecs::ShipDef& chosenShip = allShips[randIdx];

    // 2. Derive faction from ship palette then DiplomaticRegistry
    NpcMeta m;
    m.npcFaction = FactionFromPaletteId(chosenShip.paletteId);
    NpcFaction faction = RelationToNpcFaction(DiplomaticRegistry::Get(m.npcFaction, kPlayerFaction));
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

    // (Keep your existing random module loadout logic here)
    m.loadout.Resize(NpcMeta::WSlots, NpcMeta::ShSlots, NpcMeta::AuxSlots);

    for (auto& slot : m.loadout.weapons)
        slot = ModuleRegistry::Random(ModuleType::Weapon, ModuleRegistry::RollGrade());

    m.loadout.armor  = ModuleRegistry::Random(ModuleType::Armor,  ModuleRegistry::RollGrade());
    m.loadout.engine = ModuleRegistry::Random(ModuleType::Engine, ModuleRegistry::RollGrade());

    for (auto& slot : m.loadout.shields)
        slot = ModuleRegistry::Random(ModuleType::Shield, ModuleRegistry::RollGrade());

    return { e, m };
}

void SpaceFlight::SpawnNpcShips() {
    _w->entities.clear();
    _w->npcMeta.clear();
    _w->npcFreeSlots.clear();
    _w->nextNpcId = 1000;

    int count = GetRandomValue(3, 5);
    float minDist = std::max(700.0f, _w->sun.active ? _w->sun.gravRange + 500.0f : 700.0f);
    float maxDist = std::max(1400.0f, _w->sun.active ? _w->sun.gravRange + 1500.0f : 1400.0f);

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

        auto [entity, meta] = MakeNpcEntity(_w->nextNpcId++, pos);
        ApplyNpcLoadout(entity, meta);
        meta.preferredRange = meta.attackRange * 0.75f;
        entity.health.currentHull = entity.health.maxStats.hull;
        entity.network.networkId = entity.id;   // expose NPC to HostBroadcast

        _w->entities.push_back(std::move(entity));
        _w->npcMeta.push_back(std::move(meta));
    }
}

void SpaceFlight::UpdateNpcShips(float dt) {
    // Player-built stations (FleetManager) aren't tagged with a system and
    // belong to the player's world — hide them from AI in background worlds.
    static const std::vector<PlayerStation> kNoPlayerStations;
    const auto& playerStations = _bgTick ? kNoPlayerStations
                                         : FleetManager::Get().PlayerStations;

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
            case NpcAiState::Attack:
                if (distToClosestTarget > m.attackRange * 1.4f) m.aiState = NpcAiState::Chase;
                if (e.health.currentHull / e.health.maxStats.hull < 0.20f) {
                    m.aiState = NpcAiState::Flee;
                    int idx = GetRandomValue(0, 1);
                    AddCommsMessage(std::string("HOSTILE: \"") + FleeLines[idx] + "\"");
                }
                break;
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
                    if (m.aiState != NpcAiState::Flee) { m.aiState = NpcAiState::Flee; m.waypointSet = false; }
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
                    if (d < best) { best = d; chaseTarget = _w->entities[j].transform.position; }
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
                    if (d < best) { best = d; chaseTarget = st.position; }
                }
            }
            else {
                float best = FLT_MAX;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; chaseTarget = _w->entities[j].transform.position; }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; chaseTarget = st.position; }
                }
                if (m.retaliatingVsPlayer) {
                    float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                    if (d < best) { best = d; chaseTarget = _playerEntity.transform.position; }
                }
                if (m.retaliationTargetId != 0) {
                    for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                        if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                            if (d < best) { best = d; chaseTarget = _w->entities[j].transform.position; }
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
            if (DiplomaticRegistry::Get(m.npcFaction, kPlayerFaction) == Relation::Hostile) {
                float best = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                hasTarget  = true;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; attackTarget = _w->entities[j].transform.position; }
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
                    if (d < best) { best = d; attackTarget = st.position; }
                }
            } else {
                float best = FLT_MAX;
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (!_w->npcMeta[j].alive || _w->npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                    if (d < best) { best = d; attackTarget = _w->entities[j].transform.position; hasTarget = true; }
                }
                for (const SpaceStation& st : _w->stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; attackTarget = st.position; hasTarget = true; }
                }
                if (m.retaliatingVsPlayer) {
                    float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                    if (d < best) { best = d; attackTarget = _playerEntity.transform.position; hasTarget = true; }
                }
                if (m.retaliationTargetId != 0) {
                    for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                        if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                            if (d < best) { best = d; attackTarget = _w->entities[j].transform.position; hasTarget = true; }
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
            desiredRot += sinf((float)GetTime() * 1.8f + (float)m.id) * 20.0f;

            // Range control: back off if too close, close in if too far, hold otherwise
            if      (dist < m.preferredRange * 0.65f)  thrustMult = -0.8f;
            else if (dist > m.preferredRange * 1.3f)   thrustMult =  1.0f;
            else                                        thrustMult =  0.0f;

            // Strafe perpendicular to target line to orbit and dodge projectiles
            if (dist > 1.0f) {
                float sPhase = sinf((float)GetTime() * 0.45f + (float)m.id * 1.7f);
                float sDir   = (sPhase > 0.0f ? 1.0f : -1.0f) * ((m.id % 2 == 0) ? 1.0f : -1.0f);
                float str    = m.npcThrust * 0.55f * dt;
                lateralBoost.x += (-toP.y / dist) * sDir * str;
                lateralBoost.y += ( toP.x / dist) * sDir * str;
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
            if (DiplomaticRegistry::Get(m.npcFaction, kPlayerFaction) != Relation::Hostile) {
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

                int     slot  = m.wingmanSlot;
                float   pr    = _playerEntity.transform.rotation * DEG2RAD;
                Vector2 fwd   = { sinf(pr), -cosf(pr) };
                Vector2 right = { cosf(pr),  sinf(pr) };

                Vector2 formTarget = _playerEntity.transform.position;
                if (slot >= 0 && slot < 4) {
                    formTarget.x += right.x * kFormRight[slot] + fwd.x * kFormBack[slot];
                    formTarget.y += right.y * kFormRight[slot] + fwd.y * kFormBack[slot];
                }

                float dToTarget = Vector2Distance(e.transform.position, formTarget);
                if (dToTarget > 120.0f) {
                    Vector2 toP = Vector2Subtract(formTarget, e.transform.position);
                    desiredRot = atan2f(toP.x, -toP.y) * RAD2DEG;
                    thrustMult = 1.0f;
                } else if (dToTarget < 40.0f) {
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
        float maxTurn = NpcTurnRate * dt;
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

        if (m.faction == NpcFaction::Hostile && !m.wingman &&
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
                    if (d < best) { best = d; fireTarget = _w->entities[j].transform.position; fireNpcId = _w->npcMeta[j].id; fireTargetIsStation = false; }
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
                    if (d < best) { best = d; fireTarget = st.position; fireNpcId = 0; fireTargetIsStation = true; }
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

        if (m.faction != NpcFaction::Hostile && !m.wingman &&
            m.aiState == NpcAiState::Attack && m.npcHasWeapon) {

            Vector2 fireTarget = {}; unsigned int fireNpcId = 0; bool fireTargetIsPlayer = false; float best = FLT_MAX;
            for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                if (!_w->npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _w->npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                if (d < best) { best = d; fireTarget = _w->entities[j].transform.position; fireNpcId = _w->npcMeta[j].id; fireTargetIsPlayer = false; }
            }
            if (m.retaliatingVsPlayer) {
                float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                if (d < best) { best = d; fireTarget = _playerEntity.transform.position; fireNpcId = 0; fireTargetIsPlayer = true; }
            }
            if (m.retaliationTargetId != 0) {
                for (size_t j = 0; j < _w->npcMeta.size(); ++j) {
                    if (_w->npcMeta[j].id == m.retaliationTargetId && _w->npcMeta[j].alive) {
                        float d = Vector2Distance(e.transform.position, _w->entities[j].transform.position);
                        if (d < best) { best = d; fireTarget = _w->entities[j].transform.position; fireNpcId = m.retaliationTargetId; fireTargetIsPlayer = false; }
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
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || !p.fromPlayer) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || m.wingman) continue;
            if (Vector2Distance(p.position, e.transform.position) < m.radius + 3.5f) {
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
                }
                break;
            }
        }
    }

    // Block 2: hostile NPC projectiles hit non-self NPCs
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
        bool fromHostile = false;
        for (size_t j = 0; j < _w->npcMeta.size(); ++j)
            if (_w->npcMeta[j].id == p.ownerId && _w->npcMeta[j].faction == NpcFaction::Hostile) { fromHostile = true; break; }
        if (!fromHostile) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || m.id == p.ownerId) continue;
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

    // Block 3: friendly/neutral NPC projectiles hit hostile NPCs
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
        bool shooterIsNonHostile = false;
        for (size_t j = 0; j < _w->npcMeta.size(); ++j)
            if (_w->npcMeta[j].id == p.ownerId && _w->npcMeta[j].faction != NpcFaction::Hostile) { shooterIsNonHostile = true; break; }
        if (!shooterIsNonHostile) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive || m.faction != NpcFaction::Hostile) continue;
            if (Vector2Distance(p.position, e.transform.position) < m.radius + 3.5f) {
                p.alive = false;
                e.health.currentHull = std::max(0.0f, e.health.currentHull - p.damage);
                if (m.aiState == NpcAiState::Patrol) m.aiState = NpcAiState::Chase;
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
    }

    // Block 4: hostile/retaliating NPC projectiles hit player — skip non-hostile non-retaliating NPC shots and station shots
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
        if (Vector2Distance(p.position, _playerEntity.transform.position) < _playerMeta.radius) {
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

    // Block 5: Hostile NPC projectiles hitting Player Station Hardpoints
    // (player-built stations belong to the player's world — skip in background)
    for (Projectile& p : _w->projectiles) {
        if (_bgTick) break;
        if (!p.alive || p.fromPlayer) continue;

        for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
            if (!ps.alive) continue;

            const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
            float rad = def ? def->radius : 120.0f;

            // 1. Check if outer defenses still exist to protect the core
            bool hasOuterHardpoints = combat::CoreIsProtected(ps.hardpoints);

            bool hitStation = false;
            for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
                HardpointState& hp = ps.hardpoints[i];
                if (!hp.alive) continue;

                // CORE INVULNERABILITY: Skip core collision if outer points exist
                if (hp.isCore && hasOuterHardpoints) continue;

                Vector2 hpPos = GetHardpointPos(ps, i, rad);
                float hitRad = hp.isCore ? 18.0f : 14.0f;

                if (Vector2Distance(p.position, hpPos) < hitRad + 3.5f) {
                    p.alive = false;

                    hp.hull -= p.damage;
                    if (hp.hull <= 0.0f) {
                        hp.alive = false;
                        AddCommsMessage(hp.displayName + " Destroyed!", true);
                    }
                    hitStation = true;
                    break;
                }
            }

            // 2. If the core just died, or all hardpoints are dead, kill the station
            if (hitStation) {
                bool coreAlive = false;
                for (const HardpointState& hp : ps.hardpoints) {
                    if (hp.isCore && hp.alive) { coreAlive = true; break; }
                }
                if (!coreAlive) {
                    ps.alive = false;
                    AddCommsMessage("CRITICAL FAILURE: Station Lost.", true);
                }
                break; // Projectile consumed
            }
        }
    }

    // Block 6: in-world station projectiles hit NPCs and player
    for (Projectile& p : _w->projectiles) {
        if (!p.alive || p.fromPlayer) continue;
        SpaceStation* stShooter = nullptr;
        for (SpaceStation& st : _w->stations)
            if (st.id == p.ownerId) { stShooter = &st; break; }
        if (!stShooter) continue;
        for (size_t i = 0; i < _w->npcMeta.size(); ++i) {
            NpcMeta&     m = _w->npcMeta[i];
            ecs::Entity& e = _w->entities[i];
            if (!m.alive) continue;
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
        if (p.alive && Vector2Distance(p.position, _playerEntity.transform.position) < _playerMeta.radius) {
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

    // Block 7: any projectile hits in-world NPC station hardpoints
    for (Projectile& p : _w->projectiles) {
        if (!p.alive) continue;
        for (SpaceStation& st : _w->stations) {
            if (!st.alive || st.id == p.ownerId) continue;
            // Quick broad-phase: skip if not near the station at all
            if (Vector2Distance(p.position, st.position) > st.radius + 20.0f) continue;

            bool hasOuterHardpoints = combat::CoreIsProtected(st.hardpoints);

            bool hitStation = false;
            for (int i = 0; i < (int)st.hardpoints.size(); ++i) {
                HardpointState& hp = st.hardpoints[i];
                if (!hp.alive) continue;
                if (hp.isCore && hasOuterHardpoints) continue; // core invulnerable while outer hardpoints live

                Vector2 hpPos = GetNpcStationHardpointPos(st, i);
                float   hitRad = hp.isCore ? 18.0f : 14.0f;
                if (Vector2Distance(p.position, hpPos) >= hitRad + 3.5f) continue;

                p.alive = false;
                hp.hull -= p.damage;
                st.hull  = std::max(0.0f, st.hull - p.damage); // keep overall health bar in sync
                if (hp.hull <= 0.0f) {
                    hp.alive = false;
                    AddCommsMessage(st.stationTypeId + " " + hp.displayName + " destroyed.");
                }
                hitStation = true;

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
                break;
            }

            if (hitStation) {
                // Station dies when its core is destroyed
                bool coreAlive = false;
                for (const HardpointState& hp : st.hardpoints)
                    if (hp.isCore && hp.alive) { coreAlive = true; break; }
                if (!coreAlive && st.alive) {
                    st.alive = false;
                    net::Game().HostBroadcastStationDead(_w->systemId, st.id);
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
        }
    }

    for (LootDrop& ld : _w->lootDrops) {
        if (ld.collected) continue;
        if (Vector2Distance(_playerEntity.transform.position, ld.position) < 32.0f) {
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
        if (md.collected) continue;
        if (Vector2Distance(_playerEntity.transform.position, md.position) >= 32.0f) continue;
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
        }
        else {
            AddCommsMessage("STORAGE FULL: Cannot collect material!", true);
        }
    }
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

const ecs::ShipDef* SpaceFlight::ResolveShipDefByHash(uint32_t shipNameHash) const {
    if (shipNameHash == 0) return nullptr;
    for (const ecs::ShipDef& def : ecs::ShipRegistry::AllShips())
        if (Fnv1a32(def.id) == shipNameHash) return &def;
    return nullptr;
}

void SpaceFlight::DrawRemotePlayers() const {
    for (const auto& [id, re] : _remoteEntities) {
        if (re.id == 0) continue;
        if (!PeerInCurrentWorld(id)) continue;  // host: peer is in another system
        const Vector2& pos = re.transform.position;
        const float    rot = re.transform.rotation;
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

        const ecs::ShipDef* shipDef = ecs::ShipRegistry::ShipById(m.shipTypeId);
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

    // Only the host (or offline player) resolves hits; clients receive asteroid
    // state via server snapshots so local hit-detection would desync health.
    if (!net::Game().IsClient()) {
        for (Projectile& p : _w->projectiles) {
            if (!p.alive || !p.fromPlayer) continue;
            for (Asteroid& a : _w->asteroids) {
                if (!a.alive) continue;
                if (Vector2Distance(p.position, a.position) < a.radius + 3.5f) {
                    p.alive = false;
                    a.health -= 1;
                    if (a.health <= 0) {
                        for (const auto& mc : a.materials)
                            if (GetRandomValue(1, 100) <= mc.percent)
                                SpawnMaterialDrop(a.position, mc.materialId);
                        DestroyAsteroid(a, spawns);
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
                    if (--b.health <= 0) {
                        for (const auto& mc : b.materials)
                            if (GetRandomValue(1, 100) <= mc.percent)
                                SpawnMaterialDrop(b.position, mc.materialId);
                        DestroyAsteroid(b, spawns);
                    }
                }
                else if (b.tier > a.tier) {
                    if (--a.health <= 0) {
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

    if (_hitCooldown <= 0.0f) {
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
                for (auto& w : _loadout.weapons) rollDestroy(w);
                for (auto& s : _loadout.shields) rollDestroy(s);
                for (auto& x : _loadout.aux)     rollDestroy(x);
                rollDestroy(_loadout.engine);
                rollDestroy(_loadout.hyperdrive);
                if (_playerEntity.health.currentHull <= 0.0f) {
                    if (_loadout.armor) {
                        _loadout.armor = std::nullopt;
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
            _target.iconTex = nullptr;
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
            _target.npcFaction  = RelationToNpcFaction(DiplomaticRegistry::Get(s.faction, kPlayerFaction));
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
                    DrawTextEx(_hudFontVal, fb, { dx, dy }, 12.0f, 1.0f, Color{ 180, 210, 255, 255 });
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
            int   sides = _target.tier == 2 ? 8 : _target.tier == 1 ? 7 : 6;
            float spin = (float)(GetTime() * 22.0);
            DrawPoly(tc, sides, tAreaR * 0.65f, spin, Color{ 30,26,21,255 });
            DrawPolyLinesEx(tc, sides, tAreaR * 0.65f, spin, 1.0f, Color{ 130,115,90,255 });
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
    for (int i = 0; i < _playerMeta.weaponSlots; ++i) {
        bool isSelected = (i == _selectedWeapon);
        Rectangle slot = { (float)(weapX + i * 38), (float)wy, 32.0f, 26.0f };
        DrawHudChamferRect(slot, 5.0f,
            isSelected ? Color{ 20,38,20,230 } : Color{ 14,22,14,200 },
            isSelected ? HudGood : HudDiv,
            isSelected ? 2.0f : 1.0f);
        char kh[2] = { (char)('1' + i), 0 };
        if (i == 9) kh[0] = '0';
        DrawText(kh, (int)slot.x + 3, (int)slot.y + 3, 7, Color{ 60,100,60,175 });
        if (i < (int)_loadout.weapons.size() && _loadout.weapons[i]) {
            const ModuleDef& wm = *_loadout.weapons[i];
            const char* abbr = (wm.weapon.fireMode == WeaponFireMode::LockOn) ? "MIS"
                : (wm.weapon.fireMode == WeaponFireMode::Charge) ? "CHG"
                : (wm.weapon.damageType == DamageType::Energy) ? "ENR"
                : (wm.weapon.effect == WeaponEffect::EMP) ? "EMP"
                : (wm.weapon.effect == WeaponEffect::Ion) ? "ION"
                : "KIN";
            DrawText(abbr, (int)(slot.x + 5), (int)(slot.y + 13), 9,
                Color{ 100,210,100,220 });
        }
        else {
            DrawText("--", (int)(slot.x + 9), (int)(slot.y + 7), 10, Color{ 50,80,50,175 });
        }
    }

    Rectangle enterBtn, buildBtn, commsBtn;
    ComputeHudButtons(sw, sh, enterBtn, buildBtn, commsBtn);
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

    // COMMS — radar dish icon, label below
    {
        bool npcActive = (_npcTargetId != 0);
        bool hovComms = npcActive && CheckCollisionPointRec(mouse, commsBtn);
        Color bg = npcActive ? (hovComms ? Color{ 20,60,80,230 } : Color{ 10,28,40,200 })
            : Color{ 16,16,16,150 };
        Color bdr = npcActive ? Color{ 30,100,160,200 } : HudDiv;
        Color fg = npcActive ? (hovComms ? WHITE : Color{ 50,140,190,255 })
            : Color{ 45,50,55,150 };
        DrawHudChamferRect(commsBtn, 6.0f, bg, bdr, 1.5f);
        DrawHudRadarIcon({ commsBtn.x + commsBtn.width / 2.0f, commsBtn.y + commsBtn.height * 0.38f }, 16.0f, fg);
        const char* commsLbl = "COMMS";
        Vector2 commsTs = MeasureTextEx(_hudFontUi, commsLbl, 9.0f, 1.0f);
        DrawTextEx(_hudFontUi, commsLbl,
            { commsBtn.x + (commsBtn.width - commsTs.x) / 2.0f, commsBtn.y + commsBtn.height - commsTs.y - 3.0f },
            9.0f, 1.0f, fg);
    }

    (void)rDiv;

    Vector2 escMapTs = MeasureTextEx(_hudFontVal, "[ESC] MAP", 11.0f, 1.0f);
    DrawTextEx(_hudFontVal, "[ESC] MAP", { (float)(hx + hw) - escMapTs.x - 8.0f, (float)(hy + HudH - 15) },
        11.0f, 1.0f, HudLabel);

    bool menuOpen = (_storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _enterPopupOpen || _stationServicesMenu.isOpen || _localMapOpen ||
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

    if (_loadout.armor)
        _playerEntity.health.maxStats.hull += _loadout.armor->armor.hullBonus;
    _playerEntity.health.currentHull = std::min(_playerEntity.health.currentHull,
                                                 _playerEntity.health.maxStats.hull);

    for (const auto& sh : _loadout.shields) {
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

    if (_loadout.engine && !_loadout.engine->engine.isHyperdrive) {
        _playerMeta.thrust    += _loadout.engine->engine.thrustBonus;
        _playerMeta.turnSpeed += _loadout.engine->engine.turnSpeedBonus;
        _playerMeta.canMove    = true;
    }

    if (_selectedWeapon >= _playerMeta.weaponSlots) _selectedWeapon = 0;

    if (_selectedWeapon < (int)_loadout.weapons.size() && _loadout.weapons[_selectedWeapon]) {
        const WeaponStats& ws = _loadout.weapons[_selectedWeapon]->weapon;
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
    }

    _hasSensors = false;
    for (const auto& ax : _loadout.aux)
        if (ax && ax->auxiliary.hasSensors) { _hasSensors = true; break; }

    _hyperdriveRange = 0.0f;
    if (_loadout.hyperdrive && _loadout.hyperdrive->engine.isHyperdrive)
        _hyperdriveRange = _loadout.hyperdrive->engine.hyperdriveRange;
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

    if (meta.loadout.armor)
        entity.health.maxStats.hull += meta.loadout.armor->armor.hullBonus;
    entity.health.currentHull = std::min(entity.health.currentHull, entity.health.maxStats.hull);

    for (const auto& sh : meta.loadout.shields) {
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

    if (!meta.loadout.weapons.empty() && meta.loadout.weapons[0]) {
        const WeaponStats& ws = meta.loadout.weapons[0]->weapon;
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

    if (meta.loadout.engine && !meta.loadout.engine->engine.isHyperdrive)
        meta.npcThrust = meta.loadout.engine->engine.thrustBonus;
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
    for (const auto& w : _loadout.weapons)
        gs.weaponIds.push_back(w ? w->id : std::string{});
    gs.armorId       = _loadout.armor      ? _loadout.armor->id      : std::string{};
    gs.engineId      = _loadout.engine     ? _loadout.engine->id     : std::string{};
    gs.hyperdriveId  = _loadout.hyperdrive ? _loadout.hyperdrive->id : std::string{};
    for (const auto& s : _loadout.shields)
        gs.shieldIds.push_back(s ? s->id : std::string{});
    for (const auto& a : _loadout.aux)
        gs.auxIds.push_back(a ? a->id : std::string{});

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
        ns.weaponId  = (nm.loadout.weapons.size() > 0 && nm.loadout.weapons[0]) ? nm.loadout.weapons[0]->id : std::string{};
        ns.armorId   = nm.loadout.armor  ? nm.loadout.armor->id  : std::string{};
        ns.shieldId  = (nm.loadout.shields.size() > 0 && nm.loadout.shields[0]) ? nm.loadout.shields[0]->id : std::string{};
        ns.engineId  = nm.loadout.engine ? nm.loadout.engine->id : std::string{};
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
        nm.shipTypeName               = (ns.shipTypeId == "gargos") ? "Gargos" : "AR-3 Saber";
        {
            const auto* sd = ecs::ShipRegistry::ShipById(nm.shipTypeId);
            nm.npcFaction = sd ? FactionFromPaletteId(sd->paletteId) : Faction::Merchant;
        }
        nm.preferredRange             = nm.attackRange * 0.75f;
        nm.loadout.Resize(NpcMeta::WSlots, NpcMeta::ShSlots, 0);
        nm.loadout.weapons[0] = ModuleById(ns.weaponId);
        nm.loadout.armor      = ModuleById(ns.armorId);
        nm.loadout.shields[0] = ModuleById(ns.shieldId);
        nm.loadout.engine     = ModuleById(ns.engineId);
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
        _w->stations.push_back(std::move(st));
    }

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

    // Fresh session: drop any prior worlds and start pointed at system 1. If a
    // save load changes _currentSystemId below, the world map is re-keyed there.
    _worlds.clear();
    _currentSystemId = 1;
    _w = &GetOrCreateWorld(_currentSystemId);
    _peerSystem.clear();
    _bgTick = false;

    auto& cfg = FleetManager::Get().PlayerShip;
    kPlayerFaction = cfg.PlayerFaction;

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

    _selectedWeapon = 0;
    _enterPopupOpen      = false;
    _stationServicesMenu.isOpen = false;
    _inPlacementMode     = false;
    _placementConfirmOpen= false;
    _placingStationDefId.clear();
    _npcTargetId = 0;
    _w->respawnTimer = 20.0f;
    _w->entities.clear();
    _w->npcMeta.clear();
    _w->lootDrops.clear();
    _w->materialDrops.clear();
    _playerDead = false;
    _deathTimer = 0.0f;
    _commsLog.clear();
    _commsMenuOpen = false;
    _commsMenuPhase = 0;
    _commsMenuNpcId = 0;
    _commsMenuNpcName = {};
    _commsMenuNpcText = {};
    _escortMenuOpen = false;
    _escortMenuSelId = 0;
    _escortModuleNpcId = 0;
    _loadout.Resize(_playerMeta.weaponSlots, _playerMeta.shieldSlots, _playerMeta.auxSlots);
    _discoveredIds.clear();
    _currentSystemId = 1;
    _discoveredSystemIds.clear();

    // ── Galaxy seed: derive (New Game) or restore (Load) before anything
    // touches StarSystemRegistry — every system's position/seed is derived
    // from this single master seed.
    if (didLoad) {
        _gameSeed = gs.gameSeed != 0 ? gs.gameSeed : 1u;
    } else {
        _gameSeed = cfg.GalaxySeedInput.empty()
            ? (uint32_t)GetRandomValue(1, 2147483647)
            : StarSystemRegistry::HashSeedString(cfg.GalaxySeedInput);
    }
    StarSystemRegistry::Init(_gameSeed);

    // ── Loadout: restore from save, or fall back to default starter kit ───────
    if (didLoad && gs.hasWorldState && !gs.engineId.empty()) {
        for (int i = 0; i < _playerMeta.weaponSlots && i < (int)gs.weaponIds.size(); ++i)
            _loadout.weapons[i] = ModuleById(gs.weaponIds[i]);
        _loadout.armor      = ModuleById(gs.armorId);
        _loadout.engine     = ModuleById(gs.engineId);
        _loadout.hyperdrive = ModuleById(gs.hyperdriveId);
        for (int i = 0; i < _playerMeta.shieldSlots && i < (int)gs.shieldIds.size(); ++i)
            _loadout.shields[i] = ModuleById(gs.shieldIds[i]);
        for (int i = 0; i < _playerMeta.auxSlots && i < (int)gs.auxIds.size(); ++i)
            _loadout.aux[i] = ModuleById(gs.auxIds[i]);
        _discoveredIds        = gs.discoveredIds;
        _currentSystemId      = gs.currentSystemId;
        _discoveredSystemIds  = gs.discoveredSystemIds;
    }
    else {
        _loadout.engine = Engine_Thruster_I();
        _loadout.armor  = Armor_HullPatch();
		_loadout.weapons[0] = Weapon_PulseCannon_I();
		_loadout.shields[0] = Shield_KineticBarrier_I();
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

    // A loaded save may have moved us to another system — re-key the world map.
    if (_w->systemId != _currentSystemId) {
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
        auto PutInStorage = [&](int idx, ModuleDef m) {
            StorageItem& slot = _storageMenu.slots[idx];
            slot.type = StorageItemType::Module;
            slot.module = m;
            slot.displayName = m.displayName;
        };
        PutInStorage(0, Hyperdrive_ShortJump());
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
    if (curSys && tgtSys) {
        Vector2 delta = Vector2Subtract(tgtSys->galacticPos, curSys->galacticPos);
        float   len   = Vector2Length(delta);
        if (len > 0.01f) dir = Vector2Scale(delta, 1.0f / len);
    }

    _warpTargetSystemId  = targetSystemId;
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
}

void SpaceFlight::CommitWarpWorldSwitch(unsigned int targetSystemId) {
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
            == _discoveredSystemIds.end())
        _discoveredSystemIds.push_back(targetSystemId);
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
    _currentSystemId = ws.systemId;
    _gameSeed        = ws.gameSeed;
    StarSystemRegistry::Init(_gameSeed);

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
                _remoteProjectiles.clear();
                _worldSynced    = false;   // also stops Input sends while in limbo
                _warpPhase      = WarpPhase::AwaitSync;
                _warpPhaseTimer = 0.0f;
                break;
            }
            CommitWarpWorldSwitch(_warpTargetSystemId);
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
            _warpPhase      = WarpPhase::None;
            _warpPhaseTimer = 0.0f;
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

        // Disconnected peers: drop their state so their system can freeze.
        for (uint32_t peerId : net::Game().disconnectedPeerIds) {
            _remoteEntities.erase(peerId);
            _remoteFireCooldown.erase(peerId);
            _remoteJoinGrace.erase(peerId);
            _peerSystem.erase(peerId);
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

        // Simulate every other occupied system; unoccupied worlds stay frozen
        // in memory (state preserved, no ticking) until someone returns.
        for (auto& [sysId, worldPtr] : _worlds) {
            if (worldPtr.get() == _w) continue;   // main Update path ticks this one
            bool occupied = false;
            for (const auto& [peerId, ps] : _peerSystem)
                if (ps == sysId) { occupied = true; break; }
            if (occupied) TickBackgroundWorld(dt, *worldPtr);
        }

        // ~20 Hz: send each occupied system's snapshot to exactly its occupants.
        _netTickAccum += dt;
        if (_netTickAccum >= 0.05f) {
            _netTickAccum = 0.0f;

            for (auto& [sysId, worldPtr] : _worlds) {
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
                if (sysId == _currentSystemId && !_playerDead) {
                    ecs::Entity pCopy = _playerEntity;
                    pCopy.network.isLocalPlayer = false;
                    broadcastList.push_back(pCopy);
                }
                // Remote entities in this system, so its occupants see each other.
                for (const auto& [netId, re] : _remoteEntities) {
                    if (re.id == 0) continue;
                    auto it = _peerSystem.find(netId);
                    if (it != _peerSystem.end() && it->second == sysId)
                        broadcastList.push_back(re);
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

                net::Game().HostSendSnapshot(occupants, sysId,
                                             broadcastList, asteroidSnaps, projSnaps);
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
                if (st.id == deadId) { st.alive = false; break; }
        }
        net::Game().pendingStationDeads.clear();
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
                    } else if (snap.shipNameHash == kGargosShipHash) {
                        re.sprite.texture = const_cast<Texture2D*>(&_gargosTex);
                        re.sprite.tint    = WHITE;
                        re.sprite.scale   = 1.0f;
                    } else if (const ecs::ShipDef* def = ResolveShipDefByHash(snap.shipNameHash)) {
                        // NPC — resolve its real sprite from the type hash carried in the snapshot.
                        re.sprite.texture = ResourceManager::Load(def->assetPath);
                        re.sprite.tint    = WHITE;
                        re.sprite.scale   = def->pixelScale;
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

            // ── Projectile sync: replace list from server snapshot ─────────────
            _remoteProjectiles = net::Game().latestProjectileSnapshots;
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

        // Flash a status verification onto the UI logging feed
        AddCommsMessage("DEBUG: Added Mining Station materials to storage.", true);
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
                for (auto& w : _loadout.weapons) w = std::nullopt;
                for (auto& s : _loadout.shields) s = std::nullopt;
                for (auto& a : _loadout.aux)     a = std::nullopt;
                _loadout.armor      = Armor_HullPatch();
                _loadout.engine     = Engine_Thruster_I();
                _loadout.hyperdrive = std::nullopt;
                if (_playerMeta.weaponSlots > 0)
                    _loadout.weapons[0] = Weapon_PulseCannon_I();
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

    if (_storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _enterPopupOpen || _localMapOpen ||
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
        else if (_localMapOpen) {
            _localMapOpen = false;
        }
        else if (_galacticMap.isOpen) {
            _galacticMap.Close();
            _systemMap.Open();
        }
        else if (!_systemMap.isOpen) {
            _systemMap.Open();
        }
        else if (!_systemMap.IsPickerOpen()) {
            _systemMap.Close();
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

    if (_galacticMap.isOpen) {
        {
            GalacticMapData mapData;
            mapData.hyperdriveRange     = _hyperdriveRange;
            mapData.currentSystemId     = _currentSystemId;
            mapData.discoveredSystemIds = _discoveredSystemIds;
            auto curSys = StarSystemRegistry::ById(_currentSystemId);
            mapData.currentSystemPos = curSys ? curSys->galacticPos : Vector2{};
            _galacticMap.SetMapData(mapData);
        }

        GalacticMapAction gAction = _galacticMap.Update(dt);

        if (gAction == GalacticMapAction::WarpToSystem) {
            unsigned int targetId = _galacticMap.WarpTargetId();
            if (StarSystemRegistry::ById(targetId)) {
                _galacticMap.Close();
                BeginWarpSequence(targetId);   // cinematic; actual system switch happens mid-sequence
            } else {
                _galacticMap.Close();
            }
            return;
        }
        if (gAction == GalacticMapAction::OpenSystemMap) {
            _galacticMap.Close();
            _systemMap.Open();
            return;
        }
        if (gAction == GalacticMapAction::GoMainMenu) {
            if (net::Game().IsHost()) net::Game().BroadcastServerClosing();
            net::Game().Shutdown();
            GameManager::Get().TransitionTo(GameMode::MainMenu);
            return;
        }
        return;
    }

    if (_systemMap.isOpen) {
        // Feed current world data to the map
        {
            SystemMapData mapData;
            mapData.playerPos       = _playerEntity.transform.position;
            mapData.hyperdriveRange = _hyperdriveRange;
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
            _systemMap.SetMapData(mapData);
        }

        MapAction action = _systemMap.Update(dt);

        if (action == MapAction::WarpTo) {
            _systemMap.Close();
            BeginLocalWarp(_systemMap.WarpTarget());
            return;
        }
        if (action == MapAction::OpenGalacticMap) {
            _systemMap.Close();
            _galacticMap.Open();
            return;
        }
        if (action == MapAction::OpenModules) {
            _systemMap.Close();
            _modulesMenu.Open(&_loadout, &_storageMenu.slots,
                _playerMeta.weaponSlots, _playerMeta.armorSlots,
                _playerMeta.shieldSlots, _playerMeta.engineSlots,
                _playerMeta.hyperdriveSlots, _playerMeta.auxSlots);
            return;
        }
        if (action == MapAction::OpenStorage) {
            _systemMap.Close();
            _storageMenu.Open((int)_storageMenu.slots.size());
            return;
        }
        if (action == MapAction::OpenEscorts) {
            int wingCount = 0;
            for (const NpcMeta& n : _w->npcMeta) if (n.alive && n.wingman) wingCount++;
            if (wingCount > 0) {
                _systemMap.Close();
                _escortMenuOpen = true;
                _escortMenuSelId = 0;
                for (const NpcMeta& n : _w->npcMeta)
                    if (n.alive && n.wingman) { _escortMenuSelId = n.id; break; }
            }
            return;
        }
        if (action == MapAction::OpenRanks) {
            _systemMap.Close();
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
                _systemMap.SavePath(), _systemMap.SaveDisplayName());
            _systemMap.AckSave();
        }
        if (action == MapAction::LoadGame) {
            SaveManager::GameState gs;
            if (SaveManager::Get().LoadGame(_systemMap.LoadFilename(), gs)) {
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
                _gameSeed        = gs.gameSeed != 0 ? gs.gameSeed : 1u;
                StarSystemRegistry::Init(_gameSeed);
                if (gs.hasWorldState && !gs.engineId.empty()) {
                    for (int i = 0; i < _playerMeta.weaponSlots && i < (int)gs.weaponIds.size(); ++i)
                        _loadout.weapons[i] = ModuleById(gs.weaponIds[i]);
                    _loadout.armor      = ModuleById(gs.armorId);
                    _loadout.engine     = ModuleById(gs.engineId);
                    _loadout.hyperdrive = ModuleById(gs.hyperdriveId);
                    for (int i = 0; i < _playerMeta.shieldSlots && i < (int)gs.shieldIds.size(); ++i)
                        _loadout.shields[i] = ModuleById(gs.shieldIds[i]);
                    for (int i = 0; i < _playerMeta.auxSlots && i < (int)gs.auxIds.size(); ++i)
                        _loadout.aux[i] = ModuleById(gs.auxIds[i]);
                    _discoveredIds       = gs.discoveredIds;
                    _currentSystemId     = gs.currentSystemId;
                    _discoveredSystemIds = gs.discoveredSystemIds;
                }
                if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), _currentSystemId)
                        == _discoveredSystemIds.end())
                    _discoveredSystemIds.push_back(_currentSystemId);
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
                    auto homeSys = StarSystemRegistry::ById(_currentSystemId);
                    SpawnPlanetsAndStations(homeSys ? homeSys->seed : 0);
                    SpawnInitialAsteroids();
                    SpawnNpcShips();
                    _playerEntity.transform.position = _w->playerSpawnPos;
                    _camera.target = _playerEntity.transform.position;
                }
                InitStars();
            }
            _systemMap.Close();
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
        _stationServicesMenu.Update();
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

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(m2, yesBtn)) {
                // Spawn Friendly NPC Ship
                auto [alliedE, alliedM] = MakeNpcEntity(_w->nextNpcId++, _shipPlacementPos);
                alliedM.faction = NpcFaction::Friendly;
                alliedM.shipTypeId = _placingShipDefId;

                std::string dName = "Ship";
                float mHull = 100.0f;
                if (const auto* ship = ecs::ShipRegistry::ShipById(_placingShipDefId)) {
                    dName = ship->displayName;
                    mHull = ship->baseStats.hull;
                }

                alliedM.shipTypeName = dName;
                alliedM.wingman = false; // Player side but NOT part of escort
                alliedE.health.maxStats.hull = mHull;
                alliedE.health.currentHull   = mHull;
                ApplyNpcLoadout(alliedE, alliedM); // Assign generic NPC loadout to fight with
                if (!_w->npcFreeSlots.empty()) {
                    size_t slot = _w->npcFreeSlots.back(); _w->npcFreeSlots.pop_back();
                    _w->entities[slot] = std::move(alliedE); _w->npcMeta[slot] = std::move(alliedM);
                } else {
                    _w->entities.push_back(std::move(alliedE)); _w->npcMeta.push_back(std::move(alliedM));
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

                FleetManager::Get().SpawnStation(_placingStationDefId, _placementPos);
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
                FleetManager::Get().SpawnStation(_placingStationDefId, _placementPos);
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
            else if (_commsMenuPhase == 0 && CheckCollisionPointRec(m2, joinBtn)) {
                int roll = GetRandomValue(0, 99);
                for (size_t ci = 0; ci < _w->npcMeta.size(); ++ci) {
                    NpcMeta& npc = _w->npcMeta[ci];
                    if (npc.id != _commsMenuNpcId || !npc.alive) continue;
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
                    NpcMeta::ShSlots, NpcMeta::EnSlots, 0);
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

    for (int k = 0; k < 9 && k < _playerMeta.weaponSlots; ++k) {
        if (IsKeyPressed(KEY_ONE + k)) {
            _selectedWeapon = k;
            _lockTargetId = 0;
            ApplyLoadout();
        }
    }
    if (IsKeyPressed(KEY_ZERO) && _playerMeta.weaponSlots > 9) {
        _selectedWeapon = 9;
        _lockTargetId = 0;
        ApplyLoadout();
    }

    // ── HUD button clicks ─────────────────────────────────────────────────────
    Vector2 mousePos = GetMousePosition();
    int hy = GetScreenHeight() - HudH - 6;

    bool clickedHudBtn = _storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen || _ranksMenuOpen || (mousePos.y >= hy);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Rectangle enterBtn, buildBtn, commsBtn;
        ComputeHudButtons(GetScreenWidth(), GetScreenHeight(), enterBtn, buildBtn, commsBtn);
        Vector2 m = GetMousePosition();
        if ((IsNearEnterableStation() || IsNearPlanet()) && CheckCollisionPointRec(m, enterBtn)) {
            if (IsNearEnterableStation()) _stationServicesMenu.Open(&_playerEntity, &_storageMenu.slots);
            else                 _enterPopupOpen   = true;
            clickedHudBtn = true;
        }
        else if (CheckCollisionPointRec(m, buildBtn)) {
            _buildMenu.Open(&_storageMenu.slots);
            clickedHudBtn = true;
        }
        else if (_npcTargetId != 0 && CheckCollisionPointRec(m, commsBtn)) {
            _commsMenuOpen = true;
            _commsMenuPhase = 0;
            _commsMenuNpcId = _npcTargetId;
            for (const NpcMeta& npc : _w->npcMeta) {
                if (npc.id != _npcTargetId || !npc.alive) continue;
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
                break;
            }
            clickedHudBtn = true;
        }
    }

    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), _camera);

    unsigned int passLockId = 0;
    Vector2      passLockPos = {};
    if (_selectedWeapon < (int)_loadout.weapons.size() && _loadout.weapons[_selectedWeapon] &&
        _loadout.weapons[_selectedWeapon]->weapon.fireMode == WeaponFireMode::LockOn) {
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
    {
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

        if (_playerMeta.weaponFireMode != WeaponFireMode::Charge) _playerMeta._chargeTimer = 0.0f;
        if (_playerMeta._fireCooldown > 0.0f) _playerMeta._fireCooldown -= dt;

        if (fireEnabled && _playerMeta.canFire && !net::Game().IsClient()) {
            Vector2 toAim  = Vector2Subtract(mouseWorld, pos);
            float   aimLen = Vector2Length(toAim);
            float   fwdRad = (rot - 90.0f) * DEG2RAD;
            Vector2 fwd    = { cosf(fwdRad), sinf(fwdRad) };
            Vector2 aimDir = (aimLen > 1.0f) ? Vector2Scale(toAim, 1.0f / aimLen) : fwd;

            switch (_playerMeta.weaponFireMode) {
            case WeaponFireMode::Standard: {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && _playerMeta._fireCooldown <= 0.0f) {
                    bool inArc = _playerMeta.hasTurret || (Vector2DotProduct(fwd, aimDir) > 0.0f);
                    if (inArc) {
                        _playerMeta._fireCooldown = _playerMeta.fireRate;
                        float ttl = _playerMeta.projRange / _playerMeta.projSpeed;
                        Projectile p;
                        p.position = pos;
                        p.velocity = { aimDir.x * _playerMeta.projSpeed, aimDir.y * _playerMeta.projSpeed };
                        p.lifetime = 0.0f; p.maxLife = ttl; p.damage = _playerMeta.weaponDamage; p.alive = true;
                        _w->projectiles.push_back(p);
                    }
                }
                break;
            }
            case WeaponFireMode::Charge: {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                    _playerMeta._chargeTimer = std::min(_playerMeta._chargeTimer + dt, _playerMeta.chargeTime);
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && _playerMeta._chargeTimer > 0.005f && _playerMeta._fireCooldown <= 0.0f) {
                    bool inArc = _playerMeta.hasTurret || (Vector2DotProduct(fwd, aimDir) > 0.0f);
                    if (inArc) {
                        float ratio    = _playerMeta._chargeTimer / _playerMeta.chargeTime;
                        int   numProj  = std::max(1, (int)std::ceil(_playerMeta.burstCount * ratio));
                        float halfSprd = (_playerMeta.spreadAngle * 0.5f) * DEG2RAD;
                        float step     = (numProj > 1) ? (_playerMeta.spreadAngle * DEG2RAD) / (numProj - 1) : 0.0f;
                        float ttl      = _playerMeta.projRange / _playerMeta.projSpeed;
                        for (int bi = 0; bi < numProj; ++bi) {
                            float a = (numProj > 1) ? -halfSprd + step * bi : 0.0f;
                            float c = cosf(a), s = sinf(a);
                            Vector2 d = { aimDir.x * c - aimDir.y * s, aimDir.x * s + aimDir.y * c };
                            Projectile p;
                            p.position = pos;
                            p.velocity = { d.x * _playerMeta.projSpeed, d.y * _playerMeta.projSpeed };
                            p.lifetime = 0.0f; p.maxLife = ttl; p.damage = _playerMeta.weaponDamage; p.alive = true;
                            _w->projectiles.push_back(p);
                        }
                        _playerMeta._fireCooldown = _playerMeta.fireRate;
                    }
                    _playerMeta._chargeTimer = 0.0f;
                }
                break;
            }
            case WeaponFireMode::LockOn: {
                if (passLockId != 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && _playerMeta._fireCooldown <= 0.0f) {
                    Vector2 toTarget = Vector2Subtract(passLockPos, pos);
                    float   tLen     = Vector2Length(toTarget);
                    if (tLen > 1.0f) {
                        Vector2 tDir = Vector2Scale(toTarget, 1.0f / tLen);
                        float   ttl  = _playerMeta.projRange / _playerMeta.projSpeed;
                        Projectile p;
                        p.position = pos;
                        p.velocity = { tDir.x * _playerMeta.projSpeed, tDir.y * _playerMeta.projSpeed };
                        p.lifetime = 0.0f; p.maxLife = ttl; p.damage = _playerMeta.weaponDamage; p.alive = true;
                        p.isHoming = true;
                        p.targetId = passLockId;
                        p.turnRate = _playerMeta.weaponTurnRate;
                        _w->projectiles.push_back(p);
                        _playerMeta._fireCooldown = _playerMeta.fireRate;
                    }
                }
                break;
            }
            default: break;
            }
        }
    }

    AdvanceProjectilesAndAsteroids(dt);

    _w->age += dt;
    UpdateOrbits(dt);
    UpdateNpcShips(dt);
    ApplySunGravity(dt);

    if (_hitCooldown > 0.0f) _hitCooldown -= dt;

    // Dynamically update Hardpoint Max Hull based on equipped Armor modules
    for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
        if (!ps.alive) continue;
        for (HardpointState& hp : ps.hardpoints) {
            float baseHull = 100.0f; // Base health without modules
            float bonus = hp.armor.has_value() ? hp.armor->armor.hullBonus : 0.0f;
            float newMax = baseHull + bonus;

            if (hp.hull > newMax) hp.hull = newMax; // Cap current health if armor is removed
            hp.maxHull = newMax;
        }

        TickStationMining(ps, dt);

        // ── ADDED: Player Station Autonomous Firing ─────────────────────────
        // (This MUST be inside the ps loop so we know which station is firing)

        // 1. Find the closest living hostile to the station
        float closestDist = FLT_MAX;
        Vector2 targetPos = { 0, 0 };
        unsigned int targetId = 0;

        for (size_t li = 0; li < _w->npcMeta.size(); ++li) {
            const NpcMeta& npc = _w->npcMeta[li];
            if (!npc.alive || npc.faction != NpcFaction::Hostile) continue;
            float d = Vector2Distance(ps.position, _w->entities[li].transform.position);
            if (d < closestDist) {
                closestDist = d;
                targetPos = _w->entities[li].transform.position;
                targetId = npc.id;
            }
        }

        // 2. If a hostile is detected, let armed hardpoints open fire
        if (targetId != 0) {
            const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
            float rad = def ? def->radius : 120.0f;

            for (int i = 0; i < (int)ps.hardpoints.size(); ++i) {
                HardpointState& hp = ps.hardpoints[i];

                // Skip dead hardpoints, or hardpoints with no weapons installed
                // FIXED: Added to check the first weapon slot specifically
                if (!hp.alive || hp.weapons.empty() || !hp.weapons[0].has_value()) continue;

                // Process weapon cooldown
                if (hp.fireCooldown > 0.0f) {
                    hp.fireCooldown -= dt;
                    continue;
                }

                // Check if the target is within the weapon's maximum range
                const WeaponStats& ws = hp.weapons[0]->weapon;
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

    UpdateWorldStationFire(dt);

    if (!net::Game().IsClient()) {
        UpdateCollisions();
        UpdateNpcCollisions();
        UpdateCollisions();
        UpdateNpcCollisions();
    }

    // Discovery: mark stellar objects the player has flown near
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

    bool anyMenuOpen = _storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen ||
                       _galacticMap.isOpen || _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen ||
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
    if (net::Game().IsClient() && _worldSynced) {
        static uint32_t s_inputSeq = 0;
        net::InputCommand cmd;
        cmd.networkId   = net::Game().LocalNetworkId();
        cmd.aimRotation = _playerEntity.transform.rotation;
        cmd.posX        = _playerEntity.transform.position.x;
        cmd.posY        = _playerEntity.transform.position.y;
        cmd.sequence    = s_inputSeq++;
        cmd.firing      = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !blockFireUntilRelease) ? 1 : 0;
        net::Game().ClientSendInput(cmd);
    }
}

void SpaceFlight::TickStationMining(PlayerStation& ps, float dt) {
    if (ps.stationDefId != "mining_station" || ps.storage.empty()) return;

    // Find the Material Probe installed in any aux slot (only the Mining
    // Drill hardpoint has one, per station_defs.json).
    const ModuleDef* probe = nullptr;
    for (const HardpointState& hp : ps.hardpoints) {
        if (!hp.alive) continue;
        for (const auto& a : hp.aux) {
            if (a.has_value() && a->id == "aux_material_probe") { probe = &(*a); break; }
        }
        if (probe) break;
    }
    if (!probe) return;   // no probe installed — station collects nothing

    ps.miningTimer -= dt;
    if (ps.miningTimer > 0.0f) return;

    // Higher-grade probes collect faster.
    int   gradeIdx  = static_cast<int>(probe->grade);   // 0=Common .. 6=Mythic
    float interval  = std::max(2.0f, 9.0f - gradeIdx * 1.0f);
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

// In-world (NPC faction) station firing, per hardpoint. Operates on `_w`, so
// it serves both the player's system and background-world ticks.
void SpaceFlight::UpdateWorldStationFire(float dt) {
    for (SpaceStation& st : _w->stations) {
        if (!st.alive) continue;
        if (st.retaliateTimer > 0.0f) st.retaliateTimer -= dt;

        // Compute max weapon range across all armed hardpoints
        float maxRange = 500.0f;
        for (const HardpointState& hp : st.hardpoints)
            if (!hp.weapons.empty() && hp.weapons[0].has_value())
                maxRange = std::max(maxRange, hp.weapons[0]->weapon.projRange);

        // Find closest valid target within range — shared template so any
        // future hostile-capable unit (ships, turrets, ...) can reuse the
        // exact same "nearest hostile in range" rule stations use here.
        combat::HostileTarget pick = combat::FindNearestHostileTarget(
            *_w, _playerEntity, kPlayerFaction, st.faction, st.position, maxRange, st.id);

        Vector2      fireTarget        = pick.position;
        bool         hasFireTarget     = pick.valid;
        float        bestDist          = pick.valid ? Vector2Distance(st.position, pick.position) : maxRange;
        unsigned int fireTargetId      = pick.id;
        bool         fireTargetIsPlayer= (pick.kind == combat::HostileTargetKind::Player);

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
                            bestDist = d; fireTarget = _w->entities[j].transform.position;
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
            HardpointState& hp = st.hardpoints[hi];
            if (!hp.alive || hp.weapons.empty() || !hp.weapons[0].has_value()) continue;
            if (hp.fireCooldown > 0.0f) { hp.fireCooldown -= dt; continue; }

            const WeaponStats& ws = hp.weapons[0]->weapon;
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
                    if (_w->npcMeta[li].id == p.targetId && _w->npcMeta[li].alive) { tPos = _w->entities[li].transform.position; found = true; break; }
            if (!found)
                for (const SpaceStation& st : _w->stations)
                    if (st.id == p.targetId && st.alive) { tPos = st.position; found = true; break; }
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
    for (Asteroid& a : _w->asteroids)
        if (a.alive && Vector2Distance(anchor, a.position) > 2800.0f)
            a.alive = false;
    for (size_t ci = 0; ci < _w->npcMeta.size(); ++ci)
        if (_w->npcMeta[ci].alive && !_w->npcMeta[ci].wingman &&
            Vector2Distance(anchor, _w->entities[ci].transform.position) > 3000.0f) {
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

        // Compute the half-diagonal of the visible world area so spawns never
        // appear inside the camera frustum regardless of zoom level.
        float halfW     = (float)GetScreenWidth()  / (2.0f * _cameraZoom);
        float halfH     = (float)GetScreenHeight() / (2.0f * _cameraZoom);
        float viewEdge  = sqrtf(halfW * halfW + halfH * halfH) + 150.0f;

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

            auto [ne, nm] = MakeNpcEntity(_w->nextNpcId++, pos);
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
    bool menuOpen = (_storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _enterPopupOpen || _stationServicesMenu.isOpen || _localMapOpen ||
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
        Texture2D* playerTexPtr = nullptr;
        if (const auto* sd = ecs::ShipRegistry::ShipById(_playerMeta.defId))
            playerTexPtr = ResourceManager::Load(sd->assetPath);
        const Texture2D& tex = (previewShipId == "gargos") ? _gargosTex
            : (playerTexPtr ? *playerTexPtr : Texture2D{});

        if (tex.id > 0) {
            float tw = (float)tex.width, th = (float)tex.height;
            DrawTexturePro(tex, { 0, 0, tw, th }, { worldMouse.x, worldMouse.y, tw, th }, { tw / 2, th / 2 }, 0.0f, Color{ 255, 255, 255, 140 });
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

    if (_hitCooldown <= 0.0f || (int)(_hitCooldown * 8) % 2 == 0) {
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
        modPtrs[0] = (!sel->loadout.weapons.empty()) ? &sel->loadout.weapons[0] : nullptr;
        modPtrs[1] = &sel->loadout.armor;
        modPtrs[2] = (!sel->loadout.shields.empty()) ? &sel->loadout.shields[0] : nullptr;
        modPtrs[3] = &sel->loadout.engine;
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

    if (_commsMenuOpen) {
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

        if (_commsMenuPhase == 0) {
            Rectangle joinBtn = { (float)(mcx + CW - 180), (float)(mcy + CH - 52), 160.0f, 34.0f };
            bool hovJoin = CheckCollisionPointRec(m2, joinBtn);
            DrawRectangleRec(joinBtn, hovJoin ? Color{ 40, 90, 50, 230 } : Color{ 14, 28, 18, 200 });
            DrawRectangleLinesEx(joinBtn, 1.0f, Color{ 40, 160, 80, 200 });
            const char* jlbl = "REQUEST JOIN";
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
        if (const auto* ship = ecs::ShipRegistry::ShipById(_placingShipDefId))
            dName = ship->displayName;

        char msgbuf[128];
        std::snprintf(msgbuf, sizeof(msgbuf), "Build %s here?", dName.c_str());
        DrawText(msgbuf, sw2 / 2 - MeasureText(msgbuf, 14) / 2, py2 + 24, 14, Color{ 190,220,255,240 });
        DrawText("Friendly ships defend the sector autonomously.",
            sw2 / 2 - MeasureText("Friendly ships defend the sector autonomously.", 11) / 2,
            py2 + 48, 11, Color{ 110,140,180,200 });

        Vector2 m2 = GetMousePosition();
        Rectangle yesBtn = { (float)(px2 + 30),        (float)(py2 + PopH - 50), 120.0f, 32.0f };
        Rectangle noBtn = { (float)(px2 + PopW - 150),  (float)(py2 + PopH - 50), 120.0f, 32.0f };
        bool hovY = CheckCollisionPointRec(m2, yesBtn);
        bool hovN = CheckCollisionPointRec(m2, noBtn);

        DrawRectangleRec(yesBtn, hovY ? Color{ 20,80,40,230 } : Color{ 12,40,20,200 });
        DrawRectangleLinesEx(yesBtn, 1.0f, Color{ 40,160,80,200 });
        DrawText("YES", (int)(yesBtn.x + (yesBtn.width - MeasureText("YES", 12)) / 2),
            (int)(yesBtn.y + 10), 12, hovY ? WHITE : Color{ 80,200,100,220 });

        DrawRectangleRec(noBtn, hovN ? Color{ 80,20,20,230 } : Color{ 40,12,12,200 });
        DrawRectangleLinesEx(noBtn, 1.0f, Color{ 160,40,40,200 });
        DrawText("NO", (int)(noBtn.x + (noBtn.width - MeasureText("NO", 12)) / 2),
            (int)(noBtn.y + 10), 12, hovN ? WHITE : Color{ 200,80,80,220 });
    }

    if (_systemMap.isOpen)   _systemMap.Draw();
    if (_galacticMap.isOpen) _galacticMap.Draw();

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
    _commsLog.clear();
    _remoteEntities.clear();
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
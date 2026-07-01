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
#include "net/NetworkManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static constexpr float kCoronaReach = 5.0f;


// Row 1: MODULES | STORAGE | ESCORTS
// Row 2: ENTER   | BUILD   | COMMS
// Row 3: RANKS (full width)
static void ComputeHudButtons(int sw, int sh,
    Rectangle& modBtn, Rectangle& stoBtn, Rectangle& escBtn,
    Rectangle& enterBtn, Rectangle& buildBtn, Rectangle& commsBtn,
    Rectangle& ranksBtn) {
    static constexpr int HudH2 = 158;
    static constexpr int CenterW = 190;
    int hx = 12, hw = sw - 24;
    int rDiv = hx + (hw - CenterW) / 2 + CenterW;
    int hy = sh - HudH2 - 6;
    int rx = rDiv + 12, ry = hy + 10;
    int rw = (hx + hw) - rDiv - 16;
    int btnW = std::min((rw - 16) / 3, 120);
    modBtn   = { (float)rx,                   (float)ry,        (float)btnW, 28.0f };
    stoBtn   = { (float)(rx + btnW + 8),      (float)ry,        (float)btnW, 28.0f };
    escBtn   = { (float)(rx + btnW * 2 + 16), (float)ry,        (float)btnW, 28.0f };
    enterBtn = { (float)rx,                   (float)(ry + 36), (float)btnW, 28.0f };
    buildBtn = { (float)(rx + btnW + 8),      (float)(ry + 36), (float)btnW, 28.0f };
    commsBtn = { (float)(rx + btnW * 2 + 16), (float)(ry + 36), (float)btnW, 28.0f };
    ranksBtn = { (float)rx,                   (float)(ry + 72), (float)(btnW * 3 + 16), 28.0f };
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

static void BuildNpcStationHardpoints(SpaceStation& st, const StationTypeDef& typeDef) {
    bool isMilitary = (typeDef.id == "military_outpost");
    auto weaponOpt  = ModuleRegistry::ById(isMilitary ? "scatter_rifle_i" : "pulse_cannon_i");
    auto armorOpt   = ModuleRegistry::ById("armor_steel_plating");

    st.hardpoints.clear();
    float totalHull = 0.0f;

    for (const StationHardpointDef& hpDef : typeDef.hardpoints) {
        HardpointState hp;
        hp.id          = hpDef.id;
        hp.displayName = hpDef.displayName;
        hp.isCore      = hpDef.isCore;
        hp.maxHull     = hpDef.maxHull;
        hp.hull        = hpDef.maxHull;
        hp.alive       = true;
        hp.wSlots      = hpDef.wSlots;
        hp.arSlots     = hpDef.arSlots;
        hp.shSlots     = hpDef.shSlots;
        hp.enSlots     = hpDef.enSlots;
        hp.auxSlots    = hpDef.auxSlots;

        for (int i = 0; i < hpDef.wSlots; ++i)
            hp.weapons.push_back(weaponOpt.has_value() ? std::optional<ModuleDef>(*weaponOpt) : std::nullopt);

        if (hpDef.arSlots > 0 && armorOpt.has_value()) {
            hp.armor = *armorOpt;
            totalHull += armorOpt->armor.hullBonus;
        }

        for (int i = 0; i < hpDef.shSlots;  ++i) hp.shields.push_back(std::nullopt);
        for (int i = 0; i < hpDef.auxSlots; ++i) hp.aux.push_back(std::nullopt);

        totalHull += hpDef.maxHull;
        st.hardpoints.push_back(hp);
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
static constexpr float StationDrawRadius =  90.0f;

void SpaceFlight::DrawPlanets() const {
    if (_planetBaseTex.id == 0) return;
    float size = PlanetDrawRadius * 2.0f;
    Rectangle src = { 0.0f, 0.0f, (float)_planetBaseTex.width, (float)_planetBaseTex.height };
    Vector2   origin = { size * 0.5f, size * 0.5f };
    for (const SpacePlanet& p : _planets) {
        DrawCircleV(p.position, p.radius * 1.10f, Color{ 80, 120, 210, 18 });
        DrawCircleV(p.position, p.radius * 1.05f, Color{ 90, 130, 220, 12 });
        Rectangle dst = { p.position.x, p.position.y, size, size };
        DrawTexturePro(_planetBaseTex, src, dst, origin, 0.0f, WHITE);
    }
}

void SpaceFlight::DrawStations() const {
    if (_stationBaseTex.id == 0) return;
    float size = StationDrawRadius * 2.0f;
    Rectangle src = { 0.0f, 0.0f, (float)_stationBaseTex.width, (float)_stationBaseTex.height };
    Vector2   origin = { size * 0.5f, size * 0.5f };
    for (const SpaceStation& s : _stations) {
        if (!s.alive) continue;
        DrawCircleV(s.position, s.radius * 1.25f, Color{ 60, 120, 200, 14 });
        Rectangle dst = { s.position.x, s.position.y, size, size };
        DrawTexturePro(_stationBaseTex, src, dst, origin, 0.0f, WHITE);

        for (int i = 0; i < (int)s.hardpoints.size(); ++i) {
            const HardpointState& hp = s.hardpoints[i];
            if (hp.isCore) continue; // core is at center, skip visual ring
            Vector2 hpPos = GetNpcStationHardpointPos(s, i);
            float hpRad = 10.0f;
            bool hasWeapon = !hp.weapons.empty() && hp.weapons[0].has_value();
            Color ringCol = hasWeapon ? Color{ 220, 100, 60, 200 } : Color{ 80, 160, 255, 200 };
            DrawCircleV(hpPos, hpRad, Color{ 15, 25, 40, 210 });
            DrawCircleLinesV(hpPos, hpRad, ringCol);
            DrawCircleV(hpPos, hpRad * 0.4f, hasWeapon ? Color{ 200, 80, 40, 255 } : Color{ 80, 140, 200, 255 });
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
            DrawTexturePro(_stationBaseTex, src, dst, origin, 0.0f, WHITE);
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
    const float r         = _sun.radius;
    const float outerDist = r * kCoronaReach;
    const float scale     = halfSize / outerDist;  // world units → texture pixels

    const Color& c  = _sun.coreColor;
    const Color& ig = _sun.innerGlow;
    const Color& og = _sun.outerGlow;

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
    if (!_sun.active) return;
    Vector2 pos = { 0.0f, 0.0f };
    float   r = _sun.radius;
    float   t = (float)GetTime();
    const Color& c = _sun.coreColor;

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
    for (const SpacePlanet& p : _planets)
        if (Vector2Distance(pos, p.position) < p.radius + 50.0f) return true;
    return false;
}

bool SpaceFlight::IsNearStation() const {
    const Vector2& pos = _playerEntity.transform.position;
    for (const SpaceStation& s : _stations) {
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
    _planets.clear();
    _stations.clear();
    unsigned int nextId = 100;

    // ── Spawn sun ──────────────────────────────────────────────────────────────
    const StarTypeDef* starDef = StarRegistry::Pick(seed != 0 ? seed : (unsigned int)GetRandomValue(1, 99999));
    if (!starDef) starDef = StarRegistry::ById("G");
    _sun.typeId      = starDef->id;
    _sun.radius      = starDef->minRadius + (float)GetRandomValue(0, (int)(starDef->maxRadius - starDef->minRadius));
    _sun.gravRange   = _sun.radius * starDef->gravRangeMult;
    _sun.gravStrength= starDef->gravStrength;
    _sun.coreColor   = starDef->coreColor;
    _sun.innerGlow   = starDef->innerGlowColor;
    _sun.outerGlow   = starDef->outerGlowColor;
    _sun.active      = true;
    BakeSunCorona();

    // Safe player spawn: outside gravity range, at a random angle
    float spawnAngle = (float)GetRandomValue(0, 359) * DEG2RAD;
    float spawnDist  = _sun.gravRange + 800.0f;
    _playerSpawnPos  = { cosf(spawnAngle) * spawnDist, sinf(spawnAngle) * spawnDist };

    // Planet orbits must start outside gravity zone
    float minOrbit = std::max(2500.0f, _sun.gravRange * 1.4f);
    float maxOrbit = minOrbit + 3000.0f;

    int planetCount = GetRandomValue(0, 10);
    for (int attempt = 0; attempt < 300 && (int)_planets.size() < planetCount; ++attempt) {
        float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
        float dist = minOrbit + (float)GetRandomValue(0, (int)(maxOrbit - minOrbit));
        Vector2 pos = { cosf(ang) * dist, sinf(ang) * dist };
        bool tooClose = false;
        for (const SpacePlanet& p : _planets)
            if (Vector2Distance(p.position, pos) < PlanetDrawRadius * 3.0f) { tooClose = true; break; }
        if (!tooClose) {
            SpacePlanet planet;
            planet.position    = pos;
            planet.radius      = PlanetDrawRadius;
            planet.id          = nextId++;
            planet.orbitRadius = dist;
            planet.orbitAngle  = ang;
            planet.orbitSpeed  = 0.1f / sqrtf(dist);  // Kepler-style: closer = faster
            _planets.push_back(planet);
        }
    }

    // Stations also pushed outside gravity zone
    float minStation = std::max(1500.0f, _sun.gravRange + 600.0f);
    float maxStation = std::max(3500.0f, _sun.gravRange + 2500.0f);

    int stationCount = GetRandomValue(1, 10);
    for (int attempt = 0; attempt < 300 && (int)_stations.size() < stationCount; ++attempt) {
        float ang  = (float)GetRandomValue(0, 359) * DEG2RAD;
        float dist = minStation + (float)GetRandomValue(0, (int)(maxStation - minStation));
        Vector2 pos = { cosf(ang) * dist, sinf(ang) * dist };
        bool tooClose = false;
        for (const SpaceStation& s : _stations)
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
            BuildNpcStationHardpoints(st, typeDef);
            _stations.push_back(std::move(st));
        }
    }
}

void SpaceFlight::UpdateOrbits(float dt) {
    for (SpacePlanet& p : _planets) {
        p.orbitAngle += p.orbitSpeed * dt;
        p.position = {
            cosf(p.orbitAngle) * p.orbitRadius,
            sinf(p.orbitAngle) * p.orbitRadius
        };
    }
}

void SpaceFlight::ApplySunGravity(float dt) {
    if (!_sun.active) return;

    // Player
    Vector2& pPos = _playerEntity.transform.position;
    Vector2& pVel = _playerEntity.transform.velocity;
    float playerDist = Vector2Length(pPos);
    if (playerDist < _sun.radius * 0.6f) {
        _playerEntity.health.currentHull = 0.0f;
    }
    else if (playerDist < _sun.gravRange && playerDist > 0.01f) {
        float t     = 1.0f - (playerDist / _sun.gravRange);
        float accel = _sun.gravStrength * t * t;
        pVel.x += (-pPos.x / playerDist) * accel * dt;
        pVel.y += (-pPos.y / playerDist) * accel * dt;
    }

    // NPC ships
    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        NpcMeta& m = _npcMeta[i];
        ecs::Entity& e = _entities[i];
        if (!m.alive) continue;
        float dist = Vector2Length(e.transform.position);
        if (dist < _sun.radius * 0.6f) {
            m.alive = false;
            _npcFreeSlots.push_back(i);
            if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
            AddCommsMessage(m.wingman ? "WINGMAN lost to stellar gravity." : "Ship destroyed by stellar gravity.");
        }
        else if (dist < _sun.gravRange && dist > 0.01f) {
            float t     = 1.0f - (dist / _sun.gravRange);
            float accel = _sun.gravStrength * t * t;
            e.transform.velocity.x += (-e.transform.position.x / dist) * accel * dt;
            e.transform.velocity.y += (-e.transform.position.y / dist) * accel * dt;
        }
    }

    // Asteroids — silently consumed, no splits or drops
    for (Asteroid& a : _asteroids) {
        if (!a.alive) continue;
        float dist = Vector2Length(a.position);
        if (dist < _sun.radius * 0.6f) {
            a.alive = false;
        }
        else if (dist < _sun.gravRange && dist > 0.01f) {
            float t     = 1.0f - (dist / _sun.gravRange);
            float accel = _sun.gravStrength * t * t;
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
    CommsEntry e;  e.text = text;  e.fromPlayer = fromPlayer;
    _commsLog.push_back(e);
    if ((int)_commsLog.size() > 5) _commsLog.erase(_commsLog.begin());
}

static uint32_t s_npcEntityIdCounter = 10000;

static Faction kPlayerFaction = Faction::Republic;

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
    _entities.clear();
    _npcMeta.clear();
    _npcFreeSlots.clear();
    _nextNpcId = 1000;
    int count = GetRandomValue(3, 5);
    float minDist = std::max(700.0f,  _sun.active ? _sun.gravRange + 500.0f  : 700.0f);
    float maxDist = std::max(1400.0f, _sun.active ? _sun.gravRange + 1500.0f : 1400.0f);
    for (int i = 0; i < count; ++i) {
        float ang = (float)GetRandomValue(0, 359) * DEG2RAD;
        float dist = minDist + (float)GetRandomValue(0, (int)(maxDist - minDist));
        Vector2 pos = { cosf(ang) * dist, sinf(ang) * dist };
        auto [entity, meta] = MakeNpcEntity(_nextNpcId++, pos);
        ApplyNpcLoadout(entity, meta);
        meta.preferredRange = meta.attackRange * 0.75f;
        entity.health.currentHull = entity.health.maxStats.hull;
        entity.network.networkId = entity.id;   // expose NPC to HostBroadcast
        _entities.push_back(std::move(entity));
        _npcMeta.push_back(std::move(meta));
    }
}

void SpaceFlight::UpdateNpcShips(float dt) {
    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        NpcMeta&    m = _npcMeta[i];
        ecs::Entity& e = _entities[i];
        if (!m.alive) continue;

        float distToPlayer = Vector2Distance(e.transform.position, _playerEntity.transform.position);

        if (m.faction == NpcFaction::Hostile && !m.wingman) {
            float distToClosestTarget = distToPlayer;
            for (size_t j = 0; j < _npcMeta.size(); ++j) {
                if (!_npcMeta[j].alive || _npcMeta[j].id == m.id) continue;
                if (DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                if (d < distToClosestTarget) distToClosestTarget = d;
            }
            for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
                if (!ps.alive) continue;
                float d = Vector2Distance(e.transform.position, ps.position);
                if (d < distToClosestTarget) distToClosestTarget = d;
            }
            for (const SpaceStation& st : _stations) {
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
                if (distToClosestTarget > m.aggroRange * 2.2f) {
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
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < closestHostileDist) closestHostileDist = d;
                }
                for (const SpaceStation& st : _stations) {
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
                    for (size_t j = 0; j < _npcMeta.size(); ++j) {
                        if (_npcMeta[j].id == m.retaliationTargetId && _npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                            if (d < closestHostileDist) closestHostileDist = d;
                            break;
                        }
                    }
                }
                if (e.health.currentHull / e.health.maxStats.hull < 0.20f) {
                    if (m.aiState != NpcAiState::Flee) { m.aiState = NpcAiState::Flee; m.waypointSet = false; }
                } else if (m.aiState == NpcAiState::Flee) {
                    if (closestHostileDist > m.aggroRange * 2.2f) {
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
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || _npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < best) { best = d; chaseTarget = _entities[j].transform.position; }
                }
                for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
                    if (!ps.alive) continue;
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                    float rad = def ? def->radius : 120.0f;
                    Vector2 aimPos = GetBestHardpointAimPos(ps, rad);
                    float d = Vector2Distance(e.transform.position, aimPos);
                    if (d < best) { best = d; chaseTarget = aimPos; }
                }
                for (const SpaceStation& st : _stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; chaseTarget = st.position; }
                }
            }
            else {
                float best = FLT_MAX;
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < best) { best = d; chaseTarget = _entities[j].transform.position; }
                }
                for (const SpaceStation& st : _stations) {
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
                    for (size_t j = 0; j < _npcMeta.size(); ++j) {
                        if (_npcMeta[j].id == m.retaliationTargetId && _npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                            if (d < best) { best = d; chaseTarget = _entities[j].transform.position; }
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
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || _npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < best) { best = d; attackTarget = _entities[j].transform.position; }
                }
                for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
                    if (!ps.alive) continue;
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                    float rad = def ? def->radius : 120.0f;
                    Vector2 aimPos = GetBestHardpointAimPos(ps, rad);
                    float d = Vector2Distance(e.transform.position, aimPos);
                    if (d < best) { best = d; attackTarget = aimPos; }
                }
                for (const SpaceStation& st : _stations) {
                    if (!st.alive) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, st.faction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, st.position);
                    if (d < best) { best = d; attackTarget = st.position; }
                }
            } else {
                float best = FLT_MAX;
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || _npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < best) { best = d; attackTarget = _entities[j].transform.position; hasTarget = true; }
                }
                for (const SpaceStation& st : _stations) {
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
                    for (size_t j = 0; j < _npcMeta.size(); ++j) {
                        if (_npcMeta[j].id == m.retaliationTargetId && _npcMeta[j].alive) {
                            float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                            if (d < best) { best = d; attackTarget = _entities[j].transform.position; hasTarget = true; }
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
            Vector2 threatPos = _playerEntity.transform.position;
            if (DiplomaticRegistry::Get(m.npcFaction, kPlayerFaction) != Relation::Hostile) {
                float best = FLT_MAX;
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < best) { best = d; threatPos = _entities[j].transform.position; }
                }
            }
            Vector2 away = Vector2Subtract(e.transform.position, threatPos);
            desiredRot = atan2f(away.x, -away.y) * RAD2DEG;
            m.waypointSet = false;
            thrustMult = 1.0f;
            break;
        }
        case NpcAiState::Escort: {
            m.escortTargetId = 0;
            float closestEnemy = 900.0f;
            for (size_t j = 0; j < _npcMeta.size(); ++j) {
                if (!_npcMeta[j].alive || _npcMeta[j].wingman || DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                if (d < closestEnemy) { closestEnemy = d; m.escortTargetId = _npcMeta[j].id; }
            }
            if (m.escortTargetId != 0) {
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (_npcMeta[j].id != m.escortTargetId) continue;
                    Vector2 toE = Vector2Subtract(_entities[j].transform.position, e.transform.position);
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

        if (_sun.active) {
            float distToSun = Vector2Length(e.transform.position);
            float avoidZone = _sun.gravRange * 1.4f;
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
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (!_npcMeta[j].alive || _npcMeta[j].id == m.id) continue;
                    if (DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                    float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                    if (d < best) { best = d; fireTarget = _entities[j].transform.position; fireNpcId = _npcMeta[j].id; fireTargetIsStation = false; }
                }
                for (const PlayerStation& ps : FleetManager::Get().PlayerStations) {
                    if (!ps.alive) continue;
                    const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
                    float rad = def ? def->radius : 120.0f;
                    Vector2 aimPos = GetBestHardpointAimPos(ps, rad);
                    float d = Vector2Distance(e.transform.position, aimPos);
                    if (d < best) { best = d; fireTarget = aimPos; fireNpcId = 0; fireTargetIsStation = true; }
                }
                for (const SpaceStation& st : _stations) {
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
                    _projectiles.push_back(p);
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
                        _projectiles.push_back(p);
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
                    _projectiles.push_back(p);
                    m.fireCooldown = m.npcFireRate;
                }
                break;
            }
            }
        }

        if (m.wingman && m.escortTargetId != 0 && m.npcHasWeapon) {
            m.fireCooldown -= dt;
            for (size_t j = 0; j < _npcMeta.size(); ++j) {
                if (_npcMeta[j].id != m.escortTargetId || !_npcMeta[j].alive) continue;
                Vector2 toE = Vector2Subtract(_entities[j].transform.position, e.transform.position);
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
                        _projectiles.push_back(ep);
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
                            _projectiles.push_back(ep);
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
                        ep.targetId = _npcMeta[j].id;
                        ep.turnRate = 3.0f;
                        _projectiles.push_back(ep);
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
            for (size_t j = 0; j < _npcMeta.size(); ++j) {
                if (!_npcMeta[j].alive || DiplomaticRegistry::Get(m.npcFaction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
                float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                if (d < best) { best = d; fireTarget = _entities[j].transform.position; fireNpcId = _npcMeta[j].id; fireTargetIsPlayer = false; }
            }
            if (m.retaliatingVsPlayer) {
                float d = Vector2Distance(e.transform.position, _playerEntity.transform.position);
                if (d < best) { best = d; fireTarget = _playerEntity.transform.position; fireNpcId = 0; fireTargetIsPlayer = true; }
            }
            if (m.retaliationTargetId != 0) {
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (_npcMeta[j].id == m.retaliationTargetId && _npcMeta[j].alive) {
                        float d = Vector2Distance(e.transform.position, _entities[j].transform.position);
                        if (d < best) { best = d; fireTarget = _entities[j].transform.position; fireNpcId = m.retaliationTargetId; fireTargetIsPlayer = false; }
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
                        _projectiles.push_back(p); m.fireCooldown = m.npcFireRate;
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
                            _projectiles.push_back(p);
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
                        _projectiles.push_back(p); m.fireCooldown = m.npcFireRate;
                    }
                    break;
                }
            }
        }
    }

    for (LootDrop& ld : _lootDrops) {
        if (ld.collected) continue;
        ld.lifetime -= dt;
        ld.pulseTimer += dt;
        if (ld.lifetime <= 0.0f) ld.collected = true;
    }
    auto isGone = [](const LootDrop& ld) { return ld.collected; };
    _lootDrops.erase(std::remove_if(_lootDrops.begin(), _lootDrops.end(), isGone),
        _lootDrops.end());

    for (MaterialDrop& md : _materialDrops) {
        if (md.collected) continue;
        md.lifetime -= dt;
        md.pulseTimer += dt;
        if (md.lifetime <= 0.0f) md.collected = true;
    }
    auto matGone = [](const MaterialDrop& md) { return md.collected; };
    _materialDrops.erase(std::remove_if(_materialDrops.begin(), _materialDrops.end(), matGone),
        _materialDrops.end());
}

void SpaceFlight::UpdateNpcCollisions() {
    // Block 1: player projectiles hit any non-escort NPC
    for (Projectile& p : _projectiles) {
        if (!p.alive || !p.fromPlayer) continue;
        for (size_t i = 0; i < _npcMeta.size(); ++i) {
            NpcMeta&     m = _npcMeta[i];
            ecs::Entity& e = _entities[i];
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
    for (Projectile& p : _projectiles) {
        if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
        bool fromHostile = false;
        for (size_t j = 0; j < _npcMeta.size(); ++j)
            if (_npcMeta[j].id == p.ownerId && _npcMeta[j].faction == NpcFaction::Hostile) { fromHostile = true; break; }
        if (!fromHostile) continue;
        for (size_t i = 0; i < _npcMeta.size(); ++i) {
            NpcMeta&     m = _npcMeta[i];
            ecs::Entity& e = _entities[i];
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
                    _npcFreeSlots.push_back(i);
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    SpawnLootDrop(e.transform.position, m.faction);
                    AddCommsMessage(m.wingman ? "WINGMAN destroyed." : m.shipTypeName + " destroyed.");
                }
                break;
            }
        }
    }

    // Block 3: friendly/neutral NPC projectiles hit hostile NPCs
    for (Projectile& p : _projectiles) {
        if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
        bool shooterIsNonHostile = false;
        for (size_t j = 0; j < _npcMeta.size(); ++j)
            if (_npcMeta[j].id == p.ownerId && _npcMeta[j].faction != NpcFaction::Hostile) { shooterIsNonHostile = true; break; }
        if (!shooterIsNonHostile) continue;
        for (size_t i = 0; i < _npcMeta.size(); ++i) {
            NpcMeta&     m = _npcMeta[i];
            ecs::Entity& e = _entities[i];
            if (!m.alive || m.faction != NpcFaction::Hostile) continue;
            if (Vector2Distance(p.position, e.transform.position) < m.radius + 3.5f) {
                p.alive = false;
                e.health.currentHull = std::max(0.0f, e.health.currentHull - p.damage);
                if (m.aiState == NpcAiState::Patrol) m.aiState = NpcAiState::Chase;
                if (e.health.currentHull <= 0.0f) {
                    m.alive = false;
                    _npcFreeSlots.push_back(i);
                    if (_npcTargetId == m.id) { _npcTargetId = 0; _target = TargetInfo{}; }
                    SpawnLootDrop(e.transform.position, m.faction);
                    AddCommsMessage(m.shipTypeName + " destroyed.");
                }
                break;
            }
        }
    }

    // Block 4: hostile/retaliating NPC projectiles hit player — skip non-hostile non-retaliating NPC shots and station shots
    for (Projectile& p : _projectiles) {
        if (!p.alive || p.fromPlayer) continue;
        if (p.ownerId != 0) {
            bool skip = false;
            for (size_t j = 0; j < _npcMeta.size(); ++j) {
                if (_npcMeta[j].id == p.ownerId && _npcMeta[j].faction != NpcFaction::Hostile) {
                    if (!_npcMeta[j].retaliatingVsPlayer) skip = true;
                    break;
                }
            }
            if (!skip) {
                for (const SpaceStation& st : _stations)
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
    for (Projectile& p : _projectiles) {
        if (!p.alive || p.fromPlayer) continue;

        for (PlayerStation& ps : FleetManager::Get().PlayerStations) {
            if (!ps.alive) continue;

            const PlayerStationDef* def = PlayerStationRegistry::ById(ps.stationDefId);
            float rad = def ? def->radius : 120.0f;

            // 1. Check if outer defenses still exist to protect the core
            bool hasOuterHardpoints = false;
            for (const HardpointState& hp : ps.hardpoints) {
                if (!hp.isCore && hp.alive) { hasOuterHardpoints = true; break; }
            }

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
    for (Projectile& p : _projectiles) {
        if (!p.alive || p.fromPlayer) continue;
        SpaceStation* stShooter = nullptr;
        for (SpaceStation& st : _stations)
            if (st.id == p.ownerId) { stShooter = &st; break; }
        if (!stShooter) continue;
        for (size_t i = 0; i < _npcMeta.size(); ++i) {
            NpcMeta&     m = _npcMeta[i];
            ecs::Entity& e = _entities[i];
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
                    _npcFreeSlots.push_back(i);
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
    for (Projectile& p : _projectiles) {
        if (!p.alive) continue;
        for (SpaceStation& st : _stations) {
            if (!st.alive || st.id == p.ownerId) continue;
            // Quick broad-phase: skip if not near the station at all
            if (Vector2Distance(p.position, st.position) > st.radius + 20.0f) continue;

            bool hasOuterHardpoints = false;
            for (const HardpointState& hp : st.hardpoints)
                if (!hp.isCore && hp.alive) { hasOuterHardpoints = true; break; }

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
                    net::Game().HostBroadcastStationDead(st.id);
                }
                break; // projectile consumed
            }
        }
    }

    // Block 8: Hostile NPC projectiles hit remote client entities (host-authoritative).
    if (net::Game().IsHost() && !_remoteEntities.empty()) {
        std::vector<uint32_t> deadClients;
        for (Projectile& p : _projectiles) {
            if (!p.alive || p.fromPlayer || p.ownerId == 0) continue;
            bool fromHostile = false;
            for (size_t j = 0; j < _npcMeta.size(); ++j)
                if (_npcMeta[j].id == p.ownerId && _npcMeta[j].faction == NpcFaction::Hostile) { fromHostile = true; break; }
            if (!fromHostile) continue;
            for (auto& [netId, re] : _remoteEntities) {
                if (re.id == 0) continue;
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

    for (LootDrop& ld : _lootDrops) {
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

    for (MaterialDrop& md : _materialDrops) {
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
    _lootDrops.push_back(ld);
}

void SpaceFlight::SpawnMaterialDrop(Vector2 pos, const std::string& materialId) {
    MaterialDrop md;
    md.position   = pos;
    md.materialId = materialId;
    _materialDrops.push_back(md);
}

void SpaceFlight::DrawRemotePlayers() const {
    for (const auto& [id, re] : _remoteEntities) {
        if (re.id == 0) continue;
        const Vector2& pos = re.transform.position;
        const float    rot = re.transform.rotation;
        Texture2D* tex = re.sprite.texture;
        if (tex && tex->id > 0) {
            float tw = (float)tex->width, th = (float)tex->height;
            // Use the same pixel scale as the local player's ship def if available.
            const ecs::ShipDef* def = ecs::ShipRegistry::ShipById(_playerMeta.defId);
            float ps = def ? def->pixelScale : 1.0f;
            Rectangle src    = { 0.0f, 0.0f, tw, th };
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
    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        const NpcMeta&    m = _npcMeta[i];
        const ecs::Entity& e = _entities[i];
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
            float     lightRange = _sun.active ? _sun.gravRange * 5.0f : 0.0f;
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

    for (const LootDrop& ld : _lootDrops) {
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

    for (const MaterialDrop& md : _materialDrops) {
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

static void AssignAsteroidMaterials(Asteroid& a) {
    struct Entry { const char* id; int minPct, maxPct, weight; };
    static const Entry kPool[] = {
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
    static constexpr int kPoolSize = 9;

    // Tier 2: 30% 1-mat, 45% 2-mat, 25% 3-mat
    // Tier 1: 60% 1-mat, 33% 2-mat,  7% 3-mat
    // Tier 0: 85% 1-mat, 15% 2-mat,  0% 3-mat
    int r = GetRandomValue(0, 99);
    int matCount;
    if      (a.tier == 2) matCount = (r < 30) ? 1 : (r < 75) ? 2 : 3;
    else if (a.tier == 1) matCount = (r < 60) ? 1 : (r < 93) ? 2 : 3;
    else                  matCount = (r < 85) ? 1 : 2;

    bool used[kPoolSize] = {};
    for (int m = 0; m < matCount; ++m) {
        int totalW = 0;
        for (int i = 0; i < kPoolSize; ++i) if (!used[i]) totalW += kPool[i].weight;
        int pick = GetRandomValue(0, totalW - 1), cumW = 0;
        for (int i = 0; i < kPoolSize; ++i) {
            if (used[i]) continue;
            cumW += kPool[i].weight;
            if (pick < cumW) {
                used[i] = true;
                a.materials.push_back({ kPool[i].id, GetRandomValue(kPool[i].minPct, kPool[i].maxPct) });
                break;
            }
        }
    }
}

void SpaceFlight::SpawnInitialAsteroids() {
    for (int i = 0; i < 8; ++i) {
        float ang = ((float)i / 8.0f) * 2.0f * PI;
        float dist = (float)GetRandomValue(500, 1200);
        Asteroid a = MakeAsteroid({ cosf(ang) * dist, sinf(ang) * dist }, 2);
        AssignAsteroidMaterials(a);
        _asteroids.push_back(std::move(a));
    }
}

static void DrawAsteroid(const Asteroid& a, const Texture2D* tex) {
    if (tex && tex->id > 0) {
        float tw = (float)tex->width, th = (float)tex->height;
        float diameter = a.radius * 2.0f;
        Rectangle src    = { 0.0f, 0.0f, tw, th };
        Rectangle dst    = { a.position.x, a.position.y, diameter, diameter };
        Vector2   origin = { diameter / 2.0f, diameter / 2.0f };
        DrawTexturePro(*tex, src, dst, origin, a.rotation, WHITE);
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

    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        if (!_npcMeta[i].alive) continue;
        for (size_t j = i + 1; j < _npcMeta.size(); ++j) {
            if (!_npcMeta[j].alive) continue;

            float dist = Vector2Distance(_entities[i].transform.position, _entities[j].transform.position);
            float minDist = _npcMeta[i].radius + _npcMeta[j].radius;

            if (dist < minDist && dist > 0.01f) {
                Vector2 norm = Vector2Scale(Vector2Subtract(_entities[i].transform.position, _entities[j].transform.position), 1.0f / dist);
                float overlap = minDist - dist;

                _entities[i].transform.position = Vector2Add(_entities[i].transform.position, Vector2Scale(norm, overlap * 0.5f));
                _entities[j].transform.position = Vector2Subtract(_entities[j].transform.position, Vector2Scale(norm, overlap * 0.5f));

                float vRelN = Vector2DotProduct(Vector2Subtract(_entities[i].transform.velocity, _entities[j].transform.velocity), norm);
                if (vRelN < 0.0f) {
                    float bounceImpulse = -1.25f * vRelN;
                    _entities[i].transform.velocity = Vector2Add(_entities[i].transform.velocity, Vector2Scale(norm, bounceImpulse * 0.5f));
                    _entities[j].transform.velocity = Vector2Subtract(_entities[j].transform.velocity, Vector2Scale(norm, bounceImpulse * 0.5f));
                }
            }
        }
    }

    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        NpcMeta&     m = _npcMeta[i];
        ecs::Entity& e = _entities[i];
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
        for (Projectile& p : _projectiles) {
            if (!p.alive || !p.fromPlayer) continue;
            for (Asteroid& a : _asteroids) {
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

    int n = (int)_asteroids.size();
    for (int i = 0; i < n; ++i) {
        Asteroid& a = _asteroids[i];
        if (!a.alive) continue;
        for (int j = i + 1; j < n; ++j) {
            Asteroid& b = _asteroids[j];
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
        for (Asteroid& a : _asteroids) {
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
    for (Asteroid& a : _asteroids) {
        if (!a.alive) continue;
        for (size_t i = 0; i < _npcMeta.size(); ++i) {
            NpcMeta&     m = _npcMeta[i];
            ecs::Entity& e = _entities[i];
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
                    _npcFreeSlots.push_back(i);
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
        for (Projectile& p : _projectiles) {
            if (!p.alive || !p.fromPlayer) continue;
            for (auto& [netId, re] : _remoteEntities) {
                if (re.id == 0) continue;
                if (p.ownerId == netId) continue;  // don't let a client's projectile hit themselves
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

    for (auto& a : spawns) _asteroids.push_back(std::move(a));
}

static constexpr int HudH = 158;
static const Color HudBg = { 7, 12,  7, 228 };
static const Color HudBorder = { 40,158, 40, 200 };
static const Color HudLabel = { 68,162, 68, 255 };
static const Color HudValue = { 192,218,192, 255 };
static const Color HudDiv = { 34, 98, 34, 175 };

void SpaceFlight::UpdateTarget() {
    Vector2 mouse = GetMousePosition();
    Vector2 mw = GetScreenToWorld2D(mouse, _camera);

    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        const NpcMeta&    m = _npcMeta[i];
        const ecs::Entity& e = _entities[i];
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

    for (const Asteroid& a : _asteroids) {
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

    for (const SpaceStation& s : _stations) {
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
            _target.health = s.hull;
            _target.maxHealth = s.maxHull;
            _target.distance = Vector2Distance(_playerEntity.transform.position, s.position);
            _target.tier = -1;
            return;
        }
    }

    for (const SpacePlanet& p : _planets) {
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
            for (size_t i = 0; i < _npcMeta.size(); ++i) {
                const NpcMeta&    m = _npcMeta[i];
                const ecs::Entity& e = _entities[i];
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
            for (const Asteroid& a : _asteroids) {
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

    DrawRectangle(hx, hy, hw, HudH, HudBg);
    DrawRectangleLinesEx({ (float)hx, (float)hy, (float)hw, (float)HudH }, 1.0f, HudBorder);
    DrawRectangle(lDiv, hy + 6, 1, HudH - 12, HudDiv);
    DrawRectangle(rDiv, hy + 6, 1, HudH - 12, HudDiv);

    auto DrawStatusRing = [](Vector2 c, float iR, float oR,
        float pct, Color fill, Color bg) {
            DrawRing(c, iR, oR, -90.0f, 270.0f, 64, bg);
            if (pct > 0.005f)
                DrawRing(c, iR, oR, -90.0f, -90.0f + 360.0f * pct, 64, fill);
        };
    auto DrawHalfRing = [](Vector2 c, float iR, float oR,
        float pct, Color fill, Color bg, bool left) {
            float s = left ? 90.0f : -90.0f;
            DrawRing(c, iR, oR, s, s + 180.0f, 32, bg);
            if (pct > 0.005f)
                DrawRing(c, iR, oR, s, s + 180.0f * pct, 32, fill);
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

    float hullPct = _playerEntity.health.currentHull / _playerEntity.health.maxStats.hull;
    Color hullCol = hullPct > 0.5f ? Color{ 48,188,68,255 }
        : hullPct > 0.25f ? Color{ 212,168,28,255 }
    : Color{ 208,42,32,255 };
    DrawStatusRing(sc, sHpIn, sHpOut, hullPct, hullCol, Color{ 22,32,22,200 });

    float ksPct = _playerEntity.health.maxStats.shield > 0.0f
        ? _playerEntity.health.currentShield / _playerEntity.health.maxStats.shield : 0.0f;
    float esPct = _playerMeta.maxEnergyShield > 0.0f
        ? _playerMeta.energyShield / _playerMeta.maxEnergyShield : 0.0f;
    DrawHalfRing(sc, sShIn, sShOut, ksPct, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
    DrawHalfRing(sc, sShIn, sShOut, esPct, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
    DrawCircleLines((int)sc.x, (int)sc.y, (int)sAreaR, Color{ 30,55,30,160 });

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
    DrawText(snc, (int)sc.x - MeasureText(snc, 11) / 2, hy + HudH - 17, 11, HudLabel);

    const float tAreaR = 26.0f;
    const float tHpIn = 28.0f, tHpOut = 35.0f;
    const float tShIn = 37.0f, tShOut = 44.0f;
    Vector2 tc = { (float)(hx + 60), (float)(hy + HudH / 2 - 6) };
    DrawCircleV(tc, tShOut + 1.0f, Color{ 6, 8, 14, 230 });

    const bool hasSensors = _hasSensors;

    if (_target.valid) {
        if (_target.isNpc) {
            float tHpPct = _target.health / _target.maxHealth;
            Color tHpCol = tHpPct > 0.5f ? Color{ 48,188,68,255 }
                : tHpPct > 0.25f ? Color{ 212,168,28,255 }
            : Color{ 208,42,32,255 };
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
            Texture2D* npcTexPtr = nullptr;
            for (size_t ni = 0; ni < _npcMeta.size(); ++ni) {
                if (_npcMeta[ni].id == _npcTargetId && _npcMeta[ni].alive) {
                    if (_npcMeta[ni].shipTypeId == "gargos")
                        npcTexPtr = const_cast<Texture2D*>(&_gargosTex);
                    else if (_entities[ni].sprite.texture && _entities[ni].sprite.texture->id > 0)
                        npcTexPtr = _entities[ni].sprite.texture;
                    else if (const auto* sd = ecs::ShipRegistry::ShipById(_npcMeta[ni].shipTypeId))
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
                int dx = (int)(tc.x + tShOut + 12), dy = hy + 12;
                DrawText(_target.name.c_str(), dx, dy, 13, HudValue); dy += 18;
                DrawText(_target.typeDesc.c_str(), dx, dy, 12, HudLabel); dy += 16;
                char db[64];
                std::snprintf(db, sizeof(db), "DIST  %.0f u", _target.distance);
                DrawText(db, dx, dy, 12, HudValue); dy += 16;
                std::snprintf(db, sizeof(db), "HP  %.0f / %.0f", _target.health, _target.maxHealth);
                DrawText(db, dx, dy, 12, HudValue); dy += 16;
                if (hasSensors && _target.hasFaction) {
                    char fb[48];
                    std::snprintf(fb, sizeof(fb), "FACTION  %s", FactionName(_target.gameFaction));
                    DrawText(fb, dx, dy, 12, Color{ 180, 210, 255, 255 }); dy += 16;
                }
                if (_target.isWingman && _target.maxKineticShield > 0.0f) {
                    std::snprintf(db, sizeof(db), "KS  %.0f / %.0f",
                        _target.kineticShield, _target.maxKineticShield);
                    DrawText(db, dx, dy, 12, Color{ 255,210,60,255 }); dy += 16;
                }
                if (_target.isWingman && _target.maxEnergyShield > 0.0f) {
                    std::snprintf(db, sizeof(db), "ES  %.0f / %.0f",
                        _target.energyShield, _target.maxEnergyShield);
                    DrawText(db, dx, dy, 12, Color{ 60,180,220,255 });
                }
            }
        }
        else if (_target.isStellar) {
            DrawStatusRing(tc, tHpIn, tHpOut, 0.0f, Color{ 48,88,188,255 }, Color{ 22,22,32,200 });
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
            DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, Color{ 30,50,100,180 });
            if (_target.iconTex && _target.iconTex->id > 0) {
                float tw = (float)_target.iconTex->width, th = (float)_target.iconTex->height;
                float sc2 = (tAreaR * 1.85f) / std::max(tw, th);
                DrawTexturePro(*_target.iconTex,
                    { 0, 0, tw, th }, { tc.x, tc.y, tw * sc2, th * sc2 },
                    { tw * sc2 * 0.5f, th * sc2 * 0.5f }, 0.0f, WHITE);
            }
            if (hasSensors) {
                int dx = (int)(tc.x + tShOut + 12), dy = hy + 12;
                DrawText(_target.name.c_str(), dx, dy, 13, HudValue); dy += 18;
                DrawText(_target.typeDesc.c_str(), dx, dy, 12, HudLabel); dy += 16;
                if (_target.hasFaction) {
                    char fb[48];
                    std::snprintf(fb, sizeof(fb), "FACTION  %s", FactionName(_target.gameFaction));
                    DrawText(fb, dx, dy, 12, Color{ 180, 210, 255, 255 });
                }
            }
        }
        else {
            float tHpPct = _target.health / _target.maxHealth;
            Color tHpCol = tHpPct > 0.5f ? Color{ 48,188,68,255 }
                : tHpPct > 0.25f ? Color{ 212,168,28,255 }
            : Color{ 208,42,32,255 };
            DrawStatusRing(tc, tHpIn, tHpOut, hasSensors ? tHpPct : 0.0f, tHpCol, Color{ 22,22,32,200 });
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
            DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
            DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, Color{ 30,30,55,180 });
            int   sides = _target.tier == 2 ? 8 : _target.tier == 1 ? 7 : 6;
            float spin = (float)(GetTime() * 22.0);
            DrawPoly(tc, sides, tAreaR * 0.65f, spin, Color{ 30,26,21,255 });
            DrawPolyLinesEx(tc, sides, tAreaR * 0.65f, spin, 1.0f, Color{ 130,115,90,255 });
            if (hasSensors) {
                int dx = (int)(tc.x + tShOut + 12), dy = hy + 12;
                DrawText(_target.name.c_str(), dx, dy, 13, HudValue); dy += 18;
                DrawText(_target.typeDesc.c_str(), dx, dy, 12, HudLabel); dy += 16;
                char db[64];
                std::snprintf(db, sizeof(db), "DIST  %.0f u", _target.distance);
                DrawText(db, dx, dy, 12, HudValue); dy += 16;
                std::snprintf(db, sizeof(db), "HP  %.0f / %.0f", _target.health, _target.maxHealth);
                DrawText(db, dx, dy, 12, HudValue); dy += 16;
                for (const auto& mc : _target.materialComps) {
                    const MatDef* mat = FindMaterial(mc.materialId);
                    char mb[48];
                    std::snprintf(mb, sizeof(mb), "%-10s %d%%",
                        mat ? mat->displayName : mc.materialId.c_str(), mc.percent);
                    DrawText(mb, dx, dy, 11, mat ? mat->hudColor : HudValue);
                    dy += 14;
                }
            }
        }
    }
    else {
        DrawStatusRing(tc, tHpIn, tHpOut, 0.0f, Color{ 48,188,68,255 }, Color{ 22,22,32,200 });
        DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 255,210,60,255 }, Color{ 62,48,14,200 }, true);
        DrawHalfRing(tc, tShIn, tShOut, 0.0f, Color{ 60,180,220,255 }, Color{ 14,34,72,200 }, false);
        DrawCircleLines((int)tc.x, (int)tc.y, (int)tAreaR, Color{ 30,30,55,110 });
        DrawText("NO", (int)tc.x - MeasureText("NO", 10) / 2, (int)tc.y - 10, 10, Color{ 80,80,100,200 });
        DrawText("TARGET", (int)tc.x - MeasureText("TARGET", 10) / 2, (int)tc.y + 2, 10, Color{ 80,80,100,200 });
    }

    bool showTargetData = hasSensors || (_target.valid && _target.isNpc && _target.isWingman);
    int weapX = (int)(tc.x + tShOut) + 10 + (showTargetData ? 168 : 12);
    int wy = hy + 10;

    DrawText("WEAPON", weapX, wy, 11, HudLabel); wy += 14;
    if (_playerMeta.canFire) {
        int barW = std::min(lDiv - weapX - 10, 140);
        if (_playerMeta.weaponFireMode == WeaponFireMode::Charge) {
            float chargePct = _playerMeta.ChargePct();
            bool  full = chargePct >= 1.0f;
            Color cFill = full ? Color{ 80,200,80,255 } : Color{ 60,150,220,255 };
            DrawRectangle(weapX, wy, barW, 10, Color{ 20,28,20,200 });
            DrawRectangle(weapX, wy, (int)(barW * chargePct), 10, cFill);
            DrawRectangleLinesEx({ (float)weapX,(float)wy,(float)barW,10.0f }, 1.0f, HudDiv);
            wy += 12;
            const char* cl = full ? "FULL CHARGE" : chargePct > 0.01f ? "CHARGING" : "HOLD TO CHARGE";
            DrawText(cl, weapX, wy, 11, full ? Color{ 80,220,80,255 } : Color{ 100,175,220,255 });
            wy += 16;
        }
        else if (_playerMeta.weaponFireMode == WeaponFireMode::LockOn) {
            bool locked = (_lockTargetId != 0);
            DrawText(locked ? "TARGET LOCKED" : "CLICK TO LOCK", weapX, wy + 2, 11,
                locked ? Color{ 220,80,80,255 } : Color{ 120,120,140,220 });
            wy += 16;
            float readyPct = 1.0f - _playerMeta.FireCooldownPct();
            DrawRectangle(weapX, wy, barW, 6, Color{ 20,28,20,200 });
            DrawRectangle(weapX, wy, (int)(barW * readyPct), 6,
                readyPct >= 1.0f ? Color{ 48,198,78,255 } : Color{ 178,138,28,255 });
            DrawRectangleLinesEx({ (float)weapX,(float)wy,(float)barW,6.0f }, 1.0f, HudDiv);
            wy += 10;
        }
        else {
            float readyPct = 1.0f - _playerMeta.FireCooldownPct();
            bool  isReady = readyPct >= 1.0f;
            Color wFill = isReady ? Color{ 48,198,78,255 } : Color{ 178,138,28,255 };
            DrawRectangle(weapX, wy, barW, 10, Color{ 20,28,20,200 });
            DrawRectangle(weapX, wy, (int)(barW * readyPct), 10, wFill);
            DrawRectangleLinesEx({ (float)weapX,(float)wy,(float)barW,10.0f }, 1.0f, HudDiv);
            wy += 12;
            DrawText(isReady ? "READY" : "CHARGING", weapX, wy, 11,
                isReady ? Color{ 75,218,75,255 } : Color{ 198,158,38,255 });
            wy += 16;
        }
    }
    else {
        DrawText("NO WEAPON", weapX, wy + 2, 11, Color{ 160,60,60,220 }); wy += 28;
    }

    DrawText("SLOTS", weapX, wy, 10, HudLabel); wy += 12;
    for (int i = 0; i < _playerMeta.weaponSlots; ++i) {
        bool isSelected = (i == _selectedWeapon);
        Rectangle slot = { (float)(weapX + i * 38), (float)wy, 32.0f, 26.0f };
        DrawRectangleRec(slot, isSelected ? Color{ 20,38,20,230 } : Color{ 14,22,14,200 });
        DrawRectangleLinesEx(slot, isSelected ? 2.0f : 1.0f,
            isSelected ? Color{ 80,200,80,255 } : HudDiv);
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

    Rectangle modBtn, stoBtn, escBtn, enterBtn, buildBtn, commsBtn, ranksBtn;
    ComputeHudButtons(sw, sh, modBtn, stoBtn, escBtn, enterBtn, buildBtn, commsBtn, ranksBtn);
    bool hovMod = CheckCollisionPointRec(mouse, modBtn);
    bool hovSto = CheckCollisionPointRec(mouse, stoBtn);
    bool nearStation = IsNearStation();
    bool nearPlanet  = nearStation || IsNearPlanet();
    bool hovEnter    = nearPlanet && CheckCollisionPointRec(mouse, enterBtn);

    // Row 1 — MODULES
    DrawRectangleRec(modBtn, hovMod ? Color{ 50,95,50,230 } : Color{ 12,22,12,200 });
    DrawRectangleLinesEx(modBtn, 1.0f, HudDiv);
    DrawText("MODULES",
        (int)(modBtn.x + (modBtn.width - MeasureText("MODULES", 10)) / 2),
        (int)(modBtn.y + 9), 10, hovMod ? WHITE : HudLabel);

    // Row 1 — STORAGE
    DrawRectangleRec(stoBtn, hovSto ? Color{ 50,95,50,230 } : Color{ 12,22,12,200 });
    DrawRectangleLinesEx(stoBtn, 1.0f, HudDiv);
    DrawText("STORAGE",
        (int)(stoBtn.x + (stoBtn.width - MeasureText("STORAGE", 10)) / 2),
        (int)(stoBtn.y + 9), 10, hovSto ? WHITE : HudLabel);

    // Row 1 — ESCORTS
    {
        int wingCount = 0;
        for (const NpcMeta& n : _npcMeta) if (n.alive && n.wingman) wingCount++;
        bool escActive = (wingCount > 0);
        bool hovEsc = escActive && CheckCollisionPointRec(mouse, escBtn);
        Color escBg = escActive ? (hovEsc ? Color{ 20,70,40,230 } : Color{ 10,28,18,200 })
            : Color{ 16,16,16,150 };
        Color escBdr = escActive ? Color{ 30,140,80,200 } : HudDiv;
        Color escFg = escActive ? (hovEsc ? WHITE : Color{ 50,180,90,255 })
            : Color{ 45,50,55,150 };
        DrawRectangleRec(escBtn, escBg);
        DrawRectangleLinesEx(escBtn, 1.0f, escBdr);
        char escLbl[64];
        std::snprintf(escLbl, sizeof(escLbl), "ESCORTS (%d)", wingCount);
        DrawText(escLbl,
            (int)(escBtn.x + (escBtn.width - MeasureText(escLbl, 10)) / 2),
            (int)(escBtn.y + 9), 10, escFg);
    }

    // Row 2 — ENTER
    {
        Color enterBg = nearPlanet ? (hovEnter ? Color{ 30,70,90,230 } : Color{ 12,25,35,200 })
            : Color{ 16,16,16,150 };
        Color enterBdr = nearPlanet ? Color{ 40,130,200,200 } : HudDiv;
        Color enterFg = nearPlanet ? (hovEnter ? WHITE : Color{ 60,160,220,255 })
            : Color{ 50,55,60,160 };
        DrawRectangleRec(enterBtn, enterBg);
        DrawRectangleLinesEx(enterBtn, 1.0f, enterBdr);
        DrawText("ENTER",
            (int)(enterBtn.x + (enterBtn.width - MeasureText("ENTER", 10)) / 2),
            (int)(enterBtn.y + 9), 10, enterFg);
    }

    // Row 2 — BUILD (blue)
    {
        bool hovBuild = CheckCollisionPointRec(mouse, buildBtn);
        DrawRectangleRec(buildBtn, hovBuild ? Color{ 20,50,110,230 } : Color{ 10,22,50,200 });
        DrawRectangleLinesEx(buildBtn, 1.0f, Color{ 40,100,200,200 });
        DrawText("BUILD",
            (int)(buildBtn.x + (buildBtn.width - MeasureText("BUILD", 10)) / 2),
            (int)(buildBtn.y + 9), 10, hovBuild ? WHITE : Color{ 80,150,230,255 });
    }

    // Row 2 — COMMS
    {
        bool npcActive = (_npcTargetId != 0);
        bool hovComms = npcActive && CheckCollisionPointRec(mouse, commsBtn);
        Color commsBg = npcActive ? (hovComms ? Color{ 20,60,80,230 } : Color{ 10,28,40,200 })
            : Color{ 16,16,16,150 };
        Color commsBdr = npcActive ? Color{ 30,100,160,200 } : HudDiv;
        Color commsFg = npcActive ? (hovComms ? WHITE : Color{ 50,140,190,255 })
            : Color{ 45,50,55,150 };
        DrawRectangleRec(commsBtn, commsBg);
        DrawRectangleLinesEx(commsBtn, 1.0f, commsBdr);
        DrawText("COMMS",
            (int)(commsBtn.x + (commsBtn.width - MeasureText("COMMS", 10)) / 2),
            (int)(commsBtn.y + 9), 10, commsFg);
    }

    // Row 3 — RANKS
    {
        bool hovRanks = CheckCollisionPointRec(mouse, ranksBtn);
        DrawRectangleRec(ranksBtn, hovRanks ? Color{ 50,95,50,230 } : Color{ 12,22,12,200 });
        DrawRectangleLinesEx(ranksBtn, 1.0f, HudDiv);
        DrawText("RANKS",
            (int)(ranksBtn.x + (ranksBtn.width - MeasureText("RANKS", 10)) / 2),
            (int)(ranksBtn.y + 9), 10, hovRanks ? WHITE : HudLabel);
    }

    (void)rDiv;

    DrawText("[ESC] MAP", hx + hw - MeasureText("[ESC] MAP", 11) - 8,
        hy + HudH - 15, 11, Color{ 55,115,55,170 });

    bool menuOpen = (_storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _enterPopupOpen || _stationPopupOpen || _localMapOpen ||
        _buildMenu.isOpen || _stationModMenu.isOpen || _placementConfirmOpen);
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
    if (_sun.active) {
        Vector2 sunMapPos = W2M({ 0.0f, 0.0f });
        float   sunMapR   = std::max(6.0f, _sun.radius * scale);
        float   gravMapR  = _sun.gravRange * scale;
        const Color& sc = _sun.coreColor;
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
            const char* slbl = _sun.typeId.c_str();
            DrawText(slbl, (int)(sunMapPos.x - MeasureText(slbl, 10) / 2),
                (int)(sunMapPos.y + sunMapR + 3), 10, { sc.r, sc.g, sc.b, 200 });
        }
    }

    for (const Asteroid& a : _asteroids) {
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
    for (const auto& a : _asteroids) {
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
    gs.nextNpcId = _nextNpcId;
    for (size_t i = 0; i < _npcMeta.size(); ++i) {
        const NpcMeta&    nm = _npcMeta[i];
        const ecs::Entity& ne = _entities[i];
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
    gs.sunTypeId = _sun.typeId;
    gs.sunRadius = _sun.radius;

    // Planets (with orbital state)
    for (const auto& p : _planets) {
        SM::PlanetSave ps;
        ps.posX        = p.position.x; ps.posY        = p.position.y;
        ps.radius      = p.radius;     ps.id          = p.id;
        ps.orbitRadius = p.orbitRadius; ps.orbitAngle  = p.orbitAngle;
        ps.orbitSpeed  = p.orbitSpeed;
        gs.planets.push_back(ps);
    }

    // Stations
    for (const auto& s : _stations) {
        SM::StationSave ss;
        ss.posX = s.position.x; ss.posY = s.position.y;
        ss.radius = s.radius;   ss.id   = s.id;
        gs.stations.push_back(ss);
    }

    // Loot drops
    for (const auto& l : _lootDrops) {
        SM::LootSave ls;
        ls.posX      = l.position.x; ls.posY      = l.position.y;
        ls.lifetime  = l.lifetime;   ls.pulseTimer = l.pulseTimer;
        ls.collected = l.collected;  ls.moduleId   = l.module.id;
        gs.lootDrops.push_back(std::move(ls));
    }

    // Material drops
    for (const auto& m : _materialDrops) {
        SM::MatDropSave ms;
        ms.posX       = m.position.x; ms.posY       = m.position.y;
        ms.lifetime   = m.lifetime;   ms.pulseTimer  = m.pulseTimer;
        ms.collected  = m.collected;  ms.materialId  = m.materialId;
        gs.matDrops.push_back(std::move(ms));
    }

    gs.discoveredIds        = _discoveredIds;
    gs.currentSystemId      = _currentSystemId;
    gs.discoveredSystemIds  = _discoveredSystemIds;
    gs.hasWorldState = true;
    return gs;
}

void SpaceFlight::ApplyWorldState(const SaveManager::GameState& gs) {
    // Asteroids
    _asteroids.clear();
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
        _asteroids.push_back(std::move(a));
    }

    // NPCs
    _entities.clear();
    _npcMeta.clear();
    _npcFreeSlots.clear();
    _nextNpcId = gs.nextNpcId;
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
        _entities.push_back(std::move(ne));
        _npcMeta.push_back(std::move(nm));
    }

    // Planets (restore orbital state)
    _planets.clear();
    for (const auto& ps : gs.planets) {
        SpacePlanet p;
        p.position    = { ps.posX, ps.posY };
        p.radius      = ps.radius;
        p.id          = ps.id;
        p.orbitRadius = ps.orbitRadius;
        p.orbitAngle  = ps.orbitAngle;
        p.orbitSpeed  = ps.orbitSpeed;
        _planets.push_back(p);
    }

    // Rebuild sun physics from saved data
    if (!gs.sunTypeId.empty()) {
        const StarTypeDef* def = StarRegistry::ById(gs.sunTypeId);
        if (def && _sun.active) {
            float savedR     = (gs.sunRadius > 0.f) ? gs.sunRadius
                                                     : (def->minRadius + def->maxRadius) * 0.5f;
            _sun.radius      = savedR;
            _sun.gravRange   = savedR * def->gravRangeMult;
            _sun.gravStrength= def->gravStrength;
        }
    }

    // Stations
    _stations.clear();
    for (const auto& ss : gs.stations) {
        SpaceStation st;
        st.position = { ss.posX, ss.posY };
        st.radius   = ss.radius;
        st.id       = ss.id;
        st.faction  = static_cast<Faction>(ss.id % static_cast<int>(Faction::COUNT));
        const auto& stTypes = StationTypeRegistry::All();
        const StationTypeDef& typeDef = stTypes[st.id % stTypes.size()];
        st.stationTypeId = typeDef.id;
        BuildNpcStationHardpoints(st, typeDef);
        _stations.push_back(std::move(st));
    }

    // Loot drops
    _lootDrops.clear();
    for (const auto& ls : gs.lootDrops) {
        auto mod = ModuleById(ls.moduleId);
        if (!mod) continue;
        LootDrop l;
        l.position  = { ls.posX, ls.posY };
        l.lifetime  = ls.lifetime;
        l.pulseTimer= ls.pulseTimer;
        l.collected = ls.collected;
        l.module    = *mod;
        _lootDrops.push_back(std::move(l));
    }

    // Material drops
    _materialDrops.clear();
    for (const auto& ms : gs.matDrops) {
        MaterialDrop m;
        m.position  = { ms.posX, ms.posY };
        m.lifetime  = ms.lifetime;
        m.pulseTimer= ms.pulseTimer;
        m.collected = ms.collected;
        m.materialId= ms.materialId;
        _materialDrops.push_back(std::move(m));
    }

    // Clear transient targeting/UI state that no longer maps to saved world
    _target    = TargetInfo{};
    _targetId  = 0;
    _npcTargetId = 0;
}

void SpaceFlight::OnEnter() {
    ModuleRegistry::Init();

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

    _projectiles.clear();
    _asteroids.clear();
    _hitCooldown = 0.0f;
    _target = TargetInfo{};
    _targetId = 0;
    _localMapOpen = false;

    _selectedWeapon = 0;
    _enterPopupOpen      = false;
    _stationPopupOpen    = false;
    _inPlacementMode     = false;
    _placementConfirmOpen= false;
    _placingStationDefId.clear();
    _npcTargetId = 0;
    _respawnTimer = 20.0f;
    _entities.clear();
    _npcMeta.clear();
    _lootDrops.clear();
    _materialDrops.clear();
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
        PutInStorage(0, Weapon_Chaingun());
        PutInStorage(1, Shield_KineticBarrier_I());
        PutInStorage(2, Hyperdrive_ShortJump());
    }

    if (_planetBaseTex.id > 0)     { UnloadTexture(_planetBaseTex);     _planetBaseTex     = {}; }
    if (_stationBaseTex.id > 0)   { UnloadTexture(_stationBaseTex);   _stationBaseTex   = {}; }
    if (_gargosTex.id > 0)        { UnloadTexture(_gargosTex);        _gargosTex        = {}; }
    if (_sunTex.id > 0)           { UnloadTexture(_sunTex);           _sunTex           = {}; }
    if (_asteroidTexLarge.id > 0) { UnloadTexture(_asteroidTexLarge); _asteroidTexLarge = {}; }
    if (_asteroidTexMedium.id > 0){ UnloadTexture(_asteroidTexMedium);_asteroidTexMedium= {}; }
    if (_asteroidTexSmall.id > 0) { UnloadTexture(_asteroidTexSmall); _asteroidTexSmall = {}; }
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

    // ── World entities: restore or spawn fresh ────────────────────────────────
    _sun = SpaceSun{};
    if (didLoad && gs.hasWorldState) {
        ApplyWorldState(gs);
        // Re-derive sun visuals/physics from saved data
        if (!gs.sunTypeId.empty()) {
            const StarTypeDef* def = StarRegistry::ById(gs.sunTypeId);
            if (def) {
                float savedR     = (gs.sunRadius > 0.f) ? gs.sunRadius
                                                         : (def->minRadius + def->maxRadius) * 0.5f;
                _sun.typeId      = def->id;
                _sun.radius      = savedR;
                _sun.gravRange   = savedR * def->gravRangeMult;
                _sun.gravStrength= def->gravStrength;
                _sun.coreColor   = def->coreColor;
                _sun.innerGlow   = def->innerGlowColor;
                _sun.outerGlow   = def->outerGlowColor;
                _sun.active      = true;
                BakeSunCorona();
            }
        }
    }
    else if (net::Game().IsHost()) {
        // Host generates a deterministic seed and broadcasts it to joining clients.
        _worldSeed   = (uint32_t)GetRandomValue(100000, 999999);
        _worldSynced = true;
        _playerEntity.id                 = 1;   // non-zero so HostBroadcast includes it
        _playerEntity.network.networkId  = 1;
        _playerEntity.network.isLocalPlayer = true;
        SpawnPlanetsAndStations(_worldSeed);
        SpawnInitialAsteroids();
        SpawnNpcShips();
        _playerEntity.transform.position = _playerSpawnPos;
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
        SpawnPlanetsAndStations();  // offline — sets _sun and _playerSpawnPos
        SpawnInitialAsteroids();
        SpawnNpcShips();
        _playerEntity.transform.position = _playerSpawnPos;
        _camera.target = _playerEntity.transform.position;
    }
    PrewarmSpriteCache();
    InitStars();
    _lighting.Init();
}

void SpaceFlight::Update(float dt) {
    net::Game().Poll(dt);   // pump ENet every frame (no-op when Offline)

    // ── Host: send WorldSync to newly joined peers ─────────────────────────────
    if (net::Game().IsHost()) {
        for (uint32_t peerId : net::Game().newPeerIds)
            net::Game().HostSendWorldSync(peerId, _currentSystemId, _worldSeed);
        net::Game().newPeerIds.clear();

        // Apply client input commands → update _remoteEntities and spawn server-side projectiles.
        for (auto& [id, fc] : _remoteFireCooldown) fc -= dt;
        for (auto& [id, g]  : _remoteJoinGrace)    g  -= dt;

        for (const auto& cmd : net::Game().pendingInputs) {
            if (cmd.networkId == 0) continue;
            auto& re = _remoteEntities[cmd.networkId];
            if (re.id == 0) {
                re.id                   = cmd.networkId;
                re.sprite.texture       = _playerShipTex;
                re.sprite.scale         = 1.0f;
                re.sprite.tint          = { 100, 200, 255, 255 };
                re.health.currentHull   = 100.0f;
                re.health.maxStats.hull = 100.0f;
                re.transform.position   = { cmd.posX, cmd.posY };
                _remoteJoinGrace[cmd.networkId] = 5.0f;  // invincible for 5 s at spawn
            }
            re.transform.position = { cmd.posX, cmd.posY };
            re.transform.rotation = cmd.aimRotation;
            re.network.networkId  = cmd.networkId;

            // Spawn a server-side projectile when the client fires.
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
                _projectiles.push_back(p);
            }
        }
        net::Game().pendingInputs.clear();

        // Broadcast snapshots at ~20 Hz — alive NPCs, host player, alive asteroids.
        _netTickAccum += dt;
        if (_netTickAccum >= 0.05f) {
            _netTickAccum = 0.0f;

            std::vector<ecs::Entity> broadcastList;
            broadcastList.reserve(_entities.size() + 1 + _remoteEntities.size());
            for (size_t i = 0; i < _entities.size(); ++i) {
                if (_npcMeta[i].alive)
                    broadcastList.push_back(_entities[i]);
            }
            if (!_playerDead) {
                ecs::Entity pCopy = _playerEntity;
                pCopy.network.isLocalPlayer = false;
                broadcastList.push_back(pCopy);
            }
            // Include remote client entities so all clients can see each other.
            for (const auto& [netId, re] : _remoteEntities) {
                if (re.id != 0) broadcastList.push_back(re);
            }

            std::vector<net::AsteroidSnapshot> asteroidSnaps;
            asteroidSnaps.reserve(_asteroids.size());
            for (const auto& a : _asteroids) {
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
            projSnaps.reserve(_projectiles.size());
            for (const Projectile& p : _projectiles) {
                if (!p.alive) continue;
                net::ProjectileSnapshot ps;
                ps.posX = p.position.x;
                ps.posY = p.position.y;
                ps.velX = p.velocity.x;
                ps.velY = p.velocity.y;
                projSnaps.push_back(ps);
            }

            net::Game().HostBroadcast(broadcastList, asteroidSnaps, projSnaps);
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
        for (uint32_t deadId : net::Game().pendingStationDeadIds) {
            for (auto& st : _stations)
                if (st.id == deadId) { st.alive = false; break; }
        }
        net::Game().pendingStationDeadIds.clear();
    }

    // ── Client: apply WorldSync then lerp remote snapshots ────────────────────
    if (net::Game().IsClient()) {
        if (net::Game().pendingWorldSync.has_value()) {
            auto ws = *net::Game().pendingWorldSync;
            net::Game().pendingWorldSync.reset();
            _currentSystemId = ws.systemId;
            _worldSeed       = ws.worldSeed;
            SpawnPlanetsAndStations(_worldSeed);
            SpawnInitialAsteroids();
            // NPC positions come from server snapshots; don't simulate them locally.
            // Give the client's player a slightly offset spawn so players don't overlap.
            _playerSpawnPos.x += 200.0f;
            _playerEntity.transform.position = _playerSpawnPos;
            _camera.target = _playerSpawnPos;
            _worldSynced = true;
        }

        // On each new snapshot: correct entity positions, sync asteroids, evict stale.
        uint32_t localId = net::Game().LocalNetworkId();
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
                        re.sprite.texture = _playerShipTex;
                        re.sprite.tint    = { 100, 200, 255, 255 };
                    } else {
                        // NPC — no client-side ship data; draw as red circle.
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
                for (auto& a : _asteroids) { if (a.id == as.id) { found = &a; break; } }
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
                    _asteroids.push_back(na);
                }
            }
            // Kill asteroids absent from this snapshot (destroyed on server).
            for (auto& a : _asteroids) {
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
        addDebugMaterial("hull_frame", "Hull Frame", 15);
        addDebugMaterial("circuit_board", "Circuit Board", 8);
        addDebugMaterial("power_cell", "Power Cell", 5);

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
                ApplyLoadout();
                _playerEntity.health.currentHull = _playerEntity.health.maxStats.hull;
                _playerEntity.transform.position = _playerSpawnPos;
                _playerEntity.transform.velocity = { 0.0f, 0.0f };
                _camera.target = _playerSpawnPos;
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
        _buildMenu.isOpen || _stationModMenu.isOpen || _placementConfirmOpen) {
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
        else if (_stationPopupOpen) {
            _stationPopupOpen = false;
        }
        else if (_buildMenu.isOpen) {
            _buildMenu.Close();
        }
        else if (_stationModMenu.isOpen) {
            _stationModMenu.Close();
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
            mapData.hyperdriveRange = _hyperdriveRange;
            mapData.currentSystemId = _currentSystemId;
            const StarSystem* curSys = StarSystemRegistry::ById(_currentSystemId);
            mapData.currentSystemPos = curSys ? curSys->galacticPos : Vector2{};
            for (const StarSystem& sys : StarSystemRegistry::All()) {
                bool disc = std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), sys.id)
                            != _discoveredSystemIds.end();
                mapData.systems.push_back({ sys.id, sys.name, sys.galacticPos, disc, sys.id == _currentSystemId });
            }
            _galacticMap.SetMapData(mapData);
        }

        GalacticMapAction gAction = _galacticMap.Update(dt);

        if (gAction == GalacticMapAction::WarpToSystem) {
            unsigned int targetId = _galacticMap.WarpTargetId();
            const StarSystem* sys = StarSystemRegistry::ById(targetId);
            if (sys) {
                _currentSystemId = targetId;
                if (std::find(_discoveredSystemIds.begin(), _discoveredSystemIds.end(), targetId)
                        == _discoveredSystemIds.end())
                    _discoveredSystemIds.push_back(targetId);
                _discoveredIds.clear();
                _projectiles.clear();
                _asteroids.clear();
                _entities.clear();
                _npcMeta.clear();
                _lootDrops.clear();
                _materialDrops.clear();
                _playerEntity.transform.velocity = { 0.0f, 0.0f };
                _sun = SpaceSun{};
                SpawnPlanetsAndStations(sys->seed);  // sets _sun and _playerSpawnPos
                SpawnInitialAsteroids();
                SpawnNpcShips();
                _playerEntity.transform.position = _playerSpawnPos;
                _camera.target = _playerEntity.transform.position;
            }
            _galacticMap.Close();
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
            for (const SpacePlanet& p : _planets) {
                bool disc = std::find(_discoveredIds.begin(), _discoveredIds.end(), p.id) != _discoveredIds.end();
                mapData.blips.push_back({ p.id, p.position, p.radius, true, disc });
            }
            for (const SpaceStation& s : _stations) {
                if (!s.alive) continue;
                bool disc = std::find(_discoveredIds.begin(), _discoveredIds.end(), s.id) != _discoveredIds.end();
                mapData.blips.push_back({ s.id, s.position, s.radius, false, disc });
            }
            _systemMap.SetMapData(mapData);
        }

        MapAction action = _systemMap.Update(dt);

        if (action == MapAction::WarpTo) {
            _playerEntity.transform.position = _systemMap.WarpTarget();
            _playerEntity.transform.velocity = { 0.0f, 0.0f };
            _camera.target = _playerEntity.transform.position;
            _systemMap.Close();
            return;
        }
        if (action == MapAction::OpenGalacticMap) {
            _systemMap.Close();
            _galacticMap.Open();
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
                _projectiles.clear();
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
                _sun = SpaceSun{};
                if (gs.hasWorldState) {
                    ApplyWorldState(gs);
                    if (!gs.sunTypeId.empty()) {
                        const StarTypeDef* def = StarRegistry::ById(gs.sunTypeId);
                        if (def) {
                            float savedR     = (gs.sunRadius > 0.f) ? gs.sunRadius
                                                                     : (def->minRadius + def->maxRadius) * 0.5f;
                            _sun.typeId      = def->id;
                            _sun.radius      = savedR;
                            _sun.gravRange   = savedR * def->gravRangeMult;
                            _sun.gravStrength= def->gravStrength;
                            _sun.coreColor   = def->coreColor;
                            _sun.innerGlow   = def->innerGlowColor;
                            _sun.outerGlow   = def->outerGlowColor;
                            _sun.active      = true;
                            BakeSunCorona();
                        }
                    }
                }
                else {
                    _asteroids.clear();
                    _entities.clear();
                    _npcMeta.clear();
                    _lootDrops.clear();
                    _materialDrops.clear();
                    SpawnPlanetsAndStations();
                    SpawnInitialAsteroids();
                    SpawnNpcShips();
                    _playerEntity.transform.position = _playerSpawnPos;
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

    if (_stationPopupOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        static constexpr int PopH = 130;
        int py2 = sh2 / 2 - PopH / 2;
        Rectangle okBtn = { (float)(sw2 / 2 - 60), (float)(py2 + PopH - 46), 120.0f, 32.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), okBtn))
            _stationPopupOpen = false;
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
                auto [alliedE, alliedM] = MakeNpcEntity(_nextNpcId++, _shipPlacementPos);
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
                if (!_npcFreeSlots.empty()) {
                    size_t slot = _npcFreeSlots.back(); _npcFreeSlots.pop_back();
                    _entities[slot] = std::move(alliedE); _npcMeta[slot] = std::move(alliedM);
                } else {
                    _entities.push_back(std::move(alliedE)); _npcMeta.push_back(std::move(alliedM));
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
                _stationModMenuId = ps.id;
                _stationModMenu.Open(&ps, &_storageMenu.slots);
                return;
            }
        }
    }

    if (_commsMenuOpen) {
        bool npcAlive = false;
        for (const NpcMeta& n : _npcMeta)
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
                for (size_t ci = 0; ci < _npcMeta.size(); ++ci) {
                    NpcMeta& npc = _npcMeta[ci];
                    if (npc.id != _commsMenuNpcId || !npc.alive) continue;
                    bool isFriendly = (npc.faction == NpcFaction::Friendly);
                    bool isNeutral  = (npc.faction == NpcFaction::Neutral);
                    int acceptChance = isFriendly ? 75 : isNeutral ? 50 : 25;
                    bool accepted = (roll < acceptChance);
                    if (accepted) {
                        int wingCount = 0;
                        for (const NpcMeta& w : _npcMeta)
                            if (w.alive && w.wingman) wingCount++;
                        if (wingCount >= 4) {
                            _commsMenuNpcText = "Wing is full. Dismiss an escort first.";
                        }
                        else {
                            bool usedSlots[4] = {};
                            for (const NpcMeta& w : _npcMeta)
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
                for (size_t ci = 0; ci < _npcMeta.size(); ++ci)
                    if (_npcMeta[ci].id == _escortModuleNpcId) {
                        ApplyNpcLoadout(_entities[ci], _npcMeta[ci]);
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
        for (size_t ci = 0; ci < _npcMeta.size(); ++ci)
            if (_npcMeta[ci].alive && _npcMeta[ci].wingman) wingmanIdxs.push_back(ci);
        if (wingmanIdxs.empty()) { _escortMenuOpen = false; return; }

        bool selValid = false;
        for (size_t ci : wingmanIdxs)
            if (_npcMeta[ci].id == _escortMenuSelId) { selValid = true; break; }
        if (!selValid) _escortMenuSelId = _npcMeta[wingmanIdxs[0]].id;

        size_t selIdx = SIZE_MAX;
        for (size_t ci : wingmanIdxs)
            if (_npcMeta[ci].id == _escortMenuSelId) { selIdx = ci; break; }
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
                _escortMenuSelId = _npcMeta[wingmanIdxs[i]].id;
                selIdx = wingmanIdxs[i];
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Rectangle editBtn = { 30.0f, 260.0f, 180.0f, 38.0f };
            Rectangle dismissBtn = { 230.0f, 260.0f, 180.0f, 38.0f };
            if (CheckCollisionPointRec(m2, editBtn)) {
                _escortModuleNpcId = _npcMeta[selIdx].id;
                _modulesMenu.Open(&_npcMeta[selIdx].loadout, &_storageMenu.slots,
                    NpcMeta::WSlots, NpcMeta::ArSlots,
                    NpcMeta::ShSlots, NpcMeta::EnSlots, 0);
            }
            else if (CheckCollisionPointRec(m2, dismissBtn)) {
                _npcMeta[selIdx].wingman     = false;
                _npcMeta[selIdx].wingmanSlot = -1;
                _npcMeta[selIdx].aiState     = NpcAiState::Patrol;
                _npcMeta[selIdx].waypointSet = false;
                _escortMenuSelId = 0;
                bool anyLeft = false;
                for (const NpcMeta& nn : _npcMeta)
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
    int hy = GetScreenHeight() - 158 - 6;

    bool clickedHudBtn = _storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen || _ranksMenuOpen || (mousePos.y >= hy);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Rectangle modBtn, stoBtn, escBtn, enterBtn, buildBtn, commsBtn, ranksBtn;
        ComputeHudButtons(GetScreenWidth(), GetScreenHeight(), modBtn, stoBtn, escBtn, enterBtn, buildBtn, commsBtn, ranksBtn);
        Vector2 m = GetMousePosition();
        if (CheckCollisionPointRec(m, modBtn)) {
            _modulesMenu.Open(&_loadout, &_storageMenu.slots,
                _playerMeta.weaponSlots, _playerMeta.armorSlots,
                _playerMeta.shieldSlots, _playerMeta.engineSlots,
                _playerMeta.hyperdriveSlots, _playerMeta.auxSlots);
            clickedHudBtn = true;
        }
        else if (CheckCollisionPointRec(m, stoBtn)) {
            _storageMenu.Open((int)_storageMenu.slots.size());
            clickedHudBtn = true;
        }
        else if (CheckCollisionPointRec(m, escBtn)) {
            int wingCount = 0;
            for (const NpcMeta& n : _npcMeta) if (n.alive && n.wingman) wingCount++;
            if (wingCount > 0) {
                _escortMenuOpen = true;
                _escortMenuSelId = 0;
                for (const NpcMeta& n : _npcMeta)
                    if (n.alive && n.wingman) { _escortMenuSelId = n.id; break; }
            }
            clickedHudBtn = true;
        }
        else if ((IsNearStation() || IsNearPlanet()) && CheckCollisionPointRec(m, enterBtn)) {
            if (IsNearStation()) _stationPopupOpen = true;
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
            for (const NpcMeta& npc : _npcMeta) {
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
        else if (CheckCollisionPointRec(m, ranksBtn)) {
            _ranksMenuOpen = true;
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
            for (size_t li = 0; li < _npcMeta.size(); ++li) {
                const NpcMeta& n = _npcMeta[li];
                if (!n.alive || n.wingman) continue;
                if (Vector2Distance(mouseWorld, _entities[li].transform.position) < n.radius + 8.0f) {
                    _lockTargetId = n.id; break;
                }
            }
            // Then asteroids
            if (_lockTargetId == 0) {
                for (const Asteroid& a : _asteroids) {
                    if (a.alive && Vector2Distance(mouseWorld, a.position) < a.radius) {
                        _lockTargetId = a.id; break;
                    }
                }
            }
        }
        if (_lockTargetId != 0) {
            bool found = false;
            for (size_t li = 0; li < _npcMeta.size(); ++li)
                if (_npcMeta[li].id == _lockTargetId && _npcMeta[li].alive) { _lockTargetPos = _entities[li].transform.position; found = true; break; }
            if (!found)
                for (const Asteroid& a : _asteroids)
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
                        _projectiles.push_back(p);
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
                            _projectiles.push_back(p);
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
                        _projectiles.push_back(p);
                        _playerMeta._fireCooldown = _playerMeta.fireRate;
                    }
                }
                break;
            }
            default: break;
            }
        }
    }

    for (Projectile& p : _projectiles) {
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.lifetime += dt;
        if (p.lifetime >= p.maxLife) p.alive = false;
    }

    for (Projectile& p : _projectiles) {
        if (!p.alive || !p.isHoming) continue;
        Vector2 tPos = {};
        bool    found = false;
        if (p.targetIsPlayer) {
            tPos = _playerEntity.transform.position; found = true;
        }
        else if (p.targetId != 0) {
            for (const Asteroid& a : _asteroids)
                if (a.id == p.targetId && a.alive) { tPos = a.position; found = true; break; }
            if (!found)
                for (size_t li = 0; li < _npcMeta.size(); ++li)
                    if (_npcMeta[li].id == p.targetId && _npcMeta[li].alive) { tPos = _entities[li].transform.position; found = true; break; }
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

    for (Asteroid& a : _asteroids) {
        a.position.x += a.velocity.x * dt;
        a.position.y += a.velocity.y * dt;
        a.rotation   += a.rotSpeed   * dt;
    }

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

        // ── ADDED: Player Station Autonomous Firing ─────────────────────────
        // (This MUST be inside the ps loop so we know which station is firing)

        // 1. Find the closest living hostile to the station
        float closestDist = FLT_MAX;
        Vector2 targetPos = { 0, 0 };
        unsigned int targetId = 0;

        for (size_t li = 0; li < _npcMeta.size(); ++li) {
            const NpcMeta& npc = _npcMeta[li];
            if (!npc.alive || npc.faction != NpcFaction::Hostile) continue;
            float d = Vector2Distance(ps.position, _entities[li].transform.position);
            if (d < closestDist) {
                closestDist = d;
                targetPos = _entities[li].transform.position;
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

                        _projectiles.push_back(p);
                    }

                    // Reset the hardpoint's cooldown (incorporate charge time so it doesn't rapid-fire)
                    hp.fireCooldown = ws.fireRate + ws.chargeTime;
                }
            }
        }
    }

    // In-world station firing (per-hardpoint)
    for (SpaceStation& st : _stations) {
        if (!st.alive) continue;
        if (st.retaliateTimer > 0.0f) st.retaliateTimer -= dt;

        // Compute max weapon range across all armed hardpoints
        float maxRange = 500.0f;
        for (const HardpointState& hp : st.hardpoints)
            if (!hp.weapons.empty() && hp.weapons[0].has_value())
                maxRange = std::max(maxRange, hp.weapons[0]->weapon.projRange);

        // Find closest valid target within range
        Vector2      fireTarget   = {};
        bool         hasFireTarget = false;
        float        bestDist     = maxRange;
        unsigned int fireTargetId = 0;

        for (size_t j = 0; j < _npcMeta.size(); ++j) {
            if (!_npcMeta[j].alive) continue;
            if (DiplomaticRegistry::Get(st.faction, _npcMeta[j].npcFaction) != Relation::Hostile) continue;
            float d = Vector2Distance(st.position, _entities[j].transform.position);
            if (d < bestDist) {
                bestDist = d; fireTarget = _entities[j].transform.position;
                hasFireTarget = true; fireTargetId = _npcMeta[j].id;
            }
        }
        if (DiplomaticRegistry::Get(st.faction, kPlayerFaction) == Relation::Hostile) {
            float d = Vector2Distance(st.position, _playerEntity.transform.position);
            if (d < bestDist) {
                bestDist = d; fireTarget = _playerEntity.transform.position;
                hasFireTarget = true; fireTargetId = 0;
            }
        }
        if (st.retaliating && st.retaliateTimer > 0.0f) {
            if (st.retaliateAtPlayer) {
                float d = Vector2Distance(st.position, _playerEntity.transform.position);
                if (!hasFireTarget || d < bestDist) {
                    bestDist = d; fireTarget = _playerEntity.transform.position;
                    hasFireTarget = true; fireTargetId = 0;
                }
            } else if (st.retaliateAtNpcId != 0) {
                for (size_t j = 0; j < _npcMeta.size(); ++j) {
                    if (_npcMeta[j].id == st.retaliateAtNpcId && _npcMeta[j].alive) {
                        float d = Vector2Distance(st.position, _entities[j].transform.position);
                        if (!hasFireTarget || d < bestDist) {
                            bestDist = d; fireTarget = _entities[j].transform.position;
                            hasFireTarget = true; fireTargetId = _npcMeta[j].id;
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
                    sp.isHoming = true; sp.turnRate = 3.0f; sp.targetId = fireTargetId;
                }
                _projectiles.push_back(sp);
            }
            hp.fireCooldown = ws.fireRate + ws.chargeTime;
        }
    }

    if (!net::Game().IsClient()) {
        UpdateCollisions();
        UpdateNpcCollisions();
        UpdateCollisions();
        UpdateNpcCollisions();
    }

    // Discovery: mark stellar objects the player has flown near
    static constexpr float DiscoveryRange = 400.0f;
    for (const SpacePlanet& p : _planets) {
        if (Vector2Distance(_playerEntity.transform.position, p.position) < p.radius + DiscoveryRange) {
            if (std::find(_discoveredIds.begin(), _discoveredIds.end(), p.id) == _discoveredIds.end())
                _discoveredIds.push_back(p.id);
        }
    }
    for (const SpaceStation& s : _stations) {
        if (!s.alive) continue;
        if (Vector2Distance(_playerEntity.transform.position, s.position) < s.radius + DiscoveryRange) {
            if (std::find(_discoveredIds.begin(), _discoveredIds.end(), s.id) == _discoveredIds.end())
                _discoveredIds.push_back(s.id);
        }
    }

    for (Asteroid& a : _asteroids)
        if (a.alive && Vector2Distance(_playerEntity.transform.position, a.position) > 2800.0f)
            a.alive = false;
    for (size_t ci = 0; ci < _npcMeta.size(); ++ci)
        if (_npcMeta[ci].alive && !_npcMeta[ci].wingman &&
            Vector2Distance(_playerEntity.transform.position, _entities[ci].transform.position) > 3000.0f) {
            _npcMeta[ci].alive = false;
            _npcFreeSlots.push_back(ci);
        }

    auto isDead = [](const auto& e) { return !e.alive; };
    _projectiles.erase(std::remove_if(_projectiles.begin(), _projectiles.end(), isDead), _projectiles.end());
    _asteroids.erase(std::remove_if(_asteroids.begin(), _asteroids.end(), isDead), _asteroids.end());
    // NPC slots are NOT erased — dead slots are held in _npcFreeSlots for reuse.

    UpdateTarget();

    static constexpr int MaxAsteroids = 40;
    static constexpr int MaxNpcShips = 20;
    _respawnTimer -= dt;
    if (_respawnTimer <= 0.0f) {
        _respawnTimer = 5.0f;

        // Compute the half-diagonal of the visible world area so spawns never
        // appear inside the camera frustum regardless of zoom level.
        float halfW     = (float)GetScreenWidth()  / (2.0f * _cameraZoom);
        float halfH     = (float)GetScreenHeight() / (2.0f * _cameraZoom);
        float viewEdge  = sqrtf(halfW * halfW + halfH * halfH) + 150.0f;

        int liveAsteroids = (int)std::count_if(_asteroids.begin(), _asteroids.end(),
            [](const Asteroid& a) { return a.alive; });
        for (int s = 0; s < 2 && liveAsteroids < MaxAsteroids; ++s, ++liveAsteroids) {
            float ang     = (float)GetRandomValue(0, 359) * DEG2RAD;
            float minDist = std::max(1100.0f, viewEdge);
            float dist    = minDist + (float)GetRandomValue(0, 800);
            Vector2 pos = { _playerEntity.transform.position.x + cosf(ang) * dist,
                            _playerEntity.transform.position.y + sinf(ang) * dist };
            Asteroid ra = MakeAsteroid(pos, GetRandomValue(0, 2));
            AssignAsteroidMaterials(ra);
            _asteroids.push_back(std::move(ra));
        }
        int liveNpcs = (int)std::count_if(_npcMeta.begin(), _npcMeta.end(),
            [](const NpcMeta& m) { return m.alive; });
        for (int s = 0; s < 2 && liveNpcs < MaxNpcShips; ++s, ++liveNpcs) {
            float ang     = (float)GetRandomValue(0, 359) * DEG2RAD;
            float minDist = std::max(1200.0f, viewEdge);
            float dist    = minDist + (float)GetRandomValue(0, 800);
            Vector2 pos = { _playerEntity.transform.position.x + cosf(ang) * dist,
                            _playerEntity.transform.position.y + sinf(ang) * dist };
            auto [ne, nm] = MakeNpcEntity(_nextNpcId++, pos);
            ApplyNpcLoadout(ne, nm);
            nm.preferredRange = nm.attackRange * 0.75f;
            ne.health.currentHull = ne.health.maxStats.hull;
            if (!_npcFreeSlots.empty()) {
                size_t slot = _npcFreeSlots.back(); _npcFreeSlots.pop_back();
                _entities[slot] = std::move(ne); _npcMeta[slot] = std::move(nm);
            } else {
                _entities.push_back(std::move(ne)); _npcMeta.push_back(std::move(nm));
            }
        }
    }

    bool anyMenuOpen = _storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen ||
                       _galacticMap.isOpen || _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen ||
                       _enterPopupOpen || _stationPopupOpen || _localMapOpen ||
                       _buildMenu.isOpen || _stationModMenu.isOpen || _placementConfirmOpen;
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

void SpaceFlight::Draw() {
    bool menuOpen = (_storageMenu.isOpen || _modulesMenu.isOpen || _systemMap.isOpen || _galacticMap.isOpen ||
        _escortMenuOpen || _commsMenuOpen || _ranksMenuOpen || _enterPopupOpen || _stationPopupOpen || _localMapOpen ||
        _buildMenu.isOpen || _stationModMenu.isOpen || _placementConfirmOpen);
    Vector2 mouse = GetMousePosition();
    int hy = GetScreenHeight() - 158 - 6;

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

    for (const Asteroid& a : _asteroids) {
        if (!a.alive) continue;
        const Texture2D* atex = a.tier == 2 ? &_asteroidTexLarge
                              : a.tier == 1 ? &_asteroidTexMedium
                              :               &_asteroidTexSmall;
        DrawAsteroid(a, atex);
    }

    DrawNpcShips();
    DrawRemotePlayers();

    for (const Projectile& p : _projectiles) {
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
    // Draw server-authoritative projectiles on client (host renders its own _projectiles above).
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
            float     lightRange = _sun.active ? _sun.gravRange * 5.0f : 0.0f;
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

    EndMode2D();

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
        for (size_t di = 0; di < _npcMeta.size(); ++di)
            if (_npcMeta[di].alive && _npcMeta[di].wingman) wingmenIdxs.push_back(di);
        if (wingmenIdxs.empty()) return;

        size_t selIdx = wingmenIdxs[0];
        for (size_t di : wingmenIdxs)
            if (_npcMeta[di].id == _escortMenuSelId) { selIdx = di; break; }
        const NpcMeta* sel = &_npcMeta[selIdx];

        static constexpr int ICON_W = 150, ICON_H = 50, ICON_GAP = 16;
        int totalIconW = (int)wingmenIdxs.size() * ICON_W + ((int)wingmenIdxs.size() - 1) * ICON_GAP;
        int iconStartX = sw2 / 2 - totalIconW / 2;
        for (int i = 0; i < (int)wingmenIdxs.size(); ++i) {
            Rectangle ir = { (float)(iconStartX + i * (ICON_W + ICON_GAP)), 70.0f,
                              (float)ICON_W, (float)ICON_H };
            bool selThis = (_npcMeta[wingmenIdxs[i]].id == _escortMenuSelId);
            bool hovThis = CheckCollisionPointRec(m2, ir);
            DrawRectangleRec(ir, selThis ? Color{ 20,55,20,240 } : (hovThis ? Color{ 18,42,18,220 } : Color{ 10,20,10,200 }));
            DrawRectangleLinesEx(ir, selThis ? 2.0f : 1.0f,
                selThis ? Color{ 80,200,80,255 } : Color{ 34,98,34,160 });
            std::string iconLblS = _npcMeta[wingmenIdxs[i]].shipTypeName + "  #" + std::to_string(_npcMeta[wingmenIdxs[i]].id);
            const char* iconLbl = iconLblS.c_str();
            DrawText(iconLbl, (int)(ir.x + (ir.width - MeasureText(iconLbl, 12)) / 2),
                (int)(ir.y + 8), 12, selThis ? WHITE : Color{ 100,180,100,220 });
            float hp = _entities[wingmenIdxs[i]].health.currentHull / _entities[wingmenIdxs[i]].health.maxStats.hull;
            int bx = (int)ir.x + 8, by = (int)(ir.y + 30), bw = ICON_W - 16;
            DrawRectangle(bx, by, bw, 5, Color{ 20,32,20,200 });
            DrawRectangle(bx, by, (int)(bw * hp), 5,
                hp > 0.5f ? Color{ 60,200,60,220 } : hp > 0.25f ? Color{ 200,160,30,220 } : Color{ 200,50,30,220 });
        }

        DrawRectangle(30, 130, sw2 - 60, 1, Color{ 34,98,34,160 });

        std::string selLblS = "ESCORT: " + sel->shipTypeName + "  #" + std::to_string(sel->id);
        DrawText(selLblS.c_str(), 30, 142, 14, Color{ 68,162,68,255 });
        float selHull    = _entities[selIdx].health.currentHull;
        float selMaxHull = _entities[selIdx].health.maxStats.hull;
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

    if (_stationPopupOpen) {
        int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
        DrawRectangle(0, 0, sw2, sh2, Color{ 0, 0, 0, 160 });
        static constexpr int PopW = 380, PopH = 130;
        int px2 = sw2 / 2 - PopW / 2, py2 = sh2 / 2 - PopH / 2;
        DrawRectangle(px2, py2, PopW, PopH, Color{ 8, 18, 30, 240 });
        DrawRectangleLinesEx({ (float)px2, (float)py2, (float)PopW, (float)PopH },
            1.5f, Color{ 60, 140, 220, 220 });
        const char* msg = "Space station function coming soon";
        DrawText(msg, sw2 / 2 - MeasureText(msg, 14) / 2, py2 + 30, 14,
            Color{ 160, 210, 255, 240 });
        Rectangle okBtn = { (float)(sw2 / 2 - 60), (float)(py2 + PopH - 46), 120.0f, 32.0f };
        bool hovOk = CheckCollisionPointRec(GetMousePosition(), okBtn);
        DrawRectangleRec(okBtn, hovOk ? Color{ 30, 80, 140, 230 } : Color{ 14, 40, 75, 200 });
        DrawRectangleLinesEx(okBtn, 1.0f, Color{ 60, 140, 220, 200 });
        DrawText("OK", (int)(okBtn.x + (okBtn.width - MeasureText("OK", 12)) / 2),
            (int)(okBtn.y + 10), 12, hovOk ? WHITE : Color{ 120, 190, 255, 220 });
    }

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
    _projectiles.clear();
    _asteroids.clear();
    _entities.clear();
    _npcMeta.clear();
    _lootDrops.clear();
    _materialDrops.clear();
    _commsLog.clear();
    _remoteEntities.clear();
    _remoteFireCooldown.clear();
    _remoteJoinGrace.clear();
    _remoteProjectiles.clear();
    _npcTargetId = 0;
    _playerEntity = ecs::Entity{};
    _playerMeta   = PlayerMeta{};
    _planets.clear();
    _stations.clear();
    if (_planetBaseTex.id    > 0) { UnloadTexture(_planetBaseTex);    _planetBaseTex    = {}; }
    if (_stationBaseTex.id   > 0) { UnloadTexture(_stationBaseTex);   _stationBaseTex   = {}; }
    if (_gargosTex.id        > 0) { UnloadTexture(_gargosTex);        _gargosTex        = {}; }
    if (_sunTex.id           > 0) { UnloadTexture(_sunTex);           _sunTex           = {}; }
    if (_sunCorona.id        > 0) { UnloadTexture(_sunCorona);        _sunCorona        = {}; }
    if (_asteroidTexLarge.id > 0) { UnloadTexture(_asteroidTexLarge); _asteroidTexLarge = {}; }
    if (_asteroidTexMedium.id> 0) { UnloadTexture(_asteroidTexMedium);_asteroidTexMedium= {}; }
    if (_asteroidTexSmall.id > 0) { UnloadTexture(_asteroidTexSmall); _asteroidTexSmall = {}; }
    _lighting.Shutdown();
}
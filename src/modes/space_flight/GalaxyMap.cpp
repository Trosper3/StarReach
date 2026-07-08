#include "GalaxyMap.h"
#include "data/registry/StarRegistry.h"
#include "data/registry/StarSystemRegistry.h"
#include "data/registry/UniverseRegistry.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "systems/diplomacy/ReputationRegistry.h"
#include "shared/ui/HudTheme.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cmath>
#include <queue>
#include <unordered_set>

static bool IsHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool IsClk(Rectangle r) { return IsHov(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

static void DrawMapBtn(Rectangle r, const char* label, bool enabled = true) {
    using namespace hudtheme;
    bool hov = enabled && IsHov(r);
    Color bg  = !enabled ? Color{ 16,16,16,150 } : (hov ? Color{ 30,55,70,230 } : Color{ 14,20,28,200 });
    Color bdr = !enabled ? HudDiv : HudBorder;
    Color fg  = !enabled ? Color{ 50,55,60,160 } : (hov ? WHITE : HudLabel);
    DrawHudChamferRect(r, 8.0f, bg, bdr, hov ? 2.0f : 1.0f);
    int tw = MeasureText(label, 15);
    DrawText(label, (int)(r.x + (r.width  - tw) / 2.0f),
                    (int)(r.y + (r.height - 15) / 2.0f), 15, fg);
}

// Small filled ellipse rotated about its center, used for Universe-tier
// galaxy icons (oriented/shaped by that galaxy's own GalaxyShapeParams).
// raylib's DrawEllipse has no rotation parameter.
//
// This used to hand-build a triangle fan and pass it to DrawTriangleFan —
// which turned out to render nothing at all (confirmed via a standalone
// raylib repro: DrawTriangleFan's degenerate quad-per-wedge duplicates the
// *second* edge vertex, leaving one real triangle and one zero-area one per
// wedge, vs. DrawCircleSector/DrawPoly which duplicate the *first* edge
// vertex, producing two real triangles of opposite winding — the version
// that actually survives whatever winding/culling this pipeline applies).
// Routing through the rlgl matrix stack + the known-good DrawCircleSector
// sidesteps the bug entirely instead of re-deriving a correct fan by hand.
static void DrawRotatedEllipse(Vector2 center, float radiusH, float radiusV, float rotationRad, Color color) {
    rlPushMatrix();
    rlTranslatef(center.x, center.y, 0.0f);
    rlRotatef(rotationRad * RAD2DEG, 0.0f, 0.0f, 1.0f);
    rlScalef(radiusH, radiusV, 1.0f);
    DrawCircleSector({ 0.0f, 0.0f }, 1.0f, 0.0f, 360.0f, 36, color);
    rlPopMatrix();
}

struct MapLayout {
    int       mx, mw;
    Rectangle title, map, bot;
};

static MapLayout CalcLayout(int sw, int sh) {
    MapLayout L;
    L.mx  = (int)(sw * 0.12f);
    L.mw  = (int)(sw * 0.76f);
    int ty = (int)(sh * 0.05f);
    int th = 40;
    int my = ty + th + 4;
    int mh = (int)(sh * 0.60f);
    int by = my + mh + 10;
    int bh = sh - by - (int)(sh * 0.04f);
    L.title = { (float)L.mx, (float)ty, (float)L.mw, (float)th };
    L.map   = { (float)L.mx, (float)my, (float)L.mw, (float)mh };
    L.bot   = { (float)L.mx, (float)by, (float)L.mw, (float)bh };
    return L;
}

// Top row: MODULES | STORAGE | ESCORTS | RANKS
// Bottom row: RESUME | SAVE | LOAD | MAIN MENU
// (No "GALACTIC MAP" hop anymore — scrolling out gets you there directly.)
static void CalcButtons(const Rectangle& bot,
                         Rectangle& modules, Rectangle& storage, Rectangle& escorts, Rectangle& ranks,
                         Rectangle& resume,  Rectangle& save,    Rectangle& load,    Rectangle& menu) {
    float bh     = 40.0f;
    float rowGap = 12.0f;
    float x0     = bot.x + 15.0f;
    float bw     = (bot.width - 60.0f) / 4.0f;
    float gap    = (bot.width - 30.0f - bw * 4.0f) / 3.0f;

    float by2 = bot.y + bot.height - bh - 14.0f;
    resume = { x0,                by2, bw, bh };
    save   = { x0 + (bw+gap),     by2, bw, bh };
    load   = { x0 + (bw+gap)*2.0f,by2, bw, bh };
    menu   = { x0 + (bw+gap)*3.0f,by2, bw, bh };

    float by1 = by2 - rowGap - bh;
    modules  = { x0,                by1, bw, bh };
    storage  = { x0 + (bw+gap),     by1, bw, bh };
    escorts  = { x0 + (bw+gap)*2.0f,by1, bw, bh };
    ranks    = { x0 + (bw+gap)*3.0f,by1, bw, bh };
}

// ── Travel-status color language ─────────────────────────────────────────────
// Shared by both the Galaxy tier (stars) and the Universe tier (galaxies):
// green = where you are right now, blue = been there before, orange = never
// been there. Replaces the old per-tier schemes (star discovery used yellow/
// blue-grey, galaxy icons were colored by Spiral-vs-Elliptical shape) with one
// consistent visual language across both scales.
static constexpr Color kColCurrent = { 90,  255, 130, 255 }; // green
static constexpr Color kColVisited = { 120, 175, 255, 255 }; // blue
static constexpr Color kColUnvisited = { 255, 175, 110, 255 }; // orange

// A name label is only worth drawing when neighboring dots are far enough
// apart on screen that it won't overlap the next one over — otherwise it's
// just visual noise. Compared against a cached average on-screen spacing
// (world sample spacing * camera scale) rather than true per-pair collision
// checks, which would be too expensive against a many-thousand-entry field.
static constexpr float kMinNameSpacingPx = 70.0f;

// ── Camera tuning ────────────────────────────────────────────────────────────
// The whole zoom range in one continuum, tightest to widest:
static constexpr float kMinViewWidth       = 4'000.0f;      // tightest zoom-in
static constexpr float kDefaultViewWidth   = 15'000.0f;     // view width at Open()/Home
static constexpr float kSystemTierMax      = 70'000.0f;     // System -> Galaxy tier boundary
static constexpr float kGalaxyShapeMin     = 4'000'000.0f;  // Galaxy -> Galaxy Shape tier boundary
static constexpr float kZoomStep           = 1.15f;
static constexpr float kPanSpeedPx         = 500.0f;        // screen px/sec at scale=1
static constexpr int   kDrawBudget         = 6'000;         // Galaxy tier decorative points/frame
static constexpr int   kShapeDrawBudget    = 45'000;        // Galaxy Shape tier: dense enough for dots to pack/overlap
static constexpr int   kRangeQueryBudget   = 2'000;         // undiscovered-in-range lookup

// ── Universe tier ─────────────────────────────────────────────────────────
// A separate camera/coordinate space from the range above (see GalaxyMap.h's
// _atUniverse comment for why) — its own zoom range, sized to the universe
// span rather than one galaxy's.
static constexpr float kUniverseReturnViewWidth  = kGalaxyShapeMin;        // zoom in past this -> back to in-galaxy camera
static constexpr int   kUniverseDrawBudget       = 2'000;                  // galaxy icons/frame
static constexpr int   kUniverseShapeDrawBudget  = 6'000;                  // UniverseShape tier decorative dots/frame

// Initial/Home view width on crossing into (or Home-recentering within) the
// Universe tier. Framed off UniverseRegistry::CellSize() (the actual galaxy-
// to-galaxy lattice spacing) rather than a fixed constant — this used to be
// kGalaxyShapeMin*25 (100,000,000u), a leftover borrowed from the *star*-
// scale tier boundary with no relation to the galaxy lattice's real spacing
// (~40,000,000u). That made the initial window barely 2.5 cells wide, so
// jittered neighbor positions often landed just outside it — for some seeds
// the player would see nothing but their own galaxy on first crossing out.
// 6 cell-widths comfortably covers a full ring of neighbors regardless of jitter.
static float UniverseInitialViewWidth() {
    return std::max(kUniverseReturnViewWidth, UniverseRegistry::CellSize() * 6.0f);
}

// Universe -> UniverseShape tier boundary: past this, individual galaxy
// icons thin into a density-dot field of the whole supercluster shape.
// Deliberately derived from UniverseInitialViewWidth() (4x it) rather than a
// fixed fraction of kUniverseSpan — a fixed span/20 (200,000,000u) happened
// to sit BELOW the entry view width (240,000,000u at the default galaxy
// count), so crossing out from the galaxy camera landed past the boundary
// immediately, skipping the individual-galaxy-icon tier on the way out (and
// symmetrically getting skipped on the way back in). Scaling off the entry
// width instead guarantees a comfortable individual-icon range between the
// two, regardless of how galaxy count/density changes cell size. Clamped so
// it can never exceed half the total span, keeping the outer UniverseShape
// tier reachable even at very low galaxy counts (huge cells).
static float UniverseShapeMinViewWidth() {
    return std::min(UniverseRegistry::kUniverseSpan * 0.5f, UniverseInitialViewWidth() * 4.0f);
}

GalaxyMap::MapProjection GalaxyMap::ComputeProjection(const Rectangle& mapRect) const {
    MapProjection p;
    p.mapCX = mapRect.x + mapRect.width  * 0.5f;
    p.mapCY = mapRect.y + mapRect.height * 0.5f;
    p.cx    = _camCenter.x;
    p.cy    = _camCenter.y;
    p.scale = _camScale;
    return p;
}

GalaxyMap::MapProjection GalaxyMap::ComputeUniverseProjection(const Rectangle& mapRect) const {
    MapProjection p;
    p.mapCX = mapRect.x + mapRect.width  * 0.5f;
    p.mapCY = mapRect.y + mapRect.height * 0.5f;
    p.cx    = _universeCamCenter.x;
    p.cy    = _universeCamCenter.y;
    p.scale = _universeCamScale;
    return p;
}

Rectangle GalaxyMap::VisibleWorldRect(const Rectangle& mapRect) const {
    float halfW = (mapRect.width  * 0.5f) / _camScale;
    float halfH = (mapRect.height * 0.5f) / _camScale;
    return { _camCenter.x - halfW, _camCenter.y - halfH, halfW * 2.0f, halfH * 2.0f };
}

Rectangle GalaxyMap::VisibleUniverseRect(const Rectangle& mapRect) const {
    float halfW = (mapRect.width  * 0.5f) / _universeCamScale;
    float halfH = (mapRect.height * 0.5f) / _universeCamScale;
    return { _universeCamCenter.x - halfW, _universeCamCenter.y - halfH, halfW * 2.0f, halfH * 2.0f };
}

GalaxyMap::Tier GalaxyMap::CurrentTier(const Rectangle& mapRect) const {
    if (_atUniverse) {
        float uViewWidth = mapRect.width / _universeCamScale;
        return (uViewWidth <= UniverseShapeMinViewWidth()) ? Tier::Universe : Tier::UniverseShape;
    }
    float viewWidth = mapRect.width / _camScale;
    if (viewWidth <= kSystemTierMax)  return Tier::System;
    if (viewWidth <= kGalaxyShapeMin) return Tier::Galaxy;
    return Tier::GalaxyShape;
}


void GalaxyMap::Open() {
    isOpen      = true;
    _time       = 0.0f;
    _selectedBlip     = -1;
    _selectedSystemId = 0;
    _selectedGalaxyId = 0;
    _dragging   = false;
    _cacheValid = false; // force a fresh build — discoveries may have changed since last Open()
    _atUniverse = false; // always reopen inside the current galaxy, not the universe view

    // Camera centering is deferred to Update()'s first call — see
    // _needsRecenter's comment in GalaxyMap.h for why it can't happen here.
    _needsRecenter = true;
}

void GalaxyMap::Close() {
    isOpen = false;
    _savePicker.Close();
    _saveWriter.Close();
    _selectedBlip     = -1;
    _selectedSystemId = 0;
    _selectedGalaxyId = 0;
    _dragging = false;
}

void GalaxyMap::HandleCameraInput(float dt, const Rectangle& mapRect) {
    if (_atUniverse) {
        HandleUniverseCameraInput(dt, mapRect);
        return;
    }

    float minScale = mapRect.width / StarSystemRegistry::kGalaxySpan;
    float maxScale = mapRect.width / kMinViewWidth;
    if (minScale > maxScale) std::swap(minScale, maxScale);

    float panSpeed = kPanSpeedPx / _camScale;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) _camCenter.x -= panSpeed * dt;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) _camCenter.x += panSpeed * dt;
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) _camCenter.y -= panSpeed * dt;
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) _camCenter.y += panSpeed * dt;
    if (IsKeyPressed(KEY_HOME)) {
        _camCenter = Vector2Add(_mapData.currentSystemPos, _mapData.playerPos);
        _camScale  = mapRect.width / kDefaultViewWidth;
    }

    Vector2 mouse = GetMousePosition();
    float   wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(mouse, mapRect)) {
        Vector2 worldBefore = ComputeProjection(mapRect).Unproject(mouse);

        float factor   = (wheel > 0.0f) ? kZoomStep : (1.0f / kZoomStep);
        float newScale = _camScale * factor;

        if (newScale < minScale) {
            // Zooming out past "the whole galaxy fills the screen" crosses
            // into the Universe tier instead of clamping — see EnterUniverseTier.
            EnterUniverseTier(mapRect);
            return;
        }

        _camScale = std::clamp(newScale, minScale, maxScale);
        MapProjection projAfter = ComputeProjection(mapRect);
        Vector2 delta = { (mouse.x - projAfter.mapCX) / _camScale,
                           (mouse.y - projAfter.mapCY) / _camScale };
        _camCenter = { worldBefore.x - delta.x, worldBefore.y - delta.y };
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, mapRect)) {
        _dragging       = true;
        _dragStartMouse = mouse;
        _dragStartCam   = _camCenter;
    }
    if (_dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 delta = Vector2Subtract(mouse, _dragStartMouse);
            _camCenter = { _dragStartCam.x - delta.x / _camScale,
                           _dragStartCam.y - delta.y / _camScale };
        } else {
            _dragging = false;
        }
    }

    _camScale = std::clamp(_camScale, minScale, maxScale);
}

// Anchors the universe camera on the current galaxy's universe-space
// position so it's exactly where you'd expect right as the view flips —
// _camCenter/_camScale are left untouched so zooming back in restores them
// exactly (see EnterUniverseTier's caller and the _atUniverse comment).
void GalaxyMap::EnterUniverseTier(const Rectangle& mapRect) {
    _universeCamCenter = UniverseRegistry::Generate(_mapData.currentGalaxyId).position;
    _universeCamScale  = mapRect.width / UniverseInitialViewWidth();
    _atUniverse         = true;
    _cacheValid         = false;
}

void GalaxyMap::HandleUniverseCameraInput(float dt, const Rectangle& mapRect) {
    float minScale = mapRect.width / UniverseRegistry::kUniverseSpan;
    float maxScale = mapRect.width / kUniverseReturnViewWidth;
    if (minScale > maxScale) std::swap(minScale, maxScale);

    float panSpeed = kPanSpeedPx / _universeCamScale;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) _universeCamCenter.x -= panSpeed * dt;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) _universeCamCenter.x += panSpeed * dt;
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) _universeCamCenter.y -= panSpeed * dt;
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) _universeCamCenter.y += panSpeed * dt;
    if (IsKeyPressed(KEY_HOME)) {
        _universeCamCenter = UniverseRegistry::Generate(_mapData.currentGalaxyId).position;
        _universeCamScale  = mapRect.width / UniverseInitialViewWidth();
    }

    Vector2 mouse = GetMousePosition();
    float   wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(mouse, mapRect)) {
        Vector2 worldBefore = ComputeUniverseProjection(mapRect).Unproject(mouse);

        float factor   = (wheel > 0.0f) ? kZoomStep : (1.0f / kZoomStep);
        float newScale = _universeCamScale * factor;

        if (newScale > maxScale) {
            // Zooming in past "a galaxy icon fills the screen" only makes
            // sense for your own galaxy (foreign ones need an actual warp,
            // once that exists) — drop back to the in-galaxy camera exactly
            // as it was left, since it was never touched while out here.
            _atUniverse = false;
            _cacheValid = false;
            return;
        }

        _universeCamScale = std::clamp(newScale, minScale, maxScale);
        MapProjection projAfter = ComputeUniverseProjection(mapRect);
        Vector2 delta = { (mouse.x - projAfter.mapCX) / _universeCamScale,
                           (mouse.y - projAfter.mapCY) / _universeCamScale };
        _universeCamCenter = { worldBefore.x - delta.x, worldBefore.y - delta.y };
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, mapRect)) {
        _dragging       = true;
        _dragStartMouse = mouse;
        _dragStartCam   = _universeCamCenter;
    }
    if (_dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 delta = Vector2Subtract(mouse, _dragStartMouse);
            _universeCamCenter = { _dragStartCam.x - delta.x / _universeCamScale,
                                    _dragStartCam.y - delta.y / _universeCamScale };
        } else {
            _dragging = false;
        }
    }

    _universeCamScale = std::clamp(_universeCamScale, minScale, maxScale);
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

// Same RED/GREEN/SKYBLUE convention SpaceFlight.cpp's MakeNpcEntity already
// uses for hostile/friendly/neutral NPC ship tinting — reused here so the
// Galaxy tier's faction overlay reads as the same color language the player
// already learned from in-system combat, not a new one.
static Color RelationColor(Relation r) {
    switch (r) {
        case Relation::Hostile:  return RED;
        case Relation::Friendly: return GREEN;
        default:                 return SKYBLUE;
    }
}

static const char* RelationName(Relation r) {
    switch (r) {
        case Relation::Hostile:  return "HOSTILE";
        case Relation::Friendly: return "FRIENDLY";
        default:                 return "NEUTRAL";
    }
}

// ── Sensor-tier intel preview ─────────────────────────────────────────────────
// What an undiscovered-but-visible system's selection popup reveals scales
// with mapSensorTier (1 = Common Proximity Array .. 7 = Mythic Omniscient
// Core — see SpaceFlight::_mapSensorTier). Deliberately reads only pure,
// side-effect-free sources: StarRegistry::Pick(seed) is a closed-form hash of
// its argument (unlike SpawnPlanetsAndStations's SetRandomSeed/GetRandomValue
// sequence, which mutates raylib's shared global RNG state and only runs once
// the player actually arrives) and StarSystem::isControlled/controllingFaction
// come straight from the registry. estPlanets is a separate hash-derived
// *estimate* in the same [0,10] range SpawnPlanetsAndStations rolls from, not
// a preview of its exact draw — sensor readings are meant to approximate,
// not spoil, what's actually there.
struct SensorIntel {
    const StarTypeDef* star       = nullptr;
    int                 estPlanets = 0;
};

static SensorIntel PreviewSensorIntel(const StarSystem& sys) {
    SensorIntel intel;
    intel.star = StarRegistry::Pick(sys.seed);
    uint32_t h = StarSystemRegistry::detail::Hash32(sys.seed, 0u, 0xE571u);
    intel.estPlanets = (int)(h % 11u); // 0..10
    return intel;
}

float GalaxyMap::DrawSensorIntel(const StarSystem& sys, float centerX, float startY, bool draw) const {
    if (_mapData.mapSensorTier < 2) return startY;

    SensorIntel intel = PreviewSensorIntel(sys);
    float y = startY;
    Color col = Color{ 180, 200, 220, 210 };
    auto drawLine = [&](const char* text) {
        if (draw) DrawText(text, (int)(centerX - MeasureText(text, 10) / 2), (int)y, 10, col);
        y += 13.0f;
    };

    if (intel.star) {
        char line[48];
        std::snprintf(line, sizeof(line), "CLASS %s STAR", intel.star->id.c_str());
        drawLine(line);
    }
    if (_mapData.mapSensorTier >= 3) {
        char line[48];
        std::snprintf(line, sizeof(line), "EST. %d PLANETS", intel.estPlanets);
        drawLine(line);
    }
    if (_mapData.mapSensorTier >= 4) {
        drawLine(sys.isControlled ? "OCCUPIED (STATION)" : "UNCLAIMED");
        if (_mapData.mapSensorTier >= 5 && sys.isControlled) {
            char line[48];
            std::snprintf(line, sizeof(line), "FACTION: %s", FactionName(sys.controllingFaction));
            drawLine(line);
        }
    }
    return y;
}

float GalaxyMap::ComputeInfoBlockEnd(const StarSystem& sys, bool discovered, float centerX, float startY, bool draw) const {
    if (!discovered) return DrawSensorIntel(sys, centerX, startY, draw);
    if (!sys.isControlled) return startY;

    Relation rel = ReputationRegistry::PlayerRelation(sys.controllingFaction);
    if (draw) {
        char line[64];
        std::snprintf(line, sizeof(line), "%s (%s)", FactionName(sys.controllingFaction), RelationName(rel));
        DrawText(line, (int)(centerX - MeasureText(line, 10) / 2), (int)startY, 10, RelationColor(rel));
    }
    return startY + 13.0f;
}

// ── Beacon-chaining ───────────────────────────────────────────────────────────

std::vector<unsigned int> GalaxyMap::ComputeChainPath(unsigned int targetId) const {
    if (_mapData.hyperdriveRange <= 0.0f || _mapData.currentSystemId == 0) return {};
    auto tgt = StarSystemRegistry::ById(targetId);
    if (!tgt) return {};

    // Graph nodes: current system + every discovered system (deduplicated).
    // targetId is resolved separately below rather than added as a node here,
    // since it's only ever a valid *destination*, never a waypoint to hop
    // through — hopping "via" an undiscovered system makes no narrative sense.
    std::vector<unsigned int> nodeIds;
    nodeIds.push_back(_mapData.currentSystemId);
    for (unsigned int id : _mapData.discoveredSystemIds)
        if (id != _mapData.currentSystemId) nodeIds.push_back(id);

    std::vector<Vector2> nodePos(nodeIds.size());
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (nodeIds[i] == _mapData.currentSystemId) nodePos[i] = _mapData.currentSystemPos;
        else if (auto s = StarSystemRegistry::ById(nodeIds[i])) nodePos[i] = s->galacticPos;
    }

    const float range = _mapData.hyperdriveRange;
    std::vector<int> prev(nodeIds.size(), -1);
    std::vector<int> depth(nodeIds.size(), -1);
    depth[0] = 0;
    std::queue<size_t> q;
    q.push(0);
    while (!q.empty()) {
        size_t u = q.front(); q.pop();
        for (size_t v = 0; v < nodeIds.size(); ++v) {
            if (depth[v] >= 0) continue;
            if (Vector2Distance(nodePos[u], nodePos[v]) <= range) {
                depth[v] = depth[u] + 1;
                prev[v]  = (int)u;
                q.push(v);
            }
        }
    }

    auto ReconstructTo = [&](int node) {
        std::vector<unsigned int> path;
        for (int cur = node; cur != 0; cur = prev[cur]) path.push_back(nodeIds[cur]);
        std::reverse(path.begin(), path.end());
        return path;
    };

    // targetId may already be a discovered node itself (a plain multi-hop
    // trip between two known systems, no "final unknown leg" involved).
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (nodeIds[i] == targetId && depth[i] >= 0) return ReconstructTo((int)i);
    }

    // Otherwise, find the reachable node with the fewest hops that's within
    // range of the (possibly undiscovered) target, and tack the target on as
    // one final ordinary hop.
    int bestNode = -1, bestDepth = INT_MAX;
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (depth[i] < 0) continue;
        if (depth[i] < bestDepth && Vector2Distance(nodePos[i], tgt->galacticPos) <= range) {
            bestDepth = depth[i];
            bestNode  = (int)i;
        }
    }
    if (bestNode < 0) return {};

    std::vector<unsigned int> path = ReconstructTo(bestNode);
    path.push_back(targetId);
    return path;
}

// ── Galaxy / Galaxy Shape tier blip queries (galactic coordinates) ───────────

std::vector<GalaxyBlip> GalaxyMap::BuildInteractiveBlips(const Rectangle& mapRect) const {
    std::vector<GalaxyBlip> out;
    if (_mapData.currentSystemId == 0) return out;

    Rectangle viewRect = VisibleWorldRect(mapRect);
    auto InView = [&](Vector2 p) {
        return p.x >= viewRect.x && p.x <= viewRect.x + viewRect.width &&
               p.y >= viewRect.y && p.y <= viewRect.y + viewRect.height;
    };

    if (auto cur = StarSystemRegistry::ById(_mapData.currentSystemId)) {
        out.push_back({ cur->id, StarSystemRegistry::NameOf(cur->seed), cur->galacticPos, cur->cellCenter, true, true });
    }

    for (unsigned int id : _mapData.discoveredSystemIds) {
        if (id == _mapData.currentSystemId) continue;
        auto sys = StarSystemRegistry::ById(id);
        if (!sys || !InView(sys->galacticPos)) continue;
        out.push_back({ sys->id, StarSystemRegistry::NameOf(sys->seed), sys->galacticPos, sys->cellCenter, true, false });
    }

    // Undiscovered systems within current SENSOR range (not hyperdrive range —
    // those are independent: sensors control what you can see, hyperdrive
    // controls what you can reach; the WARP button below still checks
    // hyperdrive range separately, so reaching this list is necessary but not
    // sufficient to actually warp). An independent, tightly-bounded query
    // (sensor range is always far smaller than the full galaxy) so these are
    // never dropped by the background field's LOD thinning.
    if (_mapData.mapSensorRange > 0.0f) {
        float     r         = _mapData.mapSensorRange;
        Rectangle rangeRect = { _mapData.currentSystemPos.x - r, _mapData.currentSystemPos.y - r,
                                 r * 2.0f, r * 2.0f };
        for (const StarSystem& sys : StarSystemRegistry::QueryRegion(rangeRect, kRangeQueryBudget)) {
            if (sys.id == _mapData.currentSystemId) continue;
            if (!InView(sys.galacticPos)) continue;
            if (Vector2Distance(_mapData.currentSystemPos, sys.galacticPos) > r) continue;
            bool alreadyKnown = std::find(_mapData.discoveredSystemIds.begin(),
                                           _mapData.discoveredSystemIds.end(), sys.id)
                                != _mapData.discoveredSystemIds.end();
            if (alreadyKnown) continue;
            out.push_back({ sys.id, {}, sys.galacticPos, sys.cellCenter, false, false });
        }
    }

    return out;
}

std::vector<StarSystem> GalaxyMap::BuildBackgroundDots(const Rectangle& mapRect,
                                                         const std::vector<GalaxyBlip>& interactive,
                                                         float* outWorldTileSize) const {
    std::unordered_set<unsigned int> known;
    known.reserve(interactive.size() * 2);
    for (const GalaxyBlip& b : interactive) known.insert(b.id);

    std::vector<StarSystem> out;
    Rectangle viewRect = VisibleWorldRect(mapRect);
    Tier      tier     = CurrentTier(mapRect);
    int       budget   = (tier == Tier::GalaxyShape) ? kShapeDrawBudget : kDrawBudget;

    // Fog of war only applies to the Galaxy tier's individual, nameable
    // systems — GalaxyShape's density field is the galaxy's overall
    // structural shape, treated as always-known "big picture" the same way
    // real astronomy can see a galaxy's shape without having visited every
    // star inside it. So GalaxyShape keeps querying the full view; only the
    // Galaxy tier gets clipped to the sensor bubble around the player.
    bool applyFog = (tier != Tier::GalaxyShape);
    if (!applyFog) {
        for (const StarSystem& sys : StarSystemRegistry::QueryRegion(viewRect, budget, outWorldTileSize)) {
            if (known.find(sys.id) != known.end()) continue;
            out.push_back(sys);
        }
        return out;
    }

    if (_mapData.mapSensorRange <= 0.0f) {
        if (outWorldTileSize) *outWorldTileSize = 0.0f;
        return out; // no sensors equipped: nothing undiscovered is visible at all
    }

    // Intersect the query rect with the sensor bubble (not just filter
    // results afterward) so the budget/stride math below is scaled to the
    // actually-revealed area — otherwise a camera panned far from the player
    // would burn the whole budget thinning a mostly-fogged view, leaving the
    // small revealed corner under-sampled.
    float     r          = _mapData.mapSensorRange;
    Rectangle sensorRect = { _mapData.currentSystemPos.x - r, _mapData.currentSystemPos.y - r,
                              r * 2.0f, r * 2.0f };
    float x0 = std::max(viewRect.x, sensorRect.x);
    float y0 = std::max(viewRect.y, sensorRect.y);
    float x1 = std::min(viewRect.x + viewRect.width,  sensorRect.x + sensorRect.width);
    float y1 = std::min(viewRect.y + viewRect.height, sensorRect.y + sensorRect.height);
    if (x1 <= x0 || y1 <= y0) {
        if (outWorldTileSize) *outWorldTileSize = 0.0f;
        return out; // sensor bubble doesn't overlap what's currently on screen
    }
    Rectangle queryRect = { x0, y0, x1 - x0, y1 - y0 };

    for (const StarSystem& sys : StarSystemRegistry::QueryRegion(queryRect, budget, outWorldTileSize)) {
        if (known.find(sys.id) != known.end()) continue;
        // The rect intersection above is square-cornered; the sensor bubble
        // itself is circular, so still need this to exclude the corners.
        if (Vector2Distance(_mapData.currentSystemPos, sys.galacticPos) > r) continue;
        out.push_back(sys);
    }
    return out;
}

void GalaxyMap::RefreshMapCache(const Rectangle& mapRect, Tier tier) const {
    // Universe/UniverseShape read from their own independent camera pair —
    // pick whichever one is active so the staleness check below covers it too.
    bool    universeCam   = (tier == Tier::Universe || tier == Tier::UniverseShape);
    Vector2 activeCenter  = universeCam ? _universeCamCenter : _camCenter;
    float   activeScale   = universeCam ? _universeCamScale  : _camScale;

    bool same = _cacheValid && tier == _cacheTier &&
                _cacheCamCenter.x == activeCenter.x && _cacheCamCenter.y == activeCenter.y &&
                _cacheCamScale == activeScale &&
                _cacheMapRect.x == mapRect.x && _cacheMapRect.y == mapRect.y &&
                _cacheMapRect.width == mapRect.width && _cacheMapRect.height == mapRect.height;
    if (same) return;

    if (universeCam) {
        _cacheUniverseTileSize = 0.0f;
        int budget = (tier == Tier::UniverseShape) ? kUniverseShapeDrawBudget : kUniverseDrawBudget;
        _cacheGalaxies = UniverseRegistry::QueryRegion(VisibleUniverseRect(mapRect), budget,
                                                        &_cacheUniverseTileSize);
    } else {
        _cacheInteractive   = BuildInteractiveBlips(mapRect);
        _cacheWorldTileSize = 0.0f;
        _cacheBackground    = BuildBackgroundDots(mapRect, _cacheInteractive, &_cacheWorldTileSize);
    }

    _cacheValid     = true;
    _cacheTier      = tier;
    _cacheCamCenter = activeCenter;
    _cacheCamScale  = activeScale;
    _cacheMapRect   = mapRect;
}

// ── Update ────────────────────────────────────────────────────────────────────

MapAction GalaxyMap::Update(float dt) {
    _time += dt;
    if (_saveFeedbackTimer > 0.0f) _saveFeedbackTimer -= dt;

    if (_saveWriter.IsOpen()) {
        auto result = _saveWriter.Update();
        if (result == SaveWriter::Result::Saved) return MapAction::SaveToFile;
        return MapAction::None;
    }
    if (_savePicker.IsOpen()) {
        auto result = _savePicker.Update();
        if (result == SavePicker::Result::Selected) return MapAction::LoadGame;
        return MapAction::None;
    }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcLayout(sw, sh);

    if (_needsRecenter) {
        // First Update() since Open() — this frame's SetMapData() (called by
        // the caller right after Open(), before Update()) has already run,
        // so _mapData reflects whatever system the player is actually in now.
        _camCenter     = Vector2Add(_mapData.currentSystemPos, _mapData.playerPos);
        _camScale      = L.map.width / kDefaultViewWidth;
        _needsRecenter = false;
    }

    Rectangle modulesBtn, storageBtn, escortsBtn, ranksBtn;
    Rectangle resumeBtn, saveBtn, loadBtn, menuBtn;
    CalcButtons(L.bot, modulesBtn, storageBtn, escortsBtn, ranksBtn,
                resumeBtn, saveBtn, loadBtn, menuBtn);

    if (IsClk(resumeBtn))  { Close(); return MapAction::Close; }
    if (IsClk(saveBtn))    { _saveWriter.Open(); return MapAction::None; }
    if (IsClk(loadBtn))    { _savePicker.Open(); return MapAction::None; }
    if (IsClk(menuBtn))                   return MapAction::GoMainMenu;
    if (IsClk(modulesBtn))                return MapAction::OpenModules;
    if (IsClk(storageBtn))                return MapAction::OpenStorage;
    if (IsClk(escortsBtn))                return MapAction::OpenEscorts;
    if (IsClk(ranksBtn))                  return MapAction::OpenRanks;

    HandleCameraInput(dt, L.map);
    Tier tier = CurrentTier(L.map);
    auto proj = ComputeProjection(L.map);
    Vector2 mouse = GetMousePosition();

    if (tier == Tier::System) {
        // Local system-space blips are displayed offset into galactic space
        // (currentSystemPos + local) so they sit at the right place as the
        // camera pulls back — but warp-target math stays purely local
        // (blip - player), since the offset cancels out in that subtraction.
        if (!_mapData.blips.empty() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mouse, L.map)) {
            if (_selectedBlip >= 0 && _selectedBlip < (int)_mapData.blips.size()) {
                const MapBlip& blip = _mapData.blips[_selectedBlip];
                float dist = Vector2Distance(_mapData.playerPos, blip.worldPos);
                bool inRange = _mapData.hyperdriveRange > 0.0f && dist <= _mapData.hyperdriveRange;
                if (inRange) {
                    Vector2 sp = proj.Project(Vector2Add(_mapData.currentSystemPos, blip.worldPos));
                    Rectangle warpBtn = { sp.x - 42.0f, sp.y + 18.0f, 84.0f, 26.0f };
                    if (CheckCollisionPointRec(mouse, warpBtn)) {
                        Vector2 toBlip = Vector2Subtract(blip.worldPos, _mapData.playerPos);
                        float toLen = Vector2Length(toBlip);
                        Vector2 dir = (toLen > 1.0f) ? Vector2Scale(toBlip, 1.0f / toLen) : Vector2{0.0f, -1.0f};
                        _warpTarget = Vector2Subtract(blip.worldPos, Vector2Scale(dir, blip.radius + 150.0f));
                        return MapAction::WarpTo;
                    }
                }
            }

            bool clickedBlip = false;
            for (int i = 0; i < (int)_mapData.blips.size(); ++i) {
                Vector2 sp = proj.Project(Vector2Add(_mapData.currentSystemPos, _mapData.blips[i].worldPos));
                if (CheckCollisionPointCircle(mouse, sp, 12.0f)) {
                    _selectedBlip = (_selectedBlip == i) ? -1 : i;
                    clickedBlip = true;
                    break;
                }
            }
            if (!clickedBlip) _selectedBlip = -1;
        }
    }
    else if (tier == Tier::Galaxy) {
        _selectedBlip = -1;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, L.map)) {
            // Reuse the same cache Draw() reads (RefreshMapCache no-ops if
            // camera/viewport haven't moved since Draw()'s own call this
            // frame) so hit-testing lines up with what's actually on screen —
            // including the plain decorative background field, not just the
            // small "interactive" set (current/discovered/in-range-
            // undiscovered). Every star dot you can see should be selectable,
            // the same way every planet/station is selectable at System tier
            // regardless of hyperdrive range.
            RefreshMapCache(L.map, tier);
            const auto& interactive = _cacheInteractive;
            const auto& background  = _cacheBackground;

            if (_selectedSystemId != 0 && _selectedSystemId != _mapData.currentSystemId) {
                // Resolved directly from the registry rather than looked up
                // in `interactive`/`background`, since a selected star may be
                // a background dot that isn't part of either list — its
                // position doesn't depend on membership in either.
                if (auto sys = StarSystemRegistry::ById(_selectedSystemId)) {
                    float dist = Vector2Distance(_mapData.currentSystemPos, sys->galacticPos);
                    bool directInRange = _mapData.hyperdriveRange > 0.0f && dist <= _mapData.hyperdriveRange;
                    // Deliberately NOT gated on "discovered": discovery only
                    // happens as a side effect of warping somewhere new (see
                    // CommitWarpWorldSwitch's _discoveredSystemIds.push_back),
                    // so requiring it here would make it impossible to ever
                    // reach a system for the first time. The range check is
                    // the only gate that matters — direct, or (below) chained
                    // through already-discovered waypoints.
                    std::vector<unsigned int> chain = directInRange
                        ? std::vector<unsigned int>{ sys->id }
                        : ComputeChainPath(sys->id);
                    if (!chain.empty()) {
                        Vector2 sp = proj.Project(sys->galacticPos);
                        // Must match Draw()'s warpBtn placement exactly — the
                        // info block (sensor intel / faction line) above it
                        // can push the visible button down by several lines,
                        // and the hit-test has to follow or clicks land on
                        // nothing (see ComputeInfoBlockEnd's comment).
                        bool discovered = std::find(_mapData.discoveredSystemIds.begin(),
                                                     _mapData.discoveredSystemIds.end(), sys->id)
                                          != _mapData.discoveredSystemIds.end();
                        float intelY = ComputeInfoBlockEnd(*sys, discovered, sp.x, sp.y + 6.0f, /*draw=*/false);
                        Rectangle warpBtn = { sp.x - 42.0f, std::max(sp.y + 24.0f, intelY + 4.0f), 84.0f, 26.0f };
                        if (CheckCollisionPointRec(mouse, warpBtn)) {
                            _warpTargetId = sys->id;
                            _warpChain    = chain;
                            return MapAction::WarpToSystem;
                        }
                    }
                }
            }

            bool clickedBlip = false;
            for (const GalaxyBlip& b : interactive) {
                Vector2 sp = proj.Project(b.galacticPos);
                if (CheckCollisionPointCircle(mouse, sp, 10.0f)) {
                    _selectedSystemId = (_selectedSystemId == b.id) ? 0 : b.id;
                    clickedBlip = true;
                    break;
                }
            }
            if (!clickedBlip) {
                for (const StarSystem& sys : background) {
                    Vector2 sp = proj.Project(sys.galacticPos);
                    if (CheckCollisionPointCircle(mouse, sp, 6.0f)) {
                        _selectedSystemId = (_selectedSystemId == sys.id) ? 0 : sys.id;
                        clickedBlip = true;
                        break;
                    }
                }
            }
            if (!clickedBlip) _selectedSystemId = 0;
        }
    }
    else if (tier == Tier::Universe) {
        _selectedSystemId = 0;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, L.map)) {
            RefreshMapCache(L.map, tier);
            const auto& galaxies = _cacheGalaxies;
            auto uproj = ComputeUniverseProjection(L.map);

            if (_selectedGalaxyId != 0) {
                auto it = std::find_if(galaxies.begin(), galaxies.end(),
                    [&](const UniverseRegistry::GalaxyInfo& g) { return g.id == _selectedGalaxyId; });
                if (it != galaxies.end() && it->id != _mapData.currentGalaxyId) {
                    Vector2 curGalaxyPos = UniverseRegistry::Generate(_mapData.currentGalaxyId).position;
                    float   dist    = Vector2Distance(curGalaxyPos, it->position);
                    bool    inRange = _mapData.hyperdriveRange > 0.0f && dist <= _mapData.hyperdriveRange;
                    if (inRange) {
                        Vector2 sp = uproj.Project(it->position);
                        Rectangle warpBtn = { sp.x - 42.0f, sp.y + 14.0f, 84.0f, 26.0f };
                        if (CheckCollisionPointRec(mouse, warpBtn)) {
                            _warpTargetGalaxyId = it->id;
                            return MapAction::WarpToGalaxy;
                        }
                    }
                }
            }

            bool clickedGalaxy = false;
            for (const UniverseRegistry::GalaxyInfo& g : galaxies) {
                Vector2 sp = uproj.Project(g.position);
                if (CheckCollisionPointCircle(mouse, sp, 16.0f)) {
                    _selectedGalaxyId = (_selectedGalaxyId == g.id) ? 0 : g.id;
                    clickedGalaxy = true;
                    break;
                }
            }
            if (!clickedGalaxy) _selectedGalaxyId = 0;
        }
    }
    else { // GalaxyShape or UniverseShape — no per-entity interaction at either
        _selectedBlip = -1;
        _selectedSystemId = 0;
        _selectedGalaxyId = 0;
    }

    return MapAction::None;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void GalaxyMap::Draw() const {
    using namespace hudtheme;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcLayout(sw, sh);

    DrawRectangle(0, 0, sw, sh, Color{ 2, 4, 9, 215 });

    Tier tier = CurrentTier(L.map);
    const char* titleText = tier == Tier::System        ? "SYSTEM NAVIGATION ARRAY"
                           : tier == Tier::Galaxy        ? "GALACTIC NAVIGATION ARRAY"
                           : tier == Tier::GalaxyShape   ? "GALACTIC OVERVIEW"
                           : tier == Tier::Universe      ? "UNIVERSE NAVIGATION ARRAY"
                                                          : "UNIVERSE OVERVIEW"; // UniverseShape

    // ── Title bar ─────────────────────────────────────────────────────────────
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText(titleText, L.mx + 14, (int)L.title.y + 11, 16, HudValue);

    bool hasHyperdrive = _mapData.hyperdriveRange > 0.0f;
    bool hasMapSensors = _mapData.mapSensorRange > 0.0f;

    // Hyperdrive (what you can reach) and sensors (what you can see) are
    // independent, so both get their own status readout — see
    // BuildInteractiveBlips/BuildBackgroundDots for the fog-of-war gating
    // this reflects.
    char hyperLabel[64];
    const char* hyperText;
    Color hyperColor;
    if (hasHyperdrive) {
        std::snprintf(hyperLabel, sizeof(hyperLabel), "[ HYPERDRIVE RANGE: %.0f u ]", _mapData.hyperdriveRange);
        hyperText = hyperLabel; hyperColor = HudGood;
    } else {
        hyperText = "[ NO HYPERDRIVE EQUIPPED ]"; hyperColor = HudCaution;
    }

    char sensorLabel[64];
    const char* sensorText;
    Color sensorColor;
    if (hasMapSensors) {
        std::snprintf(sensorLabel, sizeof(sensorLabel), "[ SENSOR RANGE: %.0f u ]", _mapData.mapSensorRange);
        sensorText = sensorLabel; sensorColor = HudGood;
    } else {
        sensorText = "[ NO SENSORS EQUIPPED ]"; sensorColor = HudCaution;
    }

    // Epic 4: fuel readout alongside range/sensors — a jump can be in range
    // but still fuel-blocked (SpaceFlight::JumpFuelCost/BeginWarpSequence),
    // so this needs its own visible status rather than being implied by
    // hyperdrive range alone.
    char fuelLabel[48];
    Color fuelColor = HudGood;
    if (hasHyperdrive && _mapData.maxFuel > 0.0f) {
        float pct = _mapData.fuel / _mapData.maxFuel;
        std::snprintf(fuelLabel, sizeof(fuelLabel), "[ FUEL: %.0f%% ]", pct * 100.0f);
        fuelColor = pct > 0.5f ? HudGood : pct > 0.2f ? HudCaution : HudCritical;
    } else {
        fuelLabel[0] = '\0';
    }

    {
        int rightEdge = L.mx + L.mw - 14;
        int hyperW    = MeasureText(hyperText, 14);
        int sensorW   = MeasureText(sensorText, 14);
        int x         = rightEdge - hyperW;
        DrawText(hyperText,  x, (int)L.title.y + 13, 14, hyperColor);
        x -= 16 + sensorW;
        DrawText(sensorText, x, (int)L.title.y + 13, 14, sensorColor);
        if (fuelLabel[0] != '\0') {
            int fuelW = MeasureText(fuelLabel, 14);
            x -= 16 + fuelW;
            DrawText(fuelLabel, x, (int)L.title.y + 13, 14, fuelColor);
        }
    }

    // ── Map panel ────────────────────────────────────────────────────────────
    DrawHudBracketPanel(L.map, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);
    BeginScissorMode((int)L.map.x, (int)L.map.y, (int)L.map.width, (int)L.map.height);

    auto proj = ComputeProjection(L.map);
    Vector2 mouse = GetMousePosition();

    if (tier == Tier::System) {
        Vector2 playerUnified = Vector2Add(_mapData.currentSystemPos, _mapData.playerPos);
        Vector2 playerSP = proj.Project(playerUnified);

        // Sun always sits at local (0,0) — draw it first so blips layer on top.
        if (_mapData.hasSun) {
            Vector2 sunSP = proj.Project(_mapData.currentSystemPos);
            if (CheckCollisionPointRec(sunSP, L.map)) {
                DrawCircleV(sunSP, 15.0f, Fade(_mapData.sunGlow, 0.30f));
                DrawCircleV(sunSP, 9.0f,  _mapData.sunCore);
                DrawCircleLines((int)sunSP.x, (int)sunSP.y, 9.0f, Fade(_mapData.sunGlow, 0.85f));
            }
        }

        if (_mapData.blips.empty()) {
            const char* msg = "NO STELLAR OBJECTS DETECTED";
            DrawText(msg, (int)(L.map.x + (L.map.width - MeasureText(msg, 16)) / 2), (int)(L.map.y + L.map.height / 2 - 8), 16, HudLabel);
        } else {
            if (hasHyperdrive) {
                float screenRange = _mapData.hyperdriveRange * proj.scale;
                DrawCircleLines((int)playerSP.x, (int)playerSP.y, screenRange + 1.0f, Color{0, 180, 255, 30});
                DrawCircleLines((int)playerSP.x, (int)playerSP.y, screenRange,        Color{0, 200, 255, 90});
                DrawCircleLines((int)playerSP.x, (int)playerSP.y, screenRange - 1.0f, Color{0, 180, 255, 40});
            }

            for (int i = 0; i < (int)_mapData.blips.size(); ++i) {
                const MapBlip& blip = _mapData.blips[i];
                Vector2 sp = proj.Project(Vector2Add(_mapData.currentSystemPos, blip.worldPos));
                if (!CheckCollisionPointRec(sp, L.map)) continue;

                bool selected = (i == _selectedBlip);
                float dist = Vector2Distance(_mapData.playerPos, blip.worldPos);
                bool inRange = hasHyperdrive && dist <= _mapData.hyperdriveRange;

                Color baseColor = blip.isPlanet
                    ? (blip.discovered ? Color{80, 150, 230, 255} : Color{45, 75, 130, 160})
                    : (blip.discovered ? Color{220, 210, 130, 255} : Color{100, 95, 60, 160});

                if (selected) {
                    DrawCircleLines((int)sp.x, (int)sp.y, 14.0f, inRange ? Color{0,220,120,200} : Color{220,80,60,200});
                    DrawCircleLines((int)sp.x, (int)sp.y, 15.0f, inRange ? Color{0,180,100,120} : Color{180,60,40,120});
                }

                if (blip.discovered) {
                    if (blip.isPlanet) {
                        DrawCircleV(sp, 8.0f, baseColor);
                        DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, Color{140, 190, 255, 180});
                    } else {
                        float sz = 6.0f;
                        DrawTriangle({sp.x, sp.y - sz}, {sp.x + sz, sp.y}, {sp.x - sz, sp.y}, baseColor);
                        DrawTriangle({sp.x - sz, sp.y}, {sp.x + sz, sp.y}, {sp.x, sp.y + sz}, baseColor);
                        DrawTriangleLines({sp.x, sp.y - sz}, {sp.x + sz, sp.y}, {sp.x - sz, sp.y}, Color{255, 245, 180, 200});
                        DrawTriangleLines({sp.x - sz, sp.y}, {sp.x + sz, sp.y}, {sp.x, sp.y + sz}, Color{255, 245, 180, 200});
                    }
                } else {
                    DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, baseColor);
                    const char* q = "?";
                    DrawText(q, (int)sp.x - MeasureText(q, 10) / 2, (int)sp.y - 5, 10, baseColor);
                }

                if (selected) {
                    const char* typeStr = blip.discovered
                        ? (blip.isPlanet ? "PLANET" : "STATION")
                        : (blip.isPlanet ? "UNKNOWN PLANET" : "UNKNOWN STATION");
                    int tw = MeasureText(typeStr, 11);
                    DrawText(typeStr, (int)sp.x - tw / 2, (int)sp.y - 24, 11, Color{200, 230, 200, 230});

                    char distLabel[32];
                    std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                    int dw = MeasureText(distLabel, 10);
                    DrawText(distLabel, (int)sp.x - dw / 2, (int)sp.y - 13, 10, Color{150, 180, 150, 200});

                    Rectangle warpBtn = { sp.x - 42.0f, sp.y + 18.0f, 84.0f, 26.0f };
                    if (inRange) {
                        bool hov = CheckCollisionPointRec(mouse, warpBtn);
                        DrawRectangleRec(warpBtn, hov ? Color{0,120,60,230} : Color{0,60,30,200});
                        DrawRectangleLinesEx(warpBtn, 1.5f, hov ? Color{0,220,100,255} : Color{0,150,70,200});
                        const char* wl = "WARP";
                        DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 14)) / 2.0f),
                                 (int)(warpBtn.y + (warpBtn.height - 14) / 2.0f), 14, WHITE);
                    } else if (!hasHyperdrive) {
                        DrawRectangleRec(warpBtn, Color{30,15,10,180});
                        DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,60,20,180});
                        const char* wl = "NO DRIVE";
                        DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 11)) / 2.0f),
                                 (int)(warpBtn.y + (warpBtn.height - 11) / 2.0f), 11, Color{200,120,60,200});
                    } else {
                        DrawRectangleRec(warpBtn, Color{30,10,10,180});
                        DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,30,30,180});
                        const char* wl = "OUT OF RANGE";
                        DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 9)) / 2.0f),
                                 (int)(warpBtn.y + (warpBtn.height - 9) / 2.0f), 9, Color{200,80,60,200});
                    }
                }
            }
        }

        float pulse = sinf(_time * 3.0f) * 0.5f + 0.5f;
        float pr = 4.0f + pulse * 2.0f;
        DrawCircleV(playerSP, pr, Color{80, 255, 120, 230});
        DrawCircleLines((int)playerSP.x, (int)playerSP.y, pr + 4.0f, Color{60, 200, 100, (unsigned char)(90 + (int)(pulse * 80))});
        const char* youLabel = "YOU";
        DrawText(youLabel, (int)playerSP.x - MeasureText(youLabel, 9) / 2, (int)playerSP.y - 18, 9, Color{100, 230, 130, 210});
    }
    else if (tier == Tier::Galaxy) {
        RefreshMapCache(L.map, tier);
        const auto& interactive = _cacheInteractive;
        const auto& background  = _cacheBackground;
        // Whether neighboring stars are far enough apart on screen for a
        // name label not to overlap the next one over — see kMinNameSpacingPx.
        bool namesFit = (_cacheWorldTileSize * proj.scale) >= kMinNameSpacingPx;

        // Hyperdrive reach, drawn as a ring regardless of what sensors reveal
        // inside it — you always know how far you could jump, independent of
        // whether you can currently see anything out there (see
        // BuildInteractiveBlips/BuildBackgroundDots for the sensor-gated fog).
        if (hasHyperdrive) {
            Vector2 curSP = proj.Project(_mapData.currentSystemPos);
            float   screenRange = _mapData.hyperdriveRange * proj.scale;
            DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange + 1.0f, Color{0, 180, 255, 25});
            DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange,        Color{0, 200, 255, 70});
            DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange - 1.0f, Color{0, 180, 255, 30});
        }

        for (const StarSystem& sys : background) {
            Vector2 sp = proj.Project(sys.galacticPos);
            if (!CheckCollisionPointRec(sp, L.map)) continue;

            bool selected = (sys.id == _selectedSystemId);
            // Background dots are always undiscovered by construction — any
            // discovered/current system on screen is already promoted into
            // `interactive` below (see BuildInteractiveBlips), so these are
            // always the "unvisited" color, matching the Universe tier's
            // travel-status language rather than a separate decorative shade.
            if (!selected) {
                DrawCircleV(sp, 2.2f, kColUnvisited);
                continue;
            }

            // Selected background dot gets the same treatment as a selected
            // "undiscovered" interactive blip below — it's just not close
            // enough to home or in jump range to have been promoted into
            // that list (see BuildInteractiveBlips).
            Color baseColor = kColUnvisited;
            DrawCircleLines((int)sp.x, (int)sp.y, 12.0f, Color{0,220,120,200});
            DrawCircleLines((int)sp.x, (int)sp.y, 13.0f, Color{0,180,100,120});
            DrawCircleV(sp, 3.5f, baseColor);
            const char* q = "?";
            DrawText(q, (int)sp.x - MeasureText(q, 10) / 2, (int)sp.y - 5, 10, baseColor);

            float dist = Vector2Distance(_mapData.currentSystemPos, sys.galacticPos);
            bool directInRange = hasHyperdrive && dist <= _mapData.hyperdriveRange;
            std::vector<unsigned int> chain = directInRange
                ? std::vector<unsigned int>{ sys.id } : ComputeChainPath(sys.id);
            bool reachable = !chain.empty();

            char distLabel[32];
            std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
            DrawText(distLabel, (int)sp.x - MeasureText(distLabel, 10) / 2, (int)sp.y - 20, 10, Color{150, 180, 150, 200});

            // Background dots are always undiscovered by construction — see
            // BuildInteractiveBlips.
            float intelY = ComputeInfoBlockEnd(sys, /*discovered=*/false, sp.x, sp.y + 6.0f, /*draw=*/true);

            Rectangle warpBtn = { sp.x - 42.0f, std::max(sp.y + 24.0f, intelY + 4.0f), 84.0f, 26.0f };
            if (reachable) {
                bool hov = CheckCollisionPointRec(mouse, warpBtn);
                DrawRectangleRec(warpBtn, hov ? Color{0,120,60,230} : Color{0,60,30,200});
                DrawRectangleLinesEx(warpBtn, 1.5f, hov ? Color{0,220,100,255} : Color{0,150,70,200});
                char wl[24];
                if (chain.size() > 1) std::snprintf(wl, sizeof(wl), "CHAIN x%d", (int)chain.size());
                else std::snprintf(wl, sizeof(wl), "WARP");
                DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 14)) / 2.0f),
                         (int)(warpBtn.y + (warpBtn.height - 14) / 2.0f), 14, WHITE);
            } else {
                DrawRectangleRec(warpBtn, Color{30,10,10,180});
                DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,30,30,180});
                const char* wl = hasHyperdrive ? "OUT OF RANGE" : "NO DRIVE";
                DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 9)) / 2.0f),
                         (int)(warpBtn.y + (warpBtn.height - 9) / 2.0f), 9, Color{200,80,60,200});
            }
        }

        for (const GalaxyBlip& b : interactive) {
            Vector2 sp = proj.Project(b.galacticPos);
            if (!CheckCollisionPointRec(sp, L.map)) continue;

            bool selected = (b.id == _selectedSystemId);
            Color baseColor = b.isCurrent  ? kColCurrent
                             : b.discovered ? kColVisited
                                            : kColUnvisited;

            if (selected) {
                DrawCircleLines((int)sp.x, (int)sp.y, 12.0f, Color{0,220,120,200});
                DrawCircleLines((int)sp.x, (int)sp.y, 13.0f, Color{0,180,100,120});
            }
            DrawCircleV(sp, b.isCurrent ? 5.0f : 3.5f, baseColor);
            if (b.discovered || b.isCurrent) {
                // Faction/danger overlay: a thin relation-colored ring around
                // any controlled discovered (or current) system, always
                // visible — not gated on selection — so the whole Galaxy tier
                // reads as a danger map at a glance. Sits between the base dot
                // (radius 3.5/5) and the selection ring (radius 12/13) so it
                // never collides with either. Uncontrolled systems get no
                // ring, same "absence = nothing there" language the plain
                // undiscovered dots already use. Same RED/GREEN/SKYBLUE
                // convention as in-system NPC ship tinting (see RelationColor).
                if (auto full = StarSystemRegistry::ById(b.id); full && full->isControlled) {
                    Relation rel = ReputationRegistry::PlayerRelation(full->controllingFaction);
                    float ringR = (b.isCurrent ? 5.0f : 3.5f) + 3.0f;
                    DrawCircleLines((int)sp.x, (int)sp.y, ringR, RelationColor(rel));
                }
                if (namesFit || selected)
                    DrawText(b.name.c_str(), (int)sp.x + 8, (int)sp.y - 6, 11, Color{200, 215, 230, 220});
            } else {
                const char* q = "?";
                DrawText(q, (int)sp.x - MeasureText(q, 10) / 2, (int)sp.y - 5, 10, baseColor);
            }

            if (selected && !b.isCurrent) {
                float dist = Vector2Distance(_mapData.currentSystemPos, b.galacticPos);
                bool directInRange = hasHyperdrive && dist <= _mapData.hyperdriveRange;
                std::vector<unsigned int> chain = directInRange
                    ? std::vector<unsigned int>{ b.id } : ComputeChainPath(b.id);
                bool reachable = !chain.empty();

                char distLabel[32];
                std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                DrawText(distLabel, (int)sp.x - MeasureText(distLabel, 10) / 2, (int)sp.y - 20, 10, Color{150, 180, 150, 200});

                // Sensor-tier intel for undiscovered systems (see
                // DrawSensorIntel/PreviewSensorIntel), or the faction/relation
                // line for discovered controlled ones (spelling out the ring
                // overlay drawn above, since color alone doesn't name the
                // faction) — ComputeInfoBlockEnd picks the right one.
                float intelY = sp.y + 6.0f;
                if (auto full = StarSystemRegistry::ById(b.id))
                    intelY = ComputeInfoBlockEnd(*full, b.discovered, sp.x, intelY, /*draw=*/true);

                // Undiscovered is not a separate button state: an undiscovered
                // system in range is exactly how you're meant to discover it
                // (see Update()'s click handler), so it gets a normal WARP
                // button too, matching the System tier's "unknown planet/
                // station, still warpable" behavior. A target beyond direct
                // range but reachable through a chain of discovered waypoints
                // (see ComputeChainPath) gets a "CHAIN xN" button instead of
                // OUT OF RANGE.
                Rectangle warpBtn = { sp.x - 42.0f, std::max(sp.y + 24.0f, intelY + 4.0f), 84.0f, 26.0f };
                if (reachable) {
                    bool hov = CheckCollisionPointRec(mouse, warpBtn);
                    DrawRectangleRec(warpBtn, hov ? Color{0,120,60,230} : Color{0,60,30,200});
                    DrawRectangleLinesEx(warpBtn, 1.5f, hov ? Color{0,220,100,255} : Color{0,150,70,200});
                    char wl[24];
                    if (chain.size() > 1) std::snprintf(wl, sizeof(wl), "CHAIN x%d", (int)chain.size());
                    else std::snprintf(wl, sizeof(wl), "WARP");
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 14)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 14) / 2.0f), 14, WHITE);
                } else {
                    DrawRectangleRec(warpBtn, Color{30,10,10,180});
                    DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,30,30,180});
                    const char* wl = hasHyperdrive ? "OUT OF RANGE" : "NO DRIVE";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 9)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 9) / 2.0f), 9, Color{200,80,60,200});
                }
            }
        }
    }
    else if (tier == Tier::GalaxyShape) {
        // Each surviving lattice cell is drawn as a round dot rather than a
        // flat-shaded rectangle — reads as a starfield/cluster rather than a
        // pixel mosaic, and matches the dot language every other tier already
        // uses (System tier blips, Galaxy tier stars, Universe tier galaxy
        // icons). Both size and brightness scale with local density, so dense
        // regions (core, spiral arms) read as bigger, more overlapping,
        // brighter dots that blend into a solid mass, while sparse regions
        // (rim, inter-arm gaps) read as small, dim, separated points —
        // exactly the "dense cluster fading to scattered stars" look a real
        // galaxy has, instead of a uniform grid of identical tiles.
        RefreshMapCache(L.map, tier);
        const auto& interactive = _cacheInteractive;
        const auto& background  = _cacheBackground;
        // Circles cover less of their cell than a square (no corners), and
        // don't tile edge-to-edge the way squares can — sized up from the
        // tile-overlap version's 15% to 30% so dense regions still read as
        // a continuous mass rather than a lattice of visible gaps.
        float screenDiameter = std::max(1.0f, _cacheWorldTileSize * proj.scale) * 1.3f;

        auto DrawDot = [&](Vector2 sp, float density) {
            float radius = screenDiameter * 0.5f * (0.55f + 0.45f * density);
            DrawCircleV(sp, radius, Fade(Color{170, 185, 235, 255}, 0.35f + 0.65f * density));
        };

        // Dots are placed at the un-jittered cell center, not galacticPos —
        // jitter is a small fraction of a tile spanning many cells, but a
        // large one once the tile shrinks to a single cell (dense regions,
        // where the sample stride drops toward 1), which is exactly what
        // was leaving gaps between otherwise-adjacent dots in the core.
        // Density comes straight from StarSystem::density, cached from the
        // existence-roll evaluation in Generate() — no second density call.
        for (const StarSystem& sys : background) {
            Vector2 sp = proj.Project(sys.cellCenter);
            if (CheckCollisionPointRec(sp, L.map)) DrawDot(sp, sys.density);
        }
        for (const GalaxyBlip& b : interactive) {
            Vector2 tilePos = b.isCurrent ? b.galacticPos : b.cellCenter;
            Vector2 sp = proj.Project(tilePos);
            if (!CheckCollisionPointRec(sp, L.map)) continue;
            if (b.isCurrent) {
                BeginBlendMode(BLEND_ADDITIVE);
                DrawCircleV(sp, 3.0f, Color{140, 255, 170, 255});
                EndBlendMode();
            } else {
                DrawDot(sp, StarSystemRegistry::Density(b.cellCenter));
            }
        }

        const char* hint = "SCROLL IN TO NAVIGATE INDIVIDUAL SYSTEMS";
        DrawText(hint, (int)(L.map.x + (L.map.width - MeasureText(hint, 12)) / 2), (int)(L.map.y + L.map.height - 22), 12, Color{140,150,170,160});
    }
    else if (tier == Tier::Universe) {
        RefreshMapCache(L.map, tier);
        const auto& galaxies = _cacheGalaxies;
        auto uproj = ComputeUniverseProjection(L.map);
        Vector2 curGalaxyPos = UniverseRegistry::Generate(_mapData.currentGalaxyId).position;
        // Same spacing heuristic as the Galaxy tier's stars — see kMinNameSpacingPx.
        bool namesFit = (_cacheUniverseTileSize * uproj.scale) >= kMinNameSpacingPx;

        for (const UniverseRegistry::GalaxyInfo& g : galaxies) {
            Vector2 sp = uproj.Project(g.position);
            if (!CheckCollisionPointRec(sp, L.map)) continue;

            bool  isCurrent = (g.id == _mapData.currentGalaxyId);
            bool  visited   = isCurrent || std::find(_mapData.visitedGalaxyIds.begin(),
                                  _mapData.visitedGalaxyIds.end(), g.id) != _mapData.visitedGalaxyIds.end();
            bool  selected  = (g.id == _selectedGalaxyId);
            float baseR     = isCurrent ? 16.0f : 9.0f;
            // Clamp the minor axis so a very elongated galaxy's icon never
            // collapses down to an invisible sliver.
            float radiusV = std::max(3.0f, baseR * g.shapeParams.aspectRatio);
            Color col = isCurrent ? kColCurrent : visited ? kColVisited : kColUnvisited;

            if (selected) {
                DrawCircleLines((int)sp.x, (int)sp.y, baseR + 6.0f, Color{0,220,120,200});
                DrawCircleLines((int)sp.x, (int)sp.y, baseR + 7.0f, Color{0,180,100,120});
            }

            if (isCurrent) {
                // "You are here" — a bright pulsing ring plus a persistent
                // label, the same visual language as the System tier's YOU
                // marker, so the player's own galaxy is unmistakable at a
                // glance instead of just being a slightly different color.
                float pulse = sinf(_time * 3.0f) * 0.5f + 0.5f;
                float pr    = baseR + 6.0f + pulse * 4.0f;
                BeginBlendMode(BLEND_ADDITIVE);
                DrawRotatedEllipse(sp, baseR, radiusV, g.shapeParams.axisRotation, col);
                EndBlendMode();
                DrawCircleLines((int)sp.x, (int)sp.y, pr, Color{80, 255, 120, (unsigned char)(90 + (int)(pulse * 80))});
                const char* label = "YOUR GALAXY";
                DrawText(label, (int)sp.x - MeasureText(label, 10) / 2, (int)sp.y - (int)pr - 16, 10, Color{140, 255, 170, 230});
            } else {
                DrawRotatedEllipse(sp, baseR, radiusV, g.shapeParams.axisRotation, col);
            }

            if (namesFit || selected) {
                DrawText(g.name.c_str(), (int)sp.x - MeasureText(g.name.c_str(), 12) / 2,
                         (int)sp.y + (int)baseR + 8, 12, Color{200, 255, 215, 230});
            }

            if (selected && !isCurrent) {
                float dist = Vector2Distance(curGalaxyPos, g.position);
                bool inRange = hasHyperdrive && dist <= _mapData.hyperdriveRange;

                char distLabel[32];
                std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                DrawText(distLabel, (int)sp.x - MeasureText(distLabel, 10) / 2, (int)sp.y - 20, 10, Color{150, 180, 150, 200});

                Rectangle warpBtn = { sp.x - 42.0f, sp.y + 14.0f, 84.0f, 26.0f };
                if (inRange) {
                    bool hov = CheckCollisionPointRec(mouse, warpBtn);
                    DrawRectangleRec(warpBtn, hov ? Color{0,120,60,230} : Color{0,60,30,200});
                    DrawRectangleLinesEx(warpBtn, 1.5f, hov ? Color{0,220,100,255} : Color{0,150,70,200});
                    const char* wl = "WARP";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 14)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 14) / 2.0f), 14, WHITE);
                } else {
                    DrawRectangleRec(warpBtn, Color{30,10,10,180});
                    DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,30,30,180});
                    const char* wl = hasHyperdrive ? "OUT OF RANGE" : "NO DRIVE";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 9)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 9) / 2.0f), 9, Color{200,80,60,200});
                }

                // Explains why an unvisited galaxy is targetable at all despite
                // sensors never having reached it — see Hyperdrive_VoidPiercer's
                // comment: every galaxy's home system is a universally-charted
                // arrival beacon, unlike an unscanned star system.
                if (!visited) {
                    const char* note = "CHARTED HOME BEACON - CONTENTS UNKNOWN";
                    DrawText(note, (int)sp.x - MeasureText(note, 8) / 2,
                             (int)(warpBtn.y + warpBtn.height + 4), 8, Color{140, 160, 190, 200});
                }
            }
        }

        const char* hint = "SCROLL IN TO RETURN TO YOUR GALAXY";
        DrawText(hint, (int)(L.map.x + (L.map.width - MeasureText(hint, 12)) / 2), (int)(L.map.y + L.map.height - 22), 12, Color{140,150,170,160});
    }
    else { // UniverseShape — mirrors the GalaxyShape tier, one scale up:
        // round dots sized/shaded by the universe's own density field
        // (UniverseRegistry::Density) instead of individual galaxy icons.
        RefreshMapCache(L.map, tier);
        const auto& galaxies = _cacheGalaxies;
        auto uproj = ComputeUniverseProjection(L.map);
        // Same overlap reasoning as the GalaxyShape tier's screenDiameter.
        float screenDiameter = std::max(1.0f, _cacheUniverseTileSize * uproj.scale) * 1.3f;

        auto DrawGalaxyDot = [&](Vector2 sp, float density) {
            float radius = screenDiameter * 0.5f * (0.55f + 0.45f * density);
            DrawCircleV(sp, radius, Fade(Color{170, 185, 235, 255}, 0.35f + 0.65f * density));
        };

        // Dots are placed at the un-jittered cellCenter, not position, for
        // the same gapless-tiling reason as GalaxyShape's stars.
        for (const UniverseRegistry::GalaxyInfo& g : galaxies) {
            Vector2 sp = uproj.Project(g.cellCenter);
            if (!CheckCollisionPointRec(sp, L.map)) continue;
            if (g.id == _mapData.currentGalaxyId) {
                BeginBlendMode(BLEND_ADDITIVE);
                DrawCircleV(sp, 3.0f, kColCurrent);
                EndBlendMode();
            } else {
                DrawGalaxyDot(sp, g.density);
            }
        }

        const char* hint = "SCROLL IN TO NAVIGATE INDIVIDUAL GALAXIES";
        DrawText(hint, (int)(L.map.x + (L.map.width - MeasureText(hint, 12)) / 2), (int)(L.map.y + L.map.height - 22), 12, Color{140,150,170,160});
    }

    EndScissorMode();

    // ── Bottom panel ─────────────────────────────────────────────────────────
    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);

    Rectangle modulesBtn, storageBtn, escortsBtn, ranksBtn;
    Rectangle resumeBtn, saveBtn, loadBtn, menuBtn;
    CalcButtons(L.bot, modulesBtn, storageBtn, escortsBtn, ranksBtn,
                resumeBtn, saveBtn, loadBtn, menuBtn);

    DrawMapBtn(modulesBtn, "MODULES");
    DrawMapBtn(storageBtn, "STORAGE");
    DrawMapBtn(escortsBtn, "ESCORTS", _mapData.wingmanCount > 0);
    DrawMapBtn(ranksBtn,   "RANKS");

    DrawMapBtn(resumeBtn, "RESUME  [ESC]");
    DrawMapBtn(saveBtn,   "SAVE");
    DrawMapBtn(loadBtn,   "LOAD");
    DrawMapBtn(menuBtn,   "MAIN MENU");

    if (_saveFeedbackTimer > 0.0f) {
        const char* msg = "GAME SAVED";
        float alpha = std::min(1.0f, _saveFeedbackTimer / 0.4f);
        Color col = { HudGood.r, HudGood.g, HudGood.b, (unsigned char)(alpha * 255) };
        DrawText(msg, L.mx + L.mw - MeasureText(msg, 16) - 14, (int)L.bot.y + 6, 16, col);
    }

    _savePicker.Draw();
    _saveWriter.Draw();
}

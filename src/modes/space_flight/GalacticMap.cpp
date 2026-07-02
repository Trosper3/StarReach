#include "GalacticMap.h"
#include "data/registry/StarSystemRegistry.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <unordered_set>
#include <utility>

static bool GIsHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool GIsClk(Rectangle r) { return GIsHov(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

static void DrawGalBtn(Rectangle r, const char* label) {
    Color bg  = GIsHov(r) ? Color{20,50,90,230}  : Color{8,18,38,210};
    Color bdr = GIsHov(r) ? Color{60,140,220,255} : Color{30,80,160,180};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1.0f, bdr);
    int tw = MeasureText(label, 15);
    DrawText(label, (int)(r.x + (r.width  - tw) / 2.0f),
                    (int)(r.y + (r.height - 15) / 2.0f), 15, WHITE);
}

struct GalLayout {
    int       mx, mw;
    Rectangle title, map, bot;
};

static GalLayout CalcGalLayout(int sw, int sh) {
    GalLayout L;
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

static void CalcGalButtons(const Rectangle& bot, Rectangle& sysMap, Rectangle& mainMenu) {
    float bh  = 44.0f;
    float bw  = (bot.width - 60.0f) / 2.0f;
    float by  = bot.y + (bot.height - bh) / 2.0f;
    float x0  = bot.x + 15.0f;
    float gap = bot.width - 30.0f - bw * 2.0f;
    sysMap   = { x0,          by, bw, bh };
    mainMenu = { x0 + bw + gap, by, bw, bh };
}

// ── Camera tuning ────────────────────────────────────────────────────────────
static constexpr float kDefaultViewWidth = 350000.0f; // world units across at Open()/Home
static constexpr float kMaxZoomViewWidth = 20000.0f;   // world units across at max zoom-in
static constexpr float kZoomStep         = 1.15f;
static constexpr float kPanSpeedPx       = 500.0f;     // screen px/sec at scale=1
static constexpr int   kDrawBudget       = 6000;       // decorative background points/frame
static constexpr int   kRangeQueryBudget = 2000;       // undiscovered-in-range lookup

GalacticMap::MapProjection GalacticMap::ComputeProjection(const Rectangle& mapRect) const {
    MapProjection p;
    p.mapCX = mapRect.x + mapRect.width  * 0.5f;
    p.mapCY = mapRect.y + mapRect.height * 0.5f;
    p.cx    = _camCenter.x;
    p.cy    = _camCenter.y;
    p.scale = _camScale;
    return p;
}

Rectangle GalacticMap::VisibleWorldRect(const Rectangle& mapRect) const {
    float halfW = (mapRect.width  * 0.5f) / _camScale;
    float halfH = (mapRect.height * 0.5f) / _camScale;
    return { _camCenter.x - halfW, _camCenter.y - halfH, halfW * 2.0f, halfH * 2.0f };
}

void GalacticMap::Open() {
    isOpen      = true;
    _time       = 0.0f;
    _selectedId = 0;
    _dragging   = false;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcGalLayout(sw, sh);
    _camCenter = _mapData.currentSystemPos;
    _camScale  = L.map.width / kDefaultViewWidth;
}

void GalacticMap::HandleCameraInput(float dt, const Rectangle& mapRect) {
    float minScale = mapRect.width / StarSystemRegistry::kGalaxySpan;
    float maxScale = mapRect.width / kMaxZoomViewWidth;
    if (minScale > maxScale) std::swap(minScale, maxScale);

    float panSpeed = kPanSpeedPx / _camScale;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) _camCenter.x -= panSpeed * dt;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) _camCenter.x += panSpeed * dt;
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) _camCenter.y -= panSpeed * dt;
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) _camCenter.y += panSpeed * dt;
    if (IsKeyPressed(KEY_HOME)) {
        _camCenter = _mapData.currentSystemPos;
        _camScale  = mapRect.width / kDefaultViewWidth;
    }

    Vector2 mouse = GetMousePosition();
    float   wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(mouse, mapRect)) {
        Vector2 worldBefore = ComputeProjection(mapRect).Unproject(mouse);

        float factor = (wheel > 0.0f) ? kZoomStep : (1.0f / kZoomStep);
        _camScale = std::clamp(_camScale * factor, minScale, maxScale);

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

std::vector<GalacticBlip> GalacticMap::BuildInteractiveBlips(const Rectangle& mapRect) const {
    std::vector<GalacticBlip> out;
    if (_mapData.currentSystemId == 0) return out;

    Rectangle viewRect = VisibleWorldRect(mapRect);
    auto InView = [&](Vector2 p) {
        return p.x >= viewRect.x && p.x <= viewRect.x + viewRect.width &&
               p.y >= viewRect.y && p.y <= viewRect.y + viewRect.height;
    };

    if (auto cur = StarSystemRegistry::ById(_mapData.currentSystemId)) {
        out.push_back({ cur->id, StarSystemRegistry::NameOf(cur->seed), cur->galacticPos, true, true });
    }

    for (unsigned int id : _mapData.discoveredSystemIds) {
        if (id == _mapData.currentSystemId) continue;
        auto sys = StarSystemRegistry::ById(id);
        if (!sys || !InView(sys->galacticPos)) continue;
        out.push_back({ sys->id, StarSystemRegistry::NameOf(sys->seed), sys->galacticPos, true, false });
    }

    // Undiscovered systems within current hyperdrive range — an independent,
    // tightly-bounded query (jump range is always far smaller than the full
    // galaxy) so these are never dropped by the background field's LOD thinning.
    if (_mapData.hyperdriveRange > 0.0f) {
        float     r         = _mapData.hyperdriveRange;
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
            out.push_back({ sys.id, {}, sys.galacticPos, false, false });
        }
    }

    return out;
}

std::vector<Vector2> GalacticMap::BuildBackgroundDots(const Rectangle& mapRect,
                                                        const std::vector<GalacticBlip>& interactive) const {
    std::unordered_set<unsigned int> known;
    known.reserve(interactive.size() * 2);
    for (const GalacticBlip& b : interactive) known.insert(b.id);

    std::vector<Vector2> out;
    Rectangle viewRect = VisibleWorldRect(mapRect);
    for (const StarSystem& sys : StarSystemRegistry::QueryRegion(viewRect, kDrawBudget)) {
        if (known.find(sys.id) != known.end()) continue;
        out.push_back(sys.galacticPos);
    }
    return out;
}

GalacticMapAction GalacticMap::Update(float dt) {
    _time += dt;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcGalLayout(sw, sh);
    Rectangle sysMapBtn, mainMenuBtn;
    CalcGalButtons(L.bot, sysMapBtn, mainMenuBtn);

    if (GIsClk(sysMapBtn))   return GalacticMapAction::OpenSystemMap;
    if (GIsClk(mainMenuBtn)) return GalacticMapAction::GoMainMenu;

    HandleCameraInput(dt, L.map);

    if (_mapData.currentSystemId == 0) return GalacticMapAction::None;

    auto    proj  = ComputeProjection(L.map);
    Vector2 mouse = GetMousePosition();

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        std::vector<GalacticBlip> blips = BuildInteractiveBlips(L.map);

        // Warp button for the currently selected blip.
        for (const GalacticBlip& blip : blips) {
            if (blip.id != _selectedId || blip.isCurrent) continue;
            float dist    = Vector2Distance(_mapData.currentSystemPos, blip.galacticPos);
            bool  inRange = _mapData.hyperdriveRange > 0.0f && dist <= _mapData.hyperdriveRange;
            if (inRange) {
                Vector2   sp      = proj.Project(blip.galacticPos);
                Rectangle warpBtn = { sp.x - 46.0f, sp.y + 20.0f, 92.0f, 26.0f };
                if (CheckCollisionPointRec(mouse, warpBtn)) {
                    _warpTargetId = blip.id;
                    return GalacticMapAction::WarpToSystem;
                }
            }
            break;
        }

        bool clickedBlip = false;
        for (const GalacticBlip& blip : blips) {
            Vector2 sp = proj.Project(blip.galacticPos);
            if (CheckCollisionPointCircle(mouse, sp, 14.0f) && CheckCollisionPointRec(mouse, L.map)) {
                _selectedId  = (_selectedId == blip.id) ? 0 : blip.id;
                clickedBlip  = true;
                break;
            }
        }
        if (!clickedBlip && CheckCollisionPointRec(mouse, L.map))
            _selectedId = 0;
    }

    return GalacticMapAction::None;
}

static void DrawStarShape(Vector2 c, float outerR, float innerR, Color fill, Color outline) {
    // 4-point star
    static const int kPts = 4;
    for (int i = 0; i < kPts; ++i) {
        float a0 = (i * 90.0f - 90.0f) * DEG2RAD;
        float a1 = ((i * 90.0f + 45.0f) - 90.0f) * DEG2RAD;
        float a2 = ((i * 90.0f + 90.0f) - 90.0f) * DEG2RAD;
        Vector2 outer = { c.x + outerR * cosf(a0), c.y + outerR * sinf(a0) };
        Vector2 mid1  = { c.x + innerR * cosf(a1), c.y + innerR * sinf(a1) };
        Vector2 mid2  = { c.x + innerR * cosf(a2), c.y + innerR * sinf(a2) };
        DrawTriangle(outer, mid1, c,   fill);
        DrawTriangle(mid1,  mid2, c,   fill);
        DrawTriangle(outer, mid2, mid1, fill);
        DrawTriangleLines(outer, mid1, c,   outline);
        DrawTriangleLines(mid1,  mid2, c,   outline);
    }
}

void GalacticMap::Draw() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcGalLayout(sw, sh);

    DrawRectangle(0, 0, sw, sh, Color{0, 0, 12, 220});

    // ── Title bar ─────────────────────────────────────────────────────────────
    DrawRectangleRec(L.title, Color{6,12,28,245});
    DrawRectangleLinesEx(L.title, 1.0f, Color{30,100,200,200});
    DrawText("GALACTIC NAVIGATION ARRAY", L.mx + 14, (int)L.title.y + 11, 16, Color{60,160,240,255});

    bool hasHyperdrive = _mapData.hyperdriveRange > 0.0f;
    if (hasHyperdrive) {
        char rangeLabel[64];
        std::snprintf(rangeLabel, sizeof(rangeLabel), "[ JUMP RANGE: %.0f u ]", _mapData.hyperdriveRange);
        int tw = MeasureText(rangeLabel, 14);
        DrawText(rangeLabel, L.mx + L.mw - tw - 14, (int)L.title.y + 13, 14, Color{60,180,240,255});
    } else {
        const char* statusLbl = "[ NO HYPERDRIVE EQUIPPED ]";
        DrawText(statusLbl, L.mx + L.mw - MeasureText(statusLbl, 14) - 14,
                 (int)L.title.y + 13, 14, Color{220,100,35,255});
    }

    // ── Map panel ────────────────────────────────────────────────────────────
    DrawRectangleRec(L.map, Color{1,2,8,252});
    DrawRectangleLinesEx(L.map, 1.5f, Color{30,100,200,200});

    BeginScissorMode((int)L.map.x, (int)L.map.y, (int)L.map.width, (int)L.map.height);

    int mpx = (int)L.map.x, mpy = (int)L.map.y;
    int mpw = (int)L.map.width, mph = (int)L.map.height;
    for (int x = mpx + 60; x < mpx + mpw; x += 60)
        DrawLine(x, mpy, x, mpy + mph, Color{15,25,50,50});
    for (int y = mpy + 40; y < mpy + mph; y += 40)
        DrawLine(mpx, y, mpx + mpw, y, Color{15,25,50,50});

    if (_mapData.currentSystemId == 0) {
        const char* msg = "NO STAR SYSTEMS CHARTED";
        DrawText(msg, mpx + (mpw - MeasureText(msg, 16)) / 2, mpy + mph / 2 - 8, 16, Color{60,80,120,200});
        EndScissorMode();
        DrawRectangleRec(L.bot, Color{6,10,24,245});
        DrawRectangleLinesEx(L.bot, 1.0f, Color{30,100,200,200});
        return;
    }

    auto proj = ComputeProjection(L.map);
    std::vector<GalacticBlip> blips = BuildInteractiveBlips(L.map);
    std::vector<Vector2>      dots  = BuildBackgroundDots(L.map, blips);

    // Hyperdrive range ring
    if (hasHyperdrive) {
        Vector2 curSP = proj.Project(_mapData.currentSystemPos);
        float screenRange = _mapData.hyperdriveRange * proj.scale;
        DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange + 1.0f, Color{0, 160, 220, 30});
        DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange,        Color{0, 190, 255, 95});
        DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange - 1.0f, Color{0, 160, 220, 45});
    }

    // Decorative background starfield — the vast bulk of the galaxy. No text,
    // no click handling; just a brighter-than-before dot so the shape of a
    // huge galaxy actually reads on screen.
    for (const Vector2& wp : dots) {
        Vector2 sp = proj.Project(wp);
        DrawCircleV(sp, 1.4f, Color{130, 150, 210, 175});
    }

    for (int i = 0; i < (int)blips.size(); ++i) {
        const GalacticBlip& blip = blips[i];
        Vector2 sp = proj.Project(blip.galacticPos);

        bool  selected = (blip.id == _selectedId);
        float dist     = blip.isCurrent ? 0.0f
            : Vector2Distance(_mapData.currentSystemPos, blip.galacticPos);
        bool inRange = !blip.isCurrent && hasHyperdrive && dist <= _mapData.hyperdriveRange;

        if (blip.isCurrent) {
            // Pulsing current system marker
            float pulse = sinf(_time * 2.5f) * 0.5f + 0.5f;
            float outerR = 9.0f + pulse * 3.0f;
            float innerR = outerR * 0.42f;
            DrawStarShape(sp, outerR, innerR,
                Color{110, 255, 150, 255},
                Color{180, 255, 210, 230});
            DrawCircleLines((int)sp.x, (int)sp.y, outerR + 5.0f,
                Color{80, 220, 130, (unsigned char)(60 + (int)(pulse * 80))});
            const char* curLbl = "HERE";
            DrawText(curLbl, (int)sp.x - MeasureText(curLbl, 9) / 2,
                (int)sp.y - 22, 9, Color{130, 255, 165, 230});
            const char* sysName = blip.name.c_str();
            DrawText(sysName, (int)sp.x - MeasureText(sysName, 10) / 2,
                (int)sp.y + 14, 10, Color{110, 230, 145, 230});
        } else if (blip.discovered) {
            // Known system star
            Color starCol = inRange
                ? Color{255, 235, 140, 255}
                : Color{175, 165, 115, 220};
            float outerR = selected ? 8.0f : 6.0f;
            float innerR = outerR * 0.42f;
            DrawStarShape(sp, outerR, innerR, starCol,
                inRange ? Color{255, 250, 200, 230} : Color{200, 190, 135, 190});

            if (selected) {
                DrawCircleLines((int)sp.x, (int)sp.y, 14.0f,
                    inRange ? Color{0,220,140,220} : Color{220,100,80,220});
                const char* sysName = blip.name.c_str();
                int snw = MeasureText(sysName, 11);
                DrawText(sysName, (int)sp.x - snw / 2, (int)sp.y - 26, 11,
                    Color{235, 235, 200, 240});
                char distLabel[48];
                std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                int dlw = MeasureText(distLabel, 10);
                DrawText(distLabel, (int)sp.x - dlw / 2, (int)sp.y - 14, 10,
                    Color{180, 180, 145, 220});

                Rectangle warpBtn = { sp.x - 46.0f, sp.y + 20.0f, 92.0f, 26.0f };
                if (inRange) {
                    bool hov = CheckCollisionPointRec(GetMousePosition(), warpBtn);
                    Color btnBg  = hov ? Color{0,100,50,230}  : Color{0,50,25,200};
                    Color btnBdr = hov ? Color{0,200,100,255} : Color{0,130,60,200};
                    DrawRectangleRec(warpBtn, btnBg);
                    DrawRectangleLinesEx(warpBtn, 1.5f, btnBdr);
                    const char* wl = "JUMP";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 14)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 14) / 2.0f), 14, WHITE);
                } else if (!hasHyperdrive) {
                    DrawRectangleRec(warpBtn, Color{30,15,10,180});
                    DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,60,20,180});
                    const char* wl = "NO DRIVE";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 11)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 11) / 2.0f), 11,
                             Color{200,120,60,200});
                } else {
                    DrawRectangleRec(warpBtn, Color{30,10,10,180});
                    DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,30,30,180});
                    const char* wl = "OUT OF RANGE";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 9)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 9) / 2.0f), 9,
                             Color{200,80,60,200});
                }
            } else {
                // Hover: show system name
                const char* sysName = blip.name.c_str();
                int snw = MeasureText(sysName, 10);
                DrawText(sysName, (int)sp.x - snw / 2, (int)sp.y + 10, 10,
                    inRange ? Color{230, 225, 170, 230} : Color{170, 160, 120, 190});
            }
        } else {
            // Unknown system in sensor/jump range
            Color dimCol = inRange
                ? Color{190, 160, 90, 220}
                : Color{110, 100, 70, 170};
            DrawCircleLines((int)sp.x, (int)sp.y, 6.0f, dimCol);
            const char* q = "?";
            DrawText(q, (int)sp.x - MeasureText(q, 10) / 2, (int)sp.y - 5, 10, dimCol);

            if (selected) {
                DrawCircleLines((int)sp.x, (int)sp.y, 14.0f,
                    inRange ? Color{0,220,140,220} : Color{220,100,80,220});
                const char* unkLabel = "UNKNOWN SYSTEM";
                int ulw = MeasureText(unkLabel, 11);
                DrawText(unkLabel, (int)sp.x - ulw / 2, (int)sp.y - 26, 11,
                    Color{200, 185, 135, 230});
                char distLabel[48];
                std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                int dlw = MeasureText(distLabel, 10);
                DrawText(distLabel, (int)sp.x - dlw / 2, (int)sp.y - 14, 10,
                    Color{165, 150, 115, 220});

                Rectangle warpBtn = { sp.x - 46.0f, sp.y + 20.0f, 92.0f, 26.0f };
                if (inRange) {
                    bool hov = CheckCollisionPointRec(GetMousePosition(), warpBtn);
                    Color btnBg  = hov ? Color{0,100,50,230}  : Color{0,50,25,200};
                    Color btnBdr = hov ? Color{0,200,100,255} : Color{0,130,60,200};
                    DrawRectangleRec(warpBtn, btnBg);
                    DrawRectangleLinesEx(warpBtn, 1.5f, btnBdr);
                    const char* wl = "JUMP";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 14)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 14) / 2.0f), 14, WHITE);
                } else if (!hasHyperdrive) {
                    DrawRectangleRec(warpBtn, Color{30,15,10,180});
                    DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,60,20,180});
                    const char* wl = "NO DRIVE";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 11)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 11) / 2.0f), 11,
                             Color{200,120,60,200});
                } else {
                    DrawRectangleRec(warpBtn, Color{30,10,10,180});
                    DrawRectangleLinesEx(warpBtn, 1.0f, Color{120,30,30,180});
                    const char* wl = "OUT OF RANGE";
                    DrawText(wl, (int)(warpBtn.x + (warpBtn.width - MeasureText(wl, 9)) / 2.0f),
                             (int)(warpBtn.y + (warpBtn.height - 9) / 2.0f), 9,
                             Color{200,80,60,200});
                }
            }
        }
    }

    EndScissorMode();

    // ── Bottom panel ─────────────────────────────────────────────────────────
    DrawRectangleRec(L.bot, Color{6,10,24,245});
    DrawRectangleLinesEx(L.bot, 1.0f, Color{30,100,200,200});

    Rectangle sysMapBtn, mainMenuBtn;
    CalcGalButtons(L.bot, sysMapBtn, mainMenuBtn);
    DrawGalBtn(sysMapBtn,  "< SYSTEM MAP");
    DrawGalBtn(mainMenuBtn, "MAIN MENU");

    const char* hint = "RIGHT-DRAG / WASD: PAN   WHEEL: ZOOM   HOME: RECENTER";
    DrawText(hint, (int)(L.map.x + L.map.width - MeasureText(hint, 11) - 10),
              (int)L.map.y + 8, 11, Color{80, 140, 200, 190});
}

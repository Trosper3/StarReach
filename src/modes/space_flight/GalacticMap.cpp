#include "GalacticMap.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

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

GalacticMap::MapProjection GalacticMap::ComputeProjection(const Rectangle& mapRect) const {
    MapProjection p;
    p.mapCX = mapRect.x + mapRect.width  * 0.5f;
    p.mapCY = mapRect.y + mapRect.height * 0.5f;

    float minX = _mapData.currentSystemPos.x, maxX = _mapData.currentSystemPos.x;
    float minY = _mapData.currentSystemPos.y, maxY = _mapData.currentSystemPos.y;
    for (const GalacticBlip& b : _mapData.systems) {
        minX = std::min(minX, b.galacticPos.x);
        maxX = std::max(maxX, b.galacticPos.x);
        minY = std::min(minY, b.galacticPos.y);
        maxY = std::max(maxY, b.galacticPos.y);
    }
    p.cx = (minX + maxX) * 0.5f;
    p.cy = (minY + maxY) * 0.5f;

    // Minimum half-extent to always show a reasonable galactic view
    float halfExtX = std::max((maxX - minX) * 0.5f, 50000.0f) * 1.25f;
    float halfExtY = std::max((maxY - minY) * 0.5f, 50000.0f) * 1.25f;

    float scaleX = (mapRect.width  * 0.5f) / halfExtX;
    float scaleY = (mapRect.height * 0.5f) / halfExtY;
    p.scale = std::min(scaleX, scaleY);
    return p;
}

GalacticMapAction GalacticMap::Update(float dt) {
    _time += dt;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcGalLayout(sw, sh);
    Rectangle sysMapBtn, mainMenuBtn;
    CalcGalButtons(L.bot, sysMapBtn, mainMenuBtn);

    if (GIsClk(sysMapBtn))  return GalacticMapAction::OpenSystemMap;
    if (GIsClk(mainMenuBtn)) return GalacticMapAction::GoMainMenu;

    if (!_mapData.systems.empty() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        auto proj = ComputeProjection(L.map);
        Vector2 mouse = GetMousePosition();

        // Check warp button for selected blip
        if (_selectedBlip >= 0 && _selectedBlip < (int)_mapData.systems.size()) {
            const GalacticBlip& blip = _mapData.systems[_selectedBlip];
            if (!blip.isCurrent) {
                float dist = Vector2Distance(_mapData.currentSystemPos, blip.galacticPos);
                bool inRange = _mapData.hyperdriveRange > 0.0f && dist <= _mapData.hyperdriveRange;

                if (inRange) {
                    Vector2 sp = proj.Project(blip.galacticPos);
                    Rectangle warpBtn = { sp.x - 46.0f, sp.y + 20.0f, 92.0f, 26.0f };
                    if (CheckCollisionPointRec(mouse, warpBtn)) {
                        _warpTargetId = blip.id;
                        return GalacticMapAction::WarpToSystem;
                    }
                }
            }
        }

        // Check blip clicks
        bool clickedBlip = false;
        for (int i = 0; i < (int)_mapData.systems.size(); ++i) {
            Vector2 sp = proj.Project(_mapData.systems[i].galacticPos);
            if (CheckCollisionPointCircle(mouse, sp, 14.0f) &&
                CheckCollisionPointRec(mouse, L.map)) {
                _selectedBlip = (_selectedBlip == i) ? -1 : i;
                clickedBlip = true;
                break;
            }
        }
        if (!clickedBlip && CheckCollisionPointRec(mouse, L.map))
            _selectedBlip = -1;
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

    int mpx = (int)L.map.x, mpy = (int)L.map.y;
    int mpw = (int)L.map.width, mph = (int)L.map.height;
    for (int x = mpx + 60; x < mpx + mpw; x += 60)
        DrawLine(x, mpy, x, mpy + mph, Color{15,25,50,50});
    for (int y = mpy + 40; y < mpy + mph; y += 40)
        DrawLine(mpx, y, mpx + mpw, y, Color{15,25,50,50});

    if (_mapData.systems.empty()) {
        const char* msg = "NO STAR SYSTEMS CHARTED";
        DrawText(msg, mpx + (mpw - MeasureText(msg, 16)) / 2, mpy + mph / 2 - 8, 16, Color{60,80,120,200});
    } else {
        auto proj = ComputeProjection(L.map);
        Vector2 curSP = proj.Project(_mapData.currentSystemPos);

        // Hyperdrive range ring
        if (hasHyperdrive) {
            float screenRange = _mapData.hyperdriveRange * proj.scale;
            DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange + 1.0f, Color{0, 160, 220, 25});
            DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange,        Color{0, 180, 255, 75});
            DrawCircleLines((int)curSP.x, (int)curSP.y, screenRange - 1.0f, Color{0, 160, 220, 35});
        }

        // Draw background star dots (decorative)
        for (int i = 0; i < (int)_mapData.systems.size(); ++i) {
            const GalacticBlip& blip = _mapData.systems[i];
            Vector2 sp = proj.Project(blip.galacticPos);
            if (sp.x < L.map.x || sp.x > L.map.x + L.map.width ||
                sp.y < L.map.y || sp.y > L.map.y + L.map.height)
                continue;

            bool selected = (i == _selectedBlip);
            float dist = blip.isCurrent ? 0.0f
                : Vector2Distance(_mapData.currentSystemPos, blip.galacticPos);
            bool inRange = !blip.isCurrent && hasHyperdrive && dist <= _mapData.hyperdriveRange;

            if (blip.isCurrent) {
                // Pulsing current system marker
                float pulse = sinf(_time * 2.5f) * 0.5f + 0.5f;
                float outerR = 9.0f + pulse * 3.0f;
                float innerR = outerR * 0.42f;
                DrawStarShape(sp, outerR, innerR,
                    Color{80, 255, 120, 230},
                    Color{140, 255, 180, 200});
                DrawCircleLines((int)sp.x, (int)sp.y, outerR + 5.0f,
                    Color{60, 200, 100, (unsigned char)(40 + (int)(pulse * 60))});
                const char* curLbl = "HERE";
                DrawText(curLbl, (int)sp.x - MeasureText(curLbl, 9) / 2,
                    (int)sp.y - 22, 9, Color{100, 230, 130, 200});
                const char* sysName = blip.name.c_str();
                DrawText(sysName, (int)sp.x - MeasureText(sysName, 10) / 2,
                    (int)sp.y + 14, 10, Color{80, 200, 110, 200});
            } else if (blip.discovered) {
                // Known system star
                Color starCol = inRange
                    ? Color{220, 200, 100, 255}
                    : Color{120, 115, 80, 200};
                float outerR = selected ? 8.0f : 6.0f;
                float innerR = outerR * 0.42f;
                DrawStarShape(sp, outerR, innerR, starCol,
                    inRange ? Color{255, 240, 160, 200} : Color{160, 155, 100, 160});

                if (selected) {
                    DrawCircleLines((int)sp.x, (int)sp.y, 14.0f,
                        inRange ? Color{0,200,120,200} : Color{200,80,60,200});
                    const char* sysName = blip.name.c_str();
                    int snw = MeasureText(sysName, 11);
                    DrawText(sysName, (int)sp.x - snw / 2, (int)sp.y - 26, 11,
                        Color{220, 220, 180, 230});
                    char distLabel[48];
                    std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                    int dlw = MeasureText(distLabel, 10);
                    DrawText(distLabel, (int)sp.x - dlw / 2, (int)sp.y - 14, 10,
                        Color{160, 160, 130, 200});

                    // Warp button
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
                        inRange ? Color{200, 195, 140, 210} : Color{130, 125, 95, 170});
                }
            } else {
                // Unknown system
                Color dimCol = inRange
                    ? Color{120, 100, 50, 160}
                    : Color{60, 55, 40, 120};
                DrawCircleLines((int)sp.x, (int)sp.y, 6.0f, dimCol);
                const char* q = "?";
                DrawText(q, (int)sp.x - MeasureText(q, 10) / 2, (int)sp.y - 5, 10, dimCol);

                if (selected) {
                    DrawCircleLines((int)sp.x, (int)sp.y, 14.0f,
                        inRange ? Color{0,200,120,200} : Color{200,80,60,200});
                    const char* unkLabel = "UNKNOWN SYSTEM";
                    int ulw = MeasureText(unkLabel, 11);
                    DrawText(unkLabel, (int)sp.x - ulw / 2, (int)sp.y - 26, 11,
                        Color{160, 150, 110, 210});
                    char distLabel[48];
                    std::snprintf(distLabel, sizeof(distLabel), "%.0f u", dist);
                    int dlw = MeasureText(distLabel, 10);
                    DrawText(distLabel, (int)sp.x - dlw / 2, (int)sp.y - 14, 10,
                        Color{130, 120, 90, 190});

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
    }

    // ── Bottom panel ─────────────────────────────────────────────────────────
    DrawRectangleRec(L.bot, Color{6,10,24,245});
    DrawRectangleLinesEx(L.bot, 1.0f, Color{30,100,200,200});

    Rectangle sysMapBtn, mainMenuBtn;
    CalcGalButtons(L.bot, sysMapBtn, mainMenuBtn);
    DrawGalBtn(sysMapBtn,  "< SYSTEM MAP");
    DrawGalBtn(mainMenuBtn, "MAIN MENU");
}

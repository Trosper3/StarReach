#include "SystemMap.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

static bool IsHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool IsClk(Rectangle r) { return IsHov(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

static void DrawMapBtn(Rectangle r, const char* label) {
    Color bg  = IsHov(r) ? Color{50,95,50,230} : Color{15,30,15,210};
    Color bdr = IsHov(r) ? Color{80,210,80,255} : Color{40,130,40,180};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1.0f, bdr);
    int tw = MeasureText(label, 15);
    DrawText(label, (int)(r.x + (r.width  - tw) / 2.0f),
                    (int)(r.y + (r.height - 15) / 2.0f), 15, WHITE);
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

// 5 buttons: RESUME, GALACTIC, SAVE, LOAD, MAIN MENU
static void CalcButtons(const Rectangle& bot,
                         Rectangle& resume, Rectangle& galactic,
                         Rectangle& save,   Rectangle& load, Rectangle& menu) {
    float bh  = 44.0f;
    float bw  = (bot.width - 80.0f) / 5.0f;
    float by  = bot.y + (bot.height - bh) / 2.0f;
    float x0  = bot.x + 15.0f;
    float gap = (bot.width - 30.0f - bw * 5.0f) / 4.0f;
    resume  = { x0,                  by, bw, bh };
    galactic= { x0 + (bw+gap),       by, bw, bh };
    save    = { x0 + (bw+gap)*2.0f,  by, bw, bh };
    load    = { x0 + (bw+gap)*3.0f,  by, bw, bh };
    menu    = { x0 + (bw+gap)*4.0f,  by, bw, bh };
}

SystemMap::MapProjection SystemMap::ComputeProjection(const Rectangle& mapRect) const {
    MapProjection p;
    p.mapCX = mapRect.x + mapRect.width  * 0.5f;
    p.mapCY = mapRect.y + mapRect.height * 0.5f;

    float minX = _mapData.playerPos.x, maxX = _mapData.playerPos.x;
    float minY = _mapData.playerPos.y, maxY = _mapData.playerPos.y;
    for (const MapBlip& b : _mapData.blips) {
        minX = std::min(minX, b.worldPos.x);
        maxX = std::max(maxX, b.worldPos.x);
        minY = std::min(minY, b.worldPos.y);
        maxY = std::max(maxY, b.worldPos.y);
    }
    p.cx = (minX + maxX) * 0.5f;
    p.cy = (minY + maxY) * 0.5f;

    float halfExtX = std::max((maxX - minX) * 0.5f, 2500.0f) * 1.25f;
    float halfExtY = std::max((maxY - minY) * 0.5f, 2500.0f) * 1.25f;

    float scaleX = (mapRect.width  * 0.5f) / halfExtX;
    float scaleY = (mapRect.height * 0.5f) / halfExtY;
    p.scale = std::min(scaleX, scaleY);
    return p;
}

MapAction SystemMap::Update(float dt) {
    _time += dt;
    if (_saveFeedbackTimer > 0.0f) _saveFeedbackTimer -= dt;

    if (_saveWriter.IsOpen()) {
        auto result = _saveWriter.Update();
        if (result == SaveWriter::Result::Saved)
            return MapAction::SaveToFile;
        return MapAction::None;
    }

    if (_savePicker.IsOpen()) {
        auto result = _savePicker.Update();
        if (result == SavePicker::Result::Selected)
            return MapAction::LoadGame;
        return MapAction::None;
    }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcLayout(sw, sh);
    Rectangle resumeBtn, galacticBtn, saveBtn, loadBtn, menuBtn;
    CalcButtons(L.bot, resumeBtn, galacticBtn, saveBtn, loadBtn, menuBtn);

    if (IsClk(resumeBtn))  { Close(); return MapAction::Close;           }
    if (IsClk(galacticBtn))               return MapAction::OpenGalacticMap;
    if (IsClk(saveBtn))    { _saveWriter.Open(); return MapAction::None; }
    if (IsClk(loadBtn))    { _savePicker.Open(); return MapAction::None; }
    if (IsClk(menuBtn))                   return MapAction::GoMainMenu;

    // Blip selection and warp
    if (!_mapData.blips.empty() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        auto proj = ComputeProjection(L.map);
        Vector2 mouse = GetMousePosition();

        if (_selectedBlip >= 0 && _selectedBlip < (int)_mapData.blips.size()) {
            const MapBlip& blip = _mapData.blips[_selectedBlip];
            float dist = Vector2Distance(_mapData.playerPos, blip.worldPos);
            bool inRange = _mapData.hyperdriveRange > 0.0f && dist <= _mapData.hyperdriveRange;

            if (inRange) {
                Vector2 sp = proj.Project(blip.worldPos);
                Rectangle warpBtn = { sp.x - 42.0f, sp.y + 18.0f, 84.0f, 26.0f };
                if (CheckCollisionPointRec(mouse, warpBtn)) {
                    Vector2 toBlip = Vector2Subtract(blip.worldPos, _mapData.playerPos);
                    float toLen = Vector2Length(toBlip);
                    Vector2 dir = (toLen > 1.0f)
                        ? Vector2Scale(toBlip, 1.0f / toLen)
                        : Vector2{0.0f, -1.0f};
                    _warpTarget = Vector2Subtract(blip.worldPos,
                        Vector2Scale(dir, blip.radius + 150.0f));
                    return MapAction::WarpTo;
                }
            }
        }

        bool clickedBlip = false;
        for (int i = 0; i < (int)_mapData.blips.size(); ++i) {
            Vector2 sp = proj.Project(_mapData.blips[i].worldPos);
            if (CheckCollisionPointCircle(mouse, sp, 12.0f) &&
                CheckCollisionPointRec(mouse, L.map)) {
                _selectedBlip = (_selectedBlip == i) ? -1 : i;
                clickedBlip = true;
                break;
            }
        }
        if (!clickedBlip && CheckCollisionPointRec(mouse, L.map))
            _selectedBlip = -1;
    }

    return MapAction::None;
}

void SystemMap::Draw() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    auto L = CalcLayout(sw, sh);

    DrawRectangle(0, 0, sw, sh, Color{0, 0, 8, 215});

    // ── Title bar ─────────────────────────────────────────────────────────────
    DrawRectangleRec(L.title, Color{8,20,8,245});
    DrawRectangleLinesEx(L.title, 1.0f, Color{40,160,40,200});
    DrawText("SYSTEM NAVIGATION ARRAY", L.mx + 14, (int)L.title.y + 11, 16, Color{75,195,75,255});

    bool hasHyperdrive = _mapData.hyperdriveRange > 0.0f;
    if (hasHyperdrive) {
        char rangeLabel[64];
        std::snprintf(rangeLabel, sizeof(rangeLabel), "[ HYPERDRIVE RANGE: %.0f u ]", _mapData.hyperdriveRange);
        int tw = MeasureText(rangeLabel, 14);
        DrawText(rangeLabel, L.mx + L.mw - tw - 14, (int)L.title.y + 13, 14, Color{80,220,80,255});
    } else {
        const char* statusLbl = "[ NO HYPERDRIVE EQUIPPED ]";
        DrawText(statusLbl, L.mx + L.mw - MeasureText(statusLbl, 14) - 14,
                 (int)L.title.y + 13, 14, Color{220,100,35,255});
    }

    // ── Map panel ────────────────────────────────────────────────────────────
    DrawRectangleRec(L.map, Color{2,5,2,250});
    DrawRectangleLinesEx(L.map, 1.5f, Color{40,160,40,200});

    int mpx = (int)L.map.x, mpy = (int)L.map.y;
    int mpw = (int)L.map.width, mph = (int)L.map.height;
    for (int x = mpx + 60; x < mpx + mpw; x += 60)
        DrawLine(x, mpy, x, mpy + mph, Color{25,50,25,60});
    for (int y = mpy + 40; y < mpy + mph; y += 40)
        DrawLine(mpx, y, mpx + mpw, y, Color{25,50,25,60});

    if (_mapData.blips.empty()) {
        const char* msg = "NO STELLAR OBJECTS DETECTED";
        DrawText(msg, mpx + (mpw - MeasureText(msg, 16)) / 2, mpy + mph / 2 - 8, 16, Color{80,100,80,200});
    } else {
        auto proj = ComputeProjection(L.map);
        Vector2 playerSP = proj.Project(_mapData.playerPos);

        if (hasHyperdrive) {
            float screenRange = _mapData.hyperdriveRange * proj.scale;
            DrawCircleLines((int)playerSP.x, (int)playerSP.y, screenRange + 1.0f, Color{0, 180, 255, 30});
            DrawCircleLines((int)playerSP.x, (int)playerSP.y, screenRange,        Color{0, 200, 255, 90});
            DrawCircleLines((int)playerSP.x, (int)playerSP.y, screenRange - 1.0f, Color{0, 180, 255, 40});
        }

        for (int i = 0; i < (int)_mapData.blips.size(); ++i) {
            const MapBlip& blip = _mapData.blips[i];
            Vector2 sp = proj.Project(blip.worldPos);

            if (sp.x < L.map.x || sp.x > L.map.x + L.map.width ||
                sp.y < L.map.y || sp.y > L.map.y + L.map.height)
                continue;

            bool selected = (i == _selectedBlip);
            float dist = Vector2Distance(_mapData.playerPos, blip.worldPos);
            bool inRange = hasHyperdrive && dist <= _mapData.hyperdriveRange;

            Color baseColor = blip.isPlanet
                ? (blip.discovered ? Color{80, 150, 230, 255} : Color{45, 75, 130, 160})
                : (blip.discovered ? Color{220, 210, 130, 255} : Color{100, 95, 60, 160});

            if (selected) {
                DrawCircleLines((int)sp.x, (int)sp.y, 14.0f,
                                inRange ? Color{0,220,120,200} : Color{220,80,60,200});
                DrawCircleLines((int)sp.x, (int)sp.y, 15.0f,
                                inRange ? Color{0,180,100,120} : Color{180,60,40,120});
            }

            if (blip.discovered) {
                if (blip.isPlanet) {
                    DrawCircleV(sp, 8.0f, baseColor);
                    DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, Color{140, 190, 255, 180});
                } else {
                    float sz = 6.0f;
                    DrawTriangle({sp.x, sp.y - sz}, {sp.x + sz, sp.y}, {sp.x - sz, sp.y}, baseColor);
                    DrawTriangle({sp.x - sz, sp.y}, {sp.x + sz, sp.y}, {sp.x, sp.y + sz}, baseColor);
                    DrawTriangleLines({sp.x, sp.y - sz}, {sp.x + sz, sp.y}, {sp.x - sz, sp.y},
                                      Color{255, 245, 180, 200});
                    DrawTriangleLines({sp.x - sz, sp.y}, {sp.x + sz, sp.y}, {sp.x, sp.y + sz},
                                      Color{255, 245, 180, 200});
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
                    bool hov = CheckCollisionPointRec(GetMousePosition(), warpBtn);
                    Color btnBg  = hov ? Color{0,120,60,230}  : Color{0,60,30,200};
                    Color btnBdr = hov ? Color{0,220,100,255} : Color{0,150,70,200};
                    DrawRectangleRec(warpBtn, btnBg);
                    DrawRectangleLinesEx(warpBtn, 1.5f, btnBdr);
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

        float pulse = sinf(_time * 3.0f) * 0.5f + 0.5f;
        float pr = 4.0f + pulse * 2.0f;
        DrawCircleV(playerSP, pr, Color{80, 255, 120, 230});
        DrawCircleLines((int)playerSP.x, (int)playerSP.y, pr + 4.0f,
                        Color{60, 200, 100, (unsigned char)(90 + (int)(pulse * 80))});
        const char* youLabel = "YOU";
        DrawText(youLabel, (int)playerSP.x - MeasureText(youLabel, 9) / 2,
                 (int)playerSP.y - 18, 9, Color{100, 230, 130, 210});
    }

    // ── Bottom panel ─────────────────────────────────────────────────────────
    DrawRectangleRec(L.bot, Color{8,14,8,245});
    DrawRectangleLinesEx(L.bot, 1.0f, Color{40,160,40,200});

    Rectangle resumeBtn, galacticBtn, saveBtn, loadBtn, menuBtn;
    CalcButtons(L.bot, resumeBtn, galacticBtn, saveBtn, loadBtn, menuBtn);
    DrawMapBtn(resumeBtn,  "RESUME  [ESC]");
    DrawMapBtn(galacticBtn,"GALACTIC MAP");
    DrawMapBtn(saveBtn,    "SAVE");
    DrawMapBtn(loadBtn,    "LOAD");
    DrawMapBtn(menuBtn,    "MAIN MENU");

    if (_saveFeedbackTimer > 0.0f) {
        const char* msg = "GAME SAVED";
        float alpha = std::min(1.0f, _saveFeedbackTimer / 0.4f);
        Color col = { 75, 218, 75, (unsigned char)(alpha * 255) };
        DrawText(msg, L.mx + L.mw - MeasureText(msg, 16) - 14, (int)L.bot.y + 6, 16, col);
    }

    _savePicker.Draw();
    _saveWriter.Draw();
}

#include "ui/SavePicker.h"
#include "raylib.h"
#include "core/SaveManager.h"
#include <cstdio>

static const Color PkBg = { 6, 10,  6, 245 };
static const Color PkBorder = { 40,158, 40, 210 };
static const Color PkLabel = { 68,162, 68, 255 };
static const Color PkValue = { 192,218,192, 255 };
static const Color PkDiv = { 34, 98, 34, 175 };

static bool IsHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool IsClk(Rectangle r) { return IsHov(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

void SavePicker::CalcRects(int& px, int& py, int& pw, int& ph,
    int& listX, int& listY, int& listW, int& rowH) const {
    pw = 680; ph = 480;
    px = (GetScreenWidth() - pw) / 2;
    py = (GetScreenHeight() - ph) / 2;
    listX = px + 18;
    listY = py + 58;
    listW = pw - 36;
    rowH = 52;
}

void SavePicker::Open() {
    _open = true;
    _selectedIdx = -1;
    _hoveredIdx = -1;
    _selectedFile.clear();
    _saves = SaveManager::Get().ListSaves();
}

SavePicker::Result SavePicker::Update() {
    if (!_open) return Result::None;
    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return Result::Cancelled; }

    int px, py, pw, ph, listX, listY, listW, rowH;
    CalcRects(px, py, pw, ph, listX, listY, listW, rowH);

    _hoveredIdx = -1;
    for (int i = 0; i < (int)_saves.size(); ++i) {
        Rectangle row = { (float)listX, (float)(listY + i * (rowH + 4)), (float)listW, (float)rowH };
        if (IsHov(row)) _hoveredIdx = i;
        if (IsClk(row)) _selectedIdx = i;
    }

    float btnY = (float)(py + ph - 58);
    Rectangle loadBtn = { (float)(px + 40),            btnY, 160.f, 42.f };
    Rectangle deleteBtn = { (float)(px + pw / 2 - 80),   btnY, 160.f, 42.f };
    Rectangle cancBtn = { (float)(px + pw - 200),      btnY, 160.f, 42.f };

    if (IsClk(loadBtn) && _selectedIdx >= 0) {
        _selectedFile = _saves[_selectedIdx].filename;
        Close();
        return Result::Selected;
    }

    if (IsClk(deleteBtn) && _selectedIdx >= 0) {
        std::string targetFile = _saves[_selectedIdx].filename;
        SaveManager::Get().DeleteSave(targetFile);

        // Reset selections and refresh listing layout
        _selectedIdx = -1;
        _saves = SaveManager::Get().ListSaves();
        return Result::None;
    }

    if (IsClk(cancBtn)) { Close(); return Result::Cancelled; }

    return Result::None;
}

void SavePicker::Draw() const {
    if (!_open) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 0, 0, 0, 180 });

    int px, py, pw, ph, listX, listY, listW, rowH;
    CalcRects(px, py, pw, ph, listX, listY, listW, rowH);
    DrawRectangle(px, py, pw, ph, PkBg);
    DrawRectangleLinesEx({ (float)px, (float)py, (float)pw, (float)ph }, 1.5f, PkBorder);

    const char* title = "LOAD GAME";
    DrawText(title, px + (pw - MeasureText(title, 22)) / 2, py + 14, 22, PkLabel);
    DrawRectangle(px + 16, py + 46, pw - 32, 1, PkDiv);

    if (_saves.empty()) {
        const char* msg = "No save data found.";
        DrawText(msg, px + (pw - MeasureText(msg, 17)) / 2, py + ph / 2 - 10, 17, Color{ 80,100,80,200 });
    }
    else {
        for (int i = 0; i < (int)_saves.size(); ++i) {
            Rectangle row = { (float)listX, (float)(listY + i * (rowH + 4)), (float)listW, (float)rowH };
            bool sel = (i == _selectedIdx);
            bool hov = (i == _hoveredIdx);
            Color bg = sel ? Color{ 30,80,30,230 } : hov ? Color{ 18,48,18,220 } : Color{ 10,18,10,200 };
            Color bdr = sel ? PkBorder : PkDiv;
            DrawRectangleRec(row, bg);
            DrawRectangleLinesEx(row, 1.0f, bdr);

            DrawText(_saves[i].timestamp.c_str(), (int)row.x + 12, (int)row.y + 8, 15, PkValue);

            int   hpct = (int)(_saves[i].hullPct * 100.f);
            char  hbuf[24];
            std::snprintf(hbuf, sizeof(hbuf), "Hull %d%%", hpct);
            Color hcol = hpct > 50 ? Color{ 48,188,68,255 } : hpct > 25 ? Color{ 212,168,28,255 } : Color{ 208,42,32,255 };
            DrawText(hbuf, (int)row.x + 12, (int)row.y + 30, 13, hcol);

            // Index number flush right
            char nbuf[24];
            std::snprintf(nbuf, sizeof(nbuf), "#%d", (int)_saves.size() - i);
            DrawText(nbuf, (int)(row.x + row.width - MeasureText(nbuf, 14) - 12), (int)row.y + 18, 14, PkLabel);
        }
    }

    float btnY = (float)(py + ph - 58);
    Rectangle loadBtn = { (float)(px + 40),            btnY, 160.f, 42.f };
    Rectangle deleteBtn = { (float)(px + pw / 2 - 80),   btnY, 160.f, 42.f };
    Rectangle cancBtn = { (float)(px + pw - 200),      btnY, 160.f, 42.f };

    bool canAct = (_selectedIdx >= 0 && !_saves.empty());

    // Render LOAD Button
    Color loadBg = canAct && IsHov(loadBtn) ? Color{ 50,95,50,230 } : canAct ? Color{ 15,30,15,215 } : Color{ 14,20,14,150 };
    DrawRectangleRec(loadBtn, loadBg);
    DrawRectangleLinesEx(loadBtn, 1.0f, canAct ? PkBorder : Color{ 40,55,40,130 });
    DrawText("LOAD", (int)(loadBtn.x + (loadBtn.width - MeasureText("LOAD", 17)) / 2), (int)(loadBtn.y + 12), 17, canAct ? WHITE : Color{ 90,110,90,150 });

    // Render DELETE Button
    Color delBg = canAct && IsHov(deleteBtn) ? Color{ 140,30,30,230 } : canAct ? Color{ 45,15,15,215 } : Color{ 20,14,14,150 };
    Color delBdr = canAct ? Color{ 230,60,60,255 } : Color{ 60,40,40,130 };
    DrawRectangleRec(deleteBtn, delBg);
    DrawRectangleLinesEx(deleteBtn, 1.0f, delBdr);
    DrawText("DELETE", (int)(deleteBtn.x + (deleteBtn.width - MeasureText("DELETE", 17)) / 2), (int)(deleteBtn.y + 12), 17, canAct ? WHITE : Color{ 120,90,90,150 });

    // Render CANCEL Button
    Color cancBg = IsHov(cancBtn) ? Color{ 50,50,50,230 } : Color{ 20,20,20,215 };
    DrawRectangleRec(cancBtn, cancBg);
    DrawRectangleLinesEx(cancBtn, 1.0f, Color{ 100,100,100,200 });
    DrawText("CANCEL", (int)(cancBtn.x + (cancBtn.width - MeasureText("CANCEL", 17)) / 2), (int)(cancBtn.y + 12), 17, WHITE);
}
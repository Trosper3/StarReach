#include "ui/SaveWriter.h"
#include "raylib.h"
#include "core/SaveManager.h"
#include <cstdio>

static const Color WrBg     = {  6, 10,  6, 245 };
static const Color WrBorder = { 40,158, 40, 210 };
static const Color WrLabel  = { 68,162, 68, 255 };
static const Color WrValue  = {192,218,192, 255 };
static const Color WrDiv    = { 34, 98, 34, 175 };

static bool IsHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool IsClk(Rectangle r) { return IsHov(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

void SaveWriter::CalcRects(int& px, int& py, int& pw, int& ph,
    int& listX, int& listY, int& listW, int& rowH) const {
    pw = 680; ph = 500;
    px = (GetScreenWidth()  - pw) / 2;
    py = (GetScreenHeight() - ph) / 2;
    listX = px + 18;
    listY = py + 58;
    listW = pw - 36;
    rowH  = 52;
}

void SaveWriter::Open() {
    _open        = true;
    _hoveredIdx  = -1;
    _textActive  = false;
    _inputText.clear();
    _targetPath.clear();
    _displayName.clear();
    _saves = SaveManager::Get().ListSaves();
}

SaveWriter::Result SaveWriter::Update() {
    if (!_open) return Result::None;
    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return Result::Cancelled; }

    int px, py, pw, ph, listX, listY, listW, rowH;
    CalcRects(px, py, pw, ph, listX, listY, listW, rowH);

    int inputY = py + ph - 106;
    Rectangle textBox = { (float)listX,                (float)inputY, (float)(listW - 168), 38.f };
    Rectangle newBtn  = { (float)(listX + listW - 160), (float)inputY, 160.f, 38.f };
    Rectangle backBtn = { (float)(px + pw/2 - 80),      (float)(py + ph - 58), 160.f, 42.f };

    // Text box focus on click
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        _textActive = IsHov(textBox);

    // Keyboard input for name
    if (_textActive) {
        int ch = GetCharPressed();
        while (ch > 0) {
            if (ch >= 32 && ch <= 125 && (int)_inputText.size() < 24)
                _inputText += (char)ch;
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !_inputText.empty())
            _inputText.pop_back();
    }

    // NEW SAVE button
    if (IsClk(newBtn) && !_inputText.empty()) {
        std::string name = _inputText;
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        while (!name.empty() && name.back()  == ' ') name.pop_back();
        if (!name.empty()) {
            _targetPath  = "saves/" + name + ".json";
            _displayName = name;
            Close();
            return Result::Saved;
        }
    }

    // List rows — click to overwrite
    int listAreaH = ph - 58 - 116;
    _hoveredIdx = -1;
    for (int i = 0; i < (int)_saves.size(); ++i) {
        int ry = listY + i * (rowH + 4);
        if (ry + rowH > py + 58 + listAreaH) break;
        Rectangle row = { (float)listX, (float)ry, (float)listW, (float)rowH };
        if (IsHov(row)) _hoveredIdx = i;
        if (IsClk(row)) {
            _targetPath  = _saves[i].filename;
            _displayName = "";   // overwrite: refresh to current timestamp
            Close();
            return Result::Saved;
        }
    }

    if (IsClk(backBtn)) { Close(); return Result::Cancelled; }

    return Result::None;
}

void SaveWriter::Draw() const {
    if (!_open) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 0, 0, 0, 180 });

    int px, py, pw, ph, listX, listY, listW, rowH;
    CalcRects(px, py, pw, ph, listX, listY, listW, rowH);
    DrawRectangle(px, py, pw, ph, WrBg);
    DrawRectangleLinesEx({ (float)px, (float)py, (float)pw, (float)ph }, 1.5f, WrBorder);

    const char* title = "SAVE GAME";
    DrawText(title, px + (pw - MeasureText(title, 22)) / 2, py + 14, 22, WrLabel);
    DrawRectangle(px + 16, py + 46, pw - 32, 1, WrDiv);

    int listAreaH = ph - 58 - 116;

    if (_saves.empty()) {
        const char* msg = "No existing saves — create one below.";
        DrawText(msg, px + (pw - MeasureText(msg, 15)) / 2,
                 py + 58 + listAreaH / 2 - 8, 15, Color{ 80,100,80,200 });
    }
    else {
        const char* hint = "Click a save to overwrite it";
        DrawText(hint, px + (pw - MeasureText(hint, 13)) / 2,
                 py + 58 + listAreaH + 5, 13, Color{ 100,130,100,170 });

        for (int i = 0; i < (int)_saves.size(); ++i) {
            int ry = listY + i * (rowH + 4);
            if (ry + rowH > py + 58 + listAreaH) break;
            Rectangle row = { (float)listX, (float)ry, (float)listW, (float)rowH };
            bool hov = (i == _hoveredIdx);
            Color bg  = hov ? Color{ 70, 25, 10, 230 } : Color{ 10, 18, 10, 200 };
            Color bdr = hov ? Color{ 200,100, 40, 220 } : WrDiv;
            DrawRectangleRec(row, bg);
            DrawRectangleLinesEx(row, 1.0f, bdr);

            DrawText(_saves[i].timestamp.c_str(), (int)row.x + 12, (int)row.y + 8,  15, WrValue);

            int  hpct = (int)(_saves[i].hullPct * 100.f);
            char hbuf[24];
            std::snprintf(hbuf, sizeof(hbuf), "Hull %d%%", hpct);
            Color hcol = hpct > 50 ? Color{48,188,68,255} : hpct > 25 ? Color{212,168,28,255} : Color{208,42,32,255};
            DrawText(hbuf, (int)row.x + 12, (int)row.y + 30, 13, hcol);

            char nbuf[16];
            std::snprintf(nbuf, sizeof(nbuf), "#%d", (int)_saves.size() - i);
            int nw = MeasureText(nbuf, 14);
            DrawText(nbuf, (int)(row.x + row.width - nw - 12), (int)row.y + 18, 14, WrLabel);

            if (hov) {
                const char* ow = "OVERWRITE";
                DrawText(ow, (int)(row.x + row.width - MeasureText(ow, 13) - nw - 24),
                         (int)row.y + 18, 13, Color{ 220,120, 50, 220 });
            }
        }
    }

    // Divider above input area
    DrawRectangle(px + 16, py + ph - 116, pw - 32, 1, WrDiv);

    int inputY = py + ph - 106;
    Rectangle textBox = { (float)listX,                (float)inputY, (float)(listW - 168), 38.f };
    Rectangle newBtn  = { (float)(listX + listW - 160), (float)inputY, 160.f, 38.f };
    Rectangle backBtn = { (float)(px + pw/2 - 80),      (float)(py + ph - 58), 160.f, 42.f };

    // Text box
    DrawRectangleRec(textBox, Color{ 4, 10, 4, 230 });
    DrawRectangleLinesEx(textBox, 1.5f, _textActive ? WrBorder : WrDiv);
    if (_inputText.empty() && !_textActive) {
        DrawText("Enter save name...", (int)textBox.x + 10, (int)textBox.y + 10, 16, Color{ 60,90,60,160 });
    }
    else {
        std::string disp = _inputText;
        if (_textActive && (int)(GetTime() * 2) % 2 == 0) disp += '|';
        DrawText(disp.c_str(), (int)textBox.x + 10, (int)textBox.y + 10, 16, WrValue);
    }

    // NEW SAVE button
    bool canNew = !_inputText.empty();
    bool hovNew = IsHov(newBtn);
    DrawRectangleRec(newBtn, canNew && hovNew ? Color{50,95,50,230} : canNew ? Color{15,30,15,215} : Color{14,20,14,150});
    DrawRectangleLinesEx(newBtn, 1.0f, canNew ? WrBorder : Color{40,55,40,130});
    DrawText("NEW SAVE",
        (int)(newBtn.x + (newBtn.width  - MeasureText("NEW SAVE", 16)) / 2),
        (int)(newBtn.y + (newBtn.height - 16) / 2),
        16, canNew ? WHITE : Color{90,110,90,150});

    // BACK button
    bool hovBack = IsHov(backBtn);
    DrawRectangleRec(backBtn, hovBack ? Color{40,40,40,230} : Color{20,20,20,215});
    DrawRectangleLinesEx(backBtn, 1.0f, Color{100,100,100,200});
    DrawText("BACK",
        (int)(backBtn.x + (backBtn.width  - MeasureText("BACK", 16)) / 2),
        (int)(backBtn.y + (backBtn.height - 16) / 2),
        16, WHITE);
}

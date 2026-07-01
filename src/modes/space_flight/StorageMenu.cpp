#include "StorageMenu.h"
#include <algorithm>
#include <cstdio>
#include "core/InventoryManager.h"

// ── Static helpers ────────────────────────────────────────────────────────────

Color StorageMenu::GradeColor(ModuleGrade g) {
    switch (g) {
    case ModuleGrade::Common:     return { 140,140,140,255 };
    case ModuleGrade::Uncommon:   return {  68,187, 68,255 };
    case ModuleGrade::Unique:     return {  68,110,255,255 };
    case ModuleGrade::Remarkable: return { 153, 51,255,255 };
    case ModuleGrade::Epic:       return { 255,130, 34,255 };
    case ModuleGrade::Legendary:  return { 255,204,  0,255 };
    case ModuleGrade::Mythic:     return { 255, 34,102,255 };
    default:                      return { 100,100,100,255 };
    }
}

const char* StorageMenu::GradeName(ModuleGrade g) {
    switch (g) {
    case ModuleGrade::Common:     return "Common";
    case ModuleGrade::Uncommon:   return "Uncommon";
    case ModuleGrade::Unique:     return "Unique";
    case ModuleGrade::Remarkable: return "Remarkable";
    case ModuleGrade::Epic:       return "Epic";
    case ModuleGrade::Legendary:  return "Legendary";
    case ModuleGrade::Mythic:     return "Mythic";
    default:                      return "Unknown";
    }
}

const char* StorageMenu::TypeName(ModuleType t) {
    switch (t) {
    case ModuleType::Weapon:     return "WEAPON";
    case ModuleType::Armor:      return "ARMOR";
    case ModuleType::Shield:     return "SHIELD";
    case ModuleType::Engine:     return "ENGINE";
    case ModuleType::Hyperdrive: return "HYPERDRIVE";
    case ModuleType::Auxiliary:  return "AUXILIARY";
    default:                     return "UNKNOWN";
    }
}

Color StorageMenu::TypeColor(ModuleType t) {
    switch (t) {
    case ModuleType::Weapon:     return { 220, 60, 60,255 };
    case ModuleType::Armor:      return {  80,130,200,255 };
    case ModuleType::Shield:     return { 255,210, 60,255 };
    case ModuleType::Engine:     return { 255,140, 30,255 };
    case ModuleType::Hyperdrive: return { 180, 80,255,255 };
    case ModuleType::Auxiliary:  return {  60,200,100,255 };
    default:                     return { 120,120,120,255 };
    }
}

void StorageMenu::GetRects(int x, int y, int w, int count, Rectangle* out) {
    int cols = std::max(1, (w + SlotGap) / (SlotPx + SlotGap));
    for (int i = 0; i < count; ++i) {
        out[i] = {
            (float)(x + (i % cols) * (SlotPx + SlotGap)),
            (float)(y + (i / cols) * (SlotPx + SlotGap)),
            (float)SlotPx, (float)SlotPx
        };
    }
}

void StorageMenu::DrawItemInSlot(Rectangle r, const StorageItem& item,
                                  bool hovered, bool dimmed) {
    Color border = hovered ? Color{ 80,200, 80,255 } : Color{ 34, 98, 34,160 };
    DrawRectangleRec(r, Color{ 10, 18, 10, 220 });
    DrawRectangleLinesEx(r, hovered ? 2.0f : 1.0f, border);

    if (dimmed) {
        // ghost / placeholder during drag
        DrawRectangleLinesEx(r, 1.0f, Color{50,80,50,120});
        return;
    }

    if (item.type == StorageItemType::Empty) {
        const char* em = "EMPTY";
        DrawText(em,
                 (int)(r.x + (r.width - MeasureText(em, 9)) / 2.0f),
                 (int)(r.y + r.height / 2.0f - 5), 9,
                 Color{ 50, 70, 50, 110 });
        return;
    }

    if (item.type == StorageItemType::Module) {
        Color gc = GradeColor(item.module.grade);
        DrawRectangleLinesEx({ r.x+1, r.y+1, r.width-2, r.height-2 }, 2.0f,
                             { gc.r, gc.g, gc.b, 170 });
        const char* letter =
            item.module.type == ModuleType::Weapon     ? "W" :
            item.module.type == ModuleType::Armor      ? "A" :
            item.module.type == ModuleType::Shield     ? "S" :
            item.module.type == ModuleType::Engine     ? "E" :
            item.module.type == ModuleType::Hyperdrive ? "H" : "U";
        Color lc = TypeColor(item.module.type);
        int fs = 28;
        int lw = MeasureText(letter, fs);
        DrawText(letter,
                 (int)(r.x + (r.width - lw) / 2.0f),
                 (int)(r.y + (r.height - fs) / 2.0f - 4),
                 fs, { lc.r, lc.g, lc.b, 210 });
        const char* gn = GradeName(item.module.grade);
        int gnw = MeasureText(gn, 8);
        DrawText(gn,
                 (int)(r.x + (r.width - gnw) / 2.0f),
                 (int)(r.y + r.height - 12),
                 8, { gc.r, gc.g, gc.b, 160 });
        return;
    }

    if (item.type == StorageItemType::Material) {
        DrawText(item.displayName.c_str(), (int)r.x+6, (int)r.y+10, 11,
                 Color{180,200,180,255});
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "x%d", item.count);
        DrawText(cnt, (int)r.x+6, (int)(r.y + r.height - 18), 12,
                 Color{140,220,140,255});
    }
}

void StorageMenu::DrawItemTooltip(const StorageItem& item, int x, int y) {
    if (item.type == StorageItemType::Empty) return;
    static constexpr int TW = 300, TH = 110;
    DrawRectangle(x, y, TW, TH, Color{  6, 14,  6, 235 });
    DrawRectangleLinesEx({ (float)x,(float)y,(float)TW,(float)TH }, 1.0f,
                          Color{ 34, 98, 34, 200 });
    int ty = y + 10;
    DrawText(item.displayName.c_str(), x+10, ty, 14, Color{192,218,192,255}); ty += 22;
    if (item.type == StorageItemType::Module) {
        Color gc = GradeColor(item.module.grade);
        Color tc = TypeColor(item.module.type);
        DrawText(TypeName(item.module.type), x+10, ty, 12, tc); ty += 18;
        DrawText(GradeName(item.module.grade), x+10, ty, 12, gc); ty += 18;
        if (!item.module.description.empty())
            DrawText(item.module.description.c_str(), x+10, ty, 11,
                     Color{130,175,130,200});
    } else {
        char cnt[32];
        std::snprintf(cnt, sizeof(cnt), "%d / %d", item.count, MaxStack);
        DrawText(cnt, x+10, ty, 12, Color{140,200,140,220});
    }
}

// ── Trash can widget ──────────────────────────────────────────────────────────

Rectangle StorageMenu::TrashRect(int sw, int sh) {
    return { (float)(sw - 104), (float)(sh - 104), 76.0f, 76.0f };
}

void StorageMenu::DrawTrashCan(Rectangle r, bool hovered) {
    Color bg  = hovered ? Color{ 80, 14, 14, 245 } : Color{ 18,  5,  5, 220 };
    Color bdr = hovered ? Color{230, 55, 55, 255 } : Color{130, 32, 32, 200 };
    Color ic  = hovered ? Color{255, 85, 85, 255 } : Color{170, 45, 45, 230 };

    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 2.0f, bdr);

    float cx = r.x + r.width  * 0.5f;
    float cy = r.y + r.height * 0.5f;
    float u  = r.width * 0.085f;

    // Handle
    DrawRectangle((int)(cx - u*1.4f), (int)(cy - u*4.4f), (int)(u*2.8f), (int)(u*1.0f), ic);
    // Lid
    DrawRectangle((int)(cx - u*3.6f), (int)(cy - u*3.2f), (int)(u*7.2f), (int)(u*1.2f), ic);
    // Body
    int bx = (int)(cx - u*3.0f), by = (int)(cy - u*1.8f);
    int bw = (int)(u*6.0f),      bh = (int)(u*5.6f);
    DrawRectangle(bx, by, bw, bh, ic);
    // Cutout lines
    for (int i = 1; i <= 3; ++i) {
        int lx = bx + bw * i / 4;
        DrawRectangle(lx - 1, by + (int)(u*0.4f), 2, bh - (int)(u*0.8f), bg);
    }

    const char* lbl = hovered ? "RELEASE TO DELETE" : "DRAG TO DELETE";
    DrawText(lbl, (int)(r.x + (r.width - MeasureText(lbl, 8)) * 0.5f),
             (int)(r.y + r.height + 4), 8, bdr);
}

// ── StorageMenu ───────────────────────────────────────────────────────────────

void StorageMenu::Open(int numSlots) {
    isOpen = true;
    if ((int)slots.size() < numSlots)
        slots.resize(numSlots);
    _hovSlot  = -1;
    _dragging = false;
    _dragIdx  = -1;
}

bool StorageMenu::Update() {
    if (!isOpen) return false;
    Vector2 mouse = GetMousePosition();

    // BACK / ESC — cancel any drag first
    Rectangle back = { 18.0f, 16.0f, 110.0f, 36.0f };
    if (IsKeyPressed(KEY_ESCAPE) ||
        (CheckCollisionPointRec(mouse, back) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
        _dragging = false;
        _dragIdx  = -1;
        isOpen    = false;
        return false;
    }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int gx = 100, gy = 90, gw = sw - 200;
    int n  = (int)slots.size();
    std::vector<Rectangle> rects(n);
    GetRects(gx, gy, gw, n, rects.data());
    Rectangle trash = TrashRect(sw, sh);

    // Cancel drag on right-click
    if (_dragging && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        _dragging = false;
        _dragIdx  = -1;
    }

    // Drop
    if (_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, trash) && _dragIdx >= 0 && _dragIdx < n)
            slots[_dragIdx] = StorageItem{};
        _dragging = false;
        _dragIdx  = -1;
    }

    // Start drag on press (non-empty slot)
    if (!_dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < n; ++i) {
            if (!CheckCollisionPointRec(mouse, rects[i])) continue;
            if (slots[i].type != StorageItemType::Empty) {
                _dragging = true;
                _dragIdx  = i;
                _dragItem = slots[i];
            }
            break;
        }
    }

    // Hover (only when not dragging)
    _hovSlot = -1;
    if (!_dragging) {
        for (int i = 0; i < n; ++i)
            if (CheckCollisionPointRec(mouse, rects[i])) { _hovSlot = i; break; }
    }

    return true;
}

void StorageMenu::Draw() const {
    if (!isOpen) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 1, 4, 1, 255 });

    const char* title = "STORAGE";
    DrawText(title, (sw - MeasureText(title, 26)) / 2, 18, 26, Color{ 68,162,68,255 });
    DrawRectangle((sw - 400) / 2, 54, 400, 1, Color{ 34,98,34,170 });

    // ── ADDED: CREDITS OVERLAY LABEL ──────────────────────────────────────────
    char credsBuf[32];
    std::snprintf(credsBuf, sizeof(credsBuf), "CREDITS: %d", InventoryManager::Get().Credits);
    int credsWidth = MeasureText(credsBuf, 18);
    // Position it comfortably on the right side of the header section
    DrawText(credsBuf, sw - credsWidth - 36, 22, 18, Color{ 255, 210, 60, 255 }); // Gold colored text
    // ──────────────────────────────────────────────────────────────────────────

    // BACK button
    Vector2 mouse = GetMousePosition();
    Rectangle back = { 18.0f, 16.0f, 110.0f, 36.0f };
    bool hb = CheckCollisionPointRec(mouse, back);
    DrawRectangleRec(back, hb ? Color{ 50,95,50,230 } : Color{ 12,28,12,220 });
    DrawRectangleLinesEx(back, 1.0f, Color{ 40,160,40,200 });
    const char* bl = "< BACK";
    DrawText(bl, (int)(back.x + (back.width - MeasureText(bl, 15)) / 2),
        (int)(back.y + (back.height - 15) / 2), 15, WHITE);

    int gx = 100, gy = 90, gw = sw - 200, n = (int)slots.size();
    std::vector<Rectangle> rects(n);
    GetRects(gx, gy, gw, n, rects.data());
    for (int i = 0; i < n; ++i) {
        bool dimmed = (_dragging && i == _dragIdx);
        DrawItemInSlot(rects[i], slots[i], i == _hovSlot && !_dragging, dimmed);
    }

    // Trash can (bottom-right)
    Rectangle trash = TrashRect(sw, sh);
    bool      hovTr = _dragging && CheckCollisionPointRec(mouse, trash);
    DrawTrashCan(trash, hovTr);

    // Tooltip (only when not dragging)
    if (!_dragging && _hovSlot >= 0 && _hovSlot < n)
        DrawItemTooltip(slots[_hovSlot], gx, sh - 130);

    // Drag ghost follows cursor
    if (_dragging) {
        Rectangle ghost = { mouse.x - 40.0f, mouse.y - 40.0f, 80.0f, 80.0f };
        DrawItemInSlot(ghost, _dragItem, false, false);
    }

    // Cursor management: hide system cursor when hand/drag is active
    bool hovNonEmpty = !_dragging && _hovSlot >= 0 && _hovSlot < n &&
                       slots[_hovSlot].type != StorageItemType::Empty;
    if (_dragging || hovNonEmpty) {
        HideCursor();
        if (hovNonEmpty) {
            // Draw hand cursor (open hand)
            float mx = mouse.x, my = mouse.y;
            DrawRectangle((int)mx - 5, (int)my - 2, 10, 7, Color{ 20,100,20,200 });
            DrawRectangleLines((int)mx - 5, (int)my - 2, 10, 7, Color{ 100,220,100,255 });
            for (int f = 0; f < 4; ++f) {
                int fx = (int)mx - 4 + f * 2;
                DrawLine(fx, (int)my - 2, fx, (int)my - 8, Color{ 150,255,150,255 });
            }
            DrawLine((int)mx - 5, (int)my + 2, (int)mx - 9, (int)my - 1, Color{ 150,255,150,255 });
        }
        // While dragging, the ghost already serves as the visual — no extra cursor needed
    } else {
        ShowCursor();
    }
}

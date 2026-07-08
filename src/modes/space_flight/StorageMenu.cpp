#include "StorageMenu.h"
#include "shared/ui/HudTheme.h"
#include <algorithm>
#include <cstdio>
#include "core/InventoryManager.h"

// ── Shared chrome/glass HUD theme (see shared/ui/HudTheme.h) — same helpers
// ModulesMenu/StationServicesMenu/BuildMenu already use. Grade/type colors
// stay as-is (semantic rarity/module-type), only structural chrome changes.

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
    case ModuleType::Consumable: return "REPAIR";
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
    case ModuleType::Consumable: return {  90,230,140,255 };
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
    using namespace hudtheme;
    Color border = hovered ? HudGood : HudDiv;
    DrawHudChamferRect(r, 8.0f, Color{ 10, 16, 24, 220 }, border, hovered ? 2.0f : 1.0f);

    if (dimmed) {
        // ghost / placeholder during drag
        DrawRectangleLinesEx(r, 1.0f, Color{ 50, 65, 80, 120 });
        return;
    }

    if (item.type == StorageItemType::Empty) {
        const char* em = "EMPTY";
        DrawText(em,
                 (int)(r.x + (r.width - MeasureText(em, 9)) / 2.0f),
                 (int)(r.y + r.height / 2.0f - 5), 9,
                 Color{ 60, 75, 90, 140 });
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
            item.module.type == ModuleType::Hyperdrive ? "H" :
            item.module.type == ModuleType::Consumable ? "R" : "U";
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
                 hudtheme::HudValue);
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "x%d", item.count);
        DrawText(cnt, (int)r.x+6, (int)(r.y + r.height - 18), 12,
                 hudtheme::HudGood);
        return;
    }

    if (item.type == StorageItemType::Hardpoint) {
        const Color hc = hudtheme::HudCaution;
        DrawRectangleLinesEx({ r.x+1, r.y+1, r.width-2, r.height-2 }, 2.0f,
                             { hc.r, hc.g, hc.b, 170 });
        const char* letter = "H";
        int fs = 28;
        int lw = MeasureText(letter, fs);
        DrawText(letter,
                 (int)(r.x + (r.width - lw) / 2.0f),
                 (int)(r.y + (r.height - fs) / 2.0f - 4),
                 fs, { hc.r, hc.g, hc.b, 210 });
        const char* nm = item.hardpoint.displayName.c_str();
        int nfs = 8;
        while (MeasureText(nm, nfs) > (int)r.width - 6 && nfs > 6) nfs--;
        DrawText(nm,
                 (int)(r.x + (r.width - MeasureText(nm, nfs)) / 2.0f),
                 (int)(r.y + r.height - 12),
                 nfs, { hc.r, hc.g, hc.b, 160 });
    }
}

void StorageMenu::DrawItemTooltip(const StorageItem& item, int x, int y) {
    if (item.type == StorageItemType::Empty) return;
    using namespace hudtheme;
    static constexpr int TW = 300, TH = 110;
    DrawRectangle(x, y, TW, TH, Color{ 4, 8, 14, 245 });
    DrawRectangleLinesEx({ (float)x,(float)y,(float)TW,(float)TH }, 1.0f, HudBorder);
    int ty = y + 10;
    DrawText(item.displayName.c_str(), x+10, ty, 14, HudValue); ty += 22;
    if (item.type == StorageItemType::Module) {
        Color gc = GradeColor(item.module.grade);
        Color tc = TypeColor(item.module.type);
        DrawText(TypeName(item.module.type), x+10, ty, 12, tc); ty += 18;
        DrawText(GradeName(item.module.grade), x+10, ty, 12, gc); ty += 18;
        if (!item.module.description.empty())
            DrawText(item.module.description.c_str(), x+10, ty, 11, HudLabel);
    } else if (item.type == StorageItemType::Hardpoint) {
        const StationHardpointDef& hp = item.hardpoint;
        char buf[80];
        std::snprintf(buf, sizeof(buf), "Hull %.0f", hp.maxHull);
        DrawText(buf, x+10, ty, 12, HudCaution); ty += 18;
        std::snprintf(buf, sizeof(buf), "W:%d  A:%d  S:%d  E:%d  X:%d",
                      hp.wSlots, hp.arSlots, hp.shSlots, hp.enSlots, hp.auxSlots);
        DrawText(buf, x+10, ty, 12, HudLabel);
    } else {
        char cnt[32];
        std::snprintf(cnt, sizeof(cnt), "%d / %d", item.count, MaxStack);
        DrawText(cnt, x+10, ty, 12, HudGood);
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
        if (CheckCollisionPointRec(mouse, trash) && _dragIdx >= 0 && _dragIdx < n) {
            slots[_dragIdx] = StorageItem{};
        } else if (_dragIdx >= 0 && _dragIdx < n) {
            // Move to another empty slot; anywhere else (occupied slot, back
            // on itself, outside the grid) snaps back — slots[_dragIdx] was
            // never cleared during the drag, so doing nothing is correct.
            for (int i = 0; i < n; ++i) {
                if (i == _dragIdx || !CheckCollisionPointRec(mouse, rects[i])) continue;
                if (slots[i].type == StorageItemType::Empty) {
                    slots[i]         = _dragItem;
                    slots[_dragIdx]  = StorageItem{};
                }
                break;
            }
        }
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
    using namespace hudtheme;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 2, 4, 9, 255 });

    const char* title = "STORAGE";
    DrawText(title, (sw - MeasureText(title, 26)) / 2, 18, 26, HudValue);
    DrawRectangle((sw - 400) / 2, 54, 400, 1, HudDiv);

    // Credits readout, right side of the header section.
    char credsBuf[32];
    std::snprintf(credsBuf, sizeof(credsBuf), "CREDITS: %d", InventoryManager::Get().Credits);
    int credsWidth = MeasureText(credsBuf, 18);
    DrawText(credsBuf, sw - credsWidth - 36, 22, 18, HudGood);

    // BACK button
    Vector2 mouse = GetMousePosition();
    Rectangle back = { 18.0f, 16.0f, 110.0f, 36.0f };
    bool hb = CheckCollisionPointRec(mouse, back);
    DrawHudChamferRect(back, 6.0f, hb ? Color{ 30, 55, 70, 230 } : Color{ 14, 20, 28, 200 }, HudBorder, hb ? 2.0f : 1.0f);
    const char* bl = "< BACK";
    DrawText(bl, (int)(back.x + (back.width - MeasureText(bl, 15)) / 2),
        (int)(back.y + (back.height - 15) / 2), 15, hb ? WHITE : HudLabel);

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
            DrawRectangle((int)mx - 5, (int)my - 2, 10, 7, Color{ 20, 55, 70, 200 });
            DrawRectangleLines((int)mx - 5, (int)my - 2, 10, 7, hudtheme::HudBorder);
            for (int f = 0; f < 4; ++f) {
                int fx = (int)mx - 4 + f * 2;
                DrawLine(fx, (int)my - 2, fx, (int)my - 8, hudtheme::HudValue);
            }
            DrawLine((int)mx - 5, (int)my + 2, (int)mx - 9, (int)my - 1, hudtheme::HudValue);
        }
        // While dragging, the ghost already serves as the visual — no extra cursor needed
    } else {
        ShowCursor();
    }
}

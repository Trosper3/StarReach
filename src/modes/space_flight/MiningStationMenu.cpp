#include "MiningStationMenu.h"
#include "shared/ui/HudTheme.h"
#include "raylib.h"
#include <algorithm>
#include <cstdio>

// Shared chrome/glass HUD theme (see shared/ui/HudTheme.h) — same helpers
// ModulesMenu/StorageMenu/StationServicesMenu/BuildMenu already use.
using hudtheme::HudBg;
using hudtheme::HudBorder;
using hudtheme::HudLabel;
using hudtheme::HudValue;
using hudtheme::HudDiv;
using hudtheme::HudCritical;
using hudtheme::DrawHudBracketPanel;
using hudtheme::DrawHudChamferRect;

void MiningStationMenu::Open(PlayerStation* station, std::vector<StorageItem>* playerStorage) {
    _station  = station;
    _player   = playerStorage;
    _dragging = false;
    _dragSrc  = DragSrc::None;
    _dragIdx  = -1;
    isOpen    = true;
    openModulesRequested = false;
}

void MiningStationMenu::Close() {
    isOpen    = false;
    _dragging = false;
    _dragSrc  = DragSrc::None;
    _dragIdx  = -1;
}

void MiningStationMenu::GetColumnRects(int x, int y, int count, std::vector<Rectangle>& out) const {
    out.resize(count);
    StorageMenu::GetRects(x, y, ColW, count, out.data());
}

bool MiningStationMenu::IsMouseOverMenu() const {
    if (!isOpen) return false;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2, panelY = sh / 2 - PanelH / 2;
    return CheckCollisionPointRec(GetMousePosition(), { (float)panelX, (float)panelY, (float)PanelW, (float)PanelH });
}

bool MiningStationMenu::Update() {
    if (!isOpen || !_station) return false;
    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return false; }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - PanelH / 2;
    Vector2 m = GetMousePosition();

    Rectangle closeBtn = { (float)(panelX + PanelW - 28), (float)(panelY + 6), 22.0f, 22.0f };
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, closeBtn)) {
        Close(); return false;
    }

    Rectangle modBtn = { (float)(panelX + 8), (float)(panelY + 6), 96.0f, 22.0f };
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, modBtn)) {
        openModulesRequested = true;
        Close(); return false;
    }

    int stoX = panelX + 16;
    int plrX = panelX + PanelW - ColW - 16;
    int colY = panelY + 70;

    std::vector<Rectangle> stoRects, plrRects;
    if (_station) GetColumnRects(stoX, colY, (int)_station->storage.size(), stoRects);
    if (_player)  GetColumnRects(plrX, colY, (int)_player->size(),          plrRects);

    // Cancel drag on right-click — returns the item to its origin slot.
    if (_dragging && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        StorageItem* origin = (_dragSrc == DragSrc::Station) ? &_station->storage[_dragIdx]
                             : (_player ? &(*_player)[_dragIdx] : nullptr);
        if (origin) *origin = _dragItem;
        _dragging = false; _dragSrc = DragSrc::None; _dragIdx = -1;
    }

    // Begin drag on press over a non-empty slot in either column.
    if (!_dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < (int)stoRects.size(); ++i) {
            if (!CheckCollisionPointRec(m, stoRects[i])) continue;
            if (_station->storage[i].type != StorageItemType::Empty) {
                _dragging = true; _dragSrc = DragSrc::Station; _dragIdx = i;
                _dragItem = _station->storage[i];
                _station->storage[i] = StorageItem{};
            }
            break;
        }
        if (!_dragging && _player) {
            for (int i = 0; i < (int)plrRects.size(); ++i) {
                if (!CheckCollisionPointRec(m, plrRects[i])) continue;
                if ((*_player)[i].type != StorageItemType::Empty) {
                    _dragging = true; _dragSrc = DragSrc::Player; _dragIdx = i;
                    _dragItem = (*_player)[i];
                    (*_player)[i] = StorageItem{};
                }
                break;
            }
        }
    }

    // Drop: swap the carried item with whatever's under the cursor (or
    // nothing, if it's empty). Falling outside both columns returns it home.
    if (_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        StorageItem* target = nullptr;
        for (int i = 0; i < (int)stoRects.size(); ++i) {
            if (CheckCollisionPointRec(m, stoRects[i])) { target = &_station->storage[i]; break; }
        }
        if (!target && _player) {
            for (int i = 0; i < (int)plrRects.size(); ++i) {
                if (CheckCollisionPointRec(m, plrRects[i])) { target = &(*_player)[i]; break; }
            }
        }

        StorageItem* origin = (_dragSrc == DragSrc::Station) ? &_station->storage[_dragIdx]
                             : (_player ? &(*_player)[_dragIdx] : nullptr);
        if (target) {
            if (target->type == StorageItemType::Material && _dragItem.type == StorageItemType::Material &&
                target->materialId == _dragItem.materialId) {
                // Same material — merge stacks instead of swapping.
                int total    = target->count + _dragItem.count;
                int merged   = std::min(total, StorageMenu::MaxStack);
                int leftover = total - merged;
                target->count = merged;
                if (origin) *origin = (leftover > 0) ? StorageItem{ StorageItemType::Material, _dragItem.displayName,
                                                                     _dragItem.materialId, leftover, {} }
                                                      : StorageItem{};
            } else {
                // Dropping back onto the same slot it came from: target and
                // origin are the same StorageItem, so just restore it there —
                // writing to `origin` too would immediately clobber it back
                // to empty.
                StorageItem displaced = *target;
                *target = _dragItem;
                if (origin && origin != target) *origin = displaced;
            }
        } else if (origin) {
            *origin = _dragItem;
        }
        _dragging = false; _dragSrc = DragSrc::None; _dragIdx = -1;
    }

    // Hover (only when not dragging)
    _hovSide = -1; _hovIdx = -1;
    if (!_dragging) {
        for (int i = 0; i < (int)stoRects.size(); ++i)
            if (CheckCollisionPointRec(m, stoRects[i])) { _hovSide = 0; _hovIdx = i; break; }
        if (_hovSide < 0) {
            for (int i = 0; i < (int)plrRects.size(); ++i)
                if (CheckCollisionPointRec(m, plrRects[i])) { _hovSide = 1; _hovIdx = i; break; }
        }
    }

    return true;
}

void MiningStationMenu::Draw() const {
    if (!isOpen || !_station) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - PanelH / 2;
    Vector2 mouse = GetMousePosition();

    DrawRectangle(0, 0, sw, sh, Color{ 0, 0, 0, 160 });
    DrawHudBracketPanel({ (float)panelX, (float)panelY, (float)PanelW, (float)PanelH }, HudBg, HudBorder, 14.0f, 2.0f);

    char titlebuf[128];
    std::snprintf(titlebuf, sizeof(titlebuf), "%s — STORAGE", _station->displayName.c_str());
    DrawText(titlebuf, panelX + (PanelW - MeasureText(titlebuf, 14)) / 2, panelY + 8, 14, HudValue);

    Rectangle modBtn = { (float)(panelX + 8), (float)(panelY + 6), 96.0f, 22.0f };
    bool hovMod = CheckCollisionPointRec(mouse, modBtn);
    DrawHudChamferRect(modBtn, 5.0f, hovMod ? Color{ 30, 55, 70, 230 } : Color{ 14, 20, 28, 200 }, HudBorder, hovMod ? 1.5f : 1.0f);
    DrawText("MODULES", (int)(modBtn.x + 10), (int)(modBtn.y + 5), 11, hovMod ? WHITE : HudLabel);

    Rectangle closeBtn = { (float)(panelX + PanelW - 28), (float)(panelY + 6), 22.0f, 22.0f };
    bool hovX = CheckCollisionPointRec(mouse, closeBtn);
    DrawHudChamferRect(closeBtn, 4.0f, hovX ? Color{ 90, 25, 25, 220 } : Color{ 30, 12, 12, 180 }, HudCritical, 1.0f);
    DrawText("X", (int)(closeBtn.x + 7), (int)(closeBtn.y + 5), 12, hovX ? WHITE : HudLabel);

    int stoX = panelX + 16;
    int plrX = panelX + PanelW - ColW - 16;
    int colY = panelY + 70;

    DrawText("STATION", stoX, colY - 18, 11, HudLabel);
    DrawText("YOUR SHIP", plrX, colY - 18, 11, HudLabel);
    DrawLine(panelX + PanelW / 2, panelY + 50, panelX + PanelW / 2, panelY + PanelH - 16, HudDiv);

    std::vector<Rectangle> stoRects, plrRects;
    GetColumnRects(stoX, colY, (int)_station->storage.size(), stoRects);
    for (int i = 0; i < (int)stoRects.size(); ++i) {
        bool dimmed = (_dragging && _dragSrc == DragSrc::Station && i == _dragIdx);
        StorageMenu::DrawItemInSlot(stoRects[i], _station->storage[i],
                                     _hovSide == 0 && _hovIdx == i && !_dragging, dimmed);
    }

    if (_player) {
        GetColumnRects(plrX, colY, (int)_player->size(), plrRects);
        for (int i = 0; i < (int)plrRects.size(); ++i) {
            bool dimmed = (_dragging && _dragSrc == DragSrc::Player && i == _dragIdx);
            StorageMenu::DrawItemInSlot(plrRects[i], (*_player)[i],
                                         _hovSide == 1 && _hovIdx == i && !_dragging, dimmed);
        }
    }

    // Tooltip (only when not dragging)
    if (!_dragging && _hovSide == 0 && _hovIdx >= 0 && _hovIdx < (int)_station->storage.size())
        StorageMenu::DrawItemTooltip(_station->storage[_hovIdx], stoX, sh - 130);
    else if (!_dragging && _player && _hovSide == 1 && _hovIdx >= 0 && _hovIdx < (int)_player->size())
        StorageMenu::DrawItemTooltip((*_player)[_hovIdx], plrX, sh - 130);

    // Drag ghost follows cursor
    if (_dragging) {
        Rectangle ghost = { mouse.x - SlotPx / 2.0f, mouse.y - SlotPx / 2.0f, (float)SlotPx, (float)SlotPx };
        StorageMenu::DrawItemInSlot(ghost, _dragItem, false, false);
    }
}

#include "BuildMenu.h"
#include "data/registry/ItemRegistry.h"
#include "data/registry/BuildableRegistry.h"
#include "data/registry/ModuleRegistry.h"
#include "shared/ui/HudTheme.h"
#include "raylib.h"
#include <algorithm>
#include <cstdio>

// ── Palette ─────────────────────────────────────────────────────────────────
// Shared chrome/glass HUD theme (see shared/ui/HudTheme.h) — same helpers
// StationServicesMenu already uses, so the two menus read as one UI language.

static bool BmHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

static void DrawBmBtn(Rectangle r, const char* label, bool enabled, bool good = true) {
    using namespace hudtheme;
    bool  hov = enabled && BmHov(r);
    Color accent = good ? HudGood : HudCritical;
    Color bg  = !enabled ? Color{ 16,16,16,150 } : (hov ? Color{ 30,55,70,230 } : Color{ 14,20,28,200 });
    Color bdr = !enabled ? HudDiv : accent;
    Color fg  = !enabled ? Color{ 60,65,70,160 } : (hov ? WHITE : HudLabel);
    DrawHudChamferRect(r, 6.0f, bg, bdr, hov ? 2.0f : 1.0f);
    int tw = MeasureText(label, 12);
    DrawText(label, (int)(r.x + (r.width - tw) / 2.0f), (int)(r.y + (r.height - 12) / 2.0f), 12, fg);
}

// ── Storage helpers ───────────────────────────────────────────────────────────

int BuildMenu::CountInStorage(const std::string& id) const {
    if (!_storage) return 0;
    for (const StorageItem& s : *_storage)
        if (s.type == StorageItemType::Material && s.materialId == id)
            return s.count;
    return 0;
}

bool BuildMenu::RemoveFromStorage(const std::string& id, int amount) {
    if (!_storage) return false;
    for (StorageItem& s : *_storage) {
        if (s.type == StorageItemType::Material && s.materialId == id) {
            if (s.count < amount) return false;
            s.count -= amount;
            if (s.count == 0) s = StorageItem{};
            return true;
        }
    }
    return false;
}

void BuildMenu::AddToStorage(const std::string& id, const std::string& displayName, int amount) {
    if (!_storage) return;
    for (StorageItem& s : *_storage) {
        if (s.type == StorageItemType::Material && s.materialId == id) {
            s.count = std::min(s.count + amount, StorageMenu::MaxStack);
            return;
        }
    }
    for (StorageItem& s : *_storage) {
        if (s.type == StorageItemType::Empty) {
            s.type = StorageItemType::Material;
            s.materialId = id;
            s.displayName = displayName;
            s.count = amount;
            return;
        }
    }
}

// ── Affordability / crafting ──────────────────────────────────────────────────

bool BuildMenu::CanFitResult(const std::vector<BuildIngredient>& cost,
    const std::string& resultId, bool isModule) const {
    if (!_storage) return false;

    int slotsFreed = 0;
    for (const auto& ing : cost) {
        for (const StorageItem& s : *_storage) {
            if (s.type == StorageItemType::Material && s.materialId == ing.itemId) {
                if (s.count <= ing.amount) slotsFreed++;
                break;
            }
        }
    }

    if (!isModule) {
        for (const StorageItem& s : *_storage) {
            if (s.type == StorageItemType::Material && s.materialId == resultId &&
                s.count < StorageMenu::MaxStack)
                return true;
        }
    }

    int emptySlots = 0;
    for (const StorageItem& s : *_storage)
        if (s.type == StorageItemType::Empty) emptySlots++;

    return (emptySlots + slotsFreed) >= 1;
}

bool BuildMenu::CanAffordBuild(const std::vector<BuildIngredient>& cost) const {
    for (const auto& ing : cost)
        if (CountInStorage(ing.itemId) < ing.amount) return false;
    return true;
}

bool BuildMenu::CanCraftItem(const ItemDef& item) const {
    for (const auto& ing : item.craftCost)
        if (CountInStorage(ing.materialId) < ing.amount) return false;
    return true;
}

void BuildMenu::DoCraftItem(const ItemDef& item) {
    if (!CanCraftItem(item)) return;
    for (const auto& ing : item.craftCost)
        RemoveFromStorage(ing.materialId, ing.amount);
    AddToStorage(item.id, item.displayName, 1);
}

void BuildMenu::DoSpendItems(const std::vector<BuildIngredient>& cost) {
    for (const auto& ing : cost)
        RemoveFromStorage(ing.itemId, ing.amount);
}

bool BuildMenu::PlaceInFirstEmptySlot(const StorageItem& item) {
    if (!_storage) return false;
    for (StorageItem& s : *_storage) {
        if (s.type == StorageItemType::Empty) { s = item; return true; }
    }
    return false;
}

const char* BuildMenu::ItemName(const std::string& itemId) {
    const ItemDef* def = ItemRegistry::ById(itemId);
    return def ? def->displayName.c_str() : itemId.c_str();
}

std::string BuildMenu::GetSelectedStationId() const {
    if (!isOpen || _tab != 0 || _selIdx < 0) return "";
    auto items = BuildableRegistry::ByType(BuildableType::Station);

    // Only return the ID if the player has the materials to build it
    if (_selIdx < (int)items.size() && CanAffordBuild(items[_selIdx].itemCost)) {
        return items[_selIdx].stationDefId;
    }
    return "";
}

bool BuildMenu::IsMouseOverMenu() const {
    if (!isOpen) return false;
    int sh = GetScreenHeight();
    int panelH = std::min(sh - 20, 480);
    int px = 12, py = sh / 2 - panelH / 2;
    // Check if the cursor is physically inside the left UI panel
    return CheckCollisionPointRec(GetMousePosition(), { (float)px, (float)py, (float)PanelW, (float)panelH });
}

// ── Open / Close ──────────────────────────────────────────────────────────────

void BuildMenu::Open(std::vector<StorageItem>* storage) {
    _storage = storage;
    isOpen = true;
    pendingBuildId.clear();
    _tab = 0;
    _selIdx = -1;
    _scroll = 0; // Reset scroll on open
}

void BuildMenu::Close() {
    isOpen = false;
    pendingBuildId.clear();
}

// ── Update ────────────────────────────────────────────────────────────────────

void BuildMenu::Update() {
    if (!isOpen) return;

    if (_errorOpen) {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) ||
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            _errorOpen = false;
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return; }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelH = std::min(sh - 20, 480);
    int px = 12, py = sh / 2 - panelH / 2;
    Vector2 m = GetMousePosition();

    // Close [X]
    Rectangle closeBtn = { (float)(px + PanelW - 28), (float)(py + 6), 22.0f, 22.0f };
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, closeBtn)) {
        Close(); return;
    }

    // Tabs
    int tabY = py + 32;
    int tabW = PanelW / 4;
    for (int i = 0; i < 4; ++i) {
        Rectangle r = { (float)(px + i * tabW), (float)tabY, (float)tabW, (float)TabH };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r)) {
            _tab = i;
            _selIdx = -1;
            _scroll = 0; // Reset scroll on tab change
        }
    }

    int listY = tabY + TabH + 6;
    int listH = panelH - (listY - py) - 50;

    // Process mouse wheel scrolling
    if (CheckCollisionPointRec(m, { (float)px, (float)py, (float)PanelW, (float)panelH })) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            _scroll -= (int)(wheel * 24.0f); // Scroll speed multiplier
        }
    }

    // Clamp scroll bounds dynamically depending on active tab contents
    int maxScroll = 0;
    if (_tab == 0 || _tab == 1 || _tab == 2) {
        BuildableType wantType = (_tab == 0) ? BuildableType::Station
                                : (_tab == 1) ? BuildableType::Module : BuildableType::Hardpoint;
        auto items = BuildableRegistry::ByType(wantType);
        int totalH = (int)items.size() * (ItemH + 4) - 4;
        if (totalH > listH) maxScroll = totalH - listH;
    }
    else {
        const auto& allItems = ItemRegistry::All();
        int totalH = (int)allItems.size() * (CraftH + 4) - 4;
        if (totalH > listH) maxScroll = totalH - listH;
    }
    if (_scroll > maxScroll) _scroll = maxScroll;
    if (_scroll < 0) _scroll = 0;

    if (_tab == 0 || _tab == 1 || _tab == 2) {
        BuildableType wantType = (_tab == 0) ? BuildableType::Station
                                : (_tab == 1) ? BuildableType::Module : BuildableType::Hardpoint;
        auto items = BuildableRegistry::ByType(wantType);

        // Item row clicks (modified to incorporate _scroll and handle out-of-bounds skipping)
        for (int i = 0; i < (int)items.size(); ++i) {
            int iy = listY + i * (ItemH + 4) - _scroll;
            if (iy + ItemH <= listY || iy >= listY + listH) continue;
            Rectangle r = { (float)px, (float)iy, (float)PanelW, (float)ItemH };
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r))
                _selIdx = i;
        }

        // BUILD / PLACE button
        int btnBottom = py + panelH - 10;
        Rectangle buildBtn = { (float)(px + PanelW / 2 - 60), (float)(btnBottom - 36), 120.0f, 30.0f };

        // ── Update this block inside BuildMenu.cpp ──
        if (_selIdx >= 0 && _selIdx < (int)items.size()) {
            bool canAfford = CanAffordBuild(items[_selIdx].itemCost);
            if (canAfford && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(m, buildBtn)) {
                if (_tab == 0) {
                    // REMOVED: DoSpendItems(items[_selIdx].itemCost);
                    // We do not spend items here anymore! We pass the request along untouched.
                    pendingBuildId = items[_selIdx].stationDefId;
                    Close();
                }
                else if (_tab == 1) {
                    auto modOpt = ModuleRegistry::ById(items[_selIdx].moduleDefId);
                    if (modOpt.has_value()) {
                        if (!CanFitResult(items[_selIdx].itemCost,
                            items[_selIdx].moduleDefId, true)) {
                            _errorOpen = true;
                        }
                        else {
                            DoSpendItems(items[_selIdx].itemCost);
                            PlaceInFirstEmptySlot(StorageItem{
                                StorageItemType::Module, modOpt->displayName, "", 0, *modOpt });
                        }
                    }
                    _selIdx = -1;
                }
                else {
                    // Hardpoint tab: crafted blueprint lands in storage, later
                    // dragged onto a built station's "ATTACH HARDPOINT" slot.
                    if (!CanFitResult(items[_selIdx].itemCost, items[_selIdx].id, true)) {
                        _errorOpen = true;
                    }
                    else {
                        DoSpendItems(items[_selIdx].itemCost);
                        PlaceInFirstEmptySlot(StorageItem{
                            StorageItemType::Hardpoint, items[_selIdx].displayName, "", 0,
                            ModuleDef{}, items[_selIdx].hardpointDef });
                    }
                    _selIdx = -1;
                }
            }
        }
    }
    else {
        // CRAFT tab clicks (modified to incorporate _scroll and continue looping)
        const auto& allItems = ItemRegistry::All();
        for (int i = 0; i < (int)allItems.size(); ++i) {
            int iy = listY + i * (CraftH + 4) - _scroll;
            if (iy + CraftH <= listY || iy >= listY + listH) continue;
            Rectangle btnCraft = { (float)(px + PanelW - 90), (float)(iy + CraftH - 28), 82.0f, 22.0f };
            if (CanCraftItem(allItems[i]) &&
                IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(m, btnCraft)) {
                if (!CanFitResult(
                    [&]() -> std::vector<BuildIngredient> {
                        std::vector<BuildIngredient> v;
                        for (const auto& c : allItems[i].craftCost)
                            v.push_back({ c.materialId, c.amount });
                        return v;
                    }(),
                        allItems[i].id, false)) {
                    _errorOpen = true;
                }
                else {
                    DoCraftItem(allItems[i]);
                }
            }
        }
    }
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void BuildMenu::Draw() const {
    if (!isOpen) return;
    using namespace hudtheme;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelH = std::min(sh - 20, 480);
    int px = 12, py = sh / 2 - panelH / 2;
    Vector2 mouse = GetMousePosition();

    DrawHudBracketPanel({ (float)px,(float)py,(float)PanelW,(float)panelH }, HudBg, HudBorder, 16.0f, 2.0f);

    const char* title = "BUILD MENU";
    DrawText(title, px + (PanelW - MeasureText(title, 13)) / 2, py + 8, 13, HudValue);

    // Close [X]
    Rectangle closeBtn = { (float)(px + PanelW - 28), (float)(py + 6), 22.0f, 22.0f };
    DrawBmBtn(closeBtn, "X", true, false);

    // Tabs
    const char* tabLabels[] = { "STATIONS", "MODULES", "HARDPOINTS", "CRAFT" };
    int tabY = py + 32, tabW = PanelW / 4;
    for (int i = 0; i < 4; ++i) {
        Rectangle r = { (float)(px + i * tabW),(float)tabY,(float)tabW,(float)TabH };
        bool active = (_tab == i);
        DrawHudChamferRect(r, 6.0f, active ? Color{ 20,55,80,230 } : HudBg,
            active ? HudBorder : HudDiv, active ? 2.0f : 1.0f);
        DrawText(tabLabels[i],
            (int)(r.x + (r.width - MeasureText(tabLabels[i], 10)) / 2),
            (int)(r.y + 8), 10, active ? HudValue : HudLabel);
    }

    int listY = tabY + TabH + 6;
    int listH = panelH - (listY - py) - 50;

    BeginScissorMode(px, listY, PanelW, listH);

    if (_tab == 0 || _tab == 1 || _tab == 2) {
        BuildableType wantType = (_tab == 0) ? BuildableType::Station
                                : (_tab == 1) ? BuildableType::Module : BuildableType::Hardpoint;
        auto items = BuildableRegistry::ByType(wantType);

        for (int i = 0; i < (int)items.size(); ++i) {
            const BuildableDef& bd = items[i];
            int iy = listY + i * (ItemH + 4) - _scroll;
            if (iy + ItemH <= listY || iy >= listY + listH) continue; // Skip out-of-bounds instead of breaking

            bool canAfford = CanAffordBuild(bd.itemCost);
            bool sel = (i == _selIdx);
            bool hov = CheckCollisionPointRec(mouse, { (float)px,(float)iy,(float)PanelW,(float)ItemH });

            Color bg = sel ? Color{ 20,60,80,230 } : (hov ? Color{ 16,35,70,220 } : Color{ 10,20,45,200 });
            Color bdr = canAfford ? HudGood : HudCritical;
            DrawRectangle(px, iy, PanelW, ItemH, bg);
            DrawRectangleLinesEx({ (float)px,(float)iy,(float)PanelW,(float)ItemH },
                sel ? 2.0f : 1.5f, bdr);

            DrawText(bd.displayName.c_str(), px + 8, iy + 7, 12, HudValue);
            DrawText(bd.description.c_str(), px + 8, iy + 23, 10, HudLabel);

            int cx = px + 8, cy = iy + 42;
            for (const auto& ing : bd.itemCost) {
                int have = CountInStorage(ing.itemId);
                bool ok = have >= ing.amount;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s %d/%d",
                    ItemName(ing.itemId), have, ing.amount);
                Color c = ok ? HudGood : HudCritical;
                DrawText(buf, cx, cy, 10, c);
                cx += MeasureText(buf, 10) + 10;
                if (cx > px + PanelW - 8) { cx = px + 8; cy += 14; }
            }
        }
    }
    else {
        // CRAFT tab
        const auto& allItems = ItemRegistry::All();
        for (int i = 0; i < (int)allItems.size(); ++i) {
            const ItemDef& item = allItems[i];
            int iy = listY + i * (CraftH + 4) - _scroll;
            if (iy + CraftH <= listY || iy >= listY + listH) continue; // Skip out-of-bounds instead of breaking

            bool canCraft = CanCraftItem(item);
            bool hov = CheckCollisionPointRec(mouse,
                { (float)px,(float)iy,(float)PanelW,(float)CraftH });

            DrawRectangle(px, iy, PanelW, CraftH, hov ? Color{ 16,35,70,220 } : Color{ 10,20,45,200 });
            DrawRectangleLinesEx({ (float)px,(float)iy,(float)PanelW,(float)CraftH }, 1.2f,
                canCraft ? HudGood : HudDiv);

            int owned = CountInStorage(item.id);
            char namebuf[128];
            std::snprintf(namebuf, sizeof(namebuf), "%s  (have %d)", item.displayName.c_str(), owned);
            DrawText(namebuf, px + 8, iy + 6, 11, HudValue);

            int cx = px + 8, cy = iy + 24;
            for (const auto& ing : item.craftCost) {
                int have = CountInStorage(ing.materialId);
                bool ok = have >= ing.amount;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s %d/%d",
                    ing.materialId.c_str(), have, ing.amount);
                DrawText(buf, cx, cy, 10, ok ? HudGood : HudCritical);
                cx += MeasureText(buf, 10) + 8;
            }

            Rectangle btn = { (float)(px + PanelW - 90),(float)(iy + CraftH - 28), 82.0f, 22.0f };
            DrawBmBtn(btn, "CRAFT", canCraft);
        }
    }

    EndScissorMode();

    // BUILD / PLACE button
    if (_tab == 0 || _tab == 1 || _tab == 2) {
        BuildableType wantType = (_tab == 0) ? BuildableType::Station
                                : (_tab == 1) ? BuildableType::Module : BuildableType::Hardpoint;
        auto items = BuildableRegistry::ByType(wantType);
        bool hasSel = (_selIdx >= 0 && _selIdx < (int)items.size());
        bool canAff = hasSel && CanAffordBuild(items[_selIdx].itemCost);

        int btnBottom = py + panelH - 10;
        Rectangle buildBtn = { (float)(px + PanelW / 2 - 60),(float)(btnBottom - 36), 120.0f, 30.0f };
        const char* blbl = hasSel ? (_tab == 0 ? "PLACE" : "CRAFT") : "SELECT";
        DrawBmBtn(buildBtn, blbl, canAff);
    }

    // Storage full error popup
    if (_errorOpen) {
        DrawRectangle(0, 0, sw, sh, Color{ 0, 0, 0, 140 });

        static constexpr int PopW = 340, PopH = 110;
        int ppx = sw / 2 - PopW / 2, ppy = sh / 2 - PopH / 2;
        DrawHudBracketPanel({ (float)ppx,(float)ppy,(float)PopW,(float)PopH },
            Color{ 18, 8, 8, 248 }, HudCritical, 12.0f, 2.0f);

        const char* hdr = "STORAGE FULL";
        DrawText(hdr, ppx + (PopW - MeasureText(hdr, 14)) / 2, ppy + 14, 14, HudCritical);

        const char* msg = "You need more room in storage.";
        DrawText(msg, ppx + (PopW - MeasureText(msg, 11)) / 2, ppy + 38, 11,
            Color{ 200, 170, 170, 230 });

        Rectangle okBtn = { (float)(ppx + PopW / 2 - 44), (float)(ppy + PopH - 36), 88.0f, 26.0f };
        DrawBmBtn(okBtn, "OK", true, false);
    }
}
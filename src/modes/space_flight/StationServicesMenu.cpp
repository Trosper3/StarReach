#include "StationServicesMenu.h"
#include "modes/space_flight/StorageMenu.h"
#include "shared/ui/HudTheme.h"
#include "core/InventoryManager.h"
#include "data/registry/ModuleRegistry.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static bool IsHov(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool IsClk(Rectangle r) { return IsHov(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

static void DrawSvcBtn(Rectangle r, const char* label, bool enabled = true) {
    using namespace hudtheme;
    bool  hov = enabled && IsHov(r);
    Color bg  = !enabled ? Color{ 16,16,16,150 } : (hov ? Color{ 30,55,70,230 } : Color{ 14,20,28,200 });
    Color bdr = !enabled ? HudDiv : HudBorder;
    Color fg  = !enabled ? Color{ 50,55,60,160 } : (hov ? WHITE : HudLabel);
    DrawHudChamferRect(r, 8.0f, bg, bdr, hov ? 2.0f : 1.0f);
    int tw = MeasureText(label, 15);
    DrawText(label, (int)(r.x + (r.width - tw) / 2.0f),
                    (int)(r.y + (r.height - 15) / 2.0f), 15, fg);
}

struct SvcLayout { Rectangle title, content, bot; };

static SvcLayout CalcSvcLayout(int sw, int sh) {
    SvcLayout L;
    int mx = (int)(sw * 0.12f);
    int mw = (int)(sw * 0.76f);
    int ty = (int)(sh * 0.05f);
    int th = 40;
    int cy = ty + th + 4;
    int ch = (int)(sh * 0.60f);
    int by = cy + ch + 10;
    int bh = sh - by - (int)(sh * 0.04f);
    L.title   = { (float)mx, (float)ty, (float)mw, (float)th };
    L.content = { (float)mx, (float)cy, (float)mw, (float)ch };
    L.bot     = { (float)mx, (float)by, (float)mw, (float)bh };
    return L;
}

static int  GradeIndex(ModuleGrade g) { return (int)g; }
static int  SellPrice(ModuleGrade g)  { return (GradeIndex(g) + 1) * 50; }
static int  MergeCost(ModuleGrade g)  { return (GradeIndex(g) + 1) * 100; }
static bool NextGrade(ModuleGrade g, ModuleGrade& out) {
    if (g == ModuleGrade::Mythic) return false;
    out = (ModuleGrade)(GradeIndex(g) + 1);
    return true;
}

static Rectangle BackBtnRect(const SvcLayout& L) {
    return { L.bot.x + 15.0f, L.bot.y + L.bot.height - 54.0f, 140.0f, 40.0f };
}
static Rectangle ConfirmBtnRect(const SvcLayout& L) {
    return { L.bot.x + L.bot.width - 195.0f, L.bot.y + L.bot.height - 54.0f, 180.0f, 40.0f };
}

// ── Open / Close ──────────────────────────────────────────────────────────────

void StationServicesMenu::Open(ecs::Entity* player, std::vector<StorageItem>* storage) {
    _player  = player;
    _storage = storage;
    _screen  = Screen::Main;
    _selA = _selB = -1;
    isOpen = true;
}

void StationServicesMenu::Close() {
    if (_screen != Screen::Main) {
        _screen = Screen::Main;
        _selA = _selB = -1;
    } else {
        isOpen = false;
    }
}

// ── Update ────────────────────────────────────────────────────────────────────

void StationServicesMenu::Update() {
    if (!isOpen) return;
    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return; }

    switch (_screen) {
    case Screen::Main:     UpdateMain();     break;
    case Screen::Sell:     UpdateSell();     break;
    case Screen::Repair:   UpdateRepair();   break;
    case Screen::Engineer: UpdateEngineer(); break;
    }
}

void StationServicesMenu::UpdateMain() {
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    float bw   = (L.bot.width - 60.0f) / 4.0f;
    float gap  = (L.bot.width - 30.0f - bw * 4.0f) / 3.0f;
    float by   = L.bot.y + L.bot.height - 40.0f - 14.0f;
    float x0   = L.bot.x + 15.0f;
    Rectangle sellBtn   = { x0,                    by, bw, 40.0f };
    Rectangle repairBtn = { x0 + (bw + gap),        by, bw, 40.0f };
    Rectangle engBtn    = { x0 + (bw + gap) * 2.0f, by, bw, 40.0f };
    Rectangle closeBtn  = { x0 + (bw + gap) * 3.0f, by, bw, 40.0f };

    if (IsClk(sellBtn))   { _screen = Screen::Sell;     _selA = _selB = -1; }
    if (IsClk(repairBtn)) { _screen = Screen::Repair; }
    if (IsClk(engBtn))    { _screen = Screen::Engineer; _selA = _selB = -1; }
    if (IsClk(closeBtn))  { isOpen = false; }
}

void StationServicesMenu::UpdateSell() {
    if (!_storage) return;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    int gx = (int)L.content.x + 20, gy = (int)L.content.y + 20, gw = (int)L.content.width - 40;
    int n  = (int)_storage->size();
    std::vector<Rectangle> rects(n);
    StorageMenu::GetRects(gx, gy, gw, n, rects.data());

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < n; ++i) {
            if (!CheckCollisionPointRec(mouse, rects[i])) continue;
            if ((*_storage)[i].type == StorageItemType::Module)
                _selA = (_selA == i) ? -1 : i;
            break;
        }
    }

    if (IsClk(BackBtnRect(L))) { Close(); return; }

    if (_selA >= 0 && _selA < n && (*_storage)[_selA].type == StorageItemType::Module) {
        if (IsClk(ConfirmBtnRect(L))) {
            int price = SellPrice((*_storage)[_selA].module.grade);
            InventoryManager::Get().AddCredits(price);
            (*_storage)[_selA] = StorageItem{};
            _selA = -1;
        }
    }
}

void StationServicesMenu::UpdateRepair() {
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    if (IsClk(BackBtnRect(L))) { Close(); return; }
    if (!_player) return;

    float missing = _player->health.maxStats.hull - _player->health.currentHull;
    if (missing <= 0.01f) return;
    int cost = std::max(10, (int)std::ceil(missing) * 2);
    if (IsClk(ConfirmBtnRect(L)) && InventoryManager::Get().Credits >= cost) {
        InventoryManager::Get().SpendCredits(cost);
        _player->health.currentHull = _player->health.maxStats.hull;
    }
}

void StationServicesMenu::UpdateEngineer() {
    if (!_storage) return;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    int gx = (int)L.content.x + 20, gy = (int)L.content.y + 20, gw = (int)L.content.width - 40;
    int n  = (int)_storage->size();
    std::vector<Rectangle> rects(n);
    StorageMenu::GetRects(gx, gy, gw, n, rects.data());

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < n; ++i) {
            if (!CheckCollisionPointRec(mouse, rects[i])) continue;
            if ((*_storage)[i].type != StorageItemType::Module) break;
            if      (i == _selA) { _selA = _selB; _selB = -1; }
            else if (i == _selB) { _selB = -1; }
            else if (_selA < 0)  { _selA = i; }
            else if (_selB < 0)  { _selB = i; }
            else                 { _selA = i; _selB = -1; } // start a fresh pair
            break;
        }
    }

    if (IsClk(BackBtnRect(L))) { Close(); return; }

    bool validPair = _selA >= 0 && _selB >= 0 && _selA < n && _selB < n
        && (*_storage)[_selA].type == StorageItemType::Module
        && (*_storage)[_selB].type == StorageItemType::Module
        && (*_storage)[_selA].module.type  == (*_storage)[_selB].module.type
        && (*_storage)[_selA].module.grade == (*_storage)[_selB].module.grade;
    if (!validPair) return;

    ModuleGrade nextG;
    bool hasNext = NextGrade((*_storage)[_selA].module.grade, nextG);
    int  cost    = MergeCost((*_storage)[_selA].module.grade);
    if (hasNext && IsClk(ConfirmBtnRect(L)) && InventoryManager::Get().Credits >= cost) {
        ModuleType type = (*_storage)[_selA].module.type;
        InventoryManager::Get().SpendCredits(cost);
        ModuleDef result = ModuleRegistry::Random(type, nextG);
        (*_storage)[_selA] = StorageItem{ StorageItemType::Module, result.displayName, "", 0, result };
        (*_storage)[_selB] = StorageItem{};
        _selA = _selB = -1;
    }
}

// ── Draw ──────────────────────────────────────────────────────────────────────

void StationServicesMenu::Draw() const {
    if (!isOpen) return;
    using namespace hudtheme;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 2, 4, 9, 215 });

    switch (_screen) {
    case Screen::Main:     DrawMain();     break;
    case Screen::Sell:     DrawSell();     break;
    case Screen::Repair:   DrawRepair();   break;
    case Screen::Engineer: DrawEngineer(); break;
    }
}

void StationServicesMenu::DrawMain() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());

    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    char credLbl[48];
    std::snprintf(credLbl, sizeof(credLbl), "CREDITS: %d", InventoryManager::Get().Credits);
    int cw = MeasureText(credLbl, 14);
    DrawText(credLbl, (int)(L.title.x + L.title.width - cw - 14), (int)L.title.y + 13, 14, HudGood);

    DrawHudBracketPanel(L.content, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);
    const char* msg = "Choose a service.";
    DrawText(msg, (int)(L.content.x + (L.content.width - MeasureText(msg, 16)) / 2.0f),
             (int)(L.content.y + L.content.height / 2.0f - 8), 16, HudLabel);

    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    float bw   = (L.bot.width - 60.0f) / 4.0f;
    float gap  = (L.bot.width - 30.0f - bw * 4.0f) / 3.0f;
    float by   = L.bot.y + L.bot.height - 40.0f - 14.0f;
    float x0   = L.bot.x + 15.0f;
    DrawSvcBtn({ x0,                    by, bw, 40.0f }, "SELL MODULES");
    DrawSvcBtn({ x0 + (bw + gap),        by, bw, 40.0f }, "REPAIR");
    DrawSvcBtn({ x0 + (bw + gap) * 2.0f, by, bw, 40.0f }, "ENGINEER");
    DrawSvcBtn({ x0 + (bw + gap) * 3.0f, by, bw, 40.0f }, "CLOSE  [ESC]");
}

static void DrawSlotGrid(const SvcLayout& L, const std::vector<StorageItem>& storage,
                          int selA, int selB) {
    using namespace hudtheme;
    DrawHudBracketPanel(L.content, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);

    int gx = (int)L.content.x + 20, gy = (int)L.content.y + 20, gw = (int)L.content.width - 40;
    int n  = (int)storage.size();
    std::vector<Rectangle> rects(n);
    StorageMenu::GetRects(gx, gy, gw, n, rects.data());
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < n; ++i) {
        bool hovered  = CheckCollisionPointRec(mouse, rects[i]);
        StorageMenu::DrawItemInSlot(rects[i], storage[i], hovered, false);
        if (i == selA || i == selB) {
            DrawRectangleLinesEx({ rects[i].x - 2, rects[i].y - 2, rects[i].width + 4, rects[i].height + 4 },
                                 2.0f, HudGood);
        }
    }
}

void StationServicesMenu::DrawSell() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > SELL MODULES", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    if (_storage) DrawSlotGrid(L, *_storage, _selA, -1);

    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");

    bool hasSel = _storage && _selA >= 0 && _selA < (int)_storage->size()
        && (*_storage)[_selA].type == StorageItemType::Module;
    if (hasSel) {
        const StorageItem& item = (*_storage)[_selA];
        int price = SellPrice(item.module.grade);
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "SELL FOR %d CR", price);
        DrawSvcBtn(ConfirmBtnRect(L), lbl);
    } else {
        DrawSvcBtn(ConfirmBtnRect(L), "SELECT A MODULE", false);
    }
}

void StationServicesMenu::DrawRepair() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > REPAIR", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    DrawHudBracketPanel(L.content, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);

    float hull = _player ? _player->health.currentHull       : 0.0f;
    float maxH = _player ? _player->health.maxStats.hull      : 1.0f;
    char hullLbl[64];
    std::snprintf(hullLbl, sizeof(hullLbl), "HULL INTEGRITY: %.0f / %.0f", hull, maxH);
    DrawText(hullLbl, (int)(L.content.x + (L.content.width - MeasureText(hullLbl, 18)) / 2.0f),
             (int)(L.content.y + L.content.height / 2.0f - 30.0f), 18, HudValue);

    float missing = maxH - hull;
    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");

    if (missing <= 0.01f) {
        DrawSvcBtn(ConfirmBtnRect(L), "HULL AT FULL", false);
        return;
    }
    int  cost      = std::max(10, (int)std::ceil(missing) * 2);
    bool canAfford = InventoryManager::Get().Credits >= cost;
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "REPAIR FOR %d CR", cost);
    DrawSvcBtn(ConfirmBtnRect(L), lbl, canAfford);

    char costLbl[64];
    std::snprintf(costLbl, sizeof(costLbl), "REPAIR COST: %d CR/PT MISSING (MIN 10)", 2);
    DrawText(costLbl, (int)(L.content.x + (L.content.width - MeasureText(costLbl, 12)) / 2.0f),
             (int)(L.content.y + L.content.height / 2.0f + 4.0f), 12, HudLabel);
}

void StationServicesMenu::DrawEngineer() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > ENGINEER", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    if (_storage) DrawSlotGrid(L, *_storage, _selA, _selB);

    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");

    int n = _storage ? (int)_storage->size() : 0;
    bool validPair = _storage && _selA >= 0 && _selB >= 0 && _selA < n && _selB < n
        && (*_storage)[_selA].type == StorageItemType::Module
        && (*_storage)[_selB].type == StorageItemType::Module
        && (*_storage)[_selA].module.type  == (*_storage)[_selB].module.type
        && (*_storage)[_selA].module.grade == (*_storage)[_selB].module.grade;

    if (!validPair) {
        DrawSvcBtn(ConfirmBtnRect(L), "SELECT 2 MATCHING MODULES", false);
        return;
    }
    ModuleGrade nextG;
    bool hasNext = NextGrade((*_storage)[_selA].module.grade, nextG);
    if (!hasNext) {
        DrawSvcBtn(ConfirmBtnRect(L), "ALREADY MYTHIC", false);
        return;
    }
    int  cost      = MergeCost((*_storage)[_selA].module.grade);
    bool canAfford = InventoryManager::Get().Credits >= cost;
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "MERGE FOR %d CR", cost);
    DrawSvcBtn(ConfirmBtnRect(L), lbl, canAfford);
}

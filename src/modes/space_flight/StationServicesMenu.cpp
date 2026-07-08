#include "StationServicesMenu.h"
#include "modes/space_flight/StorageMenu.h"
#include "shared/ui/HudTheme.h"
#include "core/InventoryManager.h"
#include "data/registry/ModuleRegistry.h"
#include "data/registry/MaterialRegistry.h"
#include "data/registry/ItemRegistry.h"
#include "data/registry/BuildableRegistry.h"
#include "systems/diplomacy/ReputationRegistry.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// Buy screen: 3 sub-tabs + a scrollable row list, both laid out inside the
// shared content panel. Computed identically by Update/Draw, same convention
// as the rest of this menu's per-screen layout helpers.
static void CalcBuyGeom(const SvcLayout& L, Rectangle tabRects[3], Rectangle& listArea) {
    float tabY = L.content.y + 14.0f;
    float tabH = 30.0f;
    float tabW = (L.content.width - 40.0f) / 3.0f;
    float x0   = L.content.x + 20.0f;
    for (int i = 0; i < 3; ++i)
        tabRects[i] = { x0 + i * tabW, tabY, tabW - 4.0f, tabH };
    float listY = tabY + tabH + 10.0f;
    listArea = { L.content.x + 20.0f, listY,
                 L.content.width - 40.0f, (L.content.y + L.content.height) - listY - 14.0f };
}

// Finds the first Empty slot in storage and fills it. Non-stackable results
// (modules, hardpoints) only.
static bool PlaceInFirstEmptySlot(std::vector<StorageItem>& storage, const StorageItem& item) {
    for (StorageItem& s : storage) {
        if (s.type == StorageItemType::Empty) { s = item; return true; }
    }
    return false;
}

// Stacks onto an existing Material slot of the same id, or falls back to the
// first Empty slot. Returns false if neither is available (storage full).
static bool StackOrPlaceMaterial(std::vector<StorageItem>& storage, const std::string& id,
                                  const std::string& displayName, int amount) {
    for (StorageItem& s : storage) {
        if (s.type == StorageItemType::Material && s.materialId == id) {
            s.count = std::min(s.count + amount, StorageMenu::MaxStack);
            return true;
        }
    }
    return PlaceInFirstEmptySlot(storage, StorageItem{ StorageItemType::Material, displayName, id, amount });
}

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

// Base (un-economy-adjusted) per-unit value for a raw material or crafted
// item, looked up from whichever registry defines it (raw ore vs. refined
// component).
static int MaterialBaseValue(const std::string& materialId) {
    if (const MatDef* m = MaterialRegistry::ById(materialId)) return m->sellValue;
    if (const ItemDef* i = ItemRegistry::ById(materialId))    return i->sellValue;
    return 5;
}

// Per-unit price the station pays the player right now for materialId. Reads
// live per-station stock (Epic 3) when an economy is attached; otherwise
// falls back to the flat base value, same as before Epic 3.
static int MaterialSellPrice(const StationEconomy* econ, const std::string& materialId) {
    int base = MaterialBaseValue(materialId);
    return econ ? econ->SellUnitPrice(materialId, base) : base;
}

// Per-unit price the player pays to buy materialId from the station.
static int MaterialBuyPrice(const StationEconomy* econ, const std::string& materialId) {
    int base = MaterialBaseValue(materialId);
    return econ ? econ->BuyUnitPrice(materialId, base) : (int)std::ceil(base * 3.0f);
}

// Hardpoints have no ModuleGrade — their "rarity" is however much structure
// they add, so sell price scales with total slot count and hull.
static int HardpointSellPrice(const StationHardpointDef& hp) {
    int slots = hp.wSlots + hp.arSlots + hp.shSlots + hp.enSlots + hp.auxSlots;
    return 20 + slots * 15 + (int)(hp.maxHull * 0.3f);
}

// "Buy" screen convenience tax over the equivalent sell value — instant
// delivery costs more than gathering materials and crafting it yourself.
static constexpr float kBuyMarkup = 3.0f;

// Epic 4: hyperdrive fuel is purchased as "fuel_cells" stock (Epic 3's
// economy — production/hauling reaches it same as any other good, see
// SpaceFlight::TickNpcEconomy), each cell converting to a fixed amount of
// tank fuel. A station with 0 fuel_cells in stock simply can't refuel you,
// same as it can't sell any other good it's out of.
static constexpr const char* kFuelCellsId  = "fuel_cells";
static constexpr float       kFuelPerCell  = 5.0f;

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

// Repair screen: percentage slider + exact-value textbox + a MAX shortcut,
// all centered on one row below the hull readout.
static Rectangle RepairSliderRect(const SvcLayout& L) {
    float w = std::min(420.0f, L.content.width - 220.0f);
    float x = L.content.x + (L.content.width - w) / 2.0f - 60.0f;
    float y = L.content.y + L.content.height / 2.0f + 14.0f;
    return { x, y, w, 10.0f };
}
static Rectangle RepairHandleRect(const Rectangle& slider, float pct) {
    float cx = slider.x + slider.width * (pct / 100.0f);
    return { cx - 7.0f, slider.y - 7.0f, 14.0f, 24.0f };
}
static Rectangle RepairTextBoxRect(const SvcLayout& L) {
    Rectangle s = RepairSliderRect(L);
    return { s.x + s.width + 16.0f, s.y - 11.0f, 60.0f, 30.0f };
}
static Rectangle RepairMaxBtnRect(const SvcLayout& L) {
    Rectangle t = RepairTextBoxRect(L);
    return { t.x + t.width + 12.0f, t.y, 64.0f, 30.0f };
}

// ── Open / Close ──────────────────────────────────────────────────────────────

void StationServicesMenu::Open(ecs::Entity* player, std::vector<StorageItem>* storage, StationEconomy* economy,
                                float* fuel, float maxFuel, Faction stationFaction,
                                std::vector<Contract>* offers, Contract* activeContract, bool* hasActiveContract) {
    _player  = player;
    _storage = storage;
    _economy = economy;
    _fuel    = fuel;
    _maxFuel = maxFuel;
    _stationFaction    = stationFaction;
    _offers            = offers;
    _activeContract    = activeContract;
    _hasActiveContract = hasActiveContract;
    _screen  = Screen::Main;
    _selA = _selB = -1;
    _buyTab = BuyTab::Crafts;
    _buyScroll = 0;
    _repairPct = 100.0f;
    _repairText = "100";
    _repairTextActive = false;
    _repairDraggingSlider = false;
    isOpen = true;
}

void StationServicesMenu::Close() {
    if (_screen != Screen::Main) {
        _screen = Screen::Main;
        _selA = _selB = -1;
        _repairTextActive = false;
        _repairDraggingSlider = false;
    } else {
        isOpen = false;
    }
}

// ── Update ────────────────────────────────────────────────────────────────────

void StationServicesMenu::Update() {
    if (!isOpen) return;
    if (IsKeyPressed(KEY_ESCAPE) && !_repairTextActive) { Close(); return; }

    switch (_screen) {
    case Screen::Main:     UpdateMain();     break;
    case Screen::Sell:     UpdateSell();     break;
    case Screen::Buy:      UpdateBuy();      break;
    case Screen::Repair:   UpdateRepair();   break;
    case Screen::Engineer: UpdateEngineer(); break;
    case Screen::Fuel:     UpdateFuel();     break;
    case Screen::Contracts: UpdateContracts(); break;
    }
}

void StationServicesMenu::UpdateMain() {
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    float bw   = (L.bot.width - 30.0f - 60.0f) / 7.0f;
    float gap  = 10.0f;
    float by   = L.bot.y + L.bot.height - 40.0f - 14.0f;
    float x0   = L.bot.x + 15.0f;
    Rectangle sellBtn   = { x0,                    by, bw, 40.0f };
    Rectangle buyBtn    = { x0 + (bw + gap),        by, bw, 40.0f };
    Rectangle repairBtn = { x0 + (bw + gap) * 2.0f, by, bw, 40.0f };
    Rectangle fuelBtn   = { x0 + (bw + gap) * 3.0f, by, bw, 40.0f };
    Rectangle engBtn    = { x0 + (bw + gap) * 4.0f, by, bw, 40.0f };
    Rectangle contrBtn  = { x0 + (bw + gap) * 5.0f, by, bw, 40.0f };
    Rectangle closeBtn  = { x0 + (bw + gap) * 6.0f, by, bw, 40.0f };

    if (IsClk(sellBtn))   { _screen = Screen::Sell;     _selA = _selB = -1; }
    if (IsClk(buyBtn))    { _screen = Screen::Buy;      _buyTab = BuyTab::Crafts; _buyScroll = 0; }
    if (IsClk(repairBtn)) { _screen = Screen::Repair; }
    if (IsClk(fuelBtn))   { _screen = Screen::Fuel; }
    if (IsClk(engBtn))    { _screen = Screen::Engineer; _selA = _selB = -1; }
    if (IsClk(contrBtn))  { _screen = Screen::Contracts; }
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
            StorageItemType t = (*_storage)[i].type;
            if (t == StorageItemType::Module || t == StorageItemType::Material || t == StorageItemType::Hardpoint)
                _selA = (_selA == i) ? -1 : i;
            break;
        }
    }

    if (IsClk(BackBtnRect(L))) { Close(); return; }

    if (_selA >= 0 && _selA < n) {
        StorageItem& item = (*_storage)[_selA];
        if (item.type == StorageItemType::Module && IsClk(ConfirmBtnRect(L))) {
            int price = SellPrice(item.module.grade);
            InventoryManager::Get().AddCredits(price);
            item = StorageItem{};
            _selA = -1;
        }
        else if (item.type == StorageItemType::Material && IsClk(ConfirmBtnRect(L))) {
            int price = MaterialSellPrice(_economy, item.materialId) * item.count;
            InventoryManager::Get().AddCredits(price);
            if (_economy) _economy->AddStock(item.materialId, item.count);
            ReputationRegistry::Adjust(_stationFaction, 0.5f); // Epic 6.3: trade nudges standing
            item = StorageItem{};
            _selA = -1;
        }
        else if (item.type == StorageItemType::Hardpoint && IsClk(ConfirmBtnRect(L))) {
            int price = HardpointSellPrice(item.hardpoint);
            InventoryManager::Get().AddCredits(price);
            item = StorageItem{};
            _selA = -1;
        }
    }
}

void StationServicesMenu::UpdateBuy() {
    if (!_storage) return;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    Rectangle tabRects[3], listArea;
    CalcBuyGeom(L, tabRects, listArea);
    Vector2 mouse = GetMousePosition();

    if (IsClk(BackBtnRect(L))) { Close(); return; }

    for (int i = 0; i < 3; ++i)
        if (IsClk(tabRects[i])) { _buyTab = (BuyTab)i; _buyScroll = 0; }

    int count = 0;
    if      (_buyTab == BuyTab::Crafts)    count = (int)ItemRegistry::All().size();
    else if (_buyTab == BuyTab::Modules)   count = (int)BuildableRegistry::ByType(BuildableType::Module).size();
    else                                   count = (int)BuildableRegistry::ByType(BuildableType::Hardpoint).size();

    static constexpr float RowH = 54.0f, RowGap = 6.0f;
    if (CheckCollisionPointRec(mouse, listArea)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) _buyScroll -= (int)(wheel * 24.0f);
    }
    int totalH = (int)(count * (RowH + RowGap));
    int maxScroll = std::max(0, totalH - (int)listArea.height);
    _buyScroll = std::clamp(_buyScroll, 0, maxScroll);

    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) return;

    for (int i = 0; i < count; ++i) {
        Rectangle row = { listArea.x, listArea.y + i * (RowH + RowGap) - _buyScroll, listArea.width, RowH };
        if (row.y + row.height <= listArea.y || row.y >= listArea.y + listArea.height) continue;
        Rectangle btn = { row.x + row.width - 110.0f, row.y + row.height / 2.0f - 16.0f, 100.0f, 32.0f };
        if (!CheckCollisionPointRec(mouse, btn)) continue;

        if (_buyTab == BuyTab::Crafts) {
            const ItemDef& item = ItemRegistry::All()[i];
            int  price   = MaterialBuyPrice(_economy, item.id);
            bool inStock = !_economy || _economy->GetStock(item.id) > 0;
            if (inStock && InventoryManager::Get().Credits >= price &&
                StackOrPlaceMaterial(*_storage, item.id, item.displayName, 1)) {
                InventoryManager::Get().SpendCredits(price);
                if (_economy) _economy->RemoveStock(item.id, 1);
                ReputationRegistry::Adjust(_stationFaction, 0.5f); // Epic 6.3: trade nudges standing
            }
        }
        else if (_buyTab == BuyTab::Modules) {
            auto items = BuildableRegistry::ByType(BuildableType::Module);
            auto modOpt = ModuleRegistry::ById(items[i].moduleDefId);
            if (modOpt.has_value()) {
                int price = (int)std::ceil(SellPrice(modOpt->grade) * kBuyMarkup);
                StorageItem toAdd{ StorageItemType::Module, modOpt->displayName, "", 0, *modOpt };
                if (InventoryManager::Get().Credits >= price && PlaceInFirstEmptySlot(*_storage, toAdd)) {
                    InventoryManager::Get().SpendCredits(price);
                }
            }
        }
        else {
            auto items = BuildableRegistry::ByType(BuildableType::Hardpoint);
            int price = (int)std::ceil(HardpointSellPrice(items[i].hardpointDef) * kBuyMarkup);
            StorageItem toAdd{ StorageItemType::Hardpoint, items[i].displayName, "", 0, ModuleDef{}, items[i].hardpointDef };
            if (InventoryManager::Get().Credits >= price && PlaceInFirstEmptySlot(*_storage, toAdd)) {
                InventoryManager::Get().SpendCredits(price);
            }
        }
        break;
    }
}

void StationServicesMenu::UpdateRepair() {
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());

    if (IsKeyPressed(KEY_ESCAPE) && _repairTextActive) { _repairTextActive = false; return; }
    if (IsClk(BackBtnRect(L))) { Close(); return; }
    if (!_player) return;

    float missing = _player->health.maxStats.hull - _player->health.currentHull;

    Rectangle slider  = RepairSliderRect(L);
    Rectangle handle  = RepairHandleRect(slider, _repairPct);
    Rectangle textBox = RepairTextBoxRect(L);
    Rectangle maxBtn  = RepairMaxBtnRect(L);
    Vector2   mouse   = GetMousePosition();

    auto SyncTextFromPct = [&]() {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", (int)std::round(_repairPct));
        _repairText = buf;
    };

    // Slider drag (click-drag anywhere on the track or its handle)
    if (!_repairDraggingSlider && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        (CheckCollisionPointRec(mouse, handle) || CheckCollisionPointRec(mouse, slider))) {
        _repairDraggingSlider = true;
        _repairTextActive = false;
    }
    if (_repairDraggingSlider) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float t = (mouse.x - slider.x) / slider.width;
            _repairPct = std::clamp(t, 0.0f, 1.0f) * 100.0f;
            SyncTextFromPct();
        } else {
            _repairDraggingSlider = false;
        }
    }

    // Textbox: click to focus, type digits, Enter/click-away to commit
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        _repairTextActive = IsHov(textBox);

    if (_repairTextActive) {
        int ch = GetCharPressed();
        while (ch > 0) {
            if (ch >= '0' && ch <= '9' && _repairText.size() < 3)
                _repairText += (char)ch;
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !_repairText.empty())
            _repairText.pop_back();
        if (IsKeyPressed(KEY_ENTER)) _repairTextActive = false;

        _repairPct = _repairText.empty() ? 0.0f
                   : (float)std::clamp(std::atoi(_repairText.c_str()), 0, 100);
    }

    if (IsClk(maxBtn) && missing > 0.01f) {
        int credits = InventoryManager::Get().Credits;
        // Largest % whose cost (rounded up per existing per-point rate) still
        // fits current credits; floor keeps the eventual Confirm affordable.
        float pctAfford = (credits < 10) ? 0.0f
                         : std::min(100.0f, std::floor((credits / 2.0f) / missing * 100.0f));
        _repairPct = pctAfford;
        SyncTextFromPct();
    }

    if (missing <= 0.01f) return;
    float healAmount = missing * (_repairPct / 100.0f);
    if (healAmount <= 0.01f) return;
    int cost = std::max(10, (int)std::ceil(healAmount) * 2);

    if (IsClk(ConfirmBtnRect(L)) && InventoryManager::Get().Credits >= cost) {
        InventoryManager::Get().SpendCredits(cost);
        _player->health.currentHull = std::min(_player->health.currentHull + healAmount,
                                                _player->health.maxStats.hull);
    }
}

void StationServicesMenu::UpdateFuel() {
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    if (IsClk(BackBtnRect(L))) { Close(); return; }
    if (!_fuel || !_economy) return; // no fuel system / no station stock to draw from

    float missing = _maxFuel - *_fuel;
    if (missing <= 0.01f) return;

    int cellsNeeded    = (int)std::ceil(missing / kFuelPerCell);
    int availableCells = _economy->GetStock(kFuelCellsId);
    int cellsToBuy      = std::min(cellsNeeded, availableCells);
    if (cellsToBuy <= 0) return;

    int base      = MaterialBaseValue(kFuelCellsId);
    int unitPrice = _economy->BuyUnitPrice(kFuelCellsId, base);
    int totalCost = unitPrice * cellsToBuy;

    if (IsClk(ConfirmBtnRect(L)) && InventoryManager::Get().Credits >= totalCost) {
        InventoryManager::Get().SpendCredits(totalCost);
        _economy->RemoveStock(kFuelCellsId, cellsToBuy);
        *_fuel = std::min(_maxFuel, *_fuel + cellsToBuy * kFuelPerCell);
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

// Epic 7: offers are display-only while a contract is already active (Draw
// shows its progress instead of the offer list in that state); accepting a
// Courier debits its cargo from the docked station's live stock immediately
// (skipped if stock shifted below what was quoted since the offer was
// generated — the whole offer list is regenerated fresh on the next dock).
void StationServicesMenu::UpdateContracts() {
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    if (IsClk(BackBtnRect(L))) { Close(); return; }
    if (!_offers || !_activeContract || !_hasActiveContract || *_hasActiveContract) return;
    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) return;

    Vector2 mouse = GetMousePosition();
    static constexpr float RowH = 74.0f, RowGap = 10.0f;
    float listY = L.content.y + 20.0f;
    for (size_t i = 0; i < _offers->size(); ++i) {
        Rectangle row = { L.content.x + 20.0f, listY + i * (RowH + RowGap), L.content.width - 40.0f, RowH };
        Rectangle btn = { row.x + row.width - 130.0f, row.y + row.height / 2.0f - 18.0f, 110.0f, 36.0f };
        if (!CheckCollisionPointRec(mouse, btn)) continue;

        Contract& offer = (*_offers)[i];
        if (offer.type == ContractType::Courier) {
            if (!_economy || _economy->GetStock(offer.goodId) < offer.amount) continue;
            _economy->RemoveStock(offer.goodId, offer.amount);
        }
        *_activeContract    = offer;
        *_hasActiveContract = true;
        break;
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
    case Screen::Buy:      DrawBuy();      break;
    case Screen::Repair:   DrawRepair();   break;
    case Screen::Engineer: DrawEngineer(); break;
    case Screen::Fuel:     DrawFuel();     break;
    case Screen::Contracts: DrawContracts(); break;
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
    float bw   = (L.bot.width - 30.0f - 60.0f) / 7.0f;
    float gap  = 10.0f;
    float by   = L.bot.y + L.bot.height - 40.0f - 14.0f;
    float x0   = L.bot.x + 15.0f;
    DrawSvcBtn({ x0,                    by, bw, 40.0f }, "SELL ITEMS");
    DrawSvcBtn({ x0 + (bw + gap),        by, bw, 40.0f }, "BUY ITEMS");
    DrawSvcBtn({ x0 + (bw + gap) * 2.0f, by, bw, 40.0f }, "REPAIR");
    DrawSvcBtn({ x0 + (bw + gap) * 3.0f, by, bw, 40.0f }, "FUEL");
    DrawSvcBtn({ x0 + (bw + gap) * 4.0f, by, bw, 40.0f }, "ENGINEER");
    DrawSvcBtn({ x0 + (bw + gap) * 5.0f, by, bw, 40.0f }, "CONTRACTS");
    DrawSvcBtn({ x0 + (bw + gap) * 6.0f, by, bw, 40.0f }, "CLOSE  [ESC]");
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
    DrawText("STATION SERVICES > SELL ITEMS", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    if (_storage) DrawSlotGrid(L, *_storage, _selA, -1);

    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");

    bool validIdx = _storage && _selA >= 0 && _selA < (int)_storage->size();
    StorageItemType selType = validIdx ? (*_storage)[_selA].type : StorageItemType::Empty;
    if (validIdx && selType == StorageItemType::Module) {
        const StorageItem& item = (*_storage)[_selA];
        int price = SellPrice(item.module.grade);
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "SELL FOR %d CR", price);
        DrawSvcBtn(ConfirmBtnRect(L), lbl);
    } else if (validIdx && selType == StorageItemType::Material) {
        const StorageItem& item = (*_storage)[_selA];
        int price = MaterialSellPrice(_economy, item.materialId) * item.count;
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "SELL x%d FOR %d CR", item.count, price);
        DrawSvcBtn(ConfirmBtnRect(L), lbl);
        if (_economy) {
            int stock = _economy->GetStock(item.materialId);
            const char* trend = stock < StationEconomy::kBaselineStock ? "SCARCE - HIGH PRICE"
                              : stock > StationEconomy::kBaselineStock * 2 ? "SURPLUS - LOW PRICE" : "STABLE";
            char stockLbl[64];
            std::snprintf(stockLbl, sizeof(stockLbl), "STATION STOCK: %d  (%s)", stock, trend);
            DrawText(stockLbl, (int)(L.content.x + 20), (int)(L.content.y + L.content.height - 24.0f), 12, HudLabel);
        }
    } else if (validIdx && selType == StorageItemType::Hardpoint) {
        const StorageItem& item = (*_storage)[_selA];
        int price = HardpointSellPrice(item.hardpoint);
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "SELL FOR %d CR", price);
        DrawSvcBtn(ConfirmBtnRect(L), lbl);
    } else {
        DrawSvcBtn(ConfirmBtnRect(L), "SELECT AN ITEM", false);
    }
}

void StationServicesMenu::DrawBuy() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > BUY ITEMS", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    char credLbl[48];
    std::snprintf(credLbl, sizeof(credLbl), "CREDITS: %d", InventoryManager::Get().Credits);
    int cw = MeasureText(credLbl, 14);
    DrawText(credLbl, (int)(L.title.x + L.title.width - cw - 14), (int)L.title.y + 13, 14, HudGood);

    DrawHudBracketPanel(L.content, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);

    Rectangle tabRects[3], listArea;
    CalcBuyGeom(L, tabRects, listArea);
    Vector2 mouse = GetMousePosition();

    const char* tabLabels[3] = { "CRAFTS", "MODULES", "HARDPOINTS" };
    for (int i = 0; i < 3; ++i) {
        bool active = ((int)_buyTab == i);
        bool hov    = CheckCollisionPointRec(mouse, tabRects[i]);
        Color bg  = active ? Color{ 20,55,80,230 } : (hov ? Color{ 14,35,55,220 } : Color{ 10,20,35,200 });
        Color bdr = active ? HudBorder : Color{ 30,60,90,160 };
        DrawRectangleRec(tabRects[i], bg);
        DrawRectangleLinesEx(tabRects[i], active ? 1.5f : 1.0f, bdr);
        DrawText(tabLabels[i],
                 (int)(tabRects[i].x + (tabRects[i].width - MeasureText(tabLabels[i], 11)) / 2.0f),
                 (int)(tabRects[i].y + 9), 11, active ? WHITE : HudLabel);
    }

    int count = 0;
    if      (_buyTab == BuyTab::Crafts)    count = (int)ItemRegistry::All().size();
    else if (_buyTab == BuyTab::Modules)   count = (int)BuildableRegistry::ByType(BuildableType::Module).size();
    else                                   count = (int)BuildableRegistry::ByType(BuildableType::Hardpoint).size();

    static constexpr float RowH = 54.0f, RowGap = 6.0f;
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int i = 0; i < count; ++i) {
        Rectangle row = { listArea.x, listArea.y + i * (RowH + RowGap) - _buyScroll, listArea.width, RowH };
        if (row.y + row.height <= listArea.y || row.y >= listArea.y + listArea.height) continue;

        std::string name;
        int  price = 0;
        bool valid = true;
        if (_buyTab == BuyTab::Crafts) {
            const ItemDef& item = ItemRegistry::All()[i];
            name  = item.displayName;
            price = MaterialBuyPrice(_economy, item.id);
            valid = !_economy || _economy->GetStock(item.id) > 0;
        } else if (_buyTab == BuyTab::Modules) {
            auto items = BuildableRegistry::ByType(BuildableType::Module);
            name = items[i].displayName;
            auto modOpt = ModuleRegistry::ById(items[i].moduleDefId);
            valid = modOpt.has_value();
            if (valid) price = (int)std::ceil(SellPrice(modOpt->grade) * kBuyMarkup);
        } else {
            auto items = BuildableRegistry::ByType(BuildableType::Hardpoint);
            name  = items[i].displayName;
            price = (int)std::ceil(HardpointSellPrice(items[i].hardpointDef) * kBuyMarkup);
        }

        bool hovRow = CheckCollisionPointRec(mouse, row);
        DrawRectangleRec(row, hovRow ? Color{ 16,30,45,220 } : Color{ 10,18,32,190 });
        DrawRectangleLinesEx(row, 1.0f, Color{ 30,60,90,160 });
        DrawText(name.c_str(), (int)row.x + 10, (int)row.y + 8, 13, HudValue);

        char priceLbl[48];
        if (_buyTab == BuyTab::Crafts && _economy) {
            std::snprintf(priceLbl, sizeof(priceLbl), "%d CR   STOCK: %d", price,
                          _economy->GetStock(ItemRegistry::All()[i].id));
        } else {
            std::snprintf(priceLbl, sizeof(priceLbl), "%d CR", price);
        }
        DrawText(priceLbl, (int)row.x + 10, (int)row.y + 28, 12, HudGood);

        Rectangle btn = { row.x + row.width - 110.0f, row.y + row.height / 2.0f - 16.0f, 100.0f, 32.0f };
        bool canAfford = valid && InventoryManager::Get().Credits >= price;
        DrawSvcBtn(btn, valid ? "BUY" : "OUT OF STOCK", canAfford);
    }
    EndScissorMode();

    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");
}

void StationServicesMenu::DrawRepair() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > REPAIR", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    char credLbl[48];
    std::snprintf(credLbl, sizeof(credLbl), "CREDITS: %d", InventoryManager::Get().Credits);
    int cw = MeasureText(credLbl, 14);
    DrawText(credLbl, (int)(L.title.x + L.title.width - cw - 14), (int)L.title.y + 13, 14, HudGood);

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

    // ── Slider + textbox + MAX ────────────────────────────────────────────
    Rectangle slider  = RepairSliderRect(L);
    Rectangle handle  = RepairHandleRect(slider, _repairPct);
    Rectangle textBox = RepairTextBoxRect(L);
    Rectangle maxBtn  = RepairMaxBtnRect(L);
    Vector2   mouse   = GetMousePosition();

    DrawRectangleRec(slider, Color{ 20,20,30,220 });
    DrawRectangleLinesEx(slider, 1.0f, HudDiv);
    Rectangle fill = { slider.x, slider.y, slider.width * (_repairPct / 100.0f), slider.height };
    DrawRectangleRec(fill, Color{ 60,150,220,220 });
    bool hovHandle = CheckCollisionPointRec(mouse, handle);
    DrawRectangleRec(handle, (hovHandle || _repairDraggingSlider) ? Color{ 120,210,255,255 } : Color{ 90,170,230,240 });
    DrawRectangleLinesEx(handle, 1.0f, HudBorder);

    bool hovText = IsHov(textBox);
    DrawRectangleRec(textBox, _repairTextActive ? Color{ 20,40,55,240 } : Color{ 14,20,28,210 });
    DrawRectangleLinesEx(textBox, (_repairTextActive || hovText) ? 1.5f : 1.0f,
                         _repairTextActive ? HudGood : HudBorder);
    std::string textDisp = _repairText + (_repairTextActive ? "_" : "");
    DrawText(textDisp.c_str(), (int)(textBox.x + 8), (int)(textBox.y + 7), 15, HudValue);
    DrawText("%", (int)(textBox.x + textBox.width + 4), (int)(textBox.y + 7), 15, HudLabel);

    DrawSvcBtn(maxBtn, "MAX");

    const char* pctCaption = "% OF MISSING HULL TO REPAIR";
    DrawText(pctCaption, (int)(slider.x), (int)(slider.y - 22), 11, HudLabel);

    // ── Cost preview + confirm ────────────────────────────────────────────
    float healAmount = missing * (_repairPct / 100.0f);
    if (healAmount <= 0.01f) {
        DrawSvcBtn(ConfirmBtnRect(L), "SELECT AN AMOUNT", false);
        return;
    }
    int  cost      = std::max(10, (int)std::ceil(healAmount) * 2);
    bool canAfford = InventoryManager::Get().Credits >= cost;
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "REPAIR %.0f HULL FOR %d CR", healAmount, cost);
    DrawSvcBtn(ConfirmBtnRect(L), lbl, canAfford);

    char costLbl[64];
    std::snprintf(costLbl, sizeof(costLbl), "RATE: 2 CR/PT MISSING (MIN 10 CR)");
    DrawText(costLbl, (int)(L.content.x + (L.content.width - MeasureText(costLbl, 12)) / 2.0f),
             (int)(L.content.y + L.content.height - 26.0f), 12, HudLabel);
}

void StationServicesMenu::DrawFuel() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > FUEL", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    char credLbl[48];
    std::snprintf(credLbl, sizeof(credLbl), "CREDITS: %d", InventoryManager::Get().Credits);
    int cw = MeasureText(credLbl, 14);
    DrawText(credLbl, (int)(L.title.x + L.title.width - cw - 14), (int)L.title.y + 13, 14, HudGood);

    DrawHudBracketPanel(L.content, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);
    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");

    if (!_fuel) {
        const char* msg = "FUEL SYSTEM UNAVAILABLE";
        DrawText(msg, (int)(L.content.x + (L.content.width - MeasureText(msg, 16)) / 2.0f),
                 (int)(L.content.y + L.content.height / 2.0f - 8), 16, HudCaution);
        DrawSvcBtn(ConfirmBtnRect(L), "N/A", false);
        return;
    }

    float pct = *_fuel / _maxFuel;
    Color fuelCol = pct > 0.5f ? HudGood : pct > 0.2f ? HudCaution : HudCritical;
    char fuelLbl[64];
    std::snprintf(fuelLbl, sizeof(fuelLbl), "FUEL TANK: %.0f / %.0f", *_fuel, _maxFuel);
    DrawText(fuelLbl, (int)(L.content.x + (L.content.width - MeasureText(fuelLbl, 18)) / 2.0f),
             (int)(L.content.y + L.content.height / 2.0f - 30.0f), 18, HudValue);

    Rectangle bar = { L.content.x + 60.0f, L.content.y + L.content.height / 2.0f, L.content.width - 120.0f, 16.0f };
    DrawRectangleRec(bar, Color{ 20,20,30,220 });
    DrawRectangleLinesEx(bar, 1.0f, HudDiv);
    DrawRectangleRec({ bar.x, bar.y, bar.width * std::clamp(pct, 0.0f, 1.0f), bar.height }, fuelCol);

    float missing = _maxFuel - *_fuel;
    if (missing <= 0.01f) {
        DrawSvcBtn(ConfirmBtnRect(L), "TANK FULL", false);
        return;
    }
    if (!_economy) {
        DrawSvcBtn(ConfirmBtnRect(L), "NO FUEL DEPOT HERE", false);
        return;
    }

    int cellsNeeded    = (int)std::ceil(missing / kFuelPerCell);
    int availableCells = _economy->GetStock(kFuelCellsId);
    int cellsToBuy      = std::min(cellsNeeded, availableCells);

    char stockLbl[64];
    std::snprintf(stockLbl, sizeof(stockLbl), "STATION FUEL CELLS IN STOCK: %d", availableCells);
    DrawText(stockLbl, (int)(L.content.x + 20), (int)(L.content.y + L.content.height - 24.0f), 12, HudLabel);

    if (cellsToBuy <= 0) {
        DrawSvcBtn(ConfirmBtnRect(L), "OUT OF FUEL CELLS", false);
        return;
    }

    int base      = MaterialBaseValue(kFuelCellsId);
    int unitPrice = _economy->BuyUnitPrice(kFuelCellsId, base);
    int totalCost = unitPrice * cellsToBuy;
    bool canAfford = InventoryManager::Get().Credits >= totalCost;
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "REFUEL +%.0f FOR %d CR", cellsToBuy * kFuelPerCell, totalCost);
    DrawSvcBtn(ConfirmBtnRect(L), lbl, canAfford);
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

static const char* ContractTypeLabel(ContractType t) {
    switch (t) {
        case ContractType::Bounty:  return "BOUNTY";
        case ContractType::Courier: return "COURIER";
        case ContractType::Escort:  return "ESCORT";
        default:                    return "";
    }
}

void StationServicesMenu::DrawContracts() const {
    using namespace hudtheme;
    auto L = CalcSvcLayout(GetScreenWidth(), GetScreenHeight());
    DrawHudBracketPanel(L.title, HudBg, HudBorder, 12.0f, 2.0f);
    DrawText("STATION SERVICES > CONTRACTS", (int)L.title.x + 14, (int)L.title.y + 11, 16, HudValue);

    DrawHudBracketPanel(L.content, Color{ 4, 8, 14, 250 }, HudBorder, 18.0f, 2.0f);
    DrawHudBracketPanel(L.bot, HudBg, HudBorder, 14.0f, 2.0f);
    DrawSvcBtn(BackBtnRect(L), "BACK  [ESC]");

    if (_hasActiveContract && *_hasActiveContract && _activeContract) {
        const Contract& c = *_activeContract;
        float y = L.content.y + 30.0f;
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "ACTIVE %s CONTRACT", ContractTypeLabel(c.type));
        DrawText(hdr, (int)(L.content.x + 20), (int)y, 16, HudGood); y += 28;
        DrawText(c.title.c_str(), (int)(L.content.x + 20), (int)y, 15, HudValue); y += 24;
        DrawText(c.briefing.c_str(), (int)(L.content.x + 20), (int)y, 12, HudLabel); y += 26;
        char buf[64];
        if (c.type == ContractType::Bounty)
            std::snprintf(buf, sizeof(buf), "PROGRESS: %d / %d kills", c.killsDone, c.killsRequired);
        else
            std::snprintf(buf, sizeof(buf), "TIME REMAINING: %.0fs", std::max(0.0f, c.timeRemaining));
        DrawText(buf, (int)(L.content.x + 20), (int)y, 13, HudValue);
        return;
    }

    if (!_offers || _offers->empty()) {
        const char* msg = "NO CONTRACTS AVAILABLE";
        DrawText(msg, (int)(L.content.x + (L.content.width - MeasureText(msg, 16)) / 2.0f),
                 (int)(L.content.y + L.content.height / 2.0f - 8), 16, HudCaution);
        return;
    }

    static constexpr float RowH = 74.0f, RowGap = 10.0f;
    float listY = L.content.y + 20.0f;
    for (size_t i = 0; i < _offers->size(); ++i) {
        const Contract& c = (*_offers)[i];
        Rectangle row = { L.content.x + 20.0f, listY + i * (RowH + RowGap), L.content.width - 40.0f, RowH };
        DrawHudChamferRect(row, 6.0f, Color{ 14,20,28,200 }, HudBorder, 1.0f);

        char titleLbl[160];
        std::snprintf(titleLbl, sizeof(titleLbl), "[%s] %s", ContractTypeLabel(c.type), c.title.c_str());
        DrawText(titleLbl, (int)(row.x + 14), (int)(row.y + 10), 14, HudValue);
        DrawText(c.briefing.c_str(), (int)(row.x + 14), (int)(row.y + 32), 11, HudLabel);
        char rew[48];
        std::snprintf(rew, sizeof(rew), "+%d CR", c.rewardCredits);
        DrawText(rew, (int)(row.x + 14), (int)(row.y + 52), 12, HudGood);

        Rectangle btn = { row.x + row.width - 130.0f, row.y + row.height / 2.0f - 18.0f, 110.0f, 36.0f };
        DrawSvcBtn(btn, "ACCEPT");
    }
}

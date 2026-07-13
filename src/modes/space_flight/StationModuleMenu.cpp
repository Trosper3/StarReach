#include "StationModuleMenu.h"
#include "core/ShipRegistry.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/modules/ArmorDefs.h"
#include "data/modules/FacilityDefs.h"
#include "shared/ui/HudTheme.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>

// ── Shared chrome/glass HUD theme (see shared/ui/HudTheme.h) — same helpers
// ModulesMenu/StorageMenu/StationServicesMenu/BuildMenu already use, so this
// menu reads as the same visual language rather than its own blue palette.

// Fixed column geometry shared by Update()/Draw() for the module page.
static constexpr int kModColXOff  = 16;
static constexpr int kModColWFull = 400; // hardpoint with combat slots
static constexpr int kModColWDock = 200; // docking bay: all rows read "NO SLOT"
static constexpr int kColGap      = 20;
static constexpr int kDockShipColW = 250; // storage col width when a ship column is also shown

// Builds an empty (no preloaded modules) Hardpoint from a crafted
// blueprint. Mirrors FleetManager::SpawnStation's per-hardpoint conversion,
// minus the preloaded-modules step — attached hardpoints always start bare.
static Hardpoint MakeBlankHardpointState(const StationHardpointDef& hpd) {
    Hardpoint hp;
    hp.id          = hpd.id;
    hp.displayName = hpd.displayName;
    hp.isCore      = false; // crafted attachments are never the station core
    hp.isDockingBay = (hpd.id == "docking_bay");
    hp.maxHull     = hpd.maxHull;
    hp.hull        = hpd.maxHull;
    hp.alive       = true;
    for (int i = 0; i < hpd.wSlots;   ++i) hp.slots.push_back({ ModuleType::Weapon });
    for (int i = 0; i < hpd.arSlots;  ++i) hp.slots.push_back({ ModuleType::Armor });
    for (int i = 0; i < hpd.shSlots;  ++i) hp.slots.push_back({ ModuleType::Shield });
    for (int i = 0; i < hpd.enSlots;  ++i) hp.slots.push_back({ ModuleType::Engine });
    for (int i = 0; i < hpd.auxSlots; ++i) hp.slots.push_back({ ModuleType::Auxiliary });
    for (int i = 0; i < hpd.fSlots;   ++i) hp.slots.push_back({ ModuleType::Facility });
    // P4-T4 backward-compat backfill — see FleetManager::SpawnStation's matching note.
    if (hpd.fSlots == 0) {
        if (auto kind = LegacyHardpointFacilityKind(hpd.id)) {
            ModuleSlot fs; fs.type = ModuleType::Facility; fs.equipped = Facility_ForKind(*kind);
            hp.slots.push_back(fs);
        }
    }
    if (ModuleSlot* armorSlot = hp.Armor()) {
        armorSlot->equipped = Armor_HullPatch();
        hp.maxHull = 100.0f + armorSlot->equipped->armor.hullBonus;
        hp.hull    = hp.maxHull;
    }
    return hp;
}

// P4-T5: the shipyard column used to be gated on the physical isDockingBay
// marker (a hardcoded id=="docking_bay" check) — the real "hardpointToMenuConnection"
// fix routes off the installed Facility chip's kind instead, so any hardpoint
// with a Shipyard facility shows the column, not just ones named "docking_bay".
// isDockingBay itself stays (it also drives DockRepair targeting and the
// SpaceFlight minimap/world dot color, unrelated to this menu).
static bool HasShipyardFacility(const Hardpoint& hp) {
    const ModuleSlot* f = hp.Facility();
    return f && f->equipped.has_value() && f->equipped->facility.kind == FacilityKind::Shipyard;
}

// ── Open / Close ──────────────────────────────────────────────────────────────

void StationModuleMenu::Open(PlayerStation* station, std::vector<StorageItem>* storage) {
    _station  = station;
    _storage  = storage;
    _selHp    = 0;
    _screen   = Screen::HardpointList;
    _dragging = false;
    _dragSrc  = DragSrc::None;
    _dragKind = DragKind::Module;
    _dragIdx  = -1;
    isOpen    = true;
    _storageScroll  = 0;
    _shipListScroll = 0;
    _selShipIdx     = -1;
}

void StationModuleMenu::Close() {
    isOpen    = false;
    _dragging = false;
    _dragSrc  = DragSrc::None;
    _dragIdx  = -1;
}

bool StationModuleMenu::CanAttachHardpoint() const {
    if (!_station) return false;
    const PlayerStationDef* def = PlayerStationRegistry::ById(_station->stationDefId);
    int maxHp = def ? def->maxHardpoints : (int)_station->hardpoints.size();
    return (int)_station->hardpoints.size() < maxHp;
}

// ── Geometry (shared by Update/Draw so hit-rects always match what's drawn) ──

Rectangle StationModuleMenu::HardpointRowRect(int panelX, int panelY, int i) const {
    return { (float)(panelX + 16), (float)(panelY + ContentY + i * 56), 300.0f, 52.0f };
}

Rectangle StationModuleMenu::StorageAreaRect(int panelX, int panelY) const {
    int y = panelY + ContentY;
    int h = PanelH - ContentY - 16;
    if (_screen == Screen::HardpointList) {
        int x = panelX + 16 + 300 + kColGap;
        int w = (panelX + PanelW - 16) - x;
        return { (float)x, (float)y, (float)w, (float)h };
    }
    bool docking = _station && _selHp >= 0 && _selHp < (int)_station->hardpoints.size()
                   && HasShipyardFacility(_station->hardpoints[_selHp]);
    int modColW = docking ? kModColWDock : kModColWFull;
    int x = panelX + kModColXOff + modColW + kColGap;
    int w = docking ? kDockShipColW : (panelX + PanelW - 16) - x;
    return { (float)x, (float)y, (float)w, (float)h };
}

Rectangle StationModuleMenu::ShipyardAreaRect(int panelX, int panelY) const {
    Rectangle sto = StorageAreaRect(panelX, panelY);
    int x = (int)(sto.x + sto.width) + kColGap;
    int w = (panelX + PanelW - 16) - x;
    return { (float)x, sto.y, (float)w, sto.height };
}

// ── Slot reference helpers (module page only) ────────────────────────────────

void StationModuleMenu::BuildSlotRefs(int hpIdx, std::vector<SlotRef>& out) const {
    out.clear();
    if (!_station || hpIdx < 0 || hpIdx >= (int)_station->hardpoints.size()) return;
    const Hardpoint& hp = _station->hardpoints[hpIdx];

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - PanelH / 2;
    int labelW = 90;
    int slotsX = panelX + kModColXOff + labelW;
    int startY = panelY + ContentY + 28;

    static constexpr int RowH = 76;
    auto addRow = [&](ModuleType t, int count, int row) {
        for (int i = 0; i < count; ++i) {
            SlotRef sr;
            sr.rect = { (float)(slotsX + i * (SlotPx + SlotGap)), (float)(startY + row * RowH), (float)SlotPx, (float)SlotPx };
            sr.type = t;
            sr.idx  = i;
            out.push_back(sr);
        }
        };
    addRow(ModuleType::Weapon,    (int)hp.WeaponSlots().size(), 0);
    addRow(ModuleType::Armor,     hp.Armor() ? 1 : 0,           1);
    addRow(ModuleType::Shield,    (int)hp.ShieldSlots().size(), 2);
    addRow(ModuleType::Engine,    hp.Engine() ? 1 : 0,          3);
    addRow(ModuleType::Auxiliary, (int)hp.AuxSlots().size(),    4);
    addRow(ModuleType::Facility,  hp.Facility() ? 1 : 0,        5);
}

std::optional<ModuleDef>* StationModuleMenu::GetSlotOpt(SlotRef& s) {
    if (!_station || _selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return nullptr;
    Hardpoint& hp = _station->hardpoints[_selHp];
    switch (s.type) {
    case ModuleType::Weapon:    { auto v = hp.WeaponSlots(); return (s.idx < (int)v.size()) ? &v[s.idx]->equipped : nullptr; }
    case ModuleType::Armor:     { ModuleSlot* a = hp.Armor(); return a ? &a->equipped : nullptr; }
    case ModuleType::Shield:    { auto v = hp.ShieldSlots(); return (s.idx < (int)v.size()) ? &v[s.idx]->equipped : nullptr; }
    case ModuleType::Engine:    { ModuleSlot* e = hp.Engine(); return e ? &e->equipped : nullptr; }
    case ModuleType::Auxiliary: { auto v = hp.AuxSlots();   return (s.idx < (int)v.size()) ? &v[s.idx]->equipped : nullptr; }
    case ModuleType::Facility:  { ModuleSlot* f = hp.Facility(); return f ? &f->equipped : nullptr; }
    default: return nullptr;
    }
}

const std::optional<ModuleDef>* StationModuleMenu::GetSlotOpt(const SlotRef& s) const {
    if (!_station || _selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return nullptr;
    const Hardpoint& hp = _station->hardpoints[_selHp];
    switch (s.type) {
    case ModuleType::Weapon:    { auto v = hp.WeaponSlots(); return (s.idx < (int)v.size()) ? &v[s.idx]->equipped : nullptr; }
    case ModuleType::Armor:     { const ModuleSlot* a = hp.Armor(); return a ? &a->equipped : nullptr; }
    case ModuleType::Shield:    { auto v = hp.ShieldSlots(); return (s.idx < (int)v.size()) ? &v[s.idx]->equipped : nullptr; }
    case ModuleType::Engine:    { const ModuleSlot* e = hp.Engine(); return e ? &e->equipped : nullptr; }
    case ModuleType::Auxiliary: { auto v = hp.AuxSlots();   return (s.idx < (int)v.size()) ? &v[s.idx]->equipped : nullptr; }
    case ModuleType::Facility:  { const ModuleSlot* f = hp.Facility(); return f ? &f->equipped : nullptr; }
    default: return nullptr;
    }
}

bool StationModuleMenu::IsCompatible(ModuleType slotType, const ModuleDef& mod) const {
    return mod.type == slotType;
}

// ── Update ────────────────────────────────────────────────────────────────────

bool StationModuleMenu::Update() {
    if (!isOpen || !_station) return false;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - PanelH / 2;
    Vector2 m = GetMousePosition();

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (_screen == Screen::ModulePage) { _screen = Screen::HardpointList; _selShipIdx = -1; return true; }
        Close(); return false;
    }

    // Close button (both screens)
    Rectangle closeBtn = { (float)(panelX + PanelW - 28), (float)(panelY + 6), 22.0f, 22.0f };
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, closeBtn)) {
        Close(); return false;
    }

    if (_screen == Screen::HardpointList) return UpdateHardpointList(panelX, panelY, m);
    return UpdateModulePage(panelX, panelY, m);
}

bool StationModuleMenu::UpdateHardpointList(int panelX, int panelY, Vector2 m) {
    int n = (int)_station->hardpoints.size();

    // Scrap button + row select/navigate
    for (int i = 0; i < n; ++i) {
        Rectangle r = HardpointRowRect(panelX, panelY, i);
        if (!_station->hardpoints[i].alive && n > 1) {
            Rectangle scrapBtn = { r.x + r.width - 20.0f, r.y + 2.0f, 18.0f, 18.0f };
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, scrapBtn)) {
                _station->hardpoints.erase(_station->hardpoints.begin() + i);
                if (_selHp > i) _selHp--;
                _selHp = std::clamp(_selHp, 0, (int)_station->hardpoints.size() - 1);
                return true;
            }
        }
        if (!_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r)) {
            _selHp = i;
            _screen = Screen::ModulePage;
            _selShipIdx = -1;
            return true;
        }
    }

    Rectangle attachRect = HardpointRowRect(panelX, panelY, n);

    // Storage grid + scroll
    Rectangle sto = StorageAreaRect(panelX, panelY);
    if (_storage) {
        if (CheckCollisionPointRec(m, sto)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) _storageScroll -= (int)(wheel * 24.0f);
        }
        int cols = std::max(1, ((int)sto.width + SlotGap) / (SlotPx + SlotGap));
        int rows = ((int)_storage->size() + cols - 1) / cols;
        int maxScroll = std::max(0, rows * (SlotPx + SlotGap) - (int)sto.height);
        _storageScroll = std::clamp(_storageScroll, 0, maxScroll);
    }
    std::vector<Rectangle> stoRects;
    if (_storage) {
        int cols = std::max(1, ((int)sto.width + SlotGap) / (SlotPx + SlotGap));
        for (int i = 0; i < (int)_storage->size(); ++i) {
            int col = i % cols, row = i / cols;
            stoRects.push_back({ sto.x + col * (SlotPx + SlotGap),
                                  sto.y + row * (SlotPx + SlotGap) - _storageScroll,
                                  (float)SlotPx, (float)SlotPx });
        }
    }

    // Begin drag — only Hardpoint blueprints and Consumables have a target on
    // this screen (equip-only modules need the module page's slots instead).
    if (!_dragging && _storage && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < (int)stoRects.size(); ++i) {
            if (i >= (int)_storage->size()) continue;
            if (!CheckCollisionPointRec(m, sto) || !CheckCollisionPointRec(m, stoRects[i])) continue;
            StorageItem& item = (*_storage)[i];
            bool draggable = item.type == StorageItemType::Hardpoint ||
                              (item.type == StorageItemType::Module && item.module.type == ModuleType::Consumable);
            if (!draggable) continue;
            _dragging = true;
            _dragSrc  = DragSrc::Storage;
            _dragIdx  = i;
            if (item.type == StorageItemType::Hardpoint) { _dragKind = DragKind::Hardpoint; _dragHp = item.hardpoint; }
            else                                          { _dragKind = DragKind::Module;    _dragMod = item.module; }
            item = StorageItem{};
            break;
        }
    }

    // Drop
    if (_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        bool dropped = false;
        if (_dragKind == DragKind::Hardpoint) {
            bool repaired = false;
            for (int i = 0; i < n; ++i) {
                if (_station->hardpoints[i].alive) continue;
                Rectangle r = HardpointRowRect(panelX, panelY, i);
                if (CheckCollisionPointRec(m, r)) {
                    _station->hardpoints[i] = MakeBlankHardpointState(_dragHp);
                    repaired = true; dropped = true;
                    break;
                }
            }
            if (!repaired && CheckCollisionPointRec(m, attachRect) && CanAttachHardpoint()) {
                _station->hardpoints.push_back(MakeBlankHardpointState(_dragHp));
                dropped = true;
            }
            if (!dropped && _storage && _dragIdx >= 0 && _dragIdx < (int)_storage->size()) {
                StorageItem& st = (*_storage)[_dragIdx];
                st.type = StorageItemType::Hardpoint;
                st.hardpoint = _dragHp;
                st.displayName = _dragHp.displayName;
            }
        } else {
            // Consumable — heal whichever hardpoint row it's dropped on.
            for (int i = 0; i < n; ++i) {
                Rectangle r = HardpointRowRect(panelX, panelY, i);
                if (CheckCollisionPointRec(m, r)) {
                    Hardpoint& hp = _station->hardpoints[i];
                    hp.hull = std::min(hp.hull + _dragMod.consumable.healAmount, hp.maxHull);
                    dropped = true;
                    break;
                }
            }
            if (!dropped && _storage) {
                for (int i = 0; i < (int)stoRects.size(); ++i) {
                    if (i < (int)_storage->size() && (*_storage)[i].type == StorageItemType::Empty &&
                        CheckCollisionPointRec(m, sto) && CheckCollisionPointRec(m, stoRects[i])) {
                        StorageItem& st = (*_storage)[i];
                        st.type = StorageItemType::Module;
                        st.module = _dragMod;
                        st.displayName = _dragMod.displayName;
                        dropped = true;
                        break;
                    }
                }
            }
            if (!dropped && _dragIdx >= 0 && _dragIdx < (int)_storage->size()) {
                StorageItem& st = (*_storage)[_dragIdx];
                st.type = StorageItemType::Module;
                st.module = _dragMod;
                st.displayName = _dragMod.displayName;
            }
        }
        _dragging = false;
        _dragSrc = DragSrc::None;
        _dragIdx = -1;
    }

    return true;
}

bool StationModuleMenu::UpdateModulePage(int panelX, int panelY, Vector2 m) {
    // Back button
    Rectangle backBtn = { (float)(panelX + 16), (float)(panelY + 6), 70.0f, 22.0f };
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, backBtn)) {
        _screen = Screen::HardpointList;
        _selShipIdx = -1;
        _dragging = false; _dragSrc = DragSrc::None; _dragIdx = -1;
        return true;
    }

    if (_selHp < 0 || _selHp >= (int)_station->hardpoints.size()) { _screen = Screen::HardpointList; return true; }
    const Hardpoint& selHp = _station->hardpoints[_selHp];

    // P7-T4: shed-priority nudge buttons (isCore hardpoints are never shed —
    // see RecalculatePowerBudget's !hp.isCore filter — so they get no control).
    if (!selHp.isCore) {
        Rectangle prioMinus = { (float)(panelX + 40), (float)(panelY + 44), 14.0f, 14.0f };
        Rectangle prioPlus  = { (float)(panelX + 76), (float)(panelY + 44), 14.0f, 14.0f };
        Hardpoint& hp = _station->hardpoints[_selHp];
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, prioMinus)) {
            int cur = hardpoint_power_detail::EffectiveShedPriority(hp);
            hp.shedPriority = std::clamp(cur - 1, 0, 9);
            return true;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, prioPlus)) {
            int cur = hardpoint_power_detail::EffectiveShedPriority(hp);
            hp.shedPriority = std::clamp(cur + 1, 0, 9);
            return true;
        }
    }

    // Shipyard column (only when a Shipyard facility is installed here)
    if (HasShipyardFacility(selHp)) {
        Rectangle shipArea = ShipyardAreaRect(panelX, panelY);
        const auto& allShips = ecs::ShipRegistry::AllShips();
        const int shipRowH = 44;
        Rectangle shipListRec = { shipArea.x, shipArea.y + 24.0f, shipArea.width, shipArea.height - 24.0f - 44.0f };

        if (CheckCollisionPointRec(m, shipArea)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) _shipListScroll -= (int)(wheel * 24.0f);
        }
        int maxScroll = std::max(0, (int)allShips.size() * shipRowH - (int)shipListRec.height);
        _shipListScroll = std::clamp(_shipListScroll, 0, maxScroll);

        if (CheckCollisionPointRec(m, shipListRec)) {
            for (int i = 0; i < (int)allShips.size(); ++i) {
                Rectangle r = { shipListRec.x, shipListRec.y + i * shipRowH - _shipListScroll, shipListRec.width, 40.0f };
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r)) _selShipIdx = i;
            }
        }
        Rectangle buildBtn = { shipArea.x + shipArea.width / 2 - 60.0f, (float)(panelY + PanelH - 50), 120.0f, 30.0f };
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, buildBtn) &&
            _selShipIdx >= 0 && _selShipIdx < (int)allShips.size()) {
            pendingShipBuildId = allShips[_selShipIdx].id;
            Close();
            return false;
        }
    }

    std::vector<SlotRef> slots;
    BuildSlotRefs(_selHp, slots);

    Rectangle sto = StorageAreaRect(panelX, panelY);
    if (_storage) {
        if (CheckCollisionPointRec(m, sto)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) _storageScroll -= (int)(wheel * 24.0f);
        }
        int cols = std::max(1, ((int)sto.width + SlotGap) / (SlotPx + SlotGap));
        int rows = ((int)_storage->size() + cols - 1) / cols;
        int maxScroll = std::max(0, rows * (SlotPx + SlotGap) - (int)sto.height);
        _storageScroll = std::clamp(_storageScroll, 0, maxScroll);
    }
    std::vector<Rectangle> stoRects;
    if (_storage) {
        int cols = std::max(1, ((int)sto.width + SlotGap) / (SlotPx + SlotGap));
        for (int i = 0; i < (int)_storage->size(); ++i) {
            int col = i % cols, row = i / cols;
            stoRects.push_back({ sto.x + col * (SlotPx + SlotGap),
                                  sto.y + row * (SlotPx + SlotGap) - _storageScroll,
                                  (float)SlotPx, (float)SlotPx });
        }
    }

    // Begin drag
    if (!_dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int si = 0; si < (int)slots.size(); ++si) {
            auto* opt = GetSlotOpt(slots[si]);
            if (opt && opt->has_value() && CheckCollisionPointRec(m, slots[si].rect)) {
                _dragging = true; _dragSrc = DragSrc::Slot; _dragKind = DragKind::Module;
                _dragIdx = si; _dragMod = **opt; *opt = std::nullopt;
                break;
            }
        }
        if (!_dragging && _storage) {
            for (int i = 0; i < (int)stoRects.size(); ++i) {
                if (i >= (int)_storage->size()) continue;
                StorageItemType t = (*_storage)[i].type;
                if (t != StorageItemType::Module || !CheckCollisionPointRec(m, sto) || !CheckCollisionPointRec(m, stoRects[i]))
                    continue;
                _dragging = true; _dragSrc = DragSrc::Storage; _dragKind = DragKind::Module;
                _dragIdx = i; _dragMod = (*_storage)[i].module;
                (*_storage)[i] = StorageItem{};
                break;
            }
        }
    }

    // Drop
    if (_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        bool dropped = false;

        if (_dragMod.type == ModuleType::Consumable) {
            for (int si = 0; si < (int)slots.size(); ++si) {
                if (CheckCollisionPointRec(m, slots[si].rect)) {
                    Hardpoint& hp = _station->hardpoints[_selHp];
                    hp.hull = std::min(hp.hull + _dragMod.consumable.healAmount, hp.maxHull);
                    dropped = true;
                    break;
                }
            }
            if (!dropped && _storage) {
                for (int i = 0; i < (int)stoRects.size(); ++i) {
                    if (i < (int)_storage->size() && (*_storage)[i].type == StorageItemType::Empty &&
                        CheckCollisionPointRec(m, sto) && CheckCollisionPointRec(m, stoRects[i])) {
                        StorageItem& st = (*_storage)[i];
                        st.type = StorageItemType::Module; st.module = _dragMod; st.displayName = _dragMod.displayName;
                        dropped = true;
                        break;
                    }
                }
            }
            if (!dropped && _dragSrc == DragSrc::Storage && _storage && _dragIdx < (int)_storage->size()) {
                StorageItem& st = (*_storage)[_dragIdx];
                st.type = StorageItemType::Module; st.module = _dragMod; st.displayName = _dragMod.displayName;
            }
        } else {
            for (int si = 0; si < (int)slots.size(); ++si) {
                auto* opt = GetSlotOpt(slots[si]);
                if (opt && CheckCollisionPointRec(m, slots[si].rect) && IsCompatible(slots[si].type, _dragMod)) {
                    if (opt->has_value() && _storage) {
                        for (auto& st : *_storage) {
                            if (st.type == StorageItemType::Empty) {
                                st.type = StorageItemType::Module; st.module = **opt; st.displayName = opt->value().displayName;
                                break;
                            }
                        }
                    }
                    *opt = _dragMod;
                    dropped = true;
                    break;
                }
            }
            if (!dropped && _storage) {
                for (int i = 0; i < (int)stoRects.size(); ++i) {
                    if (i < (int)_storage->size() && (*_storage)[i].type == StorageItemType::Empty &&
                        CheckCollisionPointRec(m, sto) && CheckCollisionPointRec(m, stoRects[i])) {
                        StorageItem& st = (*_storage)[i];
                        st.type = StorageItemType::Module; st.module = _dragMod; st.displayName = _dragMod.displayName;
                        dropped = true;
                        break;
                    }
                }
            }
            if (!dropped) {
                if (_dragSrc == DragSrc::Slot) {
                    auto* opt = GetSlotOpt(slots[_dragIdx]);
                    if (opt) *opt = _dragMod;
                } else if (_dragSrc == DragSrc::Storage && _storage && _dragIdx < (int)_storage->size()) {
                    StorageItem& st = (*_storage)[_dragIdx];
                    st.type = StorageItemType::Module; st.module = _dragMod; st.displayName = _dragMod.displayName;
                }
            }
        }
        _dragging = false; _dragSrc = DragSrc::None; _dragIdx = -1;
    }

    return true;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void StationModuleMenu::DrawSlot(Rectangle r, const std::optional<ModuleDef>& mod,
                                  bool hovered, bool highlighted) const {
    using namespace hudtheme;
    Color border = highlighted ? HudCaution : hovered ? HudGood : HudDiv;
    DrawHudChamferRect(r, 8.0f, Color{ 10, 16, 24, 220 }, border, (highlighted || hovered) ? 2.0f : 1.0f);

    if (!mod.has_value()) {
        const char* em = "EMPTY";
        DrawText(em, (int)(r.x + (r.width - MeasureText(em, 9)) / 2.0f), (int)(r.y + r.height / 2.0f - 5), 9,
                  Color{ 60, 75, 90, 140 });
        return;
    }

    Color gc = StorageMenu::GradeColor(mod->grade);
    DrawRectangleLinesEx({ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, 2.0f, { gc.r, gc.g, gc.b, 170 });

    const char* letter = StorageMenu::TypeName(mod->type);
    Color lc = StorageMenu::TypeColor(mod->type);
    char short1[2] = { letter[0], 0 };
    int fs = 28;
    int lw = MeasureText(short1, fs);
    DrawText(short1, (int)(r.x + (r.width - lw) / 2.0f), (int)(r.y + (r.height - fs) / 2.0f - 4), fs, { lc.r, lc.g, lc.b, 210 });

    const char* gn = StorageMenu::GradeName(mod->grade);
    int gnw = MeasureText(gn, 8);
    DrawText(gn, (int)(r.x + (r.width - gnw) / 2.0f), (int)(r.y + r.height - 12), 8, { gc.r, gc.g, gc.b, 160 });
}

bool StationModuleMenu::IsMouseOverMenu() const {
    if (!isOpen) return false;
    int panelX = GetScreenWidth() / 2 - PanelW / 2;
    int panelY = GetScreenHeight() / 2 - PanelH / 2;
    return CheckCollisionPointRec(GetMousePosition(), { (float)panelX, (float)panelY, (float)PanelW, (float)PanelH });
}

std::string StationModuleMenu::GetSelectedShipId() const {
    if (!isOpen || _screen != Screen::ModulePage) return "";
    if (_selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return "";
    if (!HasShipyardFacility(_station->hardpoints[_selHp])) return "";

    const auto& allShips = ecs::ShipRegistry::AllShips();
    if (_selShipIdx >= 0 && _selShipIdx < (int)allShips.size()) return allShips[_selShipIdx].id;
    return "";
}

void StationModuleMenu::Draw() const {
    if (!isOpen || !_station) return;
    using namespace hudtheme;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - PanelH / 2;
    Vector2 mouse = GetMousePosition();

    DrawRectangle(0, 0, sw, sh, Color{ 0, 0, 0, 160 });
    DrawHudBracketPanel({ (float)panelX, (float)panelY, (float)PanelW, (float)PanelH }, HudBg, HudBorder);

    char titlebuf[128];
    if (_screen == Screen::ModulePage && _selHp >= 0 && _selHp < (int)_station->hardpoints.size())
        std::snprintf(titlebuf, sizeof(titlebuf), "%s", _station->hardpoints[_selHp].displayName.c_str());
    else
        std::snprintf(titlebuf, sizeof(titlebuf), "%s — HARDPOINTS", _station->displayName.c_str());
    DrawText(titlebuf, panelX + (PanelW - MeasureText(titlebuf, 14)) / 2, panelY + 10, 14, HudValue);

    Rectangle closeBtn = { (float)(panelX + PanelW - 28), (float)(panelY + 6), 22.0f, 22.0f };
    bool hovX = CheckCollisionPointRec(mouse, closeBtn);
    DrawHudChamferRect(closeBtn, 4.0f, hovX ? Color{ 90, 25, 25, 220 } : Color{ 30, 12, 12, 180 }, HudCritical, hovX ? 2.0f : 1.0f);
    DrawText("X", (int)(closeBtn.x + 7), (int)(closeBtn.y + 5), 12, hovX ? WHITE : HudLabel);

    DrawPowerBar(panelX, panelY);

    if (_screen == Screen::HardpointList) DrawHardpointList(panelX, panelY, mouse);
    else                                  DrawModulePage(panelX, panelY, mouse);

    if (_dragging) {
        Rectangle dr = { mouse.x - SlotPx / 2.0f, mouse.y - SlotPx / 2.0f, (float)SlotPx, (float)SlotPx };
        if (_dragKind == DragKind::Hardpoint) {
            StorageItem tmp; tmp.type = StorageItemType::Hardpoint; tmp.hardpoint = _dragHp; tmp.displayName = _dragHp.displayName;
            StorageMenu::DrawItemInSlot(dr, tmp, false, false);
        } else {
            DrawSlot(dr, _dragMod, false, false);
        }
    }
}

void StationModuleMenu::DrawPowerBar(int panelX, int panelY) const {
    using namespace hudtheme;
    if (!_station) return;
    const PowerBudget& pb = _station->powerBudget;
    float ratio = pb.ratio();

    int   shedCount    = 0;
    float worstThrottle = 1.0f;
    for (const Hardpoint& hp : _station->hardpoints) {
        if (!hp.alive) continue;
        if (hp.shed) shedCount++;
        worstThrottle = std::min(worstThrottle, hp.throttle);
    }

    Color barColor = ratio <= 1.0f ? HudGood : ratio <= 1.25f ? HudCaution : HudCritical;

    char label[96];
    if (shedCount > 0)
        std::snprintf(label, sizeof(label), "POWER %.1f / %.1f (%.0f%%)   %d SHED", pb.load, pb.capacity, ratio * 100.0f, shedCount);
    else if (worstThrottle < 0.999f)
        std::snprintf(label, sizeof(label), "POWER %.1f / %.1f (%.0f%%)   THROTTLED %.0f%%", pb.load, pb.capacity, ratio * 100.0f, worstThrottle * 100.0f);
    else
        std::snprintf(label, sizeof(label), "POWER %.1f / %.1f (%.0f%%)", pb.load, pb.capacity, ratio * 100.0f);
    DrawText(label, panelX + 16, panelY + 27, 10, barColor);

    Rectangle bar = { (float)(panelX + 16), (float)(panelY + 40), (float)(PanelW - 32), 6.0f };
    DrawRectangle((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height, Color{ 20, 20, 30, 200 });
    // Scaled so the shed threshold (ratio 1.25, RecalculatePowerBudget's own
    // cutoff — Hardpoint.h) lands at the bar's right edge, not raw ratio=1.0,
    // so "about to shed" reads as "bar nearly full" rather than pegged early.
    float frac = std::clamp(ratio, 0.0f, 1.25f) / 1.25f;
    DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * frac), (int)bar.height, barColor);
    DrawRectangleLinesEx(bar, 0.8f, HudDiv);
}

void StationModuleMenu::DrawHardpointList(int panelX, int panelY, Vector2 mouse) const {
    using namespace hudtheme;
    int n = (int)_station->hardpoints.size();

    for (int i = 0; i < n; ++i) {
        const Hardpoint& hp = _station->hardpoints[i];
        Rectangle r = HardpointRowRect(panelX, panelY, i);
        bool hov = CheckCollisionPointRec(mouse, r);
        bool destroyed = !hp.alive;
        bool hovRepair = destroyed && _dragging && _dragKind == DragKind::Hardpoint && hov;

        Color border = hovRepair ? HudGood : destroyed ? HudCritical : hp.isCore ? HudCaution : (hov ? HudBorder : HudDiv);
        Color fill = hovRepair ? Color{ 14, 40, 20, 230 } : hov ? Color{ 16, 30, 45, 220 } : Color{ 8, 14, 22, 200 };
        DrawHudChamferRect(r, 6.0f, fill, border, (hov || hovRepair || hp.isCore) ? 2.0f : 1.0f);

        Color nameFg = destroyed ? HudCritical : hp.isCore ? HudCaution : HudValue;
        DrawText(hp.displayName.c_str(), (int)(r.x + 8), (int)(r.y + 6), 12, nameFg);

        float frac = hp.maxHull > 0 ? std::clamp(hp.hull / hp.maxHull, 0.0f, 1.0f) : 0.0f;
        int bw = (int)(r.width - 16);
        DrawRectangle((int)(r.x + 8), (int)(r.y + 26), bw, 7, Color{ 20, 20, 30, 200 });
        Color hpColor = frac > 0.6f ? HudGood : frac > 0.3f ? HudCaution : HudCritical;
        DrawRectangle((int)(r.x + 8), (int)(r.y + 26), (int)(bw * frac), 7, hpColor);
        DrawRectangleLinesEx({ r.x + 8, r.y + 26, (float)bw, 7.0f }, 0.8f, HudDiv);

        if (hp.isCore) DrawText("CORE", (int)(r.x + r.width - MeasureText("CORE", 9) - 8), (int)(r.y + 6), 9, HudCaution);

        // P7-T2: per-hardpoint ACTIVE/THROTTLED/SHED status (derived every
        // tick by RecalculatePowerBudget, Hardpoint.h). Destroyed rows already
        // show their own DESTROYED/DROP TO REPAIR text in this slot below.
        if (!destroyed) {
            char st[24];
            Color sc;
            if (hp.shed)                    { std::snprintf(st, sizeof(st), "SHED");             sc = HudCritical; }
            else if (hp.throttle < 0.999f)  { std::snprintf(st, sizeof(st), "THROTTLE %.0f%%", hp.throttle * 100.0f); sc = HudCaution; }
            else                             { std::snprintf(st, sizeof(st), "ACTIVE");           sc = HudGood; }
            DrawText(st, (int)(r.x + r.width - MeasureText(st, 9) - 8), (int)(r.y + 36), 9, sc);
        }

        if (destroyed) {
            DrawText(hovRepair ? "DROP TO REPAIR" : "DESTROYED", (int)(r.x + 8), (int)(r.y + 36), 9,
                      hovRepair ? HudGood : HudCritical);
            if (n > 1) {
                Rectangle scrapBtn = { r.x + r.width - 20.0f, r.y + 2.0f, 18.0f, 18.0f };
                bool hovScrap = CheckCollisionPointRec(mouse, scrapBtn);
                DrawHudChamferRect(scrapBtn, 3.0f, hovScrap ? Color{ 110, 20, 20, 230 } : Color{ 40, 12, 12, 180 }, HudCritical, 1.0f);
                DrawText("X", (int)(scrapBtn.x + 5), (int)(scrapBtn.y + 3), 11, hovScrap ? WHITE : HudLabel);
            }
        }
    }

    if (CanAttachHardpoint()) {
        Rectangle ar = HardpointRowRect(panelX, panelY, n);
        bool hovAttach = _dragging && _dragKind == DragKind::Hardpoint && CheckCollisionPointRec(mouse, ar);
        DrawHudChamferRect(ar, 6.0f, hovAttach ? Color{ 14, 40, 20, 230 } : Color{ 8, 16, 10, 180 },
                            hovAttach ? HudGood : Color{ HudGood.r, HudGood.g, HudGood.b, 130 }, hovAttach ? 2.0f : 1.0f);
        const char* lbl = "+ ATTACH HARDPOINT";
        DrawText(lbl, (int)(ar.x + (ar.width - MeasureText(lbl, 9)) / 2.0f), (int)(ar.y + ar.height / 2 - 4), 9,
                  hovAttach ? WHITE : Color{ HudGood.r, HudGood.g, HudGood.b, 200 });
    }

    Rectangle sto = StorageAreaRect(panelX, panelY);
    DrawText("STORAGE", (int)sto.x, panelY + ContentY - 16, 11, HudLabel);
    if (_storage) {
        int cols = std::max(1, ((int)sto.width + SlotGap) / (SlotPx + SlotGap));
        BeginScissorMode((int)sto.x, (int)sto.y, (int)sto.width, (int)sto.height);
        for (int i = 0; i < (int)_storage->size(); ++i) {
            int col = i % cols, row = i / cols;
            Rectangle r = { sto.x + col * (SlotPx + SlotGap), sto.y + row * (SlotPx + SlotGap) - _storageScroll,
                            (float)SlotPx, (float)SlotPx };
            if (r.y + r.height <= sto.y || r.y >= sto.y + sto.height) continue;
            bool isDragged = (_dragging && _dragSrc == DragSrc::Storage && _dragIdx == i);
            bool hov = CheckCollisionPointRec(mouse, sto) && CheckCollisionPointRec(mouse, r);
            StorageMenu::DrawItemInSlot(r, isDragged ? StorageItem{} : (*_storage)[i], hov, false);
        }
        EndScissorMode();
    }
}

void StationModuleMenu::DrawModulePage(int panelX, int panelY, Vector2 mouse) const {
    using namespace hudtheme;

    Rectangle backBtn = { (float)(panelX + 16), (float)(panelY + 6), 70.0f, 22.0f };
    bool hovBack = CheckCollisionPointRec(mouse, backBtn);
    DrawHudChamferRect(backBtn, 4.0f, hovBack ? Color{ 30, 55, 70, 230 } : Color{ 14, 20, 28, 200 }, HudBorder, hovBack ? 2.0f : 1.0f);
    DrawText("< BACK", (int)(backBtn.x + 8), (int)(backBtn.y + 5), 11, hovBack ? WHITE : HudLabel);

    if (_selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return;
    const Hardpoint& hp = _station->hardpoints[_selHp];

    // P5-T3: adjacency hint — a single line, priority shieldCovered >
    // reactor-cut > rate-boost (a hardpoint could technically have more than
    // one active; showing the strongest keeps this from needing its own
    // layout row — a fuller per-hardpoint status readout is P7's job). Values
    // come from RecalculateAdjacency (Hardpoint.h), recomputed every tick.
    {
        char hint[64] = "";
        if (hp.shieldCovered) {
            std::snprintf(hint, sizeof(hint), "SHIELD COVERED");
        } else if (hp.adjacencyPowerDrawMult < 1.0f) {
            std::snprintf(hint, sizeof(hint), "REACTOR ADJACENT  -%.0f%% DRAW", (1.0f - hp.adjacencyPowerDrawMult) * 100.0f);
        } else if (hp.adjacencyRateMult > 1.0f) {
            std::snprintf(hint, sizeof(hint), "ADJACENCY BOOST  +%.0f%% RATE", (hp.adjacencyRateMult - 1.0f) * 100.0f);
        }
        if (hint[0])
            DrawText(hint, (int)(backBtn.x + backBtn.width + 12), (int)(backBtn.y + 5), 11, HudGood);
    }

    // P7-T2: ACTIVE/THROTTLED/SHED badge for the selected hardpoint.
    {
        char st[24];
        Color sc;
        if (hp.shed)                    { std::snprintf(st, sizeof(st), "SHED");             sc = HudCritical; }
        else if (hp.throttle < 0.999f)  { std::snprintf(st, sizeof(st), "THROTTLE %.0f%%", hp.throttle * 100.0f); sc = HudCaution; }
        else                             { std::snprintf(st, sizeof(st), "ACTIVE");           sc = HudGood; }
        DrawText(st, (int)backBtn.x, (int)(backBtn.y + backBtn.height + 4), 10, sc);
    }

    // P7-T4: shed-priority nudge control — hidden for isCore hardpoints
    // (never shed, see RecalculatePowerBudget). Ascending = shed first, so
    // "-" makes this hardpoint shed sooner and "+" protects it longer.
    if (!hp.isCore) {
        Rectangle prioMinus = { (float)(panelX + 40), (float)(panelY + 44), 14.0f, 14.0f };
        Rectangle prioPlus  = { (float)(panelX + 76), (float)(panelY + 44), 14.0f, 14.0f };
        bool hovMinus = CheckCollisionPointRec(mouse, prioMinus);
        bool hovPlus  = CheckCollisionPointRec(mouse, prioPlus);

        DrawText("PRI", panelX + 16, (int)(prioMinus.y + 2), 9, HudLabel);

        DrawHudChamferRect(prioMinus, 2.0f, hovMinus ? Color{ 30, 40, 55, 230 } : Color{ 14, 18, 24, 200 }, HudDiv, 1.0f);
        DrawText("-", (int)(prioMinus.x + 5), (int)(prioMinus.y), 11, hovMinus ? WHITE : HudLabel);
        DrawHudChamferRect(prioPlus, 2.0f, hovPlus ? Color{ 30, 40, 55, 230 } : Color{ 14, 18, 24, 200 }, HudDiv, 1.0f);
        DrawText("+", (int)(prioPlus.x + 4), (int)(prioPlus.y), 11, hovPlus ? WHITE : HudLabel);

        char pbuf[8];
        std::snprintf(pbuf, sizeof(pbuf), "%d", hardpoint_power_detail::EffectiveShedPriority(hp));
        DrawText(pbuf, (int)(prioMinus.x + 18), (int)(prioMinus.y + 2), 10, HudValue);
    }

    // ── Module rows column ────────────────────────────────────────────────────
    static constexpr int LabelW = 90;
    static constexpr int RowH   = 76;
    bool docking = HasShipyardFacility(hp);
    int modColX = panelX + kModColXOff;
    int modColW = docking ? kModColWDock : kModColWFull;
    int rowStartY = panelY + ContentY + 28;

    struct RowDef { ModuleType type; const char* label; int count; };
    const RowDef rowDefs[] = {
        { ModuleType::Weapon,    "WEAPON",   (int)hp.WeaponSlots().size() },
        { ModuleType::Armor,     "ARMOR",    hp.Armor() ? 1 : 0           },
        { ModuleType::Shield,    "SHIELD",   (int)hp.ShieldSlots().size() },
        { ModuleType::Engine,    "ENGINE",   hp.Engine() ? 1 : 0          },
        { ModuleType::Auxiliary, "AUX",      (int)hp.AuxSlots().size()   },
        { ModuleType::Facility,  "FACILITY", hp.Facility() ? 1 : 0       },
    };
    for (int row = 0; row < 6; ++row) {
        const RowDef& rd = rowDefs[row];
        int ty = rowStartY + row * RowH + (SlotPx - 14) / 2;
        Color lc = StorageMenu::TypeColor(rd.type);
        DrawText(rd.label, modColX, ty, 12, { lc.r, lc.g, lc.b, 200 });
        if (rd.count == 0) DrawText("NO SLOT", modColX, ty + 16, 9, Color{ 55, 65, 75, 160 });
    }

    std::vector<SlotRef> slots;
    BuildSlotRefs(_selHp, slots);
    for (int si = 0; si < (int)slots.size(); ++si) {
        auto* opt = GetSlotOpt(slots[si]);
        bool isDragged = (_dragging && _dragSrc == DragSrc::Slot && _dragIdx == si);
        std::optional<ModuleDef> drawOpt = (opt && opt->has_value() && !isDragged) ? *opt : std::nullopt;
        bool hov = CheckCollisionPointRec(mouse, slots[si].rect);
        bool compat = _dragging && _dragKind == DragKind::Module && IsCompatible(slots[si].type, _dragMod);
        DrawSlot(slots[si].rect, drawOpt, hov, compat);
        if (hov && !_dragging && drawOpt.has_value()) {
            StorageItem tmp; tmp.type = StorageItemType::Module; tmp.module = *drawOpt; tmp.displayName = drawOpt->displayName;
            StorageMenu::DrawItemTooltip(tmp, (int)(slots[si].rect.x + SlotPx + 8), (int)slots[si].rect.y);
        }
    }

    // ── Storage column ────────────────────────────────────────────────────────
    Rectangle sto = StorageAreaRect(panelX, panelY);
    DrawText("STORAGE", (int)sto.x, panelY + ContentY - 16, 11, HudLabel);
    if (_storage) {
        int cols = std::max(1, ((int)sto.width + SlotGap) / (SlotPx + SlotGap));
        BeginScissorMode((int)sto.x, (int)sto.y, (int)sto.width, (int)sto.height);
        for (int i = 0; i < (int)_storage->size(); ++i) {
            int col = i % cols, row = i / cols;
            Rectangle r = { sto.x + col * (SlotPx + SlotGap), sto.y + row * (SlotPx + SlotGap) - _storageScroll,
                            (float)SlotPx, (float)SlotPx };
            if (r.y + r.height <= sto.y || r.y >= sto.y + sto.height) continue;
            bool isDragged = (_dragging && _dragSrc == DragSrc::Storage && _dragIdx == i);
            bool hov = CheckCollisionPointRec(mouse, sto) && CheckCollisionPointRec(mouse, r);
            StorageMenu::DrawItemInSlot(r, isDragged ? StorageItem{} : (*_storage)[i], hov, false);
        }
        EndScissorMode();
    }

    // ── Shipyard column (docking bay only) ───────────────────────────────────
    if (docking) {
        Rectangle shipArea = ShipyardAreaRect(panelX, panelY);
        DrawText("AVAILABLE SHIPS", (int)shipArea.x, (int)shipArea.y, 11, HudLabel);
        const auto& allShips = ecs::ShipRegistry::AllShips();
        const int shipRowH = 44;
        Rectangle shipListRec = { shipArea.x, shipArea.y + 24.0f, shipArea.width, shipArea.height - 24.0f - 44.0f };

        BeginScissorMode((int)shipListRec.x, (int)shipListRec.y, (int)shipListRec.width, (int)shipListRec.height);
        for (int i = 0; i < (int)allShips.size(); ++i) {
            const ecs::ShipDef& sdef = allShips[i];
            Rectangle r = { shipListRec.x, shipListRec.y + i * shipRowH - _shipListScroll, shipListRec.width, 40.0f };
            if (r.y + r.height <= shipListRec.y || r.y >= shipListRec.y + shipListRec.height) continue;
            bool sel = (i == _selShipIdx);
            bool hov = CheckCollisionPointRec(mouse, r) && CheckCollisionPointRec(mouse, shipListRec);
            DrawHudChamferRect(r, 4.0f, sel ? Color{ 20, 45, 70, 230 } : (hov ? Color{ 14, 28, 45, 220 } : Color{ 8, 14, 22, 180 }),
                                sel ? HudBorder : HudDiv, sel ? 1.5f : 1.0f);
            DrawText(sdef.displayName.c_str(), (int)(r.x + 8), (int)(r.y + 14), 12, HudValue);
        }
        EndScissorMode();

        Rectangle buildBtn = { shipArea.x + shipArea.width / 2 - 60.0f, (float)(panelY + PanelH - 50), 120.0f, 30.0f };
        bool hovBuild = CheckCollisionPointRec(mouse, buildBtn);
        bool canBuild = (_selShipIdx >= 0 && _selShipIdx < (int)allShips.size());
        DrawHudChamferRect(buildBtn, 5.0f, canBuild ? (hovBuild ? Color{ 16, 55, 28, 230 } : Color{ 10, 35, 18, 200 }) : Color{ 16, 18, 22, 160 },
                            canBuild ? HudGood : HudDiv, canBuild ? 1.5f : 1.0f);
        DrawText("PLACE SHIP", (int)(buildBtn.x + 20), (int)(buildBtn.y + 9), 11, canBuild ? (hovBuild ? WHITE : HudGood) : HudLabel);
    }
}

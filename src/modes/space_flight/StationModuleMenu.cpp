#include "StationModuleMenu.h"
#include "core/ShipRegistry.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/modules/ArmorDefs.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>

static constexpr Color BgPanel  = {  6, 12, 24, 248 };
static constexpr Color BdrMain  = { 40,100,200, 200 };
static constexpr Color TxtMain  = {180,215,255, 240 };
static constexpr Color TxtDim   = { 90,120,160, 200 };
static constexpr Color TxtGreen = { 70,200, 90, 255 };

// Builds an empty (no preloaded modules) HardpointState from a crafted
// blueprint. Mirrors FleetManager::SpawnStation's per-hardpoint conversion,
// minus the preloaded-modules step — attached hardpoints always start bare.
static HardpointState MakeBlankHardpointState(const StationHardpointDef& hpd) {
    HardpointState hp;
    hp.id          = hpd.id;
    hp.displayName = hpd.displayName;
    hp.isCore      = false; // crafted attachments are never the station core
    hp.maxHull     = hpd.maxHull;
    hp.hull        = hpd.maxHull;
    hp.alive       = true;
    hp.wSlots      = hpd.wSlots;
    hp.arSlots     = hpd.arSlots;
    hp.shSlots     = hpd.shSlots;
    hp.enSlots     = hpd.enSlots;
    hp.auxSlots    = hpd.auxSlots;
    hp.weapons.assign(hpd.wSlots, std::nullopt);
    hp.shields.assign(hpd.shSlots, std::nullopt);
    hp.aux.assign(hpd.auxSlots, std::nullopt);
    if (hp.arSlots > 0) {
        hp.armor   = Armor_HullPatch();
        hp.maxHull = 100.0f + hp.armor->armor.hullBonus;
        hp.hull    = hp.maxHull;
    }
    return hp;
}

// Renders a crafted (not-yet-attached) hardpoint blueprint sitting in storage —
// distinct amber styling since it's a structural item, not a ModuleDef.
static void DrawHardpointChip(Rectangle r, const std::string& displayName, bool hovered) {
    Color bg  = hovered ? Color{ 40,30,10,235 } : Color{ 20,15,6,210 };
    Color bdr = Color{ 210,160, 60, 220 };
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, hovered ? 2.0f : 1.5f, bdr);
    DrawText("HP", (int)(r.x + 4), (int)(r.y + 4), 11, Color{ 230,190, 90, 255 });

    const char* nm = displayName.c_str();
    int fs = 9;
    while (MeasureText(nm, fs) > (int)r.width - 6 && fs > 7) fs--;
    DrawText(nm, (int)(r.x + 3), (int)(r.y + r.height - fs - 5), fs, Color{ 220,210,190,240 });
}

// ── Open / Close ──────────────────────────────────────────────────────────────

void StationModuleMenu::Open(PlayerStation* station, std::vector<StorageItem>* storage) {
    _station  = station;
    _storage  = storage;
    _selHp    = 0;
    _dragging = false;
    _dragSrc  = DragSrc::None;
    _dragKind = DragKind::Module;
    _dragIdx  = -1;
    isOpen    = true;
    _storageScroll = 0;
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

// ── Slot reference helpers ────────────────────────────────────────────────────

void StationModuleMenu::BuildSlotRefs(int hpIdx, std::vector<SlotRef>& out) const {
    out.clear();
    if (!_station || hpIdx < 0 || hpIdx >= (int)_station->hardpoints.size()) return;
    const HardpointState& hp = _station->hardpoints[hpIdx];

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int modAreaX = panelX + HPListW + 12; // Middle column start
    int startX = modAreaX + 8;
    int startY = sh / 2 - 180 + 48;

    int x = startX, y = startY + 20;
    auto add = [&](ModuleType t, int count) {
        for (int i = 0; i < count; ++i) {
            SlotRef sr;
            sr.rect = { (float)x, (float)y, (float)SlotPx, (float)SlotPx };
            sr.type = t;
            sr.idx = i;
            out.push_back(sr);
            x += SlotPx + SlotGap;
            // Wrap slots to the next row if they exceed the 380px module column width
            if (x > modAreaX + 380 - 16) { x = startX; y += SlotPx + SlotGap; }
        }
        };
    if (hp.wSlots > 0) add(ModuleType::Weapon, hp.wSlots);
    if (hp.arSlots > 0) add(ModuleType::Armor, hp.arSlots);
    if (hp.shSlots > 0) add(ModuleType::Shield, hp.shSlots);
    if (hp.enSlots > 0) add(ModuleType::Engine, hp.enSlots);
    if (hp.auxSlots > 0) add(ModuleType::Auxiliary, hp.auxSlots);
}

std::optional<ModuleDef>* StationModuleMenu::GetSlotOpt(SlotRef& s) {
    if (!_station || _selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return nullptr;
    HardpointState& hp = _station->hardpoints[_selHp];
    switch (s.type) {
    case ModuleType::Weapon:    return (s.idx < (int)hp.weapons.size()) ? &hp.weapons[s.idx] : nullptr;
    case ModuleType::Armor:     return &hp.armor;
    case ModuleType::Shield:    return (s.idx < (int)hp.shields.size()) ? &hp.shields[s.idx] : nullptr;
    case ModuleType::Engine:    return &hp.engine;
    case ModuleType::Auxiliary: return (s.idx < (int)hp.aux.size())     ? &hp.aux[s.idx]    : nullptr;
    default: return nullptr;
    }
}

const std::optional<ModuleDef>* StationModuleMenu::GetSlotOpt(const SlotRef& s) const {
    if (!_station || _selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return nullptr;
    const HardpointState& hp = _station->hardpoints[_selHp];
    switch (s.type) {
    case ModuleType::Weapon:    return (s.idx < (int)hp.weapons.size()) ? &hp.weapons[s.idx] : nullptr;
    case ModuleType::Armor:     return &hp.armor;
    case ModuleType::Shield:    return (s.idx < (int)hp.shields.size()) ? &hp.shields[s.idx] : nullptr;
    case ModuleType::Engine:    return &hp.engine;
    case ModuleType::Auxiliary: return (s.idx < (int)hp.aux.size())     ? &hp.aux[s.idx]    : nullptr;
    default: return nullptr;
    }
}

bool StationModuleMenu::IsCompatible(ModuleType slotType, const ModuleDef& mod) const {
    return mod.type == slotType;
}

// ── Update ────────────────────────────────────────────────────────────────────

bool StationModuleMenu::Update() {
    if (!isOpen || !_station) return false;
    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return false; }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - 180;
    int panelH = 360;
    Vector2 m = GetMousePosition();

    // Close button
    Rectangle closeBtn = { (float)(panelX + PanelW - 28), (float)(panelY + 6), 22.0f, 22.0f };
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, closeBtn)) {
        Close(); return false;
    }

    // Hardpoint list clicks
    int hpX = panelX + 6, hpY = panelY + 38;
    if (!_station->hardpoints.empty()) {
        for (int i = 0; i < (int)_station->hardpoints.size(); ++i) {
            Rectangle r = { (float)hpX, (float)(hpY + i * 44), (float)(HPListW - 8), 40.0f };
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r))
                _selHp = i;
        }
    }
    // Row directly below the hardpoint list — drop a Hardpoint-type storage
    // item here to attach it to this station (while under maxHardpoints).
    Rectangle attachHpRect = { (float)hpX, (float)(hpY + (int)_station->hardpoints.size() * 44),
                               (float)(HPListW - 8), 40.0f };

    // Shipyard interactions (ONLY triggers if Docking Bay is selected)
    if (_selHp >= 0 && _selHp < (int)_station->hardpoints.size()) {
        if (_station->hardpoints[_selHp].displayName.find("Docking") != std::string::npos) {
            int shipAreaX = panelX + HPListW + 12 + 380 + 12; // Far right column
            int listY = panelY + 36;
            int shipAreaW = PanelW - HPListW - 380 - 36;
            const auto& allShips = ecs::ShipRegistry::AllShips();

            for (int i = 0; i < (int)allShips.size(); ++i) {
                Rectangle r = { (float)shipAreaX, (float)(listY + i * 44), (float)shipAreaW, 40.0f };
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r)) {
                    _selShipIdx = i;
                }
            }
            Rectangle buildBtn = { (float)(shipAreaX + shipAreaW / 2 - 60), (float)(panelY + panelH - 50), 120.0f, 30.0f };
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, buildBtn)) {
                if (_selShipIdx >= 0 && _selShipIdx < (int)allShips.size()) {
                    pendingShipBuildId = allShips[_selShipIdx].id;
                    Close();
                    return false;
                }
            }
        }
    }

    // Slot drag-and-drop
    std::vector<SlotRef> slots;
    BuildSlotRefs(_selHp, slots);

    // ── Storage slot rects and scrolling ──────────────────────────────────────
    int stoX = panelX + HPListW + 12;
    int stoY = panelY + panelH - 120;
    int stoW = 380; // Hard limit storage width so it doesn't cross into the shipyard
    int stoH = 106;
    Rectangle stoViewRec = { (float)stoX, (float)(stoY + 18), (float)stoW, (float)(stoH - 18) };

    if (_storage) {
        if (CheckCollisionPointRec(m, { (float)stoX, (float)stoY, (float)stoW, (float)stoH })) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) _storageScroll -= (int)(wheel * 24.0f);
        }
        int cols = std::max(1, (stoW + SlotGap) / (SlotPx + SlotGap));
        int rows = ((int)_storage->size() + cols - 1) / cols;
        int totalH = rows * (SlotPx + SlotGap);
        int maxScroll = std::max(0, totalH - (int)stoViewRec.height);
        if (_storageScroll > maxScroll) _storageScroll = maxScroll;
        if (_storageScroll < 0) _storageScroll = 0;
    }

    std::vector<Rectangle> stoRects;
    if (_storage) {
        int cols = std::max(1, (stoW + SlotGap) / (SlotPx + SlotGap));
        for (int i = 0; i < (int)_storage->size(); ++i) {
            int col = i % cols, row = i / cols;
            stoRects.push_back({ (float)(stoX + col * (SlotPx + SlotGap)),
                                  (float)(stoY + 18 + row * (SlotPx + SlotGap) - _storageScroll),
                                  (float)SlotPx, (float)SlotPx });
        }
    }

    // Begin drag from module slot
    if (!_dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int si = 0; si < (int)slots.size(); ++si) {
            auto* opt = GetSlotOpt(slots[si]);
            if (opt && opt->has_value() && CheckCollisionPointRec(m, slots[si].rect)) {
                _dragging = true;
                _dragSrc = DragSrc::Slot;
                _dragKind = DragKind::Module; // slots only ever hold equippable modules
                _dragIdx = si;
                _dragMod = **opt;
                *opt = std::nullopt;
                break;
            }
        }
        if (!_dragging && _storage) {
            for (int i = 0; i < (int)stoRects.size(); ++i) {
                if (i >= (int)_storage->size()) continue;
                StorageItemType t = (*_storage)[i].type;
                if ((t != StorageItemType::Module && t != StorageItemType::Hardpoint) ||
                    !CheckCollisionPointRec(m, stoViewRec) || !CheckCollisionPointRec(m, stoRects[i]))
                    continue;
                _dragging = true;
                _dragSrc  = DragSrc::Storage;
                _dragIdx  = i;
                if (t == StorageItemType::Hardpoint) {
                    _dragKind = DragKind::Hardpoint;
                    _dragHp   = (*_storage)[i].hardpoint;
                } else {
                    _dragKind = DragKind::Module;
                    _dragMod  = (*_storage)[i].module;
                }
                (*_storage)[i] = StorageItem{};
                break;
            }
        }
    }

    // Drop logic
    if (_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        bool dropped = false;

        if (_dragKind == DragKind::Hardpoint) {
            // Hardpoint blueprints never equip into a slot — either attach to
            // the station or bounce back to the storage slot they came from.
            if (CheckCollisionPointRec(m, attachHpRect) && CanAttachHardpoint()) {
                _station->hardpoints.push_back(MakeBlankHardpointState(_dragHp));
                dropped = true;
            }
            if (!dropped && _storage && _dragIdx >= 0 && _dragIdx < (int)_storage->size()) {
                StorageItem& st = (*_storage)[_dragIdx];
                st.type        = StorageItemType::Hardpoint;
                st.hardpoint   = _dragHp;
                st.displayName = _dragHp.displayName;
            }
        }
        else if (_dragMod.type == ModuleType::Consumable) {
            // Repair kits are consumed on drop over any slot in the selected
            // hardpoint's panel — they heal, they don't equip.
            for (int si = 0; si < (int)slots.size(); ++si) {
                if (CheckCollisionPointRec(m, slots[si].rect)) {
                    if (_selHp >= 0 && _selHp < (int)_station->hardpoints.size()) {
                        HardpointState& hp = _station->hardpoints[_selHp];
                        hp.hull = std::min(hp.hull + _dragMod.consumable.healAmount, hp.maxHull);
                    }
                    dropped = true;
                    break;
                }
            }
            if (!dropped && _storage) {
                for (int i = 0; i < (int)stoRects.size(); ++i) {
                    if (i < (int)_storage->size() && (*_storage)[i].type == StorageItemType::Empty &&
                        CheckCollisionPointRec(m, stoViewRec) && CheckCollisionPointRec(m, stoRects[i])) {
                        StorageItem& st = (*_storage)[i];
                        st.type = StorageItemType::Module;
                        st.module = _dragMod;
                        st.displayName = _dragMod.displayName;
                        dropped = true;
                        break;
                    }
                }
            }
            if (!dropped && _dragSrc == DragSrc::Storage && _storage && _dragIdx < (int)_storage->size()) {
                StorageItem& st = (*_storage)[_dragIdx];
                st.type = StorageItemType::Module;
                st.module = _dragMod;
                st.displayName = _dragMod.displayName;
            }
        }
        else {
            for (int si = 0; si < (int)slots.size(); ++si) {
                auto* opt = GetSlotOpt(slots[si]);
                if (opt && CheckCollisionPointRec(m, slots[si].rect) && IsCompatible(slots[si].type, _dragMod)) {
                    if (opt->has_value() && _storage) {
                        for (auto& st : *_storage) {
                            if (st.type == StorageItemType::Empty) {
                                st.type = StorageItemType::Module;
                                st.module = **opt;
                                st.displayName = opt->value().displayName;
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
                        CheckCollisionPointRec(m, stoViewRec) && CheckCollisionPointRec(m, stoRects[i])) {
                        StorageItem& st = (*_storage)[i];
                        st.type = StorageItemType::Module;
                        st.module = _dragMod;
                        st.displayName = _dragMod.displayName;
                        dropped = true;
                        break;
                    }
                }
            }

            if (!dropped) {
                if (_dragSrc == DragSrc::Slot) {
                    auto* opt = GetSlotOpt(slots[_dragIdx]);
                    if (opt) *opt = _dragMod;
                }
                else if (_dragSrc == DragSrc::Storage && _storage && _dragIdx < (int)_storage->size()) {
                    StorageItem& st = (*_storage)[_dragIdx];
                    st.type = StorageItemType::Module;
                    st.module = _dragMod;
                    st.displayName = _dragMod.displayName;
                }
            }
        }

        _dragging = false;
        _dragSrc  = DragSrc::None;
        _dragIdx  = -1;
    }

    return true;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void StationModuleMenu::DrawSlot(Rectangle r, const std::optional<ModuleDef>& mod,
                                  bool hovered, bool /*highlighted*/) const {
    Color bg  = hovered ? Color{ 20,45,90,220 } : Color{ 10,20,45,200 };
    Color bdr = hovered ? Color{ 60,130,220,240 } : Color{ 40,80,150,180 };
    if (mod.has_value()) {
        bdr = StorageMenu::GradeColor(mod->grade);
        bg  = hovered ? Color{ 16,36,70,240 } : Color{ 12,26,55,220 };
    }
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1.5f, bdr);

    if (mod.has_value()) {
        Color tc = StorageMenu::TypeColor(mod->type);
        const char* abbr = StorageMenu::TypeName(mod->type);
        DrawText(abbr, (int)(r.x + 4), (int)(r.y + 4), 9, { tc.r, tc.g, tc.b, 180 });

        const char* nm = mod->displayName.c_str();
        int fs = 9;
        while (MeasureText(nm, fs) > (int)r.width - 6 && fs > 7) fs--;
        DrawText(nm, (int)(r.x + 3), (int)(r.y + r.height - fs - 5), fs, TxtMain);
    }
    else {
        DrawText("--", (int)(r.x + r.width / 2 - 5), (int)(r.y + r.height / 2 - 5), 10, TxtDim);
    }
}

bool StationModuleMenu::IsMouseOverMenu() const {
    if (!isOpen) return false;
    int sh = GetScreenHeight();
    int panelY = sh / 2 - 180;
    int panelX = GetScreenWidth() / 2 - PanelW / 2;
    return CheckCollisionPointRec(GetMousePosition(), { (float)panelX, (float)panelY, (float)PanelW, 360.0f });
}

std::string StationModuleMenu::GetSelectedShipId() const {
    if (!isOpen || _selHp < 0 || _selHp >= (int)_station->hardpoints.size()) return "";
    if (_station->hardpoints[_selHp].displayName.find("Docking") == std::string::npos) return "";

    const auto& allShips = ecs::ShipRegistry::AllShips();
    if (_selShipIdx >= 0 && _selShipIdx < (int)allShips.size()) {
        return allShips[_selShipIdx].id;
    }
    return "";
}

void StationModuleMenu::Draw() const {
    if (!isOpen || !_station) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int panelX = sw / 2 - PanelW / 2;
    int panelY = sh / 2 - 180;
    int panelH = 360;
    Vector2 mouse = GetMousePosition();

    // Dim background
    DrawRectangle(0, 0, sw, sh, Color{ 0,0,0,160 });

    // Panel
    DrawRectangle(panelX, panelY, PanelW, panelH, BgPanel);
    DrawRectangleLinesEx({ (float)panelX,(float)panelY,(float)PanelW,(float)panelH }, 1.5f, BdrMain);

    // Title
    char titlebuf[128];
    std::snprintf(titlebuf, sizeof(titlebuf), "%s — MODULES", _station->displayName.c_str());
    DrawText(titlebuf, panelX + (PanelW - MeasureText(titlebuf, 13)) / 2, panelY + 8, 13, TxtMain);

    // Close button
    Rectangle closeBtn = { (float)(panelX + PanelW - 28), (float)(panelY + 6), 22.0f, 22.0f };
    bool hovX = CheckCollisionPointRec(mouse, closeBtn);
    DrawRectangleRec(closeBtn, hovX ? Color{ 100,30,30,200 } : Color{ 40,15,15,180 });
    DrawRectangleLinesEx(closeBtn, 1.0f, Color{ 150,50,50,200 });
    DrawText("X", (int)(closeBtn.x + 7), (int)(closeBtn.y + 5), 12, hovX ? WHITE : TxtDim);

    // ── Hardpoint list (left column) ─────────────────────────────────────────
    int hpX = panelX + 6, hpY = panelY + 36;
    DrawRectangle(hpX, hpY - 2, HPListW - 4, panelH - 40, Color{ 6,12,28,220 });
    DrawRectangleLinesEx({ (float)hpX,(float)(hpY - 2),(float)(HPListW - 4),(float)(panelH - 40) }, 1.0f, Color{ 30,60,110,160 });

    for (int i = 0; i < (int)_station->hardpoints.size(); ++i) {
        const HardpointState& hp = _station->hardpoints[i];
        Rectangle r = { (float)hpX, (float)(hpY + i * 44), (float)(HPListW - 8), 40.0f };
        bool sel = (i == _selHp);
        bool hov = CheckCollisionPointRec(mouse, r);
        bool destroyed = !hp.alive;

        Color bg = sel ? Color{ 20,50,120,230 } : (hov ? Color{ 14,35,80,220 } : Color{ 10,20,50,180 });
        Color bdr = destroyed ? Color{ 130,30,30,180 }
            : hp.isCore ? Color{ 200,160, 30,200 }
        : (sel ? BdrMain : Color{ 30,60,110,160 });

        DrawRectangleRec(r, bg);
        DrawRectangleLinesEx(r, sel ? 1.5f : 1.0f, bdr);

        Color nameFg = destroyed ? Color{ 180,60,60,200 } : (hp.isCore ? Color{ 220,180,40,255 } : TxtMain);
        DrawText(hp.displayName.c_str(), (int)(r.x + 6), (int)(r.y + 5), 11, nameFg);

        float frac = hp.maxHull > 0 ? std::clamp(hp.hull / hp.maxHull, 0.0f, 1.0f) : 0.0f;
        int bw = (int)(r.width - 12);
        DrawRectangle((int)(r.x + 6), (int)(r.y + 22), bw, 7, Color{ 20,20,30,200 });
        Color hpColor = frac > 0.6f ? Color{ 60,200,80,255 } : frac > 0.3f ? Color{ 220,180,40,255 } : Color{ 200,60,60,255 };
        DrawRectangle((int)(r.x + 6), (int)(r.y + 22), (int)(bw * frac), 7, hpColor);
        DrawRectangleLinesEx({ r.x + 6, r.y + 22, (float)bw, 7.0f }, 0.8f, Color{ 40,60,80,160 });
        if (hp.isCore) DrawText("CORE", (int)(r.x + r.width - MeasureText("CORE", 9) - 6), (int)(r.y + 5), 9, Color{ 200,160,30,220 });
        if (destroyed) DrawText("DESTROYED", (int)(r.x + (r.width - MeasureText("DESTROYED", 9)) / 2), (int)(r.y + 14), 9, Color{ 200,60,60,220 });
    }

    if (CanAttachHardpoint()) {
        Rectangle ar = { (float)hpX, (float)(hpY + (int)_station->hardpoints.size() * 44),
                          (float)(HPListW - 8), 40.0f };
        bool hovAttach = _dragging && _dragKind == DragKind::Hardpoint && CheckCollisionPointRec(mouse, ar);
        Color bg  = hovAttach ? Color{ 20,60,20,230 } : Color{ 8,18,8,180 };
        Color bdr = hovAttach ? Color{ 90,220,90,255 } : Color{ 40,90,40,180 };
        DrawRectangleRec(ar, bg);
        DrawRectangleLinesEx(ar, hovAttach ? 2.0f : 1.0f, bdr);
        const char* lbl = "+ ATTACH HARDPOINT";
        DrawText(lbl, (int)(ar.x + (ar.width - MeasureText(lbl, 9)) / 2.0f),
                 (int)(ar.y + ar.height / 2 - 4), 9, hovAttach ? WHITE : Color{ 120,200,120,200 });
    }

    // ── Module Slots (Middle Column) ──────────────────────────────────────────
    int slotAreaX = panelX + HPListW + 12;
    int slotAreaY = panelY + 36;
    int slotAreaW = 380; // Fixed width to leave room for shipyard
    int slotAreaH = panelH - 130;

    DrawRectangle(slotAreaX, slotAreaY, slotAreaW, slotAreaH, Color{ 6,12,28,220 });
    DrawRectangleLinesEx({ (float)slotAreaX,(float)slotAreaY,(float)slotAreaW,(float)slotAreaH }, 1.0f, Color{ 30,60,110,160 });

    if (_selHp >= 0 && _selHp < (int)_station->hardpoints.size()) {
        const HardpointState& hp = _station->hardpoints[_selHp];

        char hpTitle[128];
        std::snprintf(hpTitle, sizeof(hpTitle), "%s  MODULES", hp.displayName.c_str());
        DrawText(hpTitle, slotAreaX + 6, slotAreaY + 6, 11, TxtMain);

        std::vector<SlotRef> slots;
        BuildSlotRefs(_selHp, slots);

        for (int si = 0; si < (int)slots.size(); ++si) {
            auto* opt = GetSlotOpt(slots[si]);
            bool isDragged = (_dragging && _dragSrc == DragSrc::Slot && _dragIdx == si);
            std::optional<ModuleDef> drawOpt = (opt && opt->has_value() && !isDragged)
                ? *opt : std::nullopt;
            bool hov = CheckCollisionPointRec(mouse, slots[si].rect);
            bool compat = _dragging && _dragKind == DragKind::Module && IsCompatible(slots[si].type, _dragMod);
            DrawSlot(slots[si].rect, drawOpt, hov, compat);
        }

        // ── Shipyard Menu (Right Column) ───────────────────────────────────────
        if (hp.displayName.find("Docking") != std::string::npos) {
            int shipAreaX = slotAreaX + slotAreaW + 12;
            int shipAreaY = panelY + 36;
            int shipAreaW = PanelW - HPListW - slotAreaW - 36;
            int shipAreaH = panelH - 48;

            DrawRectangle(shipAreaX, shipAreaY, shipAreaW, shipAreaH, Color{ 6,12,28,220 });
            DrawRectangleLinesEx({ (float)shipAreaX,(float)shipAreaY,(float)shipAreaW,(float)shipAreaH }, 1.0f, Color{ 30,60,110,160 });

            DrawText("AVAILABLE SHIPS", shipAreaX + 6, shipAreaY + 6, 11, TxtMain);
            const auto& allShips = ecs::ShipRegistry::AllShips();

            for (int i = 0; i < (int)allShips.size(); ++i) {
                const ecs::ShipDef& sdef = allShips[i];
                Rectangle r = { (float)(shipAreaX + 6), (float)(shipAreaY + 24 + i * 44), (float)(shipAreaW - 12), 40.0f };
                bool sel = (i == _selShipIdx);
                bool hov = CheckCollisionPointRec(mouse, r);
                DrawRectangleRec(r, sel ? Color{ 20,50,120,230 } : (hov ? Color{ 14,35,80,220 } : Color{ 10,20,50,180 }));
                DrawRectangleLinesEx(r, sel ? 1.5f : 1.0f, sel ? BdrMain : Color{ 30,60,110,160 });
                DrawText(sdef.displayName.c_str(), (int)(r.x + 8), (int)(r.y + 14), 12, TxtMain);
            }

            Rectangle buildBtn = { (float)(shipAreaX + shipAreaW / 2 - 60), (float)(panelY + panelH - 50), 120.0f, 30.0f };
            bool hovBuild = CheckCollisionPointRec(mouse, buildBtn);
            bool canBuild = (_selShipIdx >= 0 && _selShipIdx < (int)allShips.size());
            DrawRectangleRec(buildBtn, canBuild ? (hovBuild ? Color{ 20,80,40,230 } : Color{ 12,45,22,200 }) : Color{ 22,22,28,160 });
            DrawRectangleLinesEx(buildBtn, 1.5f, canBuild ? Color{ 30,160,60,200 } : Color{ 45,45,55,140 });
            DrawText("PLACE SHIP", (int)(buildBtn.x + 28), (int)(buildBtn.y + 9), 11, canBuild ? (hovBuild ? WHITE : Color{ 80,220,100,255 }) : TxtDim);
        }
    }

    // ── Storage area (bottom middle) ──────────────────────────────────────────
    int stoY = panelY + panelH - 118;
    DrawRectangle(slotAreaX, stoY, slotAreaW, 106, Color{ 6,12,28,220 });
    DrawRectangleLinesEx({ (float)slotAreaX,(float)stoY,(float)slotAreaW,106.0f },
        1.0f, Color{ 30,60,110,160 });
    DrawText("STORAGE", slotAreaX + 6, stoY + 4, 10, TxtDim);

    Rectangle stoViewRec = { (float)slotAreaX, (float)(stoY + 18), (float)slotAreaW, (float)(106 - 18) };

    if (_storage) {
        int cols = std::max(1, (slotAreaW + SlotGap) / (SlotPx + SlotGap));
        BeginScissorMode((int)stoViewRec.x, (int)stoViewRec.y, (int)stoViewRec.width, (int)stoViewRec.height);

        for (int i = 0; i < (int)_storage->size(); ++i) {
            int col = i % cols, row = i / cols;
            Rectangle r = { (float)(slotAreaX + col * (SlotPx + SlotGap)),
                            (float)(stoY + 18 + row * (SlotPx + SlotGap) - _storageScroll),
                            (float)SlotPx, (float)SlotPx };

            if (r.y + r.height <= stoViewRec.y || r.y >= stoViewRec.y + stoViewRec.height) continue;

            const StorageItem& it = (*_storage)[i];
            bool isDragged = (_dragging && _dragSrc == DragSrc::Storage && _dragIdx == i);
            bool hov = CheckCollisionPointRec(mouse, stoViewRec) && CheckCollisionPointRec(mouse, r);

            if (it.type == StorageItemType::Hardpoint && !isDragged) {
                DrawHardpointChip(r, it.hardpoint.displayName, hov);
            } else {
                std::optional<ModuleDef> opt = (it.type == StorageItemType::Module && !isDragged)
                    ? std::optional<ModuleDef>{it.module} : std::nullopt;
                DrawSlot(r, opt, hov, false);
            }
        }
        EndScissorMode();
    }

    if (_dragging) {
        Rectangle dr = { mouse.x - SlotPx / 2.0f, mouse.y - SlotPx / 2.0f, (float)SlotPx, (float)SlotPx };
        if (_dragKind == DragKind::Hardpoint) DrawHardpointChip(dr, _dragHp.displayName, false);
        else                                  DrawSlot(dr, _dragMod, false, false);
    }
}

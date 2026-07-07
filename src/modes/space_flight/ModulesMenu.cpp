#include "ModulesMenu.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// ── Geometry helpers ──────────────────────────────────────────────────────────

void ModulesMenu::PanelSplit(int sw, int& modW, int& stoX, int& stoW) {
    int usable = sw - 100;
    modW = usable * 44 / 100;
    stoX = 50 + modW + 30;
    stoW = usable - modW - 30;
}

void ModulesMenu::StorageSlotRects(int stoX, int stoW, int count, Rectangle* out) {
    StorageMenu::GetRects(stoX, 90, stoW, count, out);
}

// ── ModulesMenu ───────────────────────────────────────────────────────────────

void ModulesMenu::Open(ShipLoadout* loadout,
    std::vector<StorageItem>* storage,
    int wSlots, int arSlots, int shSlots, int enSlots, int hdSlots, int auxSlots,
    HealthComponent* healTarget) {
    isOpen = true;
    _loadout  = loadout;
    _storage  = storage;
    _healTarget = healTarget;
    _wSlots   = wSlots;
    _arSlots  = arSlots;
    _shSlots  = shSlots;
    _enSlots  = enSlots;
    _hdSlots  = hdSlots;
    _auxSlots = auxSlots;
    _dragging = false;
    _dragSrc  = DragSrc::None;
    _dragIdx  = -1;
    _hovModSlot     = -1;
    _hovStorageSlot = -1;
    _hoverTimer  = 0.0f;
    _hoverSlotId = -999999;
}

void ModulesMenu::Close() {
    isOpen = false;
    _dragging = false;
    _dragSrc = DragSrc::None;
    _dragIdx = -1;
}

void ModulesMenu::Open(ShipLoadout* loadout,
    std::vector<StorageItem>* storage,
    int wSlots, int arSlots, int shSlots, int enSlots, int auxSlots,
    HealthComponent* healTarget) {
    isOpen = true;
    _loadout = loadout;
    _storage = storage;
    _healTarget = healTarget;
    _wSlots   = wSlots;
    _arSlots  = arSlots;
    _shSlots  = shSlots;
    _enSlots  = enSlots;
    _hdSlots  = 0;
    _auxSlots = auxSlots;
    _dragging = false;
    _dragSrc = DragSrc::None;
    _dragIdx = -1;
    _hovModSlot = -1;
    _hovStorageSlot = -1;
    _hoverTimer = 0.0f;
    _hoverSlotId = -999999;
}

// ── Slot layout ───────────────────────────────────────────────────────────────

void ModulesMenu::BuildModSlots(std::vector<ModSlotRef>& out) const {
    if (!_loadout) return;
    out.clear();
    static constexpr int LabelW = 130;
    static constexpr int SlotPx = 64;
    static constexpr int SlotGap = 8;
    static constexpr int RowH = 80;
    static constexpr int StartX = 50;
    static constexpr int StartY = 90;
    const int slotsX = StartX + LabelW;

    auto pushRow = [&](ModuleType type, int count, int row) {
        for (int i = 0; i < count; ++i) {
            float x = (float)(slotsX + i * (SlotPx + SlotGap));
            float y = (float)(StartY + row * RowH);
            out.push_back({ { x, y, (float)SlotPx, (float)SlotPx }, type, i });
        }
        };

    pushRow(ModuleType::Weapon,     _wSlots,   0);
    pushRow(ModuleType::Armor,      _arSlots,  1);
    pushRow(ModuleType::Shield,     _shSlots,  2);
    pushRow(ModuleType::Engine,     _enSlots,  3);
    pushRow(ModuleType::Hyperdrive, _hdSlots,  4);
    pushRow(ModuleType::Auxiliary,  _auxSlots, 5);
}

bool ModulesMenu::IsCompatible(ModuleType slotType, const ModuleDef& mod) const {
    return mod.type == slotType;
}

std::optional<ModuleDef>* ModulesMenu::GetModOpt(const ModSlotRef& ms)
{
    if (!_loadout)
        return nullptr;

    if (ms.type == ModuleType::Weapon &&
        ms.idx >= 0 &&
        ms.idx < (int)_loadout->weapons.size())
    {
        return &_loadout->weapons[ms.idx];
    }

    if (ms.type == ModuleType::Armor && ms.idx == 0)
        return &_loadout->armor;

    if (ms.type == ModuleType::Shield &&
        ms.idx >= 0 &&
        ms.idx < (int)_loadout->shields.size())
    {
        return &_loadout->shields[ms.idx];
    }

    if (ms.type == ModuleType::Engine && ms.idx == 0)
        return &_loadout->engine;

    if (ms.type == ModuleType::Hyperdrive && ms.idx == 0)
        return &_loadout->hyperdrive;

    if (ms.type == ModuleType::Auxiliary &&
        ms.idx >= 0 &&
        ms.idx < (int)_loadout->aux.size())
    {
        return &_loadout->aux[ms.idx];
    }

    return nullptr;
}

const std::optional<ModuleDef>* ModulesMenu::GetModOpt(const ModSlotRef& ms) const {
    return const_cast<ModulesMenu*>(this)->GetModOpt(ms);
}

static const char* ModTypeLetter(ModuleType t) {
    switch (t) {
    case ModuleType::Weapon:     return "W";
    case ModuleType::Armor:      return "A";
    case ModuleType::Shield:     return "S";
    case ModuleType::Engine:     return "E";
    case ModuleType::Hyperdrive: return "H";
    case ModuleType::Auxiliary:  return "U";
    case ModuleType::Consumable: return "R";
    }
    return "?";
}

void ModulesMenu::DrawModSlot(Rectangle r, const std::optional<ModuleDef>& mod,
    bool hovered, bool highlighted) const {
    DrawRectangleRec(r, Color{ 10, 18, 10, 220 });

    Color border = highlighted ? Color{ 255, 255, 80, 255 }
        : hovered ? Color{ 120, 220,120, 255 }
    : Color{ 34,  98, 34, 155 };
    DrawRectangleLinesEx(r, (highlighted || hovered) ? 2.0f : 1.0f, border);

    if (!mod) {
        const char* em = "EMPTY";
        DrawText(em,
            (int)(r.x + (r.width - MeasureText(em, 9)) / 2.0f),
            (int)(r.y + r.height / 2.0f - 5), 9,
            Color{ 50, 70, 50, 110 });
        return;
    }

    Color gc = StorageMenu::GradeColor(mod->grade);
    DrawRectangleLinesEx({ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, 2.0f,
        { gc.r, gc.g, gc.b, 170 });

    const char* letter = ModTypeLetter(mod->type);
    Color lc = StorageMenu::TypeColor(mod->type);
    int fs = 28;
    int lw = MeasureText(letter, fs);
    DrawText(letter,
        (int)(r.x + (r.width - lw) / 2.0f),
        (int)(r.y + (r.height - fs) / 2.0f - 4),
        fs, { lc.r, lc.g, lc.b, 210 });

    const char* gn = StorageMenu::GradeName(mod->grade);
    int gnw = MeasureText(gn, 8);
    DrawText(gn,
        (int)(r.x + (r.width - gnw) / 2.0f),
        (int)(r.y + r.height - 12),
        8, { gc.r, gc.g, gc.b, 160 });
}

void ModulesMenu::DrawDraggedItem() const {
    if (!_dragging) return;

    Vector2 m = GetMousePosition();
    const float sz = 64.0f;
    Rectangle r = { m.x - sz * 0.5f, m.y - sz * 0.5f, sz, sz };

    DrawRectangleRec(r, Color{ 12, 20, 12, 230 });
    DrawRectangleLinesEx(r, 2.0f, Color{ 100, 220, 100, 255 });

    Color gc = StorageMenu::GradeColor(_dragMod.grade);
    DrawRectangleLinesEx({ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, 2.0f,
        { gc.r, gc.g, gc.b, 170 });

    const char* letter = ModTypeLetter(_dragMod.type);
    Color lc = StorageMenu::TypeColor(_dragMod.type);
    int fs = 28;
    int lw = MeasureText(letter, fs);
    DrawText(letter,
        (int)(r.x + (r.width - lw) / 2.0f),
        (int)(r.y + (r.height - fs) / 2.0f - 4),
        fs, { lc.r, lc.g, lc.b, 230 });
}

void ModulesMenu::DrawModuleTooltip(const ModuleDef& mod, Vector2 mousePos, float alpha) const {
    if (alpha < 0.01f) return;

    auto A = [&](uint8_t base) -> uint8_t {
        return (uint8_t)(base * alpha);
        };

    static constexpr int MaxLines = 8;
    const char* labels[MaxLines]; char vals[MaxLines][32]; int nLines = 0;
    auto addStat = [&](const char* lbl, const char* fmt, float v) {
        if (nLines >= MaxLines) return;
        labels[nLines] = lbl;
        std::snprintf(vals[nLines], 32, fmt, v);
        ++nLines;
        };
    auto addStr = [&](const char* lbl, const char* val) {
        if (nLines >= MaxLines) return;
        labels[nLines] = lbl;
        std::strncpy(vals[nLines], val, 31); vals[nLines][31] = '\0';
        ++nLines;
        };

    switch (mod.type) {
    case ModuleType::Engine:
    case ModuleType::Hyperdrive:
        addStat("Thrust", "+%.0f", mod.engine.thrustBonus);
        addStat("Turn Spd", "+%.0f", mod.engine.turnSpeedBonus);
        if (mod.engine.isHyperdrive)
            addStat("Hyp Range", "%.0f u", mod.engine.hyperdriveRange);
        break;
    case ModuleType::Armor:
        addStat("Hull Bonus", "+%.0f", mod.armor.hullBonus);
        break;
    case ModuleType::Shield:
        addStr("Type", mod.shield.shieldType == ShieldType::Kinetic ? "Kinetic" : "Energy");
        addStat("Capacity", "%.0f", mod.shield.capacity);
        addStat("Recharge", "%.1f/s", mod.shield.rechargeRate);
        addStat("Delay", "%.1f s", mod.shield.rechargeDelay);
        break;
    case ModuleType::Weapon: {
        const WeaponStats& ws = mod.weapon;
        addStat("Damage", "%.0f", ws.damage);
        float rps = ws.fireRate > 0.0f ? 1.0f / ws.fireRate : 0.0f;
        addStat("Fire Rate", "%.2f/s", rps);
        addStat("Range", "%.0f u", ws.projRange);
        addStr("Dmg Type", ws.damageType == DamageType::Kinetic ? "Kinetic" : "Energy");
        if (ws.isTurret)                   addStr("Arc", "360°  (turret)");
        if (ws.effect != WeaponEffect::None) {
            const char* eff = ws.effect == WeaponEffect::EMP ? "EMP" : "Ion";
            char tmp[32]; std::snprintf(tmp, sizeof(tmp), "%s  %.1f s", eff, ws.effectDuration);
            addStr("Effect", tmp);
        }
        break;
    }
    case ModuleType::Auxiliary: {
        const AuxStats& ax = mod.auxiliary;
        if (ax.hasSensors)       addStat("Sensor Range", "%.0f u", ax.sensorRange);
        if (ax.mapSensorRange > 0.0f) addStat("Map Sensor Range", "%.0f u", ax.mapSensorRange);
        if (ax.hasCloaking)      addStr("Cloaking", "Active");
        if (ax.hasLockOnJammer)  addStr("Lock-On Jam", "Active");
        // materialFindBonus has no effect on a player-piloted ship — it only
        // governs a mining station's collection rate once installed there.
        if (nLines == 0)         addStr("Status", "No effects");
        break;
    }
    case ModuleType::Consumable:
        addStat("Heal", "+%.0f hull", mod.consumable.healAmount);
        break;
    }

    const int padX = 10, padY = 8;
    const int lineH = 15;
    const int panW = 210;
    const int panH = padY * 2 + 16 + 13 + 5 + nLines * lineH + (nLines > 0 ? 4 : 0);

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int px = (int)mousePos.x + 18;
    int py = (int)mousePos.y - panH / 2;
    if (px + panW > sw - 8) px = (int)mousePos.x - panW - 8;
    if (py < 8)              py = 8;
    if (py + panH > sh - 8) py = sh - panH - 8;

    Color gc = StorageMenu::GradeColor(mod.grade);
    DrawRectangle(px, py, panW, panH, { 6, 10, 6, A(240) });
    DrawRectangleLinesEx({ (float)px, (float)py, (float)panW, (float)panH },
        1.5f, { gc.r, gc.g, gc.b, A(200) });

    int cx = px + padX;
    int cy = py + padY;

    DrawText(mod.displayName.c_str(), cx, cy, 14, { 210, 235, 210, A(255) });
    cy += 18;

    DrawText(StorageMenu::GradeName(mod.grade), cx, cy, 11,
        { gc.r, gc.g, gc.b, A(230) });
    cy += 14;

    if (nLines > 0) {
        DrawRectangle(cx, cy, panW - padX * 2, 1, { 40, 80, 40, A(140) });
        cy += 5;

        Color lblColor = { 115, 155, 115, A(220) };
        Color valColor = { 195, 220, 195, A(255) };
        for (int i = 0; i < nLines; ++i) {
            DrawText(labels[i], cx, cy, 11, lblColor);
            DrawText(vals[i], cx + 95, cy, 11, valColor);
            cy += lineH;
        }
    }
}

void ModulesMenu::Draw() const {
    if (!isOpen || !_loadout || !_storage) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{ 1, 3, 1, 255 });

    const char* title = "MODULES";
    DrawText(title, (sw - MeasureText(title, 26)) / 2, 18, 26, Color{ 68, 162, 68, 255 });
    DrawRectangle((sw - 400) / 2, 54, 400, 1, Color{ 34, 98, 34, 170 });

    Vector2 mouse = GetMousePosition();
    Rectangle back = { 18.0f, 16.0f, 110.0f, 36.0f };
    bool hb = CheckCollisionPointRec(mouse, back);
    DrawRectangleRec(back, hb ? Color{ 50, 95, 50, 230 } : Color{ 12, 28, 12, 220 });
    DrawRectangleLinesEx(back, 1.0f, Color{ 40, 160, 40, 200 });
    const char* bl = "< BACK";
    DrawText(bl,
        (int)(back.x + (back.width - MeasureText(bl, 15)) / 2.0f),
        (int)(back.y + (back.height - 15) / 2.0f),
        15, WHITE);

    int modW, stoX, stoW;
    PanelSplit(sw, modW, stoX, stoW);
    DrawRectangle(50 + modW + 14, 60, 1, sh - 100, Color{ 34, 98, 34, 120 });
    DrawText("SHIP MODULES", 50, 64, 13, Color{ 68, 162, 68, 195 });
    DrawText("STORAGE", stoX, 64, 13, Color{ 68, 162, 68, 195 });

    static constexpr int LabelW = 130;
    static constexpr int RowH = 80;
    static constexpr int StartY = 90;
    struct RowDef { int row; ModuleType type; const char* label; };
    const RowDef rowDefs[] = {
        {0, ModuleType::Weapon,     "WEAPON"},
        {1, ModuleType::Armor,      "ARMOR"},
        {2, ModuleType::Shield,     "SHIELD"},
        {3, ModuleType::Engine,     "ENGINE"},
        {4, ModuleType::Hyperdrive, "HYPERDRIVE"},
        {5, ModuleType::Auxiliary,  "AUX"},
    };
    for (auto& rd : rowDefs) {
        int ty = StartY + rd.row * RowH + (64 - 14) / 2;
        Color lc = StorageMenu::TypeColor(rd.type);
        DrawText(rd.label, 50, ty, 14, { lc.r, lc.g, lc.b, 200 });

        int slotCount =
            rd.type == ModuleType::Weapon     ? _wSlots   :
            rd.type == ModuleType::Armor      ? _arSlots  :
            rd.type == ModuleType::Shield     ? _shSlots  :
            rd.type == ModuleType::Engine     ? _enSlots  :
            rd.type == ModuleType::Hyperdrive ? _hdSlots  :
            _auxSlots;
        if (slotCount == 0)
            DrawText("NO SLOT", 50 + LabelW, ty, 11, Color{ 50, 55, 50, 160 });
    }

    std::vector<ModSlotRef> modSlots;
    BuildModSlots(modSlots);

    for (int i = 0; i < (int)modSlots.size(); ++i) {
        const ModSlotRef& ms = modSlots[i];
        bool isSource = (_dragging && _dragSrc == DragSrc::ModSlot && i == _dragIdx);
        const std::optional<ModuleDef>* optPtr = isSource ? nullptr : GetModOpt(ms);
        bool hov = (i == _hovModSlot) && !isSource;
        bool hl = _dragging && !isSource && IsCompatible(ms.type, _dragMod);
        DrawModSlot(ms.rect, optPtr ? *optPtr : std::nullopt, hov, hl);
    }

    int n = (int)_storage->size();
    std::vector<Rectangle> stoRects(n);
    StorageSlotRects(stoX, stoW, n, stoRects.data());

    for (int i = 0; i < n; ++i) {
        bool dim = _dragging && _dragSrc == DragSrc::Storage && i == _dragIdx;
        bool hovSto = (i == _hovStorageSlot);
        bool hlSto = _dragging && _dragSrc == DragSrc::ModSlot && hovSto;
        StorageMenu::DrawItemInSlot(stoRects[i], (*_storage)[i], hovSto || hlSto, dim);
    }

    DrawDraggedItem();

    float tooltipAlpha = _dragging ? 0.0f : std::min(_hoverTimer / 0.3f, 1.0f);
    if (tooltipAlpha > 0.01f) {
        if (_hovModSlot >= 0 && _hovModSlot < (int)modSlots.size()) {
            const auto* optPtr = GetModOpt(modSlots[_hovModSlot]);
            if (optPtr && optPtr->has_value()) {
                const Rectangle& sr = modSlots[_hovModSlot].rect;
                Vector2 anchor = { sr.x + sr.width, sr.y + sr.height * 0.5f };
                DrawModuleTooltip(optPtr->value(), anchor, tooltipAlpha);
            }
        }
        else if (_hovStorageSlot >= 0 && _hovStorageSlot < n) {
            const StorageItem& si = (*_storage)[_hovStorageSlot];
            if (si.type == StorageItemType::Module) {
                const Rectangle& sr = stoRects[_hovStorageSlot];
                Vector2 anchor = { sr.x + sr.width, sr.y + sr.height * 0.5f };
                DrawModuleTooltip(si.module, anchor, tooltipAlpha);
            }
        }
    }

    // ── ADDED: CONTEXTUAL HAND CURSOR HANDLING ──────────────────────────────
    bool showHand = false;
    bool grabbed = _dragging;

    if (_dragging) {
        showHand = true;
    }
    else if (_hovModSlot >= 0 && _hovModSlot < (int)modSlots.size()) {
        const auto* optPtr = GetModOpt(modSlots[_hovModSlot]);
        if (optPtr && optPtr->has_value()) {
            showHand = true;
        }
    }
    else if (_hovStorageSlot >= 0 && _hovStorageSlot < (int)_storage->size()) {
        if ((*_storage)[_hovStorageSlot].type != StorageItemType::Empty) {
            showHand = true;
        }
    }

    if (showHand) {
        HideCursor();
        Vector2 m = GetMousePosition();
        if (grabbed) {
            DrawCircle((int)m.x, (int)m.y, 6, Color{ 40, 180, 40, 240 });
            DrawCircleLines((int)m.x, (int)m.y, 6.0f, Color{ 150, 255, 150, 255 });
            DrawCircleLines((int)m.x - 2, (int)m.y + 1, 2.0f, WHITE);
        }
        else {
            DrawRectangle((int)m.x - 5, (int)m.y - 2, 10, 7, Color{ 20, 100, 20, 200 });
            DrawRectangleLines((int)m.x - 5, (int)m.y - 2, 10, 7, Color{ 100, 220, 100, 255 });
            for (int f = 0; f < 4; ++f) {
                int fx = (int)m.x - 4 + f * 2;
                DrawLine(fx, (int)m.y - 2, fx, (int)m.y - 8, Color{ 150, 255, 150, 255 });
            }
            DrawLine((int)m.x - 5, (int)m.y + 2, (int)m.x - 9, (int)m.y - 1, Color{ 150, 255, 150, 255 });
        }
    }
    else {
        ShowCursor();
    }
}

bool ModulesMenu::Update() {
    if (!isOpen) return false;
    Vector2 mouse = GetMousePosition();
    float dt = GetFrameTime();

    if (_dragging && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        _dragging = false;
        _dragSrc = DragSrc::None;
        _dragIdx = -1;
        _hoverTimer = 0.0f;
        _hoverSlotId = -999999;
    }

    Rectangle back = { 18.0f, 16.0f, 110.0f, 36.0f };
    if (IsKeyPressed(KEY_ESCAPE) ||
        (CheckCollisionPointRec(mouse, back) &&
            (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)))) {
        Close();
        return false;
    }

    if (!_loadout || !_storage) return false;

    int sw = GetScreenWidth();
    int modW, stoX, stoW;
    PanelSplit(sw, modW, stoX, stoW);

    int n = (int)_storage->size();
    std::vector<Rectangle>  stoRects(n);
    std::vector<ModSlotRef> modSlots;
    StorageSlotRects(stoX, stoW, n, stoRects.data());
    BuildModSlots(modSlots);

    _hovModSlot = -1;
    _hovStorageSlot = -1;
    for (int i = 0; i < (int)modSlots.size(); ++i)
        if (CheckCollisionPointRec(mouse, modSlots[i].rect)) { _hovModSlot = i; break; }
    for (int i = 0; i < n; ++i)
        if (CheckCollisionPointRec(mouse, stoRects[i])) { _hovStorageSlot = i; break; }

    int slotId = (_hovModSlot >= 0) ? _hovModSlot
        : (_hovStorageSlot >= 0) ? -(_hovStorageSlot + 1)
        : -999999;
    if (slotId != _hoverSlotId) { _hoverSlotId = slotId; _hoverTimer = 0.0f; }
    else if (slotId != -999999) { _hoverTimer += dt; }

    if (_dragging && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        _dragging = false;
        _dragSrc = DragSrc::None;
        _dragIdx = -1;
        _hoverTimer = 0.0f;
        _hoverSlotId = -999999;
    }

    if (!_dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && _hovStorageSlot >= 0) {
        const StorageItem& si = (*_storage)[_hovStorageSlot];
        if (si.type == StorageItemType::Module) {
            _dragging = true;
            _dragSrc = DragSrc::Storage;
            _dragIdx = _hovStorageSlot;
            _dragMod = si.module;
            _hoverTimer = 0.0f;
            _hoverSlotId = -999999;
        }
    }

    if (!_dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && _hovModSlot >= 0) {
        if (modSlots[_hovModSlot].type != ModuleType::Armor) {
            auto* opt = GetModOpt(modSlots[_hovModSlot]);
            if (opt && opt->has_value()) {
                _dragging = true;
                _dragSrc = DragSrc::ModSlot;
                _dragIdx = _hovModSlot;
                _dragMod = opt->value();
                _hoverTimer = 0.0f;
                _hoverSlotId = -999999;
            }
        }
    }

    bool changed = false;
    if (_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        _dragging = false;
        _hoverTimer = 0.0f;
        _hoverSlotId = -999999;

        if (_dragSrc == DragSrc::Storage) {
            if (_hovModSlot >= 0 && _dragIdx >= 0 && _dragIdx < n) {
                StorageItem& src = (*_storage)[_dragIdx];
                if (src.type == StorageItemType::Module && src.module.type == ModuleType::Consumable) {
                    // Repair kits are consumed on drop over any slot — they
                    // heal the ship, they don't equip.
                    if (_healTarget) {
                        _healTarget->currentHull = std::min(
                            _healTarget->currentHull + src.module.consumable.healAmount,
                            _healTarget->maxStats.hull);
                    }
                    src = StorageItem{};
                    changed = true;
                }
                else {
                    const ModSlotRef& ms = modSlots[_hovModSlot];
                    if (src.type == StorageItemType::Module && IsCompatible(ms.type, src.module)) {
                        auto* target = GetModOpt(ms);
                        if (target) {
                            ModuleDef incoming = src.module;
                            if (*target) {
                                src.module = **target;
                                src.displayName = src.module.displayName;
                                src.type = StorageItemType::Module;
                            }
                            else {
                                src = StorageItem{};
                            }
                            *target = incoming;
                            changed = true;
                        }
                    }
                }
            }
        }
        else if (_dragSrc == DragSrc::ModSlot && _dragIdx >= 0) {
            auto* srcOpt = GetModOpt(modSlots[_dragIdx]);

            if (_hovStorageSlot >= 0 && srcOpt) {
                StorageItem& dest = (*_storage)[_hovStorageSlot];
                if (dest.type == StorageItemType::Empty) {
                    dest.type = StorageItemType::Module;
                    dest.module = _dragMod;
                    dest.displayName = _dragMod.displayName;
                    *srcOpt = std::nullopt;
                    changed = true;
                }
            }
            else if (_hovModSlot >= 0 && _hovModSlot != _dragIdx) {
                const ModSlotRef& targetMs = modSlots[_hovModSlot];
                auto* targetOpt = GetModOpt(targetMs);
                if (srcOpt && targetOpt && IsCompatible(targetMs.type, _dragMod)) {
                    bool reverseOk = !targetOpt->has_value() ||
                        IsCompatible(modSlots[_dragIdx].type, targetOpt->value());
                    if (reverseOk) {
                        std::swap(*srcOpt, *targetOpt);
                        changed = true;
                    }
                }
            }
        }

        _dragSrc = DragSrc::None;
        _dragIdx = -1;
    }
    return changed;
}
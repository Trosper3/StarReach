#include "ui/EngineerMenu.h"
#include "items/AttributeRegistry.h"
#include "items/GradeRegistry.h"
#include "items/SynergyManager.h"
#include "progression/ProgressionRegistry.h"
#include "data/registry/ModuleRegistry.h"
#include "modes/space_flight/StorageMenu.h"
#include "raylib.h"
#include <cstdio>
#include <string>

// ── Layout constants (shared by Update and Draw) ─────────────────────────────
// All computed from GetScreenWidth/Height each frame; no globals needed.
// Left panel : x=0,       width = sw*38/100
// Middle panel: x=leftW,  width = sw*30/100
// Right panel : x=rightX, width = remainder
// Panel top   : y = kPanelY = 60

static constexpr int kPanelY   = 60;
static constexpr int kListRowH = 30;     // left panel cargo rows
static constexpr int kAttrRowH = 28;     // middle panel attribute rows

// ── Helpers ──────────────────────────────────────────────────────────────────

const char* EngineerMenu::_gradeStr(ModuleGrade g) {
    switch (g) {
    case ModuleGrade::Common:     return "Common";
    case ModuleGrade::Uncommon:   return "Uncommon";
    case ModuleGrade::Unique:     return "Unique";
    case ModuleGrade::Remarkable: return "Remarkable";
    case ModuleGrade::Epic:       return "Epic";
    case ModuleGrade::Legendary:  return "Legendary";
    case ModuleGrade::Mythic:     return "Mythic";
    default:                      return "?";
    }
}

const char* EngineerMenu::_resultStr(GraftResult r) {
    switch (r) {
    case GraftResult::Success:                    return "Graft successful.";
    case GraftResult::SuboptimalGraft:            return "Sub-optimal graft (75% cap).";
    case GraftResult::ErrorInvalidAttribute:      return "Error: invalid attribute.";
    case GraftResult::ErrorMythicPrimaryBlocked:  return "Error: Mythic blocks primary grafts.";
    case GraftResult::ErrorInsufficientMaterials: return "Error: insufficient materials.";
    case GraftResult::ErrorInsufficientCredits:   return "Error: insufficient credits.";
    case GraftResult::ErrorSourceTypeMismatch:    return "Error: source/target type mismatch.";
    default:                                      return "Unknown error.";
    }
}

// Helper: returns display name for an item (falls back to defId).
static std::string ItemDisplayName(const Item& item) {
    auto opt = ModuleRegistry::ById(item.defId);
    return opt.has_value() ? opt->displayName : item.defId;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void EngineerMenu::Open(std::vector<Item>& cargo, InventoryComponent& inventory) {
    _cargo       = &cargo;
    _inventory   = &inventory;
    _targetIdx   = -1;
    _sourceIdx   = -1;
    _selAttrIdx  = -1;
    _pickTarget  = true;
    _lastSuccess = false;
    _resultMsg.clear();
    _resultTimer = 0.f;
    isOpen = true;
}

void EngineerMenu::Close() {
    isOpen     = false;
    _cargo     = nullptr;
    _inventory = nullptr;
}

// ── Update ────────────────────────────────────────────────────────────────────

bool EngineerMenu::Update() {
    if (!isOpen || !_cargo || !_inventory) return false;

    _resultTimer -= GetFrameTime();
    if (_resultTimer < 0.f) _resultTimer = 0.f;

    if (IsKeyPressed(KEY_ESCAPE)) { Close(); return false; }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return true;

    const Vector2 mp = GetMousePosition();
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int leftW  = sw * 38 / 100;
    const int midW   = sw * 30 / 100;
    const int midX   = leftW;
    const int rightX = leftW + midW;

    // ── Toggle buttons (T/S) ─────────────────────────────────────────────────
    Rectangle tBtn = { 14.f, (float)(kPanelY + 18), 80.f, 22.f };
    Rectangle sBtn = { 104.f, (float)(kPanelY + 18), 80.f, 22.f };
    if (CheckCollisionPointRec(mp, tBtn)) { _pickTarget = true;  return true; }
    if (CheckCollisionPointRec(mp, sBtn)) { _pickTarget = false; return true; }

    // ── Cargo list (left panel) ───────────────────────────────────────────────
    const int listStartY = kPanelY + 46;
    const int count      = static_cast<int>(_cargo->size());
    for (int i = 0; i < count; ++i) {
        Rectangle r = {
            12.f,
            (float)(listStartY + i * kListRowH),
            (float)(leftW - 24),
            (float)(kListRowH - 4)
        };
        if (CheckCollisionPointRec(mp, r)) {
            if (_pickTarget) {
                _targetIdx = (i == _targetIdx) ? -1 : i;
                if (_targetIdx == _sourceIdx && _targetIdx != -1) _sourceIdx = -1;
            } else {
                _sourceIdx = (i == _sourceIdx) ? -1 : i;
                if (_sourceIdx == _targetIdx && _sourceIdx != -1) _targetIdx = -1;
            }
            return true;
        }
    }

    // ── Attribute list (middle panel) ─────────────────────────────────────────
    const auto& attrs    = AttributeRegistry::All();
    const int attrStartY = kPanelY + 22;
    bool isMythic = (_targetIdx >= 0) && ((*_cargo)[_targetIdx].grade == ModuleGrade::Mythic);

    int drawIdx   = 0;
    bool inPrimary = true;
    for (int i = 0; i < static_cast<int>(attrs.size()); ++i) {
        if (inPrimary && !attrs[i].isPrimary) {
            inPrimary = false;
            drawIdx++; // secondary section header occupies one row
        }
        if (!(isMythic && attrs[i].isPrimary)) {
            Rectangle r = {
                (float)(midX + 8),
                (float)(attrStartY + 12 + drawIdx * kAttrRowH),
                (float)(midW - 16),
                (float)(kAttrRowH - 4)
            };
            if (CheckCollisionPointRec(mp, r)) {
                _selAttrIdx = (i == _selAttrIdx) ? -1 : i;
                return true;
            }
        }
        drawIdx++;
    }

    // ── Execute button (right panel) ──────────────────────────────────────────
    bool canExec = _targetIdx >= 0 && _sourceIdx >= 0 && _selAttrIdx >= 0;
    Rectangle execBtn = {
        (float)(rightX + 10),
        (float)(sh - 80),
        (float)(sw - rightX - 20),
        36.f
    };
    if (canExec && CheckCollisionPointRec(mp, execBtn)) {
        const AttributeDef& attr = attrs[_selAttrIdx];
        GraftRequest req{
            (*_cargo)[_targetIdx],
            attr.id,
            *_cargo,
            static_cast<size_t>(_sourceIdx),
            *_inventory
        };
        GraftResult res  = EngineerService::Execute(req);
        _resultMsg   = _resultStr(res);
        _resultTimer = 3.5f;
        bool completed = (res == GraftResult::Success || res == GraftResult::SuboptimalGraft);
        _lastSuccess   = completed;
        if (completed) {
            // Source was swap-and-popped from cargo — reset stale indices.
            _sourceIdx  = -1;
            _targetIdx  = -1;
            _selAttrIdx = -1;
        }
    }

    return true;
}

// ── Draw ──────────────────────────────────────────────────────────────────────

void EngineerMenu::Draw() const {
    if (!isOpen || !_cargo || !_inventory) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, {1, 4, 12, 252});

    const char* title = "ENGINEER'S WORKSHOP";
    DrawText(title, (sw - MeasureText(title, 24)) / 2, 18, 24, {100, 200, 255, 240});
    DrawRectangle((sw - 440) / 2, 52, 440, 1, {40, 110, 190, 140});

    const int leftW  = sw * 38 / 100;
    const int midW   = sw * 30 / 100;
    const int midX   = leftW;
    const int rightX = leftW + midW;
    const int panelH = sh - kPanelY - 10;

    DrawRectangle(midX,   kPanelY, 1, panelH, {40, 100, 180, 80});
    DrawRectangle(rightX, kPanelY, 1, panelH, {40, 100, 180, 80});

    _drawLeft  (0,      kPanelY, leftW,       panelH);
    _drawMiddle(midX,   kPanelY, midW,        panelH);
    _drawRight (rightX, kPanelY, sw - rightX, panelH);
}

// ── Left panel: cargo list ────────────────────────────────────────────────────

void EngineerMenu::_drawLeft(int x, int y, int w, int h) const {
    DrawText("CARGO", x + 14, y + 4, 13, {80, 170, 240, 200});

    // T / S mode toggle buttons
    Rectangle tBtn = { (float)(x + 14), (float)(y + 18), 80.f, 22.f };
    Rectangle sBtn = { (float)(x + 104), (float)(y + 18), 80.f, 22.f };

    Color tBg = _pickTarget  ? Color{30, 100, 45, 230}  : Color{15, 35, 20, 200};
    Color sBg = !_pickTarget ? Color{20, 60, 130, 230}  : Color{10, 20, 50, 200};
    DrawRectangleRec(tBtn, tBg);
    DrawRectangleLinesEx(tBtn, 1.f, _pickTarget  ? Color{60, 210, 90, 200} : Color{30, 70, 40, 140});
    DrawText("> TARGET", (int)(tBtn.x + 6), (int)(tBtn.y + 5), 11, WHITE);

    DrawRectangleRec(sBtn, sBg);
    DrawRectangleLinesEx(sBtn, 1.f, !_pickTarget ? Color{60, 140, 230, 200} : Color{20, 50, 100, 140});
    DrawText("> SOURCE", (int)(sBtn.x + 6), (int)(sBtn.y + 5), 11, WHITE);

    const int listStartY = y + 46;
    const auto& cargo    = *_cargo;
    const int count      = static_cast<int>(cargo.size());

    if (count == 0) {
        DrawText("(cargo empty)", x + 14, listStartY + 4, 12, {55, 90, 70, 160});
        return;
    }

    for (int i = 0; i < count; ++i) {
        const int iy = listStartY + i * kListRowH;
        if (iy + kListRowH > y + h) break;

        const Item& item = cargo[i];
        bool isTarget = (i == _targetIdx);
        bool isSource = (i == _sourceIdx);

        Color bgCol  = {10, 18, 28, 200};
        Color bord   = {30, 55, 90, 120};
        if      (isTarget) { bgCol = {10, 38, 20, 220}; bord = {55, 200, 80, 220}; }
        else if (isSource) { bgCol = {10, 22, 50, 220}; bord = {55, 140, 230, 220}; }

        Rectangle r = { (float)(x + 12), (float)iy, (float)(w - 24), (float)(kListRowH - 4) };
        DrawRectangleRec(r, bgCol);
        DrawRectangleLinesEx(r, 1.f, bord);

        // Grade colour swatch
        Color gc = StorageMenu::GradeColor(item.grade);
        DrawRectangle((int)(r.x + 6), (int)(r.y + (r.height - 8) * 0.5f), 8, 8, gc);

        // Item name
        std::string name = ItemDisplayName(item);
        DrawText(name.c_str(), (int)(r.x + 20), (int)(r.y + 4), 11, {200, 230, 220, 230});

        // Grade + merged flag (right-aligned)
        char statBuf[32];
        std::snprintf(statBuf, sizeof(statBuf), "%s%s", _gradeStr(item.grade), item.isMerged ? " [M]" : "");
        int stw = MeasureText(statBuf, 10);
        DrawText(statBuf, (int)(r.x + r.width - stw - 6), (int)(r.y + 5), 10, {gc.r, gc.g, gc.b, 180});

        // T / S badge corner
        if (isTarget) DrawText("T", (int)(r.x + r.width - 14), (int)(r.y + r.height - 14), 11, {60, 255, 80, 240});
        if (isSource) DrawText("S", (int)(r.x + r.width - 14), (int)(r.y + r.height - 14), 11, {80, 160, 255, 240});
    }
}

// ── Middle panel: attribute selection ────────────────────────────────────────

void EngineerMenu::_drawMiddle(int x, int y, int w, int h) const {
    DrawText("ATTRIBUTES", x + 10, y + 4, 13, {80, 170, 240, 200});

    bool isMythic = (_targetIdx >= 0) && ((*_cargo)[_targetIdx].grade == ModuleGrade::Mythic);
    const auto& attrs    = AttributeRegistry::All();
    const int attrStartY = y + 22;

    DrawText("- PRIMARY -", x + 10, attrStartY, 10, {55, 120, 190, 180});

    int drawIdx   = 0;
    bool inPrimary = true;
    for (int i = 0; i < static_cast<int>(attrs.size()); ++i) {
        if (inPrimary && !attrs[i].isPrimary) {
            inPrimary = false;
            int secY = attrStartY + 12 + drawIdx * kAttrRowH;
            DrawText("- SECONDARY -", x + 10, secY, 10, {55, 120, 190, 180});
            drawIdx++;
        }

        const int iy = attrStartY + 12 + drawIdx * kAttrRowH;
        if (iy + kAttrRowH > y + h) { drawIdx++; continue; }

        const AttributeDef& a = attrs[i];
        bool blocked  = isMythic && a.isPrimary;
        bool selected = (i == _selAttrIdx);

        Color bgCol  = blocked  ? Color{22, 16, 16, 180}
                     : selected ? Color{18, 50, 85, 230}
                                : Color{10, 18, 30, 200};
        Color textCol = blocked ? Color{65, 55, 55, 160} : Color{190, 220, 240, 230};
        Color bord    = selected ? Color{80, 165, 245, 220} : Color{28, 58, 100, 120};

        Rectangle r = { (float)(x + 8), (float)iy, (float)(w - 16), (float)(kAttrRowH - 4) };
        DrawRectangleRec(r, bgCol);
        DrawRectangleLinesEx(r, 1.f, bord);

        DrawText(selected ? ">" : " ", (int)(r.x + 4), (int)(r.y + 6), 11,
                 selected ? Color{80, 200, 245, 240} : Color{45, 85, 125, 180});
        DrawText(a.displayName.c_str(), (int)(r.x + 16), (int)(r.y + 6), 11, textCol);

        if (blocked) {
            const char* lbl = "MYTHIC";
            DrawText(lbl, (int)(r.x + r.width - MeasureText(lbl, 9) - 6), (int)(r.y + 7), 9, {140, 80, 200, 210});
        } else {
            char scaleBuf[20];
            std::snprintf(scaleBuf, sizeof(scaleBuf), "+%.0f%%/t", a.rarityScale * 100.f);
            int scw = MeasureText(scaleBuf, 9);
            DrawText(scaleBuf, (int)(r.x + r.width - scw - 6), (int)(r.y + 7), 9, {85, 135, 180, 160});
        }

        drawIdx++;
    }
}

// ── Right panel: cost preview + projection + execute ──────────────────────────

void EngineerMenu::_drawRight(int x, int y, int w, int h) const {
    const int sh = GetScreenHeight();

    DrawText("COST PREVIEW", x + 10, y + 4, 13, {80, 170, 240, 200});

    if (_selAttrIdx < 0) {
        DrawText("Select an attribute", x + 10, y + 26, 11, {60, 100, 130, 180});
    } else {
        const AttributeDef& attr = AttributeRegistry::All()[_selAttrIdx];
        int cy = y + 24;

        // Scale costs by target item grade (falls back to Common if no target selected).
        ModuleGrade tgtGrade = (_targetIdx >= 0)
                               ? (*_cargo)[_targetIdx].grade
                               : ModuleGrade::Common;
        auto scaledCost = ProgressionRegistry::ScaledCost(attr.graftCost, tgtGrade);

        // Grade multiplier indicator
        {
            char multBuf[32];
            std::snprintf(multBuf, sizeof(multBuf), "x%.2f",
                          ProgressionRegistry::GetMultiplier(tgtGrade));
            Color mc = (_targetIdx >= 0) ? Color{220, 200, 80, 200} : Color{90, 120, 150, 160};
            DrawText(multBuf, x + w - MeasureText(multBuf, 10) - 8, y + 6, 10, mc);
        }

        // Material costs (scaled)
        DrawText("MATERIALS:", x + 10, cy, 11, {80, 170, 240, 180});
        cy += 18;
        for (const auto& ing : scaledCost) {
            bool have = _inventory->items.count(ing.materialId) &&
                        _inventory->items.at(ing.materialId) >= ing.amount;
            Color col = have ? Color{75, 220, 100, 230} : Color{220, 75, 75, 230};
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%-14s x%d", ing.materialId.c_str(), ing.amount);
            DrawText(buf, x + 10, cy, 11, col);
            if (!have) DrawText("!", x + w - 18, cy, 11, {220, 75, 75, 230});
            cy += 18;
        }

        // Credits
        cy += 4;
        bool haveCredits = false;
        {
            auto it = _inventory->items.find(EngineerService::kCreditKey);
            if (it != _inventory->items.end()) haveCredits = it->second >= EngineerService::kBaseCreditCost;
        }
        char creditBuf[48];
        std::snprintf(creditBuf, sizeof(creditBuf), "%-14s x%d", "credits", EngineerService::kBaseCreditCost);
        DrawText(creditBuf, x + 10, cy, 11, haveCredits ? Color{75, 220, 100, 230} : Color{220, 75, 75, 230});
        if (!haveCredits) DrawText("!", x + w - 18, cy, 11, {220, 75, 75, 230});
        cy += 24;

        // Divider
        DrawRectangle(x + 10, cy, w - 20, 1, {40, 100, 180, 80});
        cy += 10;

        // Stat projection
        DrawText("PROJECTION:", x + 10, cy, 11, {80, 170, 240, 180});
        cy += 18;

        if (_targetIdx >= 0) {
            const Item& tgt = (*_cargo)[_targetIdx];
            float newCap = tgt.isMerged ? tgt.baseStatCap : GradeRegistry::kMergedCap;

            char capBuf[48];
            std::snprintf(capBuf, sizeof(capBuf), "Stat cap: %.0f%% -> %.0f%%",
                tgt.baseStatCap * 100.f, newCap * 100.f);
            DrawText(capBuf, x + 10, cy, 11, {180, 220, 240, 220});
            cy += 16;

            if (!tgt.isMerged) {
                DrawText("Found -> Merged", x + 10, cy, 11, {220, 200, 75, 220});
                cy += 16;
            }

            // Failure risk indicator (default MasteryParams = max skill, so 0% shown
            // until EngineerComponent wires in real values — Task 16.02).
            MasteryParams mp;
            float risk = EngineerService::FailureChance(tgt.grade, mp);
            if (risk > 0.0f) {
                char riskBuf[48];
                std::snprintf(riskBuf, sizeof(riskBuf), "Failure risk: %.0f%%", risk * 100.f);
                Color rc = risk >= 0.5f ? Color{240, 80, 80, 230}
                         : risk >= 0.2f ? Color{240, 180, 60, 230}
                                        : Color{200, 220, 80, 210};
                DrawText(riskBuf, x + 10, cy, 11, rc);
                cy += 16;
            }

            char newAttr[64];
            std::snprintf(newAttr, sizeof(newAttr), "+ %s", attr.displayName.c_str());
            DrawText(newAttr, x + 10, cy, 11, {90, 230, 130, 230});
            cy += 16;

            if (!tgt.graftedAttributes.empty()) {
                DrawText("Existing grafts:", x + 10, cy, 10, {90, 150, 190, 180});
                cy += 14;
                for (const auto& ga : tgt.graftedAttributes) {
                    char gb[64];
                    std::snprintf(gb, sizeof(gb), "  . %s", ga.c_str());
                    DrawText(gb, x + 10, cy, 10, {120, 180, 210, 180});
                    cy += 13;
                }
            }

            // Synergy projection — show rules active now and newly activated by this graft.
            cy += 6;
            DrawRectangle(x + 10, cy, w - 20, 1, {30, 80, 140, 70});
            cy += 8;
            DrawText("SYNERGIES:", x + 10, cy, 11, {80, 170, 240, 180});
            cy += 16;
            {
                std::vector<std::string> curAttrs  = tgt.graftedAttributes;
                std::vector<std::string> projAttrs = tgt.graftedAttributes;
                projAttrs.push_back(attr.id);

                bool anySynergy = false;
                for (const SynergyRule& rule : SynergyManager::Rules()) {
                    bool nowActive  = SynergyManager::IsActive(rule, curAttrs);
                    bool willActive = SynergyManager::IsActive(rule, projAttrs);
                    if (!nowActive && !willActive) continue;
                    anySynergy = true;

                    const AttributeSet& b = rule.bonus;
                    char bonusBuf[64] = {};
                    int  pos = 0;
                    if (b.hull        != 0.f) pos += std::snprintf(bonusBuf + pos, sizeof(bonusBuf) - pos, "+%.0f hull ", b.hull);
                    if (b.shield      != 0.f) pos += std::snprintf(bonusBuf + pos, sizeof(bonusBuf) - pos, "+%.0f shield ", b.shield);
                    if (b.thrust      != 0.f) pos += std::snprintf(bonusBuf + pos, sizeof(bonusBuf) - pos, "+%.0f thrust ", b.thrust);
                    if (b.damageBonus != 0.f) pos += std::snprintf(bonusBuf + pos, sizeof(bonusBuf) - pos, "+%.0f dmg ", b.damageBonus);

                    Color nameCol, bonusCol;
                    const char* tag;
                    if (willActive && !nowActive) {
                        nameCol  = {230, 240, 80, 240};
                        bonusCol = {90, 240, 130, 230};
                        tag      = "[NEW]";
                    } else {
                        nameCol  = {130, 175, 210, 180};
                        bonusCol = {80, 185, 140, 180};
                        tag      = "[ACT]";
                    }

                    int tagW = MeasureText(tag, 10);
                    DrawText(tag, x + 10, cy, 10, nameCol);
                    DrawText(rule.description, x + 10 + tagW + 4, cy, 10, nameCol);
                    cy += 13;
                    DrawText(bonusBuf, x + 22, cy, 10, bonusCol);
                    cy += 14;
                }

                if (!anySynergy) {
                    DrawText("(no synergies)", x + 10, cy, 10, {55, 90, 110, 140});
                    cy += 14;
                }
            }
        } else {
            DrawText("(select target item)", x + 10, cy, 11, {60, 100, 130, 180});
            cy += 16;
        }

        cy += 8;
        if (_sourceIdx >= 0) {
            std::string srcName = ItemDisplayName((*_cargo)[_sourceIdx]);
            char srcBuf[64];
            std::snprintf(srcBuf, sizeof(srcBuf), "Source: %s", srcName.c_str());
            DrawText(srcBuf, x + 10, cy, 11, {150, 190, 220, 210});
            DrawText("(consumed)", x + 10, cy + 13, 10, {90, 130, 160, 160});
        } else {
            DrawText("Source: (none)", x + 10, cy, 11, {60, 100, 130, 180});
        }
    }

    // ── Execute button ────────────────────────────────────────────────────────
    bool canExec = _targetIdx >= 0 && _sourceIdx >= 0 && _selAttrIdx >= 0;
    Rectangle execBtn = {
        (float)(x + 10),
        (float)(sh - 80),
        (float)(w - 20),
        36.f
    };
    Color execBg   = canExec ? Color{18, 80, 38, 235} : Color{18, 28, 22, 185};
    Color execBord = canExec ? Color{60, 210, 90, 210} : Color{28, 48, 32, 140};
    Color execTxt  = canExec ? Color{110, 255, 140, 255} : Color{45, 75, 50, 180};

    DrawRectangleRec(execBtn, execBg);
    DrawRectangleLinesEx(execBtn, 1.5f, execBord);
    const char* execLabel = "EXECUTE GRAFT";
    DrawText(execLabel,
             (int)(execBtn.x + (execBtn.width  - MeasureText(execLabel, 14)) * 0.5f),
             (int)(execBtn.y + (execBtn.height - 14) * 0.5f),
             14, execTxt);

    // Result message
    if (_resultTimer > 0.f) {
        Color rc = _lastSuccess ? Color{75, 245, 100, 230} : Color{245, 75, 75, 230};
        DrawText(_resultMsg.c_str(), x + 10, (int)(execBtn.y - 26), 12, rc);
    }

    DrawText("ESC - close", x + 10, sh - 24, 10, {45, 90, 120, 140});
}

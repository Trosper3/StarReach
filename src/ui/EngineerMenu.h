#pragma once
#include "items/Item.h"
#include "services/EngineerService.h"
#include "shared/entities/InventoryComponent.h"
#include <string>
#include <vector>

// Side-by-side attribute grafting UI.
// Left panel: cargo item list (click to assign target / source).
// Middle panel: attribute list (primary/secondary, Mythic-blocked primaries dimmed).
// Right panel: material cost preview, stat projection, execute button.
class EngineerMenu {
public:
    bool isOpen = false;

    void Open(std::vector<Item>& cargo, InventoryComponent& inventory);
    void Close();

    // Returns true while open; false when closed (ESC or post-close).
    bool Update();
    void Draw() const;

private:
    std::vector<Item>*  _cargo      = nullptr;
    InventoryComponent* _inventory  = nullptr;

    int   _targetIdx  = -1;    // cargo index of item receiving the graft
    int   _sourceIdx  = -1;    // cargo index of item being consumed
    int   _selAttrIdx = -1;    // index into AttributeRegistry::All()
    bool  _pickTarget = true;  // true = next cargo click sets target; false = source
    bool  _lastSuccess = false;
    std::string _resultMsg;
    float _resultTimer = 0.f;

    void _drawLeft  (int x, int y, int w, int h) const;
    void _drawMiddle(int x, int y, int w, int h) const;
    void _drawRight (int x, int y, int w, int h) const;

    static const char* _gradeStr (ModuleGrade g);
    static const char* _resultStr(GraftResult r);
};

#pragma once
#include "core/Module.h"
#include "core/StorageItem.h"
#include "raylib.h"
#include <string>
#include <vector>

class StorageMenu {
public:
    static constexpr int MaxStack    = 128;
    static constexpr int SlotPx      = 80;
    static constexpr int SlotGap     = 10;
    static constexpr int SlotsPerRow = 5;

    std::vector<StorageItem> slots;
    bool isOpen = false;

    void Open(int numSlots);
    void Close() { isOpen = false; }

    // Full-screen overlay: returns true while open (false when BACK clicked)
    bool Update();
    void Draw() const;

    // Shared slot rendering (used by ModulesMenu too)
    static void GetRects(int x, int y, int w, int count, Rectangle* out);
    static void DrawItemInSlot(Rectangle r, const StorageItem& item,
                               bool hovered, bool dimmed);
    static void DrawItemTooltip(const StorageItem& item, int x, int y);

    // Color / name helpers
    static Color       GradeColor(ModuleGrade g);
    static const char* GradeName (ModuleGrade g);
    static const char* TypeName  (ModuleType  t);
    static Color       TypeColor (ModuleType  t);

    // Shared trash-can widget (used by ModulesMenu too)
    static Rectangle TrashRect(int sw, int sh);
    static void      DrawTrashCan(Rectangle r, bool hovered);

private:
    mutable int _hovSlot = -1;
    bool        _dragging = false;
    int         _dragIdx  = -1;
    StorageItem _dragItem;
};

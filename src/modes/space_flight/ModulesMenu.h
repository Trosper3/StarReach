#pragma once
#include "StorageMenu.h"
#include "core/Module.h"
#include "shared/entities/HealthComponent.h"
#include "shared/entities/Hardpoint.h"
#include <optional>
#include <vector>

class ModulesMenu {
public:
    bool isOpen = false;

    void Open(HardpointRig* loadout, std::vector<StorageItem>* storage,
              int wSlots, int arSlots, int shSlots, int enSlots, int hdSlots, int auxSlots,
              HealthComponent* healTarget = nullptr);
    void Open(HardpointRig* loadout, std::vector<StorageItem>* storage,
              int wSlots, int arSlots, int shSlots, int enSlots, int auxSlots,
              HealthComponent* healTarget = nullptr);
    void Close();

    // Returns true when a module was equipped or unequipped this frame
    bool Update();
    void Draw() const;

private:
    struct ModSlotRef {
        Rectangle  rect;
        ModuleType type;
        int        idx;
    };

    enum class DragSrc { None, Storage, ModSlot };

    HardpointRig*             _loadout    = nullptr;
    std::vector<StorageItem>* _storage    = nullptr;
    HealthComponent*          _healTarget = nullptr; // consumable repair-kit drops heal this

    // Hover tracking (written by Update, read by Draw)
    int   _hovModSlot     = -1;
    int   _hovStorageSlot = -1;
    float _hoverTimer     = 0.0f;
    int   _hoverSlotId    = -999999;  // combined ID; changes reset the timer

    // Drag state
    DragSrc   _dragSrc  = DragSrc::None;
    int       _dragIdx  = -1;    // storage idx OR modSlots vector idx
    ModuleDef _dragMod  = {};    // copy of the module currently in flight
    bool      _dragging = false;

    // Explicit slot counts — set by Open(); used when _ship is nullptr (NPC modules)
    int _wSlots   = 0;
    int _arSlots  = 0;
    int _shSlots  = 0;
    int _enSlots  = 0;
    int _hdSlots  = 0;
    int _auxSlots = 0;

    void BuildModSlots(std::vector<ModSlotRef>& out) const;
    bool IsCompatible(ModuleType slotType, const ModuleDef& mod) const;

    // Look up the loadout optional for a given slot (const/non-const overloads)
    std::optional<ModuleDef>*       GetModOpt(const ModSlotRef& ms);
    const std::optional<ModuleDef>* GetModOpt(const ModSlotRef& ms) const;

    void DrawModSlot(Rectangle r, const std::optional<ModuleDef>& mod,
                     bool hovered, bool highlighted) const;
    void DrawDraggedItem() const;
    void DrawModuleTooltip(const ModuleDef& mod, Vector2 mousePos, float alpha) const;

    static void PanelSplit(int sw, int& modW, int& stoX, int& stoW);
    static void StorageSlotRects(int stoX, int stoW, int count, Rectangle* out);
};

#pragma once
#include "StorageMenu.h"
#include "core/Module.h"
#include <optional>
#include <vector>

struct ShipLoadout {
    std::vector<std::optional<ModuleDef>> weapons;
    std::vector<std::optional<ModuleDef>> shields;
    std::optional<ModuleDef>              armor;
    std::optional<ModuleDef>              engine;
    std::optional<ModuleDef>              hyperdrive;
    std::vector<std::optional<ModuleDef>> aux;

    void Resize(int wSlots, int shSlots, int auxSlots)
    {
        weapons.assign(wSlots, std::nullopt);
        shields.assign(shSlots, std::nullopt);
        aux.assign(auxSlots, std::nullopt);
        armor      = std::nullopt;
        engine     = std::nullopt;
        hyperdrive = std::nullopt;
    }
};

class ModulesMenu {
public:
    bool isOpen = false;

    void Open(ShipLoadout* loadout, std::vector<StorageItem>* storage,
              int wSlots, int arSlots, int shSlots, int enSlots, int hdSlots, int auxSlots);
    void Open(ShipLoadout* loadout, std::vector<StorageItem>* storage,
              int wSlots, int arSlots, int shSlots, int enSlots, int auxSlots);
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

    ShipLoadout*              _loadout  = nullptr;
    std::vector<StorageItem>* _storage  = nullptr;

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

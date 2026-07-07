#pragma once
#include "core/PlayerStation.h"
#include "modes/space_flight/StorageMenu.h"
#include <vector>

// Right-click menu for editing modules installed on a player-built station.
// Shows hardpoints on the left, selected hardpoint's module slots on the right.
class StationModuleMenu {
public:
    bool isOpen = false;
    std::string pendingShipBuildId;
    std::string GetSelectedShipId() const;
    bool        IsMouseOverMenu() const;

    void Open(PlayerStation* station, std::vector<StorageItem>* storage);
    void Close();
    bool Update();   // returns true while open, false when closed
    void Draw() const;

private:
    static constexpr int PanelW    = 820;
    static constexpr int SlotPx    = 68;
    static constexpr int SlotGap   = 8;
    static constexpr int HPListW   = 160;
    int _selShipIdx = -1;

    PlayerStation*            _station = nullptr;
    std::vector<StorageItem>* _storage = nullptr;
    int                       _selHp   = 0;   // selected hardpoint index
    int                       _storageScroll = 0;

    // Drag state
    enum class DragSrc  { None, Storage, Slot };
    enum class DragKind { Module, Hardpoint };
    DragSrc  _dragSrc  = DragSrc::None;
    DragKind _dragKind = DragKind::Module;
    int      _dragIdx  = -1;
    ModuleDef           _dragMod;
    StationHardpointDef _dragHp;
    bool      _dragging = false;

    // True while this station has room for another hardpoint (see
    // PlayerStationDef::maxHardpoints). Gates the "ATTACH HARDPOINT" drop row.
    bool CanAttachHardpoint() const;

    struct SlotRef {
        Rectangle  rect;
        ModuleType type;
        int        idx;    // index within its vector, or 0 for single slots
    };

    void BuildSlotRefs(int selHp, std::vector<SlotRef>& out) const;
    std::optional<ModuleDef>*       GetSlotOpt(SlotRef& s);
    const std::optional<ModuleDef>* GetSlotOpt(const SlotRef& s) const;
    bool IsCompatible(ModuleType slotType, const ModuleDef& mod) const;
    void DrawSlot(Rectangle r, const std::optional<ModuleDef>& mod,
                  bool hovered, bool highlighted) const;
};

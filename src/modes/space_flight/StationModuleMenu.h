#pragma once
#include "core/PlayerStation.h"
#include "modes/space_flight/StorageMenu.h"
#include <vector>

// Right-click menu for editing modules installed on a player-built station.
// Two-screen flow: a hardpoint list (click a hardpoint to select it), then
// that hardpoint's module page (slots + storage, and the shipyard column
// when the selected hardpoint has a Shipyard facility chip installed — see
// HasShipyardFacility in the .cpp). Renders as a floating panel
// over the (still-running, dimmed) game world rather than a full-screen takeover,
// since ship placement drags a ship icon out of the panel into world space.
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
    static constexpr int PanelW    = 860;
    static constexpr int PanelH    = 700; // 6 module rows (P4) + power budget bar (P7-T1); bumped for breathing room, user request 2026-07-09
    static constexpr int SlotPx    = 68;
    static constexpr int SlotGap   = 8;
    // Y offset (from panelY) where hardpoint-list rows / module-page slot rows
    // begin. Was a bare "40" literal until P7-T1 carved out a power-budget
    // bar in the newly-widened gap between the title and the content rows.
    static constexpr int ContentY  = 60;
    int _selShipIdx = -1;

    enum class Screen { HardpointList, ModulePage };
    Screen _screen = Screen::HardpointList;

    PlayerStation*            _station = nullptr;
    std::vector<StorageItem>* _storage = nullptr;
    int                       _selHp   = 0;   // selected hardpoint index
    int                       _storageScroll = 0;
    int                       _shipListScroll = 0;

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

    // Per-screen geometry (kept in one place so Update()/Draw() agree).
    Rectangle HardpointRowRect(int panelX, int panelY, int i) const;
    Rectangle StorageAreaRect(int panelX, int panelY) const;
    Rectangle ShipyardAreaRect(int panelX, int panelY) const;

    bool UpdateHardpointList(int panelX, int panelY, Vector2 mouse);
    bool UpdateModulePage(int panelX, int panelY, Vector2 mouse);
    void DrawHardpointList(int panelX, int panelY, Vector2 mouse) const;
    void DrawModulePage(int panelX, int panelY, Vector2 mouse) const;

    // P7-T1: station-wide load/capacity/throttle/shed-count readout, drawn on
    // both screens right under the title. Reads PlayerStation::powerBudget
    // (last tick's RecalculatePowerBudget result, SpaceFlight.cpp) plus a
    // live per-hardpoint scan for shed count / worst throttle.
    void DrawPowerBar(int panelX, int panelY) const;
};

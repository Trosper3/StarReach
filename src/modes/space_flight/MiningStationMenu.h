#pragma once
#include "core/PlayerStation.h"
#include "modes/space_flight/StorageMenu.h"
#include <vector>

// Right-click menu for a mining station: lets the player drag materials back
// and forth between the station's onboard storage (left) and their own ship
// storage (right). Also offers a shortcut into the station's module editor,
// since installing/upgrading the Material Probe happens there.
class MiningStationMenu {
public:
    bool isOpen                 = false;
    bool openModulesRequested   = false;   // set on MODULES click; caller opens StationModuleMenu

    void Open(PlayerStation* station, std::vector<StorageItem>* playerStorage);
    void Close();
    bool Update();   // returns true while open, false when closed
    void Draw() const;
    bool IsMouseOverMenu() const;

private:
    static constexpr int PanelW  = 640;
    static constexpr int PanelH  = 380;
    static constexpr int SlotPx  = 68;
    static constexpr int SlotGap = 8;
    static constexpr int ColW    = 280;

    PlayerStation*             _station = nullptr;
    std::vector<StorageItem>*  _player  = nullptr;

    enum class DragSrc { None, Station, Player };
    DragSrc     _dragSrc  = DragSrc::None;
    int         _dragIdx  = -1;
    StorageItem _dragItem;
    bool        _dragging = false;

    mutable int _hovSide = -1;   // 0 = station, 1 = player, -1 = none
    mutable int _hovIdx  = -1;

    void GetColumnRects(int x, int y, int count, std::vector<Rectangle>& out) const;
};

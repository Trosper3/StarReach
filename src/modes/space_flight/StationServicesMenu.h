#pragma once
#include "core/StorageItem.h"
#include "core/StationEconomy.h"
#include "core/FactionEnum.h"
#include "core/Contract.h"
#include "shared/Entity.h"
#include "raylib.h"
#include <string>
#include <vector>

// The player's "enter station" menu — sell modules/materials/crafted items/
// hardpoints, buy crafted items/modules/hardpoints for credits, pay to repair
// hull (adjustable %), or merge duplicate modules into a higher grade with
// the engineer. Visually styled to match the pause menu (SystemMap): bracket
// panels + chamfered buttons via shared/ui/HudTheme.h.
//
// Call Open(player, storage, economy, fuel, maxFuel, stationFaction, offers,
// activeContract, hasActiveContract) with the player's ecs::Entity (for
// hull), &_storageMenu.slots (the player's module inventory), the docked
// station's live StationEconomy (Epic 3), a pointer to the player's
// hyperdrive fuel tank (Epic 4) — same pointer-sharing convention as
// BuildMenu/ModulesMenu — the docked station's Faction (so buy/sell
// transactions can nudge ReputationRegistry standing, Epic 6.3), and the
// station's freshly-generated contract offers plus the player's single
// active-contract slot (Epic 7) — accepting an offer here copies it into
// *activeContract and sets *hasActiveContract, debiting Courier cargo from
// *economy immediately. economy/fuel may be null (fuel screen then shows
// "UNAVAILABLE" rather than refueling).
class StationServicesMenu {
public:
    bool isOpen = false;

    void Open(ecs::Entity* player, std::vector<StorageItem>* storage, StationEconomy* economy,
              float* fuel, float maxFuel, Faction stationFaction,
              std::vector<Contract>* offers, Contract* activeContract, bool* hasActiveContract);
    // Pops one level: a sub-screen returns to the main menu; the main menu closes.
    void Close();
    void Update();
    void Draw() const;

private:
    enum class Screen { Main, Sell, Buy, Repair, Engineer, Fuel, Contracts };
    Screen _screen = Screen::Main;

    ecs::Entity*              _player  = nullptr;
    std::vector<StorageItem>* _storage = nullptr;
    StationEconomy*           _economy = nullptr;
    float*                    _fuel    = nullptr;
    float                     _maxFuel = 100.0f;
    Faction                   _stationFaction = Faction::Republic;

    std::vector<Contract>* _offers          = nullptr;
    Contract*               _activeContract  = nullptr;
    bool*                   _hasActiveContract = nullptr;

    // Sell: single selection. Engineer: up to two matching modules.
    int _selA = -1;
    int _selB = -1;

    // Buy screen: which catalogue is shown, and its scroll offset.
    enum class BuyTab { Crafts, Modules, Hardpoints };
    BuyTab _buyTab    = BuyTab::Crafts;
    int    _buyScroll = 0;

    // Repair screen: percentage of missing hull to restore, adjustable via
    // slider drag or typed exact value.
    float       _repairPct        = 100.0f;
    bool        _repairTextActive = false;
    std::string _repairText       = "100";
    bool        _repairDraggingSlider = false;

    void UpdateMain();
    void UpdateSell();
    void UpdateBuy();
    void UpdateRepair();
    void UpdateEngineer();
    void UpdateFuel();
    void UpdateContracts();

    void DrawMain()      const;
    void DrawSell()      const;
    void DrawBuy()       const;
    void DrawRepair()    const;
    void DrawEngineer()  const;
    void DrawFuel()      const;
    void DrawContracts() const;
};

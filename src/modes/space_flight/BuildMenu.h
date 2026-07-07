#pragma once
#include "data/ItemDef.h"
#include "data/PlayerStationDef.h"
#include "modes/space_flight/StorageMenu.h"
#include <string>
#include <vector>

// Left-panel build/craft interface.
// Call Open(storage) passing the player's storage slots so affordability
// checks and spending read/write the same data as the rest of the game.
// After Update(), check pendingBuildId — non-empty means the player confirmed
// a station build and wants to enter placement mode for that stationDefId.
class BuildMenu {
public:
    bool        isOpen = false;
    std::string pendingBuildId;

    void Open(std::vector<StorageItem>* storage);
    void Close();
    void Update();
    void Draw() const;

    std::string GetSelectedStationId() const;
    bool IsMouseOverMenu() const;

private:
    static constexpr int PanelW = 300;
    static constexpr int TabH = 26;
    static constexpr int ItemH = 80;
    static constexpr int CraftH = 60;

    std::vector<StorageItem>* _storage = nullptr;

    int _tab = 0;   // 0=Stations, 1=Modules, 2=Hardpoints, 3=Craft
    int _selIdx = -1;
    int _scroll = 0;   // Added: Tracks vertical scrolling position in pixels

    // Count how many of id the player has in storage (material or crafted item)
    int  CountInStorage(const std::string& id) const;
    // Remove `amount` of id from storage; returns false if insufficient
    bool RemoveFromStorage(const std::string& id, int amount);
    // Add `amount` of id (with displayName) to storage
    void AddToStorage(const std::string& id, const std::string& displayName, int amount);

    bool CanAffordBuild(const std::vector<BuildIngredient>& cost) const;
    bool CanCraftItem(const ItemDef& item) const;
    // Returns true if, after consuming cost, there is room for the result.
    // isModule=true requires a Module slot; otherwise stacks as a material.
    bool CanFitResult(const std::vector<BuildIngredient>& cost,
        const std::string& resultId, bool isModule) const;
    void DoCraftItem(const ItemDef& item);
    void DoSpendItems(const std::vector<BuildIngredient>& cost);
    // Finds the first Empty slot in _storage and fills it. Used for crafted
    // Module/Hardpoint results, which are single non-stackable items.
    bool PlaceInFirstEmptySlot(const StorageItem& item);

    bool        _errorOpen = false;

    static const char* ItemName(const std::string& itemId);
};
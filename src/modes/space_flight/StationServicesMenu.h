#pragma once
#include "core/StorageItem.h"
#include "shared/Entity.h"
#include "raylib.h"
#include <vector>

// The player's "enter station" menu — sell modules, pay to repair hull, or
// merge duplicate modules into a higher grade with the engineer. Visually
// styled to match the pause menu (SystemMap): bracket panels + chamfered
// buttons via shared/ui/HudTheme.h.
//
// Call Open(player, storage) with the player's ecs::Entity (for hull) and
// &_storageMenu.slots (the player's module inventory) — same pointer-sharing
// convention as BuildMenu/ModulesMenu.
class StationServicesMenu {
public:
    bool isOpen = false;

    void Open(ecs::Entity* player, std::vector<StorageItem>* storage);
    // Pops one level: a sub-screen returns to the main menu; the main menu closes.
    void Close();
    void Update();
    void Draw() const;

private:
    enum class Screen { Main, Sell, Repair, Engineer };
    Screen _screen = Screen::Main;

    ecs::Entity*              _player  = nullptr;
    std::vector<StorageItem>* _storage = nullptr;

    // Sell: single selection. Engineer: up to two matching modules.
    int _selA = -1;
    int _selB = -1;

    void UpdateMain();
    void UpdateSell();
    void UpdateRepair();
    void UpdateEngineer();

    void DrawMain()     const;
    void DrawSell()     const;
    void DrawRepair()   const;
    void DrawEngineer() const;
};

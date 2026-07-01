#include "Builder.h"
#include "raylib.h"
#include "core/GameManager.h"
#include "core/InventoryManager.h"
#include "core/EventBus.h"

void Builder::OnEnter() {
    auto& ship = FleetManager::Get().PlayerShip;
    (void)ship;
    // TODO: initialize grid system with ship.ComponentSlots layout
    // TODO: populate component panel from InventoryManager.Items()
    // TODO: set up Dear ImGui panels (component list, inspector, toolbar)
}

void Builder::Update(float dt) {
    (void)dt;
    // TODO: handle grid click-to-place, drag-to-move, right-click-to-remove
    // TODO: process ImGui input for panel interactions
}

void Builder::Draw() {
    DrawText("Builder Mode - WIP", 10, 10, 20, WHITE);
    // TODO: draw ship grid, ghost component preview, ImGui panels
}

void Builder::OnExit() {
    // TODO: tear down ImGui context, clear grid state
}

void Builder::SaveAndExit() {
    // FleetManager already holds the live config — just transition back
    GameManager::Get().TransitionTo(GameMode::SpaceFlight);
}

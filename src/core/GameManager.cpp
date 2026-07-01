#include "GameManager.h"
#include "EventBus.h"
#include "SaveManager.h"
#include "modes/main_menu/MainMenu.h"
#include "modes/space_flight/SpaceFlight.h"
#include "modes/planet/Planet.h"
#include "modes/builder/Builder.h"

void GameManager::TransitionTo(GameMode mode) {
    if (mode == _currentMode) return;

    if (_activeMode) {
        EventBus::Get().Emit("ModeExiting");
        SaveManager::Get().Snapshot();
        _activeMode->OnExit();
    }

    _currentMode = mode;
    _activeMode  = CreateMode(mode);

    if (_activeMode) {
        _activeMode->OnEnter();
        EventBus::Get().Emit("ModeEntered");
    }
}

void GameManager::Update(float dt) {
    if (_activeMode) _activeMode->Update(dt);
}

void GameManager::Draw() {
    if (_activeMode) _activeMode->Draw();
}

std::unique_ptr<IGameMode> GameManager::CreateMode(GameMode mode) {
    switch (mode) {
        case GameMode::MainMenu:    return std::make_unique<MainMenu>();
        case GameMode::SpaceFlight: return std::make_unique<SpaceFlight>();
        case GameMode::Planet:      return std::make_unique<Planet>();
        case GameMode::Builder:     return std::make_unique<Builder>();
        default:                    return nullptr;
    }
}

#pragma once
#include <memory>
#include "modes/IGameMode.h"

enum class GameMode { None, MainMenu, SpaceFlight, Planet, Builder };

/// Owns the active game mode and drives all scene transitions.
/// Call TransitionTo() from anywhere — never swap modes directly.
class GameManager {
public:
    static GameManager& Get() {
        static GameManager instance;
        return instance;
    }

    void TransitionTo(GameMode mode);
    void Update(float dt);
    void Draw();
    void RequestQuit() { _shouldQuit = true; }

    GameMode CurrentMode() const { return _currentMode; }
    bool     ShouldQuit()  const { return _shouldQuit;  }

private:
    GameManager() = default;
    GameMode                   _currentMode  = GameMode::None;
    std::unique_ptr<IGameMode> _activeMode;
    bool                       _shouldQuit   = false;

    std::unique_ptr<IGameMode> CreateMode(GameMode mode);
};

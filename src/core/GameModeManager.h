#pragma once
#include "GameMode.h"
#include "../shared/Entity.h"
#include "../engine/Projectile.h"
#include "../engine/NetworkSyncSystem.h"
#include <vector>

namespace ecs {

// Central orchestrator: holds the current GameMode and calls only the systems
// that are active for that mode, in the canonical sequential order.
class GameModeManager {
public:
    GameModeManager();

    void SetMode(GameMode mode);
    GameMode CurrentMode()        const { return _mode; }
    bool     IsEngineerMenuActive() const { return _engineerMenuEnabled; }

    // Runs the full update pipeline for the current mode.
    // `snapshots` is optional — pass an empty vector when running offline.
    void Update(std::vector<Entity>&       entities,
                std::vector<Projectile>&   projectiles,
                float                      dt,
                const std::vector<NetworkSnapshot>& snapshots = {});

    // Calls RenderSystem if render is enabled for the current mode.
    void Draw(const std::vector<Entity>& entities);

private:
    GameMode _mode                { GameMode::SpaceFlight };
    bool _inputEnabled            { true  };
    bool _aiEnabled               { true  };
    bool _movementEnabled         { true  };
    bool _combatEnabled           { true  };
    bool _damageEnabled           { true  };
    bool _networkEnabled          { true  };
    bool _renderEnabled           { true  };
    bool _engineerMenuEnabled     { false };
};

} // namespace ecs

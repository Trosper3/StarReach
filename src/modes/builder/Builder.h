#pragma once
#include "modes/IGameMode.h"
#include "core/FleetManager.h"

class Builder : public IGameMode {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void Draw() override;
    void OnExit() override;

private:
    void SaveAndExit();
};

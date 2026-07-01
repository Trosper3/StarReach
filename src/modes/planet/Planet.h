#pragma once
#include <string>
#include "modes/IGameMode.h"

class Planet : public IGameMode {
public:
    explicit Planet(std::string planetId = "default_planet");

    void OnEnter() override;
    void Update(float dt) override;
    void Draw() override;
    void OnExit() override;

private:
    std::string _planetId;
};

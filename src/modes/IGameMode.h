#pragma once

class IGameMode {
public:
    virtual ~IGameMode() = default;
    virtual void OnEnter() = 0;
    virtual void Update(float dt) = 0;
    virtual void Draw() = 0;
    virtual void OnExit() = 0;
};

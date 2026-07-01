#pragma once
#include "../core/GameMode.h"
#include "../shared/Entity.h"
#include "raylib.h"
#include <vector>

namespace ecs {

class CameraSystem {
public:
    CameraSystem() {
        _camera.target   = { 0.0f, 0.0f };
        _camera.rotation = 0.0f;
        _camera.zoom     = 1.0f;
        // offset is set to screen center on first Update call once a window exists
        _camera.offset   = { 0.0f, 0.0f };
    }

    void Update(const std::vector<Entity>& entities, GameMode mode, float dt) {
        // Sync offset to current window size each frame (handles resizes).
        _camera.offset = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };

        switch (mode) {
        case GameMode::SpaceFlight:
        case GameMode::PlanetSide:
            _followPlayer(entities);
            break;

        case GameMode::Strategy:
            _freePan(dt);
            break;
        }
    }

    const Camera2D& GetCamera() const { return _camera; }

private:
    static constexpr float kFollowLerp = 0.1f;   // fraction of gap closed per frame
    static constexpr float kPanSpeed   = 300.0f;  // pixels/s for free-roam panning

    Camera2D _camera {};

    void _followPlayer(const std::vector<Entity>& entities) {
        for (const auto& e : entities) {
            if (e.id == 0 || !e.network.isLocalPlayer) continue;
            _camera.target.x += (e.transform.position.x - _camera.target.x) * kFollowLerp;
            _camera.target.y += (e.transform.position.y - _camera.target.y) * kFollowLerp;
            break;
        }
    }

    void _freePan(float dt) {
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    _camera.target.y -= kPanSpeed * dt;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  _camera.target.y += kPanSpeed * dt;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  _camera.target.x -= kPanSpeed * dt;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) _camera.target.x += kPanSpeed * dt;
    }
};

} // namespace ecs

#pragma once
#include "raylib.h"

struct TransformComponent {
    Vector2 position = { 0.0f, 0.0f };
    Vector2 velocity = { 0.0f, 0.0f };
    float   rotation = 0.0f;
    float   radius   = 18.0f;
};

#pragma once
#include "raylib.h"

struct SpriteComponent {
    Texture2D* texture = nullptr;
    float      scale   = 1.0f;
    Color      tint    = WHITE;
};

#pragma once
#include "raylib.h"
#include <cstdint>

namespace ecs {

struct Projectile {
    Vector2  position  = { 0.0f, 0.0f };
    Vector2  velocity  = { 0.0f, 0.0f };
    float    damage    = 10.0f;
    float    lifetime  = 2.0f;   // seconds; set <= 0 to mark for removal
    uint32_t shooterId = 0;      // entity id that fired this, excluded from hits
};

} // namespace ecs

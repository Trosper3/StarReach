#pragma once
#include "raylib.h"

struct Projectile {
    Vector2 position = { 0.f, 0.f };
    Vector2 velocity = { 0.f, 0.f };
    float        lifetime   = 0.0f;
    float        maxLife    = 0.0f;
    float        damage     = 10.0f;
    bool         alive      = true;
    bool         fromPlayer = true;   // false = NPC projectile, damages player
    unsigned int ownerId    = 0;      // NPC ship ID that fired (0 = player)
    bool         isHoming       = false;
    unsigned int targetId       = 0;     // asteroid or NPC ship ID to home toward
    bool         targetIsPlayer = false; // homing missile targeting the player
    float        turnRate       = 0.0f;  // radians/s for homing steering
};

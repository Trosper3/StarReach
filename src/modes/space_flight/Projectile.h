#pragma once
#include "raylib.h"
#include "core/Module.h" // WeaponEffect

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
    // Epic 9.2 (fighter capture): carried from the firing weapon's WeaponStats
    // so UpdateNpcCollisions' Block 1 can ion-disable a weakened fighter
    // instead of applying lethal damage. None for every non-ion weapon.
    WeaponEffect effect         = WeaponEffect::None;
    float        effectDuration = 0.0f;
};

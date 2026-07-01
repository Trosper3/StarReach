#pragma once
#include "raylib.h"
#include <string>
#include <vector>

struct MaterialChance {
    std::string materialId;
    int         percent = 0;   // 1-100: chance to drop when this asteroid is destroyed
};

struct Asteroid {
    unsigned int id       = 0;
    Vector2 position = {0.0f, 0.0f};
    Vector2 velocity = {0.0f, 0.0f};
    float   rotation = 0.0f;
    float   rotSpeed = 0.0f;  // degrees/s
    float   radius   = 0.0f;
    int     health   = 0;
    int     tier     = 2;     // 2=large, 1=medium, 0=small
    bool    alive    = true;
    std::vector<MaterialChance> materials;   // composition (% drop chance per material)
};

inline float AsteroidRadius(int tier) { return tier == 2 ? 46.0f : tier == 1 ? 24.0f : 12.0f; }
inline int   AsteroidHealth(int tier) { return tier == 2 ? 3     : tier == 1 ? 2     : 1;      }
inline float AsteroidSpeed (int tier) { return tier == 2 ? 38.0f : tier == 1 ? 65.0f : 105.0f; }
inline float AsteroidDamage(int tier) { return tier == 2 ? 12.0f : tier == 1 ? 6.0f  : 3.0f;   }

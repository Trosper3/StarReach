#pragma once
#include "raylib.h"
#include <string>

struct StarSystem {
    unsigned int id          = 0;
    std::string  name;
    unsigned int seed        = 0;
    Vector2      galacticPos = {};
};

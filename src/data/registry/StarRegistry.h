#pragma once
#include "raylib.h"
#include <string>
#include <vector>

// Morgan-Keenan stellar classification: O B A F G K M
struct StarTypeDef {
    std::string id;           // "O","B","A","F","G","K","M"
    std::string displayName;
    float       minRadius;
    float       maxRadius;
    float       gravStrength;   // peak acceleration (units/s²) at sun surface
    float       gravRangeMult;  // gravRange = radius * this
    Color       coreColor;
    Color       innerGlowColor;
    Color       outerGlowColor;
    int         spawnWeight;    // relative spawn probability (M=45 most common, O=1 rarest)
};

namespace StarRegistry {
    void                           Init();
    const std::vector<StarTypeDef>& All();
    const StarTypeDef*              ById(const std::string& id);
    // Picks a star type deterministically from a system seed
    const StarTypeDef*              Pick(unsigned int seed);
}

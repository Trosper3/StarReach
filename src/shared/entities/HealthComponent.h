#pragma once
#include "AttributeSet.h"
#include <algorithm>

class HealthComponent {
public:
    float        currentHull   = 100.0f;
    float        currentShield = 0.0f;
    AttributeSet maxStats;

    // Call whenever module loadout changes; clamps current values to new maxes.
    void RecalculateMax(const AttributeSet& totalStats) {
        maxStats      = totalStats;
        currentHull   = std::min(currentHull,   maxStats.hull);
        currentShield = std::min(currentShield, maxStats.shield);
    }

    void ApplyDamage(float amount) {
        if (currentShield > 0.0f) {
            float absorbed = std::min(currentShield, amount);
            currentShield -= absorbed;
            amount        -= absorbed;
        }
        currentHull = std::max(0.0f, currentHull - amount);
    }

    bool IsAlive() const { return currentHull > 0.0f; }
};

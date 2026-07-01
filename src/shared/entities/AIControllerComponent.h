#pragma once
#include "raylib.h"

namespace ecs {
    enum class AIState { Idle, Patrol, Chase, Attack, Flee, Escort };

    struct AIControllerComponent {
        AIState state = AIState::Idle;
        Vector2 targetPosition = { 0.0f, 0.0f };
    };
}

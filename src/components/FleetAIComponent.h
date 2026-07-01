#pragma once
#include "systems/ai/BehaviorRegistry.h"
#include "core/FactionEnum.h"
#include <cstdint>

enum class FleetAIState : uint8_t {
    Unknown,    // no contact
    Detecting,  // contact in range; detection timer counting down
    Identified, // timer expired; relation + behaviors resolved
};

struct FleetAIComponent {
    FleetAIState state          = FleetAIState::Unknown;
    float        detectionTimer = 0.0f;   // seconds remaining; 0 when idle
    Relation     relation       = Relation::Neutral;
    BehaviorSet  behaviors      = 0;
    uint32_t     targetId       = 0;      // entity id of the detected contact
    Faction      selfFaction    = Faction::Republic;
    Faction      targetFaction  = Faction::Republic;
};

static constexpr float kDetectionDelay = 2.0f; // seconds until contact is fully identified

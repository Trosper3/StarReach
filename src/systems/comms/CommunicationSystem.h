#pragma once
#include "systems/ai/FleetManager.h"
#include "shared/Entity.h"
#include <cstdint>

enum class CommType : uint8_t {
    None,
    Telemetry,       // Friendly: share sensor data with receiver
    TransitRequest,  // Neutral:  demand passage toll
    Jamming,         // Hostile:  disrupt electronics (drains shield)
    SurrenderDemand, // Hostile:  demand receiver stand down
};

struct CommEvent {
    uint32_t senderId    = 0;
    uint32_t receiverId  = 0;
    CommType type        = CommType::None;
    Relation relation    = Relation::Neutral;
    float    tollAmount  = 0.0f;  // only set for TransitRequest
};

// Event-driven system — called once per contact, not every frame.
// OnContact() derives the CommEvent from diplomatic standing.
// Apply() enacts the tangible in-game effect on the receiver entity.
class CommunicationSystem {
public:
    // Derives which comm event a sender faction triggers against a receiver entity.
    static CommEvent OnContact(uint32_t senderId, Faction senderFaction,
                               uint32_t receiverId, Faction receiverFaction);

    // Applies the comm event's game effect to the receiver entity in-place.
    // Telemetry / TransitRequest / SurrenderDemand: no entity mutation (caller handles UI).
    // Jamming: drains receiver's current shield by kJamShieldDrain.
    static void Apply(ecs::Entity& receiver, const CommEvent& evt);

    static constexpr float kJamShieldDrain = 20.0f;
    static constexpr float kBaseToll       = 50.0f;
};

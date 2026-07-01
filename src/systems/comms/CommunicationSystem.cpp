#include "systems/comms/CommunicationSystem.h"
#include <algorithm>

CommEvent CommunicationSystem::OnContact(uint32_t senderId,    Faction senderFaction,
                                         uint32_t receiverId,  Faction receiverFaction) {
    ai::ContactResult cr = ai::FleetManager::EvaluateContact(senderFaction, receiverFaction);

    CommEvent evt;
    evt.senderId   = senderId;
    evt.receiverId = receiverId;
    evt.relation   = cr.relation;

    if (Has(cr.behaviors, Behavior::OpenComms)) {
        evt.type = CommType::Telemetry;
    } else if (Has(cr.behaviors, Behavior::Transactional)) {
        evt.type       = CommType::TransitRequest;
        evt.tollAmount = kBaseToll;
    } else if (Has(cr.behaviors, Behavior::Jamming)) {
        // Prioritise Jamming first on Hostile contacts; SurrenderDemand fires separately.
        evt.type = CommType::Jamming;
    } else if (Has(cr.behaviors, Behavior::Aggressive)) {
        evt.type = CommType::SurrenderDemand;
    }

    return evt;
}

void CommunicationSystem::Apply(ecs::Entity& receiver, const CommEvent& evt) {
    switch (evt.type) {
    case CommType::Jamming:
        // Drain shield; clamp to zero.
        receiver.health.currentShield =
            std::max(0.0f, receiver.health.currentShield - kJamShieldDrain);
        break;

    // Telemetry, TransitRequest, SurrenderDemand: no entity mutation.
    // The caller reads evt.type and triggers the appropriate UI response.
    default:
        break;
    }
}

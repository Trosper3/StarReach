#include "systems/ai/BehaviorRegistry.h"

BehaviorSet BehaviorRegistry::Get(Relation rel) {
    switch (rel) {
    case Relation::Friendly: return Behavior::Escort        | Behavior::OpenComms;
    case Relation::Neutral:  return Behavior::Patrol        | Behavior::Transactional;
    case Relation::Hostile:  return Behavior::Aggressive    | Behavior::Jamming;
    }
    return static_cast<uint8_t>(Behavior::None);
}

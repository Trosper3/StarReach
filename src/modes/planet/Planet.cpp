#include "Planet.h"
#include "raylib.h"
#include "core/WorldManager.h"
#include "core/QuestManager.h"
#include "core/EventBus.h"

Planet::Planet(std::string planetId) : _planetId(std::move(planetId)) {}

void Planet::OnEnter() {
    WorldManager::Get().DiscoverLocation(_planetId);
    // TODO: load planet tilemap from config/planets/<_planetId>.json
    // TODO: spawn NPC entities from PlanetConfig.AvailableQuestIds
    // TODO: subscribe HUD to EventBus "QuestAccepted" and "QuestCompleted"
}

void Planet::Update(float dt) {
    (void)dt;
    // TODO: update player movement, NPC state machines, dialogue triggers
}

void Planet::Draw() {
    DrawText(("Planet: " + _planetId).c_str(), 10, 10, 20, WHITE);
}

void Planet::OnExit() {
    // TODO: destroy NPC entities, unsubscribe HUD signals
}

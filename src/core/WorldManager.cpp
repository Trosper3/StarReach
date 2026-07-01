#include "WorldManager.h"

void WorldManager::DiscoverLocation(const std::string& locationId) {
    DiscoveredLocations.insert(locationId);
}

bool WorldManager::HasDiscovered(const std::string& locationId) const {
    return DiscoveredLocations.count(locationId) > 0;
}

void WorldManager::SetPlanetFlag(const std::string& flagId, bool value) {
    PlanetFlags[flagId] = value;
}

bool WorldManager::GetPlanetFlag(const std::string& flagId) const {
    auto it = PlanetFlags.find(flagId);
    return it != PlanetFlags.end() && it->second;
}

void WorldManager::PlaceStation(const std::string& stationId, Vector2 position) {
    StationPositions[stationId] = position;
}

bool WorldManager::TryGetStation(const std::string& stationId, Vector2& outPosition) const {
    auto it = StationPositions.find(stationId);
    if (it == StationPositions.end()) return false;
    outPosition = it->second;
    return true;
}

#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "raylib.h"

/// Tracks persistent world state: discovered locations, planet flags,
/// and placed station positions. Survives all mode transitions.
class WorldManager {
public:
    static WorldManager& Get() {
        static WorldManager instance;
        return instance;
    }

    std::unordered_set<std::string>          DiscoveredLocations;
    std::unordered_map<std::string, bool>    PlanetFlags;
    std::unordered_map<std::string, Vector2> StationPositions;

    void DiscoverLocation(const std::string& locationId);
    bool HasDiscovered(const std::string& locationId) const;

    void SetPlanetFlag(const std::string& flagId, bool value);
    bool GetPlanetFlag(const std::string& flagId) const;

    void PlaceStation(const std::string& stationId, Vector2 position);
    bool TryGetStation(const std::string& stationId, Vector2& outPosition) const;

private:
    WorldManager() = default;
};

#pragma once
#include "core/PlayerStation.h"
#include "core/FactionEnum.h"
#include "shared/Entity.h"
#include "raylib.h"
#include <string>
#include <vector>

struct PlayerShipConfig {
    std::string              ShipTypeId      = "ar3_saber";
    float                    HullIntegrity   = 100.f;
    float                    MaxHull         = 100.f;
    Faction                  PlayerFaction   = Faction::Republic;
    std::vector<std::string> ComponentSlots  = {"", "", "", ""};
    std::string               GalaxySeedInput; // New Game only; blank = random galaxy seed
};

/// Owns the player's ship configuration, hull state, and built stations.
class FleetManager {
public:
    static FleetManager& Get() {
        static FleetManager instance;
        return instance;
    }

    PlayerShipConfig           PlayerShip;
    std::vector<PlayerStation> PlayerStations;
    unsigned int               NextStationId = 1;

    void ModifyShip(int slot, const std::string& componentId);
    void ApplyDamage(float amount);
    void RepairShip(float amount);

    // Spawns a new player station from a blueprint definition.
    // Returns a reference to the new station inside PlayerStations.
    PlayerStation& SpawnStation(const std::string& stationDefId, Vector2 position);

private:
    FleetManager() = default;
};

namespace ecs {

// ECS-layer entity factory. Separate from the legacy ::FleetManager singleton.
// Lives in namespace ecs to avoid linker collisions with the legacy class.
class FleetManager {
public:
    // Returns a fully-populated Entity, or a blank entity (id==0) if shipId is unknown.
    // Visuals routed through hybrid pipeline: designArray → SpriteCache, assetPath → ResourceManager.
    static ecs::Entity SpawnShip(const std::string& shipId,
                                 Vector2            position,
                                 Color              factionPrimary = BLUE,
                                 Color              factionAccent  = WHITE);

    // Returns a station entity whose LoadoutComponent slots carry hardpoint offsets
    // for composite rendering by the RenderSystem.
    static ecs::Entity SpawnStation(const std::string& stationId,
                                    Vector2            position,
                                    Color              factionPrimary = GRAY,
                                    Color              factionAccent  = WHITE);

private:
    static uint32_t s_nextId;
};

} // namespace ecs

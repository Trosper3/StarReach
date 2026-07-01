#include "data/DataRegistry.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "data/registry/FactionRegistry.h"
#include "data/registry/MaterialRegistry.h"
#include "data/registry/ModuleRegistry.h"
#include "data/registry/NpcProfileRegistry.h"
#include "data/registry/PlanetTypeRegistry.h"
#include "data/registry/StarRegistry.h"
#include "data/registry/StationTypeRegistry.h"
#include "data/registry/ItemRegistry.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/registry/BuildableRegistry.h"
#include "core/ShipRegistry.h"

void DataRegistry::Init() {
    DiplomaticRegistry::Init();
    FactionRegistry::Init();
    MaterialRegistry::Init();
    ModuleRegistry::Init();
    ecs::ShipRegistry::Init("config/ships.json");
    StarRegistry::Init();
    StationTypeRegistry::Init();
    PlanetTypeRegistry::Init();
    NpcProfileRegistry::Init();
    ItemRegistry::Init();
    PlayerStationRegistry::Init();
    BuildableRegistry::Init();
}

#include "FleetManager.h"
#include "EventBus.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/modules/ArmorDefs.h"
#include "core/ShipRegistry.h"
#include "engine/SpriteCache.h"
#include "engine/ResourceManager.h"
#include <algorithm>

void FleetManager::ModifyShip(int slot, const std::string& componentId) {
    if (slot < 0 || slot >= static_cast<int>(PlayerShip.ComponentSlots.size())) return;
    PlayerShip.ComponentSlots[slot] = componentId;
    EventBus::Get().Emit("ShipModified");
}

void FleetManager::ApplyDamage(float amount) {
    PlayerShip.HullIntegrity = std::max(0.f, PlayerShip.HullIntegrity - amount);
}

void FleetManager::RepairShip(float amount) {
    PlayerShip.HullIntegrity = std::min(PlayerShip.MaxHull, PlayerShip.HullIntegrity + amount);
}

PlayerStation& FleetManager::SpawnStation(const std::string& stationDefId, Vector2 position) {
    const PlayerStationDef* def = PlayerStationRegistry::ById(stationDefId);

    PlayerStation ps;
    ps.id = NextStationId++;
    ps.stationDefId = stationDefId;
    ps.displayName = def ? def->displayName : stationDefId;
    ps.position = position;
    ps.alive = true;

    if (def) {
        for (const StationHardpointDef& hpd : def->hardpoints) {
            HardpointState hp;
            hp.id = hpd.id;
            hp.displayName = hpd.displayName;
            hp.isCore = hpd.isCore;
            hp.maxHull = hpd.maxHull;
            hp.hull = hpd.maxHull;
            hp.alive = true;
            hp.wSlots = hpd.wSlots;
            hp.arSlots = hpd.arSlots;
            hp.shSlots = hpd.shSlots;
            hp.enSlots = hpd.enSlots;
            hp.auxSlots = hpd.auxSlots;
            hp.weapons.assign(hpd.wSlots, std::nullopt);
            hp.shields.assign(hpd.shSlots, std::nullopt);
            hp.aux.assign(hpd.auxSlots, std::nullopt);
            ps.hardpoints.push_back(std::move(hp));
        }
    }

    // Initialize default armor on the newly created station (ps)
    for (HardpointState& hp : ps.hardpoints) {
        if (hp.arSlots > 0) {
            hp.armor = Armor_HullPatch(); // Inject a basic starter armor module
            hp.maxHull = 100.0f + hp.armor->armor.hullBonus;
            hp.hull = hp.maxHull;         // Heal to full on spawn
        }
    }

    PlayerStations.push_back(std::move(ps));
    return PlayerStations.back();
}

// ─── ECS FleetManager ────────────────────────────────────────────────────────

namespace ecs {

uint32_t FleetManager::s_nextId = 1;

static ModuleType SlotTypeToModuleType(const std::string& slotType) {
    if (slotType == "weapon")     return ModuleType::Weapon;
    if (slotType == "armor")      return ModuleType::Armor;
    if (slotType == "shield")     return ModuleType::Shield;
    if (slotType == "engine")     return ModuleType::Engine;
    if (slotType == "hyperdrive") return ModuleType::Hyperdrive;
    return ModuleType::Auxiliary;
}

ecs::Entity FleetManager::SpawnShip(const std::string& shipId, Vector2 position,
                                    Color factionPrimary, Color factionAccent) {
    ecs::Entity e;
    const ShipDef* def = ecs::ShipRegistry::ShipById(shipId);
    if (!def) return e;  // unknown shipId — caller checks e.id == 0
    e.id = s_nextId++;

    e.transform.position = position;

    e.health.RecalculateMax(def->baseStats);
    e.health.currentHull   = def->baseStats.hull;
    e.health.currentShield = def->baseStats.shield;

    auto addSlots = [&](ModuleType t, int count) {
        for (int i = 0; i < count; ++i)
            e.loadout.slots.push_back({t, std::nullopt, {0.0f, 0.0f}});
    };
    addSlots(ModuleType::Weapon,    def->weaponSlots);
    addSlots(ModuleType::Armor,     def->armorSlots);
    addSlots(ModuleType::Shield,    def->shieldSlots);
    addSlots(ModuleType::Engine,    def->engineSlots);
    addSlots(ModuleType::Auxiliary, def->auxSlots);

    for (auto& slot : e.loadout.slots) {
        if (slot.equipped.has_value()) {
            ModuleDef& mod = const_cast<ModuleDef&>(*slot.equipped);
            if (!mod.texture)
                mod.texture = SpriteCache::Bake(mod.designArray, factionPrimary, factionAccent);
        }
    }

    e.sprite.scale = def->pixelScale > 0.0f ? def->pixelScale : 1.0f;
    e.sprite.tint  = WHITE;

    if (!def->designArray.empty())
        e.sprite.texture = SpriteCache::BakeForId(def->id, factionPrimary, factionAccent, def->designArray);
    else if (!def->assetPath.empty())
        e.sprite.texture = ResourceManager::Load(def->assetPath);

    return e;
}

ecs::Entity FleetManager::SpawnStation(const std::string& stationId, Vector2 position,
                                       Color factionPrimary, Color factionAccent) {
    ecs::Entity e;
    const StationDef* def = ecs::ShipRegistry::StationById(stationId);
    if (!def) return e;

    e.id = s_nextId++;
    e.transform.position = position;

    e.health.RecalculateMax(def->baseStats);
    e.health.currentHull   = def->baseStats.hull;
    e.health.currentShield = def->baseStats.shield;

    for (const StationHardpointDef& hp : def->hardpoints) {
        ModuleType t = SlotTypeToModuleType(hp.slotType);
        for (int i = 0; i < hp.slotCount; ++i)
            e.loadout.slots.push_back({t, std::nullopt, hp.offset});
    }

    for (auto& slot : e.loadout.slots) {
        if (slot.equipped.has_value()) {
            ModuleDef& mod = const_cast<ModuleDef&>(*slot.equipped);
            if (!mod.texture)
                mod.texture = SpriteCache::Bake(mod.designArray, factionPrimary, factionAccent);
        }
    }

    e.sprite.scale = 1.0f;
    e.sprite.tint  = WHITE;

    if (!def->designArray.empty())
        e.sprite.texture = SpriteCache::BakeForId(def->id, factionPrimary, factionAccent, def->designArray);
    else if (!def->assetPath.empty())
        e.sprite.texture = ResourceManager::Load(def->assetPath);

    return e;
}

} // namespace ecs

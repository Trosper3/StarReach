#include "FleetManager.h"
#include "EventBus.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/modules/ArmorDefs.h"
#include "data/modules/ModuleLookup.h"
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

    // Install each hardpoint's preloaded modules (from station_defs.json) into
    // the first compatible empty slot of matching type.
    if (def) {
        for (size_t hi = 0; hi < ps.hardpoints.size() && hi < def->hardpoints.size(); ++hi) {
            HardpointState& hp = ps.hardpoints[hi];
            for (const std::string& modId : def->hardpoints[hi].preloadedModules) {
                std::optional<ModuleDef> mod = ModuleById(modId);
                if (!mod) continue;
                switch (mod->type) {
                case ModuleType::Weapon:
                    for (auto& w : hp.weapons) if (!w.has_value()) { w = *mod; break; }
                    break;
                case ModuleType::Armor:
                    if (!hp.armor.has_value()) hp.armor = *mod;
                    break;
                case ModuleType::Shield:
                    for (auto& s : hp.shields) if (!s.has_value()) { s = *mod; break; }
                    break;
                case ModuleType::Engine:
                    if (!hp.engine.has_value()) hp.engine = *mod;
                    break;
                case ModuleType::Auxiliary:
                    for (auto& a : hp.aux) if (!a.has_value()) { a = *mod; break; }
                    break;
                default: break;
                }
            }
        }
    }

    // Any armor-capable hardpoint still bare (no preloaded armor in the def)
    // gets a basic starter patch, then hull totals are derived from whatever
    // armor ended up equipped.
    for (HardpointState& hp : ps.hardpoints) {
        if (hp.arSlots > 0 && !hp.armor.has_value())
            hp.armor = Armor_HullPatch();
        if (hp.armor.has_value()) {
            hp.maxHull = 100.0f + hp.armor->armor.hullBonus;
            hp.hull = hp.maxHull;         // Heal to full on spawn
        }
    }

    // Mining stations get a cargo hold for auto-collected materials.
    if (stationDefId == "mining_station") {
        ps.storage.assign(8, StorageItem{});
    }

    // Epic 3: player stations start at half the baseline NPC-station stock —
    // they haven't got an Industrialist producing for them yet, just whatever
    // the player and passing Trader NPCs bring through.
    SeedStationEconomy(ps.economy, 0.5f);

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

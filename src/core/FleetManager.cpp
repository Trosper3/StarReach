#include "FleetManager.h"
#include "EventBus.h"
#include "data/registry/PlayerStationRegistry.h"
#include "data/modules/ArmorDefs.h"
#include "data/modules/FacilityDefs.h"
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
            Hardpoint hp;
            hp.id = hpd.id;
            hp.displayName = hpd.displayName;
            hp.isCore = hpd.isCore;
            hp.isDockingBay = (hpd.id == "docking_bay");
            hp.maxHull = hpd.maxHull;
            hp.hull = hpd.maxHull;
            hp.alive = true;
            for (int i = 0; i < hpd.wSlots;   ++i) hp.slots.push_back({ ModuleType::Weapon });
            for (int i = 0; i < hpd.arSlots;  ++i) hp.slots.push_back({ ModuleType::Armor });
            for (int i = 0; i < hpd.shSlots;  ++i) hp.slots.push_back({ ModuleType::Shield });
            for (int i = 0; i < hpd.enSlots;  ++i) hp.slots.push_back({ ModuleType::Engine });
            for (int i = 0; i < hpd.auxSlots; ++i) hp.slots.push_back({ ModuleType::Auxiliary });
            for (int i = 0; i < hpd.fSlots;   ++i) hp.slots.push_back({ ModuleType::Facility });
            // P4-T4 backward-compat backfill: a def with no explicit facility
            // slot but a recognized legacy hardpoint id gets one auto-installed,
            // so existing config/station_defs.json entries keep functioning
            // (e.g. "docking_bay" still shows the Shipyard column) without edits.
            if (hpd.fSlots == 0) {
                if (auto kind = LegacyHardpointFacilityKind(hpd.id)) {
                    ModuleSlot fs; fs.type = ModuleType::Facility; fs.equipped = Facility_ForKind(*kind);
                    hp.slots.push_back(fs);
                }
            }
            ps.hardpoints.push_back(std::move(hp));
        }
    }

    // Install each hardpoint's preloaded modules (from station_defs.json) into
    // the first compatible empty slot of matching type.
    if (def) {
        for (size_t hi = 0; hi < ps.hardpoints.size() && hi < def->hardpoints.size(); ++hi) {
            Hardpoint& hp = ps.hardpoints[hi];
            for (const std::string& modId : def->hardpoints[hi].preloadedModules) {
                std::optional<ModuleDef> mod = ModuleById(modId);
                if (!mod) continue;
                switch (mod->type) {
                case ModuleType::Weapon:
                    for (auto* w : hp.WeaponSlots()) if (!w->equipped.has_value()) { w->equipped = *mod; break; }
                    break;
                case ModuleType::Armor:
                    if (auto* a = hp.Armor(); a && !a->equipped.has_value()) a->equipped = *mod;
                    break;
                case ModuleType::Shield:
                    for (auto* s : hp.ShieldSlots()) if (!s->equipped.has_value()) { s->equipped = *mod; break; }
                    break;
                case ModuleType::Engine:
                    if (auto* e = hp.Engine(); e && !e->equipped.has_value()) e->equipped = *mod;
                    break;
                case ModuleType::Auxiliary:
                    for (auto* a : hp.AuxSlots()) if (!a->equipped.has_value()) { a->equipped = *mod; break; }
                    break;
                case ModuleType::Facility:
                    if (auto* f = hp.Facility(); f && !f->equipped.has_value()) f->equipped = *mod;
                    break;
                default: break;
                }
            }
        }
    }

    // Any armor-capable hardpoint still bare (no preloaded armor in the def)
    // gets a basic starter patch, then hull totals are derived from whatever
    // armor ended up equipped.
    for (Hardpoint& hp : ps.hardpoints) {
        ModuleSlot* armorSlot = hp.Armor();
        if (armorSlot && !armorSlot->equipped.has_value())
            armorSlot->equipped = Armor_HullPatch();
        if (armorSlot && armorSlot->equipped.has_value()) {
            hp.maxHull = 100.0f + armorSlot->equipped->armor.hullBonus;
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

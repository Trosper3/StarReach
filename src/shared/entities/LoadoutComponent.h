#pragma once
#include "AttributeSet.h"
#include "../../core/Module.h"
#include "../../items/SynergyManager.h"
#include "raylib.h"
#include <optional>
#include <string>
#include <vector>

struct ModuleSlot {
    ModuleType               type;
    std::optional<ModuleDef> equipped;
    Vector2                  hardpointOffset    = { 0.0f, 0.0f };
    float                    cooldownRemaining  = 0.0f;
};

class LoadoutComponent {
public:
    std::vector<ModuleSlot> slots;

    static constexpr float kBaseCapacity        = 5.0f;  // base power budget without engines
    static constexpr float kOverloadThrustFactor = 0.5f;  // velocity scale when overloaded
    static constexpr float kOverloadCooldownMult = 2.0f;  // cooldown multiplier when overloaded

    // Power budget tracking. Engines raise maxCapacity; all other modules add to currentLoad.
    // Systems read IsOverloaded() to apply penalties.
    float currentLoad = 0.0f;
    float maxCapacity = kBaseCapacity;

    bool IsOverloaded() const { return currentLoad > maxCapacity; }

    // Recomputes currentLoad and maxCapacity from equipped modules.
    // Called automatically by Equip(); callers that batch-equip slots should call it once after.
    void RecalculateLoad() {
        currentLoad      = 0.0f;
        float enginePool = 0.0f;
        for (const auto& slot : slots) {
            if (!slot.equipped) continue;
            switch (slot.equipped->type) {
                case ModuleType::Weapon: currentLoad += 2.0f; break;
                case ModuleType::Shield: currentLoad += 1.5f; break;
                case ModuleType::Armor:  currentLoad += 1.0f; break;
                case ModuleType::Engine: enginePool  += slot.equipped->engine.thrustBonus; break;
                default:                currentLoad += 1.0f; break;
            }
        }
        maxCapacity = kBaseCapacity + enginePool;
    }

    // Places mod into the slot at slotIndex if the types match.
    // Returns true on success.
    bool Equip(int slotIndex, const ModuleDef& mod) {
        if (slotIndex < 0 || slotIndex >= static_cast<int>(slots.size()))
            return false;
        if (slots[slotIndex].type != mod.type)
            return false;
        slots[slotIndex].equipped = mod;
        RecalculateLoad();
        return true;
    }

    // Sums AttributeSet contributions from all equipped modules.
    AttributeSet GetTotalBonuses() const {
        AttributeSet total;
        for (const auto& slot : slots)
            if (slot.equipped)
                total = total + ToAttributeSet(*slot.equipped);
        return total;
    }

    // Synergy-aware overload: adds bonuses for matching attribute combos.
    // Pass the flat graftedAttributes list collected from all equipped Item instances.
    AttributeSet GetTotalBonuses(const std::vector<std::string>& equippedAttrs) const {
        return GetTotalBonuses() + SynergyManager::CalculateSynergyBonus(equippedAttrs);
    }

private:
    static AttributeSet ToAttributeSet(const ModuleDef& mod) {
        AttributeSet a;
        switch (mod.type) {
            case ModuleType::Armor:  a.hull        = mod.armor.hullBonus;    break;
            case ModuleType::Shield: a.shield      = mod.shield.capacity;    break;
            case ModuleType::Engine: a.thrust      = mod.engine.thrustBonus; break;
            case ModuleType::Weapon: a.damageBonus = mod.weapon.damage;      break;
            default: break;
        }
        return a;
    }
};

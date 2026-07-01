#pragma once
#include "shared/entities/AttributeSet.h"
#include <algorithm>
#include <string>
#include <vector>

// A combination of graftedAttributes (across all equipped items) that unlocks a hidden bonus.
// Duplicates in requiredAttrs mean "must appear at least N times" (consumed one-by-one).
struct SynergyRule {
    const char*              id;
    const char*              description;
    std::vector<std::string> requiredAttrs; // with multiplicity
    AttributeSet             bonus;
};

// Evaluates the active synergy rules for a set of grafted attributes.
//
// Usage:
//   // Collect graftedAttributes from all equipped Items into one flat list.
//   std::vector<std::string> allAttrs;
//   for (const Item& item : equippedItems)
//       for (const auto& id : item.graftedAttributes)
//           allAttrs.push_back(id);
//   AttributeSet synergy = SynergyManager::CalculateSynergyBonus(allAttrs);
//   AttributeSet total   = loadout.GetTotalBonuses(allAttrs); // calls this internally
class SynergyManager {
public:
    // Returns the combined bonus for every rule whose required attributes are all present.
    static AttributeSet CalculateSynergyBonus(const std::vector<std::string>& attrs) {
        AttributeSet total;
        for (const SynergyRule& rule : Rules())
            if (IsActive(rule, attrs))
                total = total + rule.bonus;
        return total;
    }

    // All defined synergy rules — exposed for UI display and balancing tools.
    static const std::vector<SynergyRule>& Rules() {
        // Canonical anchor: thermal_focused (see AttributeRegistry §14.03 note).
        static const std::vector<SynergyRule> kRules = {
            {
                "dual_thermal",
                "Dual Thermal-Focused: +10 thrust (cooling synergy)",
                { "thermal_focused", "thermal_focused" },
                { 0.f, 0.f, 10.f, 0.f }
            },
            {
                "energy_thermal",
                "Energy Efficient + Thermal-Focused: +8 shield (power management synergy)",
                { "energy_efficient", "thermal_focused" },
                { 0.f, 8.f, 0.f, 0.f }
            },
            {
                "damage_reload",
                "Damage Focused + Reload Optimized: +12 damage bonus (combat rhythm synergy)",
                { "damage_focused", "reload_optimized" },
                { 0.f, 0.f, 0.f, 12.f }
            },
            {
                "hull_stack",
                "Dual Hull-Reinforced: +15 hull (structural resonance synergy)",
                { "hull_reinforced", "hull_reinforced" },
                { 15.f, 0.f, 0.f, 0.f }
            },
        };
        return kRules;
    }

    // Returns true if every required attribute in the rule is present in attrs
    // (respecting multiplicity: two "thermal_focused" entries require the attr twice).
    static bool IsActive(const SynergyRule& rule, const std::vector<std::string>& attrs) {
        std::vector<std::string> pool = attrs;
        for (const auto& req : rule.requiredAttrs) {
            auto it = std::find(pool.begin(), pool.end(), req);
            if (it == pool.end()) return false;
            pool.erase(it); // consume one occurrence
        }
        return true;
    }
};

#include "items/AttributeRegistry.h"

std::vector<AttributeDef>                AttributeRegistry::s_all;
std::unordered_map<std::string, size_t>  AttributeRegistry::s_byId;

void AttributeRegistry::Init() {
    // isPrimary = true  → AttributeSet stat graft (blocked on Mythic by GradeRegistry)
    // isPrimary = false → secondary efficiency perk (allowed on all grades)
    // rarityScale       → bonus multiplier per grade tier above Common (0–6)
    // graftCost         → base material cost; tier scaling applied by ProgressionRegistry (13.01)
    s_all = {
        // ── Primary stats ──────────────────────────────────────────────────────
        { "hull_reinforced",    "Hull Reinforced",    true,  0.20f,
          { {"iron",4}, {"titanium",2} } },

        { "shield_amplified",   "Shield Amplified",   true,  0.20f,
          { {"cobalt",3}, {"crystite",2} } },

        { "thrust_overcharged", "Thrust Overcharged", true,  0.25f,
          { {"titanium",4}, {"cobalt",2} } },

        { "damage_focused",     "Damage Focused",     true,  0.20f,
          { {"tungsten",3}, {"cobalt",2} } },

        // ── Secondary efficiency perks ─────────────────────────────────────────
        // "thermal_focused" is the canonical synergy anchor (SynergyManager, 14.03):
        // dual thermal_focused on chassis + engine unlocks cooling efficiency bonus.
        { "thermal_focused",    "Thermal Focused",    false, 0.15f,
          { {"crystite",3}, {"silica",1} } },

        { "energy_efficient",   "Energy Efficient",   false, 0.15f,
          { {"cobalt",2}, {"silica",2} } },

        { "reload_optimized",   "Reload Optimized",   false, 0.15f,
          { {"iron",2}, {"cobalt",3} } },

        { "sensor_enhanced",    "Sensor Enhanced",    false, 0.10f,
          { {"silica",3}, {"crystite",1} } },

        { "stealth_coated",     "Stealth Coated",     false, 0.10f,
          { {"voidstone",2}, {"xenonite",1} } },
    };

    s_byId.clear();
    for (size_t i = 0; i < s_all.size(); ++i)
        s_byId[s_all[i].id] = i;
}

const std::vector<AttributeDef>& AttributeRegistry::All() { return s_all; }

const AttributeDef* AttributeRegistry::ById(const std::string& id) {
    auto it = s_byId.find(id);
    return it != s_byId.end() ? &s_all[it->second] : nullptr;
}

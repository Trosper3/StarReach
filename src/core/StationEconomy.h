#pragma once
#include "data/registry/MaterialRegistry.h"
#include "data/registry/ItemRegistry.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

// Per-station tradeable-goods stock + a scarcity-driven price curve (Epic 3,
// tasks_spaceflight_dynamics.md). Applies to raw materials (MaterialRegistry)
// and crafted items (ItemRegistry) only — modules/hardpoints stay on their
// existing grade-based flat pricing in StationServicesMenu.cpp, since they're
// discrete equipment rather than bulk commodities NPC traffic hauls.
//
// POD-friendly (string->int map) so it can serialize directly once the
// planned save/load system lands — see tasks_spaceflight_dynamics.md's
// cross-cutting "persistence is planned" note.
struct StationEconomy {
    std::unordered_map<std::string, int> stock;

    static constexpr int kBaselineStock = 50;
    static constexpr int kMaxStock      = 400;

    int GetStock(const std::string& id) const {
        auto it = stock.find(id);
        return it != stock.end() ? it->second : 0;
    }

    // >1 = scarce (expensive), <1 = surplus (cheap), 1 = at baseline.
    float PriceMultiplier(const std::string& id) const {
        float ratio = (float)GetStock(id) / (float)kBaselineStock;
        return std::clamp(2.0f / (1.0f + ratio), 0.4f, 2.2f);
    }

    // What the station pays the player per unit sold TO the station.
    int SellUnitPrice(const std::string& id, int baseValue) const {
        return std::max(1, (int)std::lround(baseValue * PriceMultiplier(id)));
    }

    // What the player pays per unit bought FROM the station — a modest
    // station's-cut spread over the live sell price. Replaces the old flat
    // 3x tax now that scarcity itself drives most of the price swing.
    static constexpr float kBuySpread = 1.4f;
    int BuyUnitPrice(const std::string& id, int baseValue) const {
        return std::max(1, (int)std::ceil(SellUnitPrice(id, baseValue) * kBuySpread));
    }

    // Player selling to the station, or a Trader/Industrialist NPC delivering
    // goods, increases stock (pushes price down toward/below baseline).
    void AddStock(const std::string& id, int amount) {
        int& s = stock[id];
        s = std::clamp(s + amount, 0, kMaxStock);
    }
    // Player buying from the station, or a Trader NPC picking up cargo to
    // haul elsewhere, decreases stock (pushes price up toward scarcity).
    void RemoveStock(const std::string& id, int amount) {
        int& s = stock[id];
        s = std::clamp(s - amount, 0, kMaxStock);
    }
};

// Seeds every tradeable material/item at roughly the baseline stock, with
// per-station +/-variance so not every station starts on an identical price
// curve. Called once when a SpaceStation/PlayerStation is created.
inline void SeedStationEconomy(StationEconomy& econ, float baselineScale = 1.0f, float varianceFrac = 0.4f) {
    auto seedOne = [&](const std::string& id) {
        float base = StationEconomy::kBaselineStock * baselineScale;
        float lo   = base * (1.0f - varianceFrac);
        float hi   = base * (1.0f + varianceFrac);
        econ.stock[id] = std::max(0, GetRandomValue((int)lo, (int)hi));
    };
    for (const MatDef& m : MaterialRegistry::All()) seedOne(m.id);
    for (const ItemDef& i : ItemRegistry::All())    seedOne(i.id);
}

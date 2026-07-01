#include "StarRegistry.h"
#include "data/JsonLoader.h"

namespace StarRegistry {

static std::vector<StarTypeDef> s_types;

void Init() {
    auto j = JL::LoadFile("config/stars.json");
    if (j.is_array() && !j.empty()) {
        s_types.clear();
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            StarTypeDef d;
            d.id             = JL::Str  (item, "id");
            d.displayName    = JL::Str  (item, "displayName");
            d.minRadius      = JL::Float(item, "minRadius",     450.f,  10.f, 10000.f);
            d.maxRadius      = JL::Float(item, "maxRadius",     750.f,  10.f, 10000.f);
            d.gravStrength   = JL::Float(item, "gravStrength",  200.f,   0.f,  5000.f);
            d.gravRangeMult  = JL::Float(item, "gravRangeMult",   2.5f,  0.1f,    10.f);
            d.coreColor      = JL::Clr  (item, "coreColor",      { 255, 120,  60, 255 });
            d.innerGlowColor = JL::Clr  (item, "innerGlowColor", { 255,  70,  20, 255 });
            d.outerGlowColor = JL::Clr  (item, "outerGlowColor", { 220,  30,   5, 255 });
            d.spawnWeight    = JL::Int  (item, "spawnWeight", 1, 1, 1000);
            if (d.id.empty()) continue;
            s_types.push_back(std::move(d));
        }
        TraceLog(LOG_INFO, "StarRegistry: loaded %d star types from config/stars.json", (int)s_types.size());
    } else {
        s_types = {
            { "O", "Class O (Blue)",
              2100.f, 2700.f, 900.f, 2.5f,
              { 200, 215, 255, 255 }, { 140, 165, 255, 255 }, {  80, 100, 220, 255 }, 1 },
            { "B", "Class B (Blue-White)",
              1650.f, 2250.f, 700.f, 2.5f,
              { 210, 225, 255, 255 }, { 170, 195, 255, 255 }, { 120, 150, 240, 255 }, 2 },
            { "A", "Class A (White)",
              1290.f, 1740.f, 550.f, 2.5f,
              { 240, 245, 255, 255 }, { 210, 225, 255, 255 }, { 160, 185, 240, 255 }, 4 },
            { "F", "Class F (Yellowish-White)",
              1020.f, 1410.f, 420.f, 2.5f,
              { 255, 250, 210, 255 }, { 255, 235, 160, 255 }, { 255, 210, 100, 255 }, 8 },
            { "G", "Class G (Yellow)",
              780.f, 1080.f, 330.f, 2.5f,
              { 255, 240, 120, 255 }, { 255, 200,  60, 255 }, { 255, 160,  20, 255 }, 15 },
            { "K", "Class K (Orange)",
              600.f,  900.f, 260.f, 2.5f,
              { 255, 190,  90, 255 }, { 255, 150,  40, 255 }, { 255, 100,  10, 255 }, 25 },
            { "M", "Class M (Red)",
              450.f,  750.f, 200.f, 2.5f,
              { 255, 120,  60, 255 }, { 255,  70,  20, 255 }, { 220,  30,   5, 255 }, 45 },
        };
    }
}

const std::vector<StarTypeDef>& All() { return s_types; }

const StarTypeDef* ById(const std::string& id) {
    for (const auto& t : s_types)
        if (t.id == id) return &t;
    return nullptr;
}

const StarTypeDef* Pick(unsigned int seed) {
    if (s_types.empty()) return nullptr;
    int total = 0;
    for (const auto& t : s_types) total += t.spawnWeight;
    unsigned int r = seed ^ (seed >> 16u);
    r = r * 0x45d9f3bu + 1013904223u;
    int pick = (int)(r % (unsigned int)total);
    int cumul = 0;
    for (const auto& t : s_types) {
        cumul += t.spawnWeight;
        if (pick < cumul) return &t;
    }
    return &s_types.back();
}

} // namespace StarRegistry

#include "systems/diplomacy/DiplomaticRegistry.h"
#include <nlohmann/json.hpp>
#include <fstream>

std::unordered_map<std::string, Relation> DiplomaticRegistry::s_relations;

// Canonical string IDs for the 9 enum factions.
// Index == static_cast<uint8_t>(Faction::X), so Get(Faction,Faction) is safe.
static constexpr const char* kFactionIds[9] = {
    "republic", "zenith", "korrath", "merchant",
    "eden", "reavers", "forge", "conclave", "void"
};

// Matches the original hardcoded matrix — used as fallback when JSON is absent.
static constexpr Relation kDefaultMatrix[9][9] = {
    { Relation::Friendly, Relation::Neutral,  Relation::Neutral,  Relation::Neutral,  Relation::Friendly, Relation::Hostile,  Relation::Hostile,  Relation::Friendly, Relation::Neutral  },
    { Relation::Neutral,  Relation::Friendly, Relation::Hostile,  Relation::Neutral,  Relation::Friendly, Relation::Neutral,  Relation::Neutral,  Relation::Friendly, Relation::Hostile  },
    { Relation::Neutral,  Relation::Hostile,  Relation::Friendly, Relation::Friendly, Relation::Neutral,  Relation::Neutral,  Relation::Hostile,  Relation::Neutral,  Relation::Friendly },
    { Relation::Neutral,  Relation::Neutral,  Relation::Friendly, Relation::Friendly, Relation::Neutral,  Relation::Neutral,  Relation::Hostile,  Relation::Hostile,  Relation::Friendly },
    { Relation::Friendly, Relation::Friendly, Relation::Neutral,  Relation::Neutral,  Relation::Friendly, Relation::Hostile,  Relation::Neutral,  Relation::Neutral,  Relation::Hostile  },
    { Relation::Hostile,  Relation::Neutral,  Relation::Neutral,  Relation::Neutral,  Relation::Hostile,  Relation::Friendly, Relation::Friendly, Relation::Neutral,  Relation::Friendly },
    { Relation::Hostile,  Relation::Neutral,  Relation::Hostile,  Relation::Hostile,  Relation::Neutral,  Relation::Friendly, Relation::Friendly, Relation::Neutral,  Relation::Friendly },
    { Relation::Friendly, Relation::Friendly, Relation::Neutral,  Relation::Hostile,  Relation::Neutral,  Relation::Neutral,  Relation::Neutral,  Relation::Friendly, Relation::Hostile  },
    { Relation::Neutral,  Relation::Hostile,  Relation::Friendly, Relation::Friendly, Relation::Hostile,  Relation::Friendly, Relation::Friendly, Relation::Hostile,  Relation::Friendly }
};

static std::string RelationKey(const std::string& a, const std::string& b) {
    return a + "|" + b;
}

static Relation ParseRelation(const std::string& s) {
    if (s == "friendly") return Relation::Friendly;
    if (s == "hostile")  return Relation::Hostile;
    return Relation::Neutral;
}

static void SeedDefaults() {
    for (int a = 0; a < 9; ++a)
        for (int b = 0; b < 9; ++b)
            DiplomaticRegistry::Set(kFactionIds[a], kFactionIds[b], kDefaultMatrix[a][b]);
}

void DiplomaticRegistry::Init(const std::string& jsonPath) {
    s_relations.clear();
    SeedDefaults();

    if (jsonPath.empty()) return;

    std::ifstream f(jsonPath);
    if (!f.is_open()) return;

    try {
        auto j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) return;

        auto& factions = j["factions"];
        auto& matrix   = j["matrix"];
        if (!factions.is_array() || !matrix.is_array()) return;

        const int n = static_cast<int>(factions.size());
        for (int a = 0; a < n && a < static_cast<int>(matrix.size()); ++a) {
            std::string fa = factions[a].get<std::string>();
            for (int b = 0; b < n && b < static_cast<int>(matrix[a].size()); ++b) {
                std::string fb = factions[b].get<std::string>();
                Set(fa, fb, ParseRelation(matrix[a][b].get<std::string>()));
            }
        }
    } catch (...) {}
}

Relation DiplomaticRegistry::Get(const std::string& a, const std::string& b) {
    auto it = s_relations.find(RelationKey(a, b));
    return it != s_relations.end() ? it->second : Relation::Neutral;
}

void DiplomaticRegistry::Set(const std::string& a, const std::string& b, Relation r) {
    s_relations[RelationKey(a, b)] = r;
}

Relation DiplomaticRegistry::Get(Faction a, Faction b) {
    const auto ai = static_cast<size_t>(a);
    const auto bi = static_cast<size_t>(b);
    if (ai >= 9 || bi >= 9) return Relation::Neutral;
    return Get(kFactionIds[ai], kFactionIds[bi]);
}
